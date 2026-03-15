#include "omc/omc_exif.h"

#include <string.h>

#define OMC_EXIF_TASK_CAP 256U

typedef struct omc_exif_cfg {
    int little_endian;
    int big_tiff;
    omc_u64 first_ifd;
    omc_u64 count_size;
    omc_u64 entry_size;
    omc_u64 next_size;
    omc_u64 inline_size;
} omc_exif_cfg;

typedef struct omc_exif_task {
    omc_exif_ifd_kind kind;
    omc_u32 index;
    omc_u64 offset;
} omc_exif_task;

typedef struct omc_exif_ctx {
    const omc_u8* bytes;
    omc_size size;
    omc_store* store;
    omc_block_id source_block;
    int measure_only;
    omc_exif_opts opts;
    omc_exif_cfg cfg;
    omc_exif_res res;
    omc_exif_ifd_ref* out_ifds;
    omc_u32 ifd_cap;
    omc_exif_task tasks[OMC_EXIF_TASK_CAP];
    omc_u32 task_head;
    omc_u32 task_count;
    omc_u32 next_ifd_index;
    omc_u32 next_subifd_index;
} omc_exif_ctx;

static void
omc_exif_res_init(omc_exif_res* res)
{
    res->status = OMC_EXIF_OK;
    res->ifds_written = 0U;
    res->ifds_needed = 0U;
    res->entries_decoded = 0U;
    res->limit_reason = OMC_EXIF_LIM_NONE;
    res->limit_ifd_offset = 0U;
    res->limit_tag = 0U;
}

static void
omc_exif_update_status(omc_exif_res* out, omc_exif_status status)
{
    if (out->status == OMC_EXIF_LIMIT || out->status == OMC_EXIF_NOMEM) {
        return;
    }
    if (status == OMC_EXIF_LIMIT || status == OMC_EXIF_NOMEM) {
        out->status = status;
        return;
    }
    if (out->status == OMC_EXIF_MALFORMED) {
        return;
    }
    if (status == OMC_EXIF_MALFORMED) {
        out->status = status;
        return;
    }
    if (out->status == OMC_EXIF_UNSUPPORTED) {
        return;
    }
    if (status == OMC_EXIF_UNSUPPORTED) {
        out->status = status;
        return;
    }
    if (out->status == OMC_EXIF_TRUNCATED) {
        return;
    }
    if (status == OMC_EXIF_TRUNCATED) {
        out->status = status;
    }
}

static void
omc_exif_mark_limit(omc_exif_ctx* ctx, omc_exif_lim_rsn reason,
                    omc_u64 ifd_offset, omc_u16 tag)
{
    ctx->res.status = OMC_EXIF_LIMIT;
    ctx->res.limit_reason = reason;
    ctx->res.limit_ifd_offset = ifd_offset;
    ctx->res.limit_tag = tag;
}

static int
omc_exif_read_u16(omc_exif_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 2U) {
        return 0;
    }
    if (cfg.little_endian) {
        *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 1U]) << 8)
                               | ((omc_u16)bytes[(omc_size)offset + 0U]));
    } else {
        *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                               | ((omc_u16)bytes[(omc_size)offset + 1U]));
    }
    return 1;
}

static int
omc_exif_read_u32(omc_exif_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }
    if (cfg.little_endian) {
        *out_value = (((omc_u32)bytes[(omc_size)offset + 3U]) << 24)
                     | (((omc_u32)bytes[(omc_size)offset + 2U]) << 16)
                     | (((omc_u32)bytes[(omc_size)offset + 1U]) << 8)
                     | (((omc_u32)bytes[(omc_size)offset + 0U]) << 0);
    } else {
        *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                     | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                     | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                     | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    }
    return 1;
}

static int
omc_exif_read_u64(omc_exif_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 8U) {
        return 0;
    }
    if (cfg.little_endian) {
        *out_value = (((omc_u64)bytes[(omc_size)offset + 7U]) << 56)
                     | (((omc_u64)bytes[(omc_size)offset + 6U]) << 48)
                     | (((omc_u64)bytes[(omc_size)offset + 5U]) << 40)
                     | (((omc_u64)bytes[(omc_size)offset + 4U]) << 32)
                     | (((omc_u64)bytes[(omc_size)offset + 3U]) << 24)
                     | (((omc_u64)bytes[(omc_size)offset + 2U]) << 16)
                     | (((omc_u64)bytes[(omc_size)offset + 1U]) << 8)
                     | (((omc_u64)bytes[(omc_size)offset + 0U]) << 0);
    } else {
        *out_value = (((omc_u64)bytes[(omc_size)offset + 0U]) << 56)
                     | (((omc_u64)bytes[(omc_size)offset + 1U]) << 48)
                     | (((omc_u64)bytes[(omc_size)offset + 2U]) << 40)
                     | (((omc_u64)bytes[(omc_size)offset + 3U]) << 32)
                     | (((omc_u64)bytes[(omc_size)offset + 4U]) << 24)
                     | (((omc_u64)bytes[(omc_size)offset + 5U]) << 16)
                     | (((omc_u64)bytes[(omc_size)offset + 6U]) << 8)
                     | (((omc_u64)bytes[(omc_size)offset + 7U]) << 0);
    }
    return 1;
}

static int
omc_exif_mul_u64(omc_u64 a, omc_u64 b, omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (a != 0U && b > ((omc_u64)(~(omc_u64)0) / a)) {
        return 0;
    }
    *out_value = a * b;
    return 1;
}

static int
omc_exif_elem_size(omc_u16 type, omc_u32* out_size)
{
    if (out_size == (omc_u32*)0) {
        return 0;
    }

    switch (type) {
    case 1:
    case 2:
    case 6:
    case 7:
    case 129: *out_size = 1U; return 1;
    case 3:
    case 8: *out_size = 2U; return 1;
    case 4:
    case 9:
    case 11:
    case 13: *out_size = 4U; return 1;
    case 5:
    case 10:
    case 12:
    case 16:
    case 17:
    case 18: *out_size = 8U; return 1;
    default: break;
    }

    return 0;
}

static int
omc_exif_resolve_raw(const omc_exif_ctx* ctx, omc_u64 entry_value_off,
                     omc_u16 type, omc_u64 count,
                     const omc_u8** out_ptr, omc_u64* out_size)
{
    omc_u32 elem_size;
    omc_u64 total_size;
    omc_u64 value_offset;

    if (!omc_exif_elem_size(type, &elem_size)) {
        return 0;
    }
    if (!omc_exif_mul_u64((omc_u64)elem_size, count, &total_size)) {
        return 0;
    }

    if (entry_value_off > (omc_u64)ctx->size
        || ctx->cfg.inline_size > ((omc_u64)ctx->size - entry_value_off)) {
        return 0;
    }

    if (total_size <= ctx->cfg.inline_size) {
        *out_ptr = ctx->bytes + (omc_size)entry_value_off;
        *out_size = total_size;
        return 1;
    }

    if (!ctx->cfg.big_tiff) {
        omc_u32 off32;

        if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size,
                               entry_value_off, &off32)) {
            return 0;
        }
        value_offset = off32;
    } else {
        if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size,
                               entry_value_off, &value_offset)) {
            return 0;
        }
    }

    if (value_offset > (omc_u64)ctx->size
        || total_size > ((omc_u64)ctx->size - value_offset)) {
        return 0;
    }

    *out_ptr = ctx->bytes + (omc_size)value_offset;
    *out_size = total_size;
    return 1;
}

static omc_exif_status
omc_exif_store_ref(omc_exif_ctx* ctx, const omc_u8* bytes, omc_u64 size,
                   omc_byte_ref* out_ref)
{
    omc_status status;

    if (size > (omc_u64)(~(omc_u32)0)) {
        return OMC_EXIF_LIMIT;
    }

    status = omc_arena_append(&ctx->store->arena, bytes, (omc_size)size,
                              out_ref);
    if (status == OMC_STATUS_OK) {
        return OMC_EXIF_OK;
    }
    if (status == OMC_STATUS_NO_MEMORY) {
        return OMC_EXIF_NOMEM;
    }
    return OMC_EXIF_LIMIT;
}

static omc_exif_status
omc_exif_store_cstr_len(omc_exif_ctx* ctx, const char* text, omc_size len,
                        omc_byte_ref* out_ref)
{
    return omc_exif_store_ref(ctx, (const omc_u8*)text, len, out_ref);
}

static omc_size
omc_exif_u32_to_dec(char* out_buf, omc_u32 value)
{
    char tmp[16];
    omc_size n;
    omc_size i;

    n = 0U;
    do {
        tmp[n] = (char)('0' + (value % 10U));
        value /= 10U;
        n += 1U;
    } while (value != 0U);

    for (i = 0U; i < n; ++i) {
        out_buf[i] = tmp[n - i - 1U];
    }
    return n;
}

static omc_exif_status
omc_exif_make_token(omc_exif_ctx* ctx, omc_exif_ifd_kind kind, omc_u32 index,
                    omc_byte_ref* out_ref)
{
    const char* literal;
    const char* prefix;
    char buf[64];
    omc_size prefix_len;
    omc_size digits_len;

    literal = (const char*)0;
    prefix = (const char*)0;
    prefix_len = 0U;
    digits_len = 0U;

    switch (kind) {
    case OMC_EXIF_IFD:
        prefix = ctx->opts.tokens.ifd_prefix;
        break;
    case OMC_EXIF_SUB_IFD:
        prefix = ctx->opts.tokens.subifd_prefix;
        break;
    case OMC_EXIF_EXIF_IFD:
        literal = ctx->opts.tokens.exif_ifd_token;
        break;
    case OMC_EXIF_GPS_IFD:
        literal = ctx->opts.tokens.gps_ifd_token;
        break;
    case OMC_EXIF_INTEROP_IFD:
        literal = ctx->opts.tokens.interop_ifd_token;
        break;
    default:
        literal = "ifd";
        break;
    }

    if (literal != (const char*)0) {
        return omc_exif_store_cstr_len(ctx, literal, strlen(literal), out_ref);
    }

    prefix_len = strlen(prefix);
    if (prefix_len >= sizeof(buf)) {
        return OMC_EXIF_LIMIT;
    }
    memcpy(buf, prefix, prefix_len);
    digits_len = omc_exif_u32_to_dec(buf + prefix_len, index);
    if (prefix_len + digits_len > sizeof(buf)) {
        return OMC_EXIF_LIMIT;
    }
    return omc_exif_store_cstr_len(ctx, buf, prefix_len + digits_len, out_ref);
}

static void
omc_exif_emit_ifd(omc_exif_ctx* ctx, omc_exif_ifd_kind kind, omc_u32 index,
                  omc_u64 offset)
{
    omc_exif_ifd_ref ref;

    ctx->res.ifds_needed += 1U;
    if (ctx->out_ifds == (omc_exif_ifd_ref*)0 || ctx->ifd_cap == 0U) {
        return;
    }

    ref.kind = kind;
    ref.index = index;
    ref.offset = offset;
    ref.block = OMC_INVALID_BLOCK_ID;

    if (ctx->res.ifds_written < ctx->ifd_cap) {
        ctx->out_ifds[ctx->res.ifds_written] = ref;
        ctx->res.ifds_written += 1U;
    } else {
        omc_exif_update_status(&ctx->res, OMC_EXIF_TRUNCATED);
    }
}

static int
omc_exif_push_task(omc_exif_ctx* ctx, omc_exif_ifd_kind kind, omc_u32 index,
                   omc_u64 offset)
{
    omc_u32 slot;

    if (offset == 0U) {
        return 1;
    }
    if (ctx->task_count >= OMC_EXIF_TASK_CAP) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_IFDS, offset, 0U);
        return 0;
    }

    slot = (ctx->task_head + ctx->task_count) % OMC_EXIF_TASK_CAP;
    ctx->tasks[slot].kind = kind;
    ctx->tasks[slot].index = index;
    ctx->tasks[slot].offset = offset;
    ctx->task_count += 1U;
    return 1;
}

static int
omc_exif_pop_task(omc_exif_ctx* ctx, omc_exif_task* out_task)
{
    if (ctx->task_count == 0U) {
        return 0;
    }

    *out_task = ctx->tasks[ctx->task_head];
    ctx->task_head = (ctx->task_head + 1U) % OMC_EXIF_TASK_CAP;
    ctx->task_count -= 1U;
    return 1;
}

static int
omc_exif_parse_header(omc_exif_ctx* ctx)
{
    omc_u16 version;

    if (ctx->size < 8U) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (ctx->bytes[0] == (omc_u8)'I' && ctx->bytes[1] == (omc_u8)'I') {
        ctx->cfg.little_endian = 1;
    } else if (ctx->bytes[0] == (omc_u8)'M'
               && ctx->bytes[1] == (omc_u8)'M') {
        ctx->cfg.little_endian = 0;
    } else {
        ctx->res.status = OMC_EXIF_UNSUPPORTED;
        return 0;
    }

    if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, 2U, &version)) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (version == 42U || version == 0x0055U || version == 0x4F52U) {
        omc_u32 first_ifd32;

        ctx->cfg.big_tiff = 0;
        ctx->cfg.count_size = 2U;
        ctx->cfg.entry_size = 12U;
        ctx->cfg.next_size = 4U;
        ctx->cfg.inline_size = 4U;

        if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size, 4U,
                               &first_ifd32)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        ctx->cfg.first_ifd = first_ifd32;
        return 1;
    }

    if (version == 43U) {
        omc_u16 off_size;
        omc_u16 reserved;

        ctx->cfg.big_tiff = 1;
        ctx->cfg.count_size = 8U;
        ctx->cfg.entry_size = 20U;
        ctx->cfg.next_size = 8U;
        ctx->cfg.inline_size = 8U;

        if (ctx->size < 16U) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, 4U,
                               &off_size)
            || !omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, 6U,
                                  &reserved)
            || !omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size, 8U,
                                  &ctx->cfg.first_ifd)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        if (off_size != 8U || reserved != 0U) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        return 1;
    }

    ctx->res.status = OMC_EXIF_UNSUPPORTED;
    return 0;
}

static omc_exif_status
omc_exif_add_entry(omc_exif_ctx* ctx, const omc_byte_ref* token_ref,
                   omc_u16 tag, omc_u16 type, omc_u64 count,
                   const omc_u8* raw, omc_u64 raw_size, omc_u32 order_in_block,
                   omc_entry_flags flags)
{
    omc_entry entry;
    omc_status st;
    omc_u64 total_size;
    omc_u32 elem_size;
    int treat_as_bytes;
    omc_u64 text_size;
    omc_u64 i64v;
    omc_u16 u16a;
    omc_u32 u32a;
    omc_u32 u32b;
    omc_u64 u64a;

    memset(&entry, 0, sizeof(entry));
    entry.key.kind = OMC_KEY_EXIF_TAG;
    entry.key.u.exif_tag.ifd = *token_ref;
    entry.key.u.exif_tag.tag = tag;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = type;
    if (count > (omc_u64)(~(omc_u32)0)) {
        return OMC_EXIF_LIMIT;
    }
    entry.origin.wire_count = (omc_u32)count;
    entry.flags = flags;

    if (!omc_exif_elem_size(type, &elem_size)
        || !omc_exif_mul_u64((omc_u64)elem_size, count, &total_size)) {
        return OMC_EXIF_LIMIT;
    }

    if ((type == 2U || type == 129U || type == 7U || count > 1U)
        && total_size > ctx->opts.limits.max_value_bytes) {
        entry.flags |= OMC_ENTRY_FLAG_TRUNCATED;
        entry.value.kind = OMC_VAL_EMPTY;
        st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
        if (st == OMC_STATUS_NO_MEMORY) {
            return OMC_EXIF_NOMEM;
        }
        if (st != OMC_STATUS_OK) {
            return OMC_EXIF_LIMIT;
        }
        return OMC_EXIF_OK;
    }

    if (type == 2U || type == 129U) {
        text_size = raw_size;
        if (text_size != 0U && raw[text_size - 1U] == 0U) {
            text_size -= 1U;
        }
        treat_as_bytes = 0;
        {
            omc_u64 j;
            for (j = 0U; j < text_size; ++j) {
                if (raw[j] == 0U) {
                    treat_as_bytes = 1;
                    break;
                }
            }
        }
        if (!treat_as_bytes) {
            omc_byte_ref ref;
            omc_exif_status xs;

            xs = omc_exif_store_ref(ctx, raw, text_size, &ref);
            if (xs != OMC_EXIF_OK) {
                return xs;
            }
            entry.value.kind = OMC_VAL_TEXT;
            entry.value.elem_type = OMC_ELEM_U8;
            entry.value.text_encoding =
                (type == 129U) ? OMC_TEXT_UTF8 : OMC_TEXT_ASCII;
            entry.value.count = (omc_u32)text_size;
            entry.value.u.ref = ref;
            st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
            if (st == OMC_STATUS_NO_MEMORY) {
                return OMC_EXIF_NOMEM;
            }
            if (st != OMC_STATUS_OK) {
                return OMC_EXIF_LIMIT;
            }
            return OMC_EXIF_OK;
        }
    }

    if (type == 7U || count > 1U || type == 2U || type == 129U) {
        omc_byte_ref ref2;
        omc_exif_status xs2;

        xs2 = omc_exif_store_ref(ctx, raw, raw_size, &ref2);
        if (xs2 != OMC_EXIF_OK) {
            return xs2;
        }
        entry.value.kind = (type == 7U || type == 2U || type == 129U)
                               ? OMC_VAL_BYTES
                               : OMC_VAL_ARRAY;
        switch (type) {
        case 1U: entry.value.elem_type = OMC_ELEM_U8; break;
        case 3U: entry.value.elem_type = OMC_ELEM_U16; break;
        case 4U:
        case 13U: entry.value.elem_type = OMC_ELEM_U32; break;
        case 5U: entry.value.elem_type = OMC_ELEM_URATIONAL; break;
        case 6U: entry.value.elem_type = OMC_ELEM_I8; break;
        case 8U: entry.value.elem_type = OMC_ELEM_I16; break;
        case 9U: entry.value.elem_type = OMC_ELEM_I32; break;
        case 10U: entry.value.elem_type = OMC_ELEM_SRATIONAL; break;
        case 11U: entry.value.elem_type = OMC_ELEM_F32_BITS; break;
        case 12U: entry.value.elem_type = OMC_ELEM_F64_BITS; break;
        case 16U:
        case 18U: entry.value.elem_type = OMC_ELEM_U64; break;
        case 17U: entry.value.elem_type = OMC_ELEM_I64; break;
        default: entry.value.elem_type = OMC_ELEM_U8; break;
        }
        entry.value.count =
            (entry.value.kind == OMC_VAL_ARRAY) ? (omc_u32)count
                                                : (omc_u32)raw_size;
        entry.value.u.ref = ref2;
        st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
        if (st == OMC_STATUS_NO_MEMORY) {
            return OMC_EXIF_NOMEM;
        }
        if (st != OMC_STATUS_OK) {
            return OMC_EXIF_LIMIT;
        }
        return OMC_EXIF_OK;
    }

    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.count = 1U;
    switch (type) {
    case 1U:
        entry.value.elem_type = OMC_ELEM_U8;
        entry.value.u.u64 = raw[0];
        break;
    case 3U:
        if (!omc_exif_read_u16(ctx->cfg, raw, (omc_size)raw_size, 0U, &u16a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_U16;
        entry.value.u.u64 = u16a;
        break;
    case 4U:
    case 13U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_U32;
        entry.value.u.u64 = u32a;
        break;
    case 5U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)
            || !omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 4U,
                                  &u32b)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_URATIONAL;
        entry.value.u.ur.numer = u32a;
        entry.value.u.ur.denom = u32b;
        break;
    case 6U:
        entry.value.elem_type = OMC_ELEM_I8;
        i64v = (omc_u64)(omc_s64)(signed char)raw[0];
        entry.value.u.i64 = (omc_s64)i64v;
        break;
    case 8U:
        if (!omc_exif_read_u16(ctx->cfg, raw, (omc_size)raw_size, 0U, &u16a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_I16;
        entry.value.u.i64 = (omc_s16)u16a;
        break;
    case 9U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_I32;
        entry.value.u.i64 = (omc_s32)u32a;
        break;
    case 10U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)
            || !omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 4U,
                                  &u32b)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_SRATIONAL;
        entry.value.u.sr.numer = (omc_s32)u32a;
        entry.value.u.sr.denom = (omc_s32)u32b;
        break;
    case 11U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_F32_BITS;
        entry.value.u.f32_bits = u32a;
        break;
    case 12U:
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U, &u64a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_F64_BITS;
        entry.value.u.f64_bits = u64a;
        break;
    case 16U:
    case 18U:
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U, &u64a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_U64;
        entry.value.u.u64 = u64a;
        break;
    case 17U:
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U, &u64a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_I64;
        entry.value.u.i64 = (omc_s64)u64a;
        break;
    default:
        return OMC_EXIF_UNSUPPORTED;
    }

    st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
    if (st == OMC_STATUS_NO_MEMORY) {
        return OMC_EXIF_NOMEM;
    }
    if (st != OMC_STATUS_OK) {
        return OMC_EXIF_LIMIT;
    }
    return OMC_EXIF_OK;
}

static int
omc_exif_schedule_pointer_task(omc_exif_ctx* ctx, omc_exif_ifd_kind kind,
                               omc_u32 index, omc_u16 type,
                               const omc_u8* raw, omc_u64 raw_size)
{
    omc_u64 offset;

    if (type == 4U || type == 13U) {
        omc_u32 off32;

        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &off32)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        offset = off32;
    } else if (type == 16U || type == 18U) {
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U,
                               &offset)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
    } else {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (!omc_exif_push_task(ctx, kind, index, offset)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_process_ifd(omc_exif_ctx* ctx, omc_exif_task task)
{
    omc_u64 entry_count64;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u64 next_ifd_off;
    omc_u32 entry_count32;
    omc_byte_ref token_ref;
    omc_exif_status tok_status;
    omc_u32 i;

    if (ctx->res.ifds_needed >= ctx->opts.limits.max_ifds) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_IFDS, task.offset, 0U);
        return 0;
    }

    if (task.offset > (omc_u64)ctx->size
        || ctx->cfg.count_size > ((omc_u64)ctx->size - task.offset)) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (!ctx->cfg.big_tiff) {
        omc_u16 count16;

        if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, task.offset,
                               &count16)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        entry_count64 = count16;
    } else {
        if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size, task.offset,
                               &entry_count64)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
    }

    if (entry_count64 > (omc_u64)ctx->opts.limits.max_entries_per_ifd) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_IFD, task.offset, 0U);
        return 0;
    }
    if (entry_count64 > ((omc_u64)(~(omc_u32)0))) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_IFD, task.offset, 0U);
        return 0;
    }
    if (ctx->res.entries_decoded > ctx->opts.limits.max_total_entries
        || entry_count64
               > (omc_u64)(ctx->opts.limits.max_total_entries
                           - ctx->res.entries_decoded)) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, task.offset,
                            0U);
        return 0;
    }

    entry_count32 = (omc_u32)entry_count64;
    entry_table_off = task.offset + ctx->cfg.count_size;
    if (!omc_exif_mul_u64(ctx->cfg.entry_size, entry_count64, &table_bytes)) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_IFD, task.offset, 0U);
        return 0;
    }
    if (entry_table_off > (omc_u64)ctx->size
        || table_bytes > ((omc_u64)ctx->size - entry_table_off)
        || ctx->cfg.next_size > (((omc_u64)ctx->size - entry_table_off)
                                 - table_bytes)) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    omc_exif_emit_ifd(ctx, task.kind, task.index, task.offset);

    memset(&token_ref, 0, sizeof(token_ref));
    if (!ctx->measure_only) {
        tok_status = omc_exif_make_token(ctx, task.kind, task.index, &token_ref);
        if (tok_status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, tok_status);
            return 0;
        }
    }

    for (i = 0U; i < entry_count32; ++i) {
        omc_u64 entry_off;
        omc_u16 tag;
        omc_u16 type;
        omc_u64 count;
        omc_u64 value_off;
        const omc_u8* raw;
        omc_u64 raw_size;
        int emit_pointer;
        omc_exif_status estatus;

        entry_off = entry_table_off + ((omc_u64)i * ctx->cfg.entry_size);
        if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, entry_off,
                               &tag)
            || !omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, entry_off + 2U,
                                  &type)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }

        if (!ctx->cfg.big_tiff) {
            omc_u32 count32;

            if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size,
                                   entry_off + 4U, &count32)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            count = count32;
            value_off = entry_off + 8U;
        } else {
            if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size,
                                   entry_off + 4U, &count)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            value_off = entry_off + 12U;
        }

        if (!omc_exif_resolve_raw(ctx, value_off, type, count, &raw, &raw_size)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }

        emit_pointer = 1;
        if ((tag == 0x8769U || tag == 0x8825U || tag == 0xA005U
             || tag == 0x014AU)
            && !ctx->opts.include_pointer_tags) {
            emit_pointer = 0;
        }

        if (!ctx->measure_only && emit_pointer) {
            estatus = omc_exif_add_entry(ctx, &token_ref, tag, type, count, raw,
                                         raw_size, i, OMC_ENTRY_FLAG_NONE);
            if (estatus != OMC_EXIF_OK) {
                if (estatus == OMC_EXIF_LIMIT) {
                    omc_exif_mark_limit(ctx, OMC_EXIF_LIM_VALUE_COUNT,
                                        task.offset, tag);
                } else {
                    omc_exif_update_status(&ctx->res, estatus);
                }
                return 0;
            }
        }
        if (emit_pointer) {
            ctx->res.entries_decoded += 1U;
        }

        if (tag == 0x8769U || tag == 0x8825U || tag == 0xA005U) {
            omc_exif_ifd_kind pkind;

            pkind = OMC_EXIF_EXIF_IFD;
            if (tag == 0x8825U) {
                pkind = OMC_EXIF_GPS_IFD;
            } else if (tag == 0xA005U) {
                pkind = OMC_EXIF_INTEROP_IFD;
            }
            if (!omc_exif_schedule_pointer_task(ctx, pkind, 0U, type, raw,
                                                raw_size)) {
                return 0;
            }
        } else if (tag == 0x014AU) {
            omc_u64 sub_index;
            const omc_u8* p;
            omc_u64 offv;

            p = raw;
            for (sub_index = 0U; sub_index < count; ++sub_index) {
                if (type == 4U || type == 13U) {
                    omc_u32 off32;

                    if (!omc_exif_read_u32(ctx->cfg, p, (omc_size)raw_size,
                                           (omc_u64)(sub_index * 4U), &off32)) {
                        ctx->res.status = OMC_EXIF_MALFORMED;
                        return 0;
                    }
                    offv = off32;
                } else if (type == 16U || type == 18U) {
                    if (!omc_exif_read_u64(ctx->cfg, p, (omc_size)raw_size,
                                           (omc_u64)(sub_index * 8U), &offv)) {
                        ctx->res.status = OMC_EXIF_MALFORMED;
                        return 0;
                    }
                } else {
                    ctx->res.status = OMC_EXIF_MALFORMED;
                    return 0;
                }
                if (!omc_exif_push_task(ctx, OMC_EXIF_SUB_IFD,
                                        ctx->next_subifd_index, offv)) {
                    return 0;
                }
                ctx->next_subifd_index += 1U;
            }
        }
    }

    next_ifd_off = entry_table_off + table_bytes;
    if (task.kind == OMC_EXIF_IFD) {
        omc_u64 next_offset;

        if (!ctx->cfg.big_tiff) {
            omc_u32 next32;

            if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size, next_ifd_off,
                                   &next32)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            next_offset = next32;
        } else {
            if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size, next_ifd_off,
                                   &next_offset)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
        }

        if (next_offset != 0U) {
            if (!omc_exif_push_task(ctx, OMC_EXIF_IFD,
                                    ctx->next_ifd_index + 1U, next_offset)) {
                return 0;
            }
            ctx->next_ifd_index += 1U;
        }
    }

    return 1;
}

static omc_exif_res
omc_exif_run(const omc_u8* tiff_bytes, omc_size tiff_size, omc_store* store,
             omc_block_id source_block, omc_exif_ifd_ref* out_ifds,
             omc_u32 ifd_cap,
             const omc_exif_opts* opts, int measure_only)
{
    omc_exif_ctx ctx;
    omc_exif_task task;

    memset(&ctx, 0, sizeof(ctx));
    ctx.bytes = tiff_bytes;
    ctx.size = tiff_size;
    ctx.store = store;
    ctx.source_block = source_block;
    ctx.measure_only = measure_only;
    ctx.out_ifds = out_ifds;
    ctx.ifd_cap = ifd_cap;
    omc_exif_res_init(&ctx.res);

    if (opts == (const omc_exif_opts*)0) {
        omc_exif_opts_init(&ctx.opts);
    } else {
        ctx.opts = *opts;
    }

    if (ctx.opts.limits.max_ifds == 0U) {
        ctx.opts.limits.max_ifds = 128U;
    }
    if (ctx.opts.limits.max_entries_per_ifd == 0U) {
        ctx.opts.limits.max_entries_per_ifd = 4096U;
    }
    if (ctx.opts.limits.max_total_entries == 0U) {
        ctx.opts.limits.max_total_entries = 200000U;
    }
    if (ctx.opts.limits.max_value_bytes == 0U) {
        ctx.opts.limits.max_value_bytes = 16U * 1024U * 1024U;
    }

    if (tiff_bytes == (const omc_u8*)0) {
        ctx.res.status = OMC_EXIF_MALFORMED;
        return ctx.res;
    }
    if (!measure_only && store == (omc_store*)0) {
        ctx.res.status = OMC_EXIF_NOMEM;
        return ctx.res;
    }

    if (!omc_exif_parse_header(&ctx)) {
        return ctx.res;
    }
    if (!omc_exif_push_task(&ctx, OMC_EXIF_IFD, 0U, ctx.cfg.first_ifd)) {
        return ctx.res;
    }

    while (omc_exif_pop_task(&ctx, &task)) {
        if (!omc_exif_process_ifd(&ctx, task)) {
            break;
        }
    }

    return ctx.res;
}

void
omc_exif_opts_init(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->include_pointer_tags = 1;
    opts->decode_printim = 1;
    opts->decode_geotiff = 1;
    opts->decode_makernote = 0;
    opts->decode_embedded_containers = 0;
    opts->tokens.ifd_prefix = "ifd";
    opts->tokens.subifd_prefix = "subifd";
    opts->tokens.exif_ifd_token = "exififd";
    opts->tokens.gps_ifd_token = "gpsifd";
    opts->tokens.interop_ifd_token = "interopifd";
    opts->limits.max_ifds = 128U;
    opts->limits.max_entries_per_ifd = 4096U;
    opts->limits.max_total_entries = 200000U;
    opts->limits.max_value_bytes = 16U * 1024U * 1024U;
}

omc_exif_res
omc_exif_dec(const omc_u8* tiff_bytes, omc_size tiff_size,
             omc_store* store, omc_block_id source_block,
             omc_exif_ifd_ref* out_ifds, omc_u32 ifd_cap,
             const omc_exif_opts* opts)
{
    return omc_exif_run(tiff_bytes, tiff_size, store, source_block, out_ifds,
                        ifd_cap, opts,
                        0);
}

omc_exif_res
omc_exif_meas(const omc_u8* tiff_bytes, omc_size tiff_size,
              const omc_exif_opts* opts)
{
    return omc_exif_run(tiff_bytes, tiff_size, (omc_store*)0,
                        OMC_INVALID_BLOCK_ID,
                        (omc_exif_ifd_ref*)0, 0U, opts, 1);
}
