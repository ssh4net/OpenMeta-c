#include "read/omc_native.h"
#include "omc/omc_val.h"
#include <string.h>
#include <stdio.h>

typedef struct omc_native_ctx {
    omc_input* input;
    omc_store* store;
    omc_block_id block;
    const omc_exif_limits* limits;
    omc_exif_source_res result;
} omc_native_ctx;
static void
set_status(omc_native_ctx* ctx, omc_exif_status status)
{
    if (ctx->result.decoded.status == OMC_EXIF_NOMEM) return;
    if (status != OMC_EXIF_NOMEM && ctx->result.decoded.status == OMC_EXIF_LIMIT) return;
    ctx->result.decoded.status = status;
}
static omc_u32
u32(omc_input* input, omc_u64 offset, int little)
{
    omc_u64 value = 0U;
    (void)omc_input_number(input, offset, 4U, little, &value);
    return (omc_u32)value;
}
static omc_u16
u16(omc_input* input, omc_u64 offset, int little)
{
    omc_u64 value = 0U;
    (void)omc_input_number(input, offset, 2U, little, &value);
    return (omc_u16)value;
}
static int
emit(omc_native_ctx* ctx, const char* ifd, omc_u16 tag, omc_u32 order,
      omc_u32 wire_count, const omc_val* value, omc_entry_flags flags)
{
    omc_entry entry;
    if (ctx->result.decoded.status == OMC_EXIF_NOMEM) return 0;
    if (ctx->result.decoded.entries_decoded >= ctx->limits->max_total_entries) {
        set_status(ctx, OMC_EXIF_LIMIT);
        ctx->result.decoded.limit_reason = OMC_EXIF_LIM_MAX_ENTRIES_TOTAL;
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    entry.key.kind = OMC_KEY_EXIF_TAG;
    if (omc_arena_append(&ctx->store->arena, ifd, strlen(ifd), &entry.key.u.exif_tag.ifd) != OMC_STATUS_OK) {
        set_status(ctx, OMC_EXIF_NOMEM); return 0;
    }
    entry.key.u.exif_tag.tag = tag;
    entry.origin.block = ctx->block;
    entry.origin.order_in_block = order;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_count = wire_count;
    entry.value = *value; entry.flags = flags;
    if (omc_store_add_entry(ctx->store, &entry, NULL) != OMC_STATUS_OK) {
        set_status(ctx, OMC_EXIF_NOMEM); return 0;
    }
    ctx->result.decoded.entries_decoded++;
    return 1;
}
static int
bytes_value(omc_native_ctx* ctx, const void* bytes, omc_size size, int text,
             omc_val* value)
{
    omc_byte_ref ref;
    if (ctx->result.decoded.status == OMC_EXIF_NOMEM) return 0;
    if (omc_arena_append(&ctx->store->arena, bytes, size, &ref) != OMC_STATUS_OK) {
        set_status(ctx, OMC_EXIF_NOMEM); return 0;
    }
    if (text) omc_val_make_text(value, ref, OMC_TEXT_ASCII);
    else omc_val_make_bytes(value, ref);
    return 1;
}
static int
emit_u32(omc_native_ctx* ctx, const char* ifd, omc_u16 tag, omc_u32 order,
          omc_u32 value, omc_entry_flags flags)
{
    omc_val v;
    omc_val_make_u32(&v, value);
    return emit(ctx, ifd, tag, order, 1U, &v, flags);
}
static int
printable(const omc_u8* bytes, omc_size size)
{
    omc_size i;
    for (i = 0U; i < size; ++i) if (bytes[i] < 0x20U || bytes[i] > 0x7eU) return 0;
    return 1;
}
static void
raf_directory(omc_native_ctx* ctx, omc_u64 offset, omc_u64 size, unsigned index)
{
    omc_input dir;
    omc_u32 count, i, j;
    omc_u64 p;
    char ifd[] = "raf_0";
    char derived[] = "mk_fuji_rafdata_0";
    if (offset > ctx->input->range.size || size > ctx->input->range.size - offset || size < 4U) {
        set_status(ctx, OMC_EXIF_MALFORMED); return;
    }
    dir = omc_input_slice(ctx->input, offset, size);
    count = u32(&dir, 0U, 0);
    if (count > 255U || count > ctx->limits->max_entries_per_ifd) {
        set_status(ctx, OMC_EXIF_LIMIT);
        ctx->result.decoded.limit_reason = OMC_EXIF_LIM_MAX_ENTRIES_IFD; return;
    }
    ifd[4] = (char)('0' + index); derived[sizeof(derived) - 2U] = (char)('0' + index);
    p = 4U;
    for (i = 0U; i < count && omc_input_ok(&dir); ++i) {
        omc_u16 tag, size16;
        omc_u8 local[16];
        const omc_u8* raw;
        omc_val value;
        omc_entry_flags flags = OMC_ENTRY_FLAG_NONE;
        if (p > size || 4U > size - p) { set_status(ctx, OMC_EXIF_MALFORMED); return; }
        tag = u16(&dir, p, 0); size16 = u16(&dir, p + 2U, 0); p += 4U;
        if (size16 > size - p) { set_status(ctx, OMC_EXIF_MALFORMED); return; }
        omc_val_init(&value);
        raw = NULL;
        if (size16 > ctx->limits->max_value_bytes) {
            flags = OMC_ENTRY_FLAG_TRUNCATED;
            set_status(ctx, OMC_EXIF_LIMIT);
        } else if (dir.range.source.contiguous_data != NULL) {
            raw = dir.range.source.contiguous_data + (omc_size)(dir.range.source_offset + p);
        } else {
            omc_u8* destination = local;
            if (size16 > sizeof(local)) {
                if (size16 > dir.stream_capacity) {
                    if (size16 > ctx->result.value_scratch_needed) ctx->result.value_scratch_needed = size16;
                    flags = OMC_ENTRY_FLAG_TRUNCATED;
                } else {
                    destination = dir.stream;
                    if (size16 > ctx->result.value_scratch_used) ctx->result.value_scratch_used = size16;
                }
            }
            if (flags == OMC_ENTRY_FLAG_NONE) {
                if (!omc_input_read(&dir, p, destination, size16)) return;
                raw = destination;
            }
        }
        if (raw != NULL) {
            omc_input v;
            omc_input_memory(&v, raw, size16);
            if (size16 == 4U && (tag == 0x0100U || tag == 0x0110U || tag == 0x0111U ||
                tag == 0x0115U || tag == 0x0118U || tag == 0x0119U || tag == 0x0121U)) {
                omc_u8 array[4];
                omc_byte_ref ref;
                array[0] = raw[1]; array[1] = raw[0]; array[2] = raw[3]; array[3] = raw[2];
                if (omc_arena_append(&ctx->store->arena, array, 4U, &ref) != OMC_STATUS_OK) {
                    set_status(ctx, OMC_EXIF_NOMEM); return;
                }
                omc_val_make_array(&value, OMC_ELEM_U16, 2U, ref, OMC_BYTE_ORDER_LITTLE);
            } else if (tag == 0x0117U && size16 == 4U) omc_val_make_u32(&value, u32(&v, 0U, 0));
            else if (tag == 0x0130U && size16 == 1U) omc_val_make_u8(&value, raw[0]);
            else if ((tag == 0x9200U || tag == 0x9650U) && size16 == 8U) {
                omc_srational rational;
                rational.numer = (omc_s32)u32(&v, 0U, 0); rational.denom = (omc_s32)u32(&v, 4U, 0);
                omc_val_make_srational(&value, rational);
            } else {
                if (!bytes_value(ctx, raw, size16, 0, &value)) return;
                if (tag == 0x0131U) { value.kind = OMC_VAL_ARRAY; value.elem_type = OMC_ELEM_U8; value.count = size16; }
            }
        }
        if (!emit(ctx, ifd, tag, i, size16, &value, flags)) return;
        if (tag == 0xc000U && raw != NULL && size16 >= 16U) {
            omc_input v;
            omc_input_memory(&v, raw, size16);
            for (j = 0U; j < 4U; ++j)
                if (!emit_u32(ctx, derived, (omc_u16)(4U * j), i + 1024U + j,
                               u32(&v, j * 4U, 0), OMC_ENTRY_FLAG_DERIVED)) return;
        }
        p += size16;
    }
}
static void
raf(omc_native_ctx* ctx)
{
    static const omc_u16 fields[] = {0x48U,0x4cU,0x54U,0x58U,0x5cU,0x60U,0x64U,0x68U,0x6cU,0x78U,0x7cU,0x80U,0x84U};
    omc_u8 version[4];
    omc_val value;
    omc_u32 i, n, offset, size;
    if (ctx->input->range.size >= 0x40U && omc_input_read(ctx->input, 0x3cU, version, 4U) && printable(version, 4U)) {
        if (!bytes_value(ctx, version, 4U, 1, &value) ||
            !emit(ctx, "raf_header", 0x3cU, 0U, 4U, &value, OMC_ENTRY_FLAG_NONE)) return;
    }
    for (i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if ((omc_u64)fields[i] + 4U > ctx->input->range.size) continue;
        n = u32(ctx->input, fields[i], 0);
        if (n && !emit_u32(ctx, "raf_header", fields[i], i + 1U, n, OMC_ENTRY_FLAG_NONE)) return;
    }
    for (i = 0U; i < 2U && omc_input_ok(ctx->input); ++i) {
        n = i ? 0x78U : 0x5cU;
        if ((omc_u64)n + 8U > ctx->input->range.size) continue;
        offset = u32(ctx->input, n, 0); size = u32(ctx->input, n + 4U, 0);
        if (offset && size) raf_directory(ctx, offset, size, i);
    }
}
static int
utf16_ascii(omc_input* chars, omc_u32 position, char* out, omc_size capacity,
             omc_size* length, int* truncated)
{
    omc_u64 offset = (omc_u64)position * 2U;
    omc_u16 ch;
    *length = 0U; *truncated = 0;
    while (offset < chars->range.size && 2U <= chars->range.size - offset) {
        ch = u16(chars, offset, 1); offset += 2U;
        if (!omc_input_ok(chars)) return 0;
        if (ch == 0U) { out[*length] = '\0'; return 1; }
        if (ch < 0x20U || ch > 0x7eU) return 0;
        if (*length + 1U >= capacity) { *truncated = 1; out[*length] = '\0'; return 1; }
        out[(*length)++] = (char)ch;
    }
    return 0;
}
static void
x3f_properties(omc_native_ctx* ctx, omc_u64 offset, omc_u64 size)
{
    static const char* keys[] = {
        "AEMODE","AFAREA","AFINFOCUS","AFMODE","AP_DESC","APERTURE","CAMMANUF","CAMMODEL",
        "CAMNAME","CAMSERIAL","COLORSPACE","DRIVE","EXPCOMP","EXPNET","EXPTIME","FIRMVERS",
        "FLASH","FLENGTH","FLEQ35MM","FOCUS","IMAGERTEMP","ISO","LENSMODEL","PMODE",
        "RESOLUTION","TIME","WB_DESC","CM_DESC","SHUTTER","SH_DESC","LENSARANGE","LENSFRANGE",
        "BURST","BRACKET","EVAL_STATE","IMAGERBOARDID","IMAGEBOARDID","SENSORID"
    };
    omc_input prop, chars;
    omc_u32 entries, i, j, length;
    omc_u64 char_offset;
    if (size < 24U) return;
    prop = omc_input_slice(ctx->input, offset, size);
    if (!omc_input_match(&prop, 0U, "SECp", 4U) || u32(&prop, 12U, 1) != 0U) return;
    entries = u32(&prop, 8U, 1); length = u32(&prop, 20U, 1);
    if (entries > 1024U) { set_status(ctx, OMC_EXIF_LIMIT); return; }
    char_offset = 24U + (omc_u64)entries * 8U;
    if (char_offset > size || (omc_u64)length * 2U > size - char_offset) {
        set_status(ctx, OMC_EXIF_MALFORMED); return;
    }
    chars = omc_input_slice(&prop, char_offset, (omc_u64)length * 2U);
    for (i = 0U; i < entries && omc_input_ok(&prop); ++i) {
        char key[80], text[512];
        omc_size key_size, text_size;
        int key_truncated, text_truncated;
        omc_val value;
        omc_u32 name_pos = u32(&prop, 24U + (omc_u64)i * 8U, 1);
        omc_u32 value_pos = u32(&prop, 28U + (omc_u64)i * 8U, 1);
        if (!utf16_ascii(&chars, name_pos, key, sizeof(key), &key_size, &key_truncated) || key_truncated ||
            !utf16_ascii(&chars, value_pos, text, sizeof(text), &text_size, &text_truncated)) continue;
        for (j = 0U; j < sizeof(keys) / sizeof(keys[0]); ++j) if (strcmp(keys[j], key) == 0) break;
        if (j == sizeof(keys) / sizeof(keys[0])) continue;
        if (!bytes_value(ctx, text, text_size, 1, &value) ||
            !emit(ctx, "x3f_prop", (omc_u16)(j + 1U), i, value.count, &value,
                    text_truncated ? OMC_ENTRY_FLAG_TRUNCATED : OMC_ENTRY_FLAG_NONE)) return;
    }
}
static void
x3f_fixed(omc_native_ctx* ctx, omc_u64 offset, omc_u16 tag, omc_u32 order)
{
    omc_u8 text[32];
    omc_size length = 0U;
    omc_val value;
    if (offset > ctx->input->range.size || 32U > ctx->input->range.size - offset ||
        !omc_input_read(ctx->input, offset, text, sizeof(text))) return;
    while (length < sizeof(text) && text[length] != 0U) ++length;
    if (!printable(text, length)) length = sizeof(text);
    if (bytes_value(ctx, text, length, printable(text, length), &value))
        (void)emit(ctx, "x3f_header", tag, order, value.count, &value, OMC_ENTRY_FLAG_NONE);
}
static void
x3f(omc_native_ctx* ctx)
{
    omc_u32 version, major, minor, i, directory, count, offset, size;
    omc_u64 header_size, n;
    omc_u8 id[16], type[4];
    char text[33];
    omc_val value;
    static const char hex[] = "0123456789abcdef";
    if (ctx->input->range.size >= 40U) {
        version = u32(ctx->input, 4U, 1); major = version >> 16U; minor = version & 65535U;
        /* Two 16-bit decimal fields plus dot fit in this fixed buffer. */
        (void)sprintf(text, "%u.%u", (unsigned)major, (unsigned)minor);
        if (bytes_value(ctx, text, strlen(text), 1, &value))
            (void)emit(ctx, "x3f_header", 1U, 0U, value.count, &value, OMC_ENTRY_FLAG_NONE);
        if (!omc_input_read(ctx->input, 8U, id, sizeof(id))) return;
        for (i = 0U; i < 16U; ++i) { text[i * 2U] = hex[id[i] >> 4U]; text[i * 2U + 1U] = hex[id[i] & 15U]; }
        if (bytes_value(ctx, text, 32U, 1, &value))
            (void)emit(ctx, "x3f_header", 2U, 1U, value.count, &value, OMC_ENTRY_FLAG_NONE);
        (void)emit_u32(ctx, "x3f_header", 6U, 2U, u32(ctx->input, 24U, 1), OMC_ENTRY_FLAG_NONE);
        for (i = 0U; i < 3U; ++i) {
            n = (major >= 4U ? 40U : 28U) + i * 4U;
            if (n + 4U <= ctx->input->range.size)
                (void)emit_u32(ctx, "x3f_header", (omc_u16)(7U + i), 3U + i, u32(ctx->input, n, 1), OMC_ENTRY_FLAG_NONE);
        }
        if (major < 4U) x3f_fixed(ctx, 40U, 10U, 6U);
        if (major == 2U && minor >= 3U) x3f_fixed(ctx, 72U, 18U, 7U);
        header_size = major == 2U && minor >= 1U ? (minor >= 3U ? 104U : 72U) : 0U;
        if (header_size && ctx->input->range.size >= header_size + 160U) {
            for (i = 0U; i < 32U; ++i) {
                omc_u8 tag = omc_input_byte(ctx->input, header_size + i);
                if (tag == 0U || tag > 10U) continue;
                omc_val_make_f32_bits(&value, u32(ctx->input, header_size + 32U + i * 4U, 1));
                (void)emit(ctx, "x3f_header_ext", tag, 32U + i, 1U, &value, OMC_ENTRY_FLAG_NONE);
            }
        }
    }
    if (ctx->input->range.size < 16U) return;
    directory = u32(ctx->input, ctx->input->range.size - 4U, 1);
    if (directory > ctx->input->range.size - 16U || !omc_input_match(ctx->input, directory, "SECd", 4U)) return;
    count = u32(ctx->input, (omc_u64)directory + 8U, 1);
    if (count > 64U) { count = 64U; set_status(ctx, OMC_EXIF_LIMIT); }
    if (12U + (omc_u64)count * 12U > ctx->input->range.size - directory) {
        set_status(ctx, OMC_EXIF_MALFORMED); return;
    }
    for (i = 0U; i < count && omc_input_ok(ctx->input); ++i) {
        n = (omc_u64)directory + 12U + (omc_u64)i * 12U;
        offset = u32(ctx->input, n, 1); size = u32(ctx->input, n + 4U, 1);
        if (offset > ctx->input->range.size || size > ctx->input->range.size - offset) {
            set_status(ctx, OMC_EXIF_MALFORMED); return;
        }
        if (!omc_input_read(ctx->input, n + 8U, type, 4U)) return;
        if (memcmp(type, "PROP", 4U) == 0) x3f_properties(ctx, offset, size);
    }
}
omc_exif_source_res
omc_native_dec(omc_input* input, omc_scan_fmt format, omc_store* store,
                 const omc_exif_limits* limits)
{
    omc_native_ctx ctx;
    omc_block_info block;
    memset(&ctx, 0, sizeof(ctx));
    ctx.result.decoded.status = OMC_EXIF_UNSUPPORTED;
    ctx.input = input; ctx.store = store; ctx.limits = limits;
    if (input == NULL || store == NULL || limits == NULL) return ctx.result;
    if (!((format == OMC_SCAN_FMT_RAF && omc_input_match(input, 0U, "FUJIFILMCCD-RAW ", 16U)) ||
          (format == OMC_SCAN_FMT_X3F && omc_input_match(input, 0U, "FOVb", 4U)))) return ctx.result;
    memset(&block, 0, sizeof(block));
    if (omc_store_add_block(store, &block, &ctx.block) != OMC_STATUS_OK) {
        ctx.result.decoded.status = OMC_EXIF_NOMEM; return ctx.result;
    }
    ctx.result.decoded.status = OMC_EXIF_OK;
    if (format == OMC_SCAN_FMT_RAF) raf(&ctx); else x3f(&ctx);
    if (!omc_input_ok(input)) ctx.result.decoded.status = OMC_EXIF_TRUNCATED;
    if (ctx.result.decoded.entries_decoded == 0U && ctx.result.decoded.status == OMC_EXIF_OK)
        ctx.result.decoded.status = OMC_EXIF_UNSUPPORTED;
    return ctx.result;
}
