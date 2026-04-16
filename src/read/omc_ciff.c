#include "read/omc_ciff.h"

#include <string.h>
#include <time.h>

typedef struct omc_ciff_cfg {
    int little_endian;
} omc_ciff_cfg;

static void
omc_ciff_init_res(omc_exif_res* res)
{
    if (res == (omc_exif_res*)0) {
        return;
    }

    res->status = OMC_EXIF_UNSUPPORTED;
    res->ifds_written = 0U;
    res->ifds_needed = 0U;
    res->entries_decoded = 0U;
    res->limit_reason = OMC_EXIF_LIM_NONE;
    res->limit_ifd_offset = 0U;
    res->limit_tag = 0U;
}

static void
omc_ciff_update_status(omc_exif_res* res, omc_exif_status status)
{
    if (res == (omc_exif_res*)0) {
        return;
    }
    if (res->status == OMC_EXIF_LIMIT || res->status == OMC_EXIF_NOMEM) {
        return;
    }
    if (status == OMC_EXIF_LIMIT || status == OMC_EXIF_NOMEM) {
        res->status = status;
        return;
    }
    if (res->status == OMC_EXIF_MALFORMED) {
        return;
    }
    if (status == OMC_EXIF_MALFORMED) {
        res->status = status;
        return;
    }
    if (res->status == OMC_EXIF_TRUNCATED) {
        return;
    }
    if (status == OMC_EXIF_TRUNCATED) {
        res->status = status;
        return;
    }
    if (res->status == OMC_EXIF_UNSUPPORTED && status == OMC_EXIF_OK) {
        res->status = status;
    }
}

static void
omc_ciff_set_limit(omc_exif_res* res, omc_exif_lim_rsn reason,
                   omc_u16 limit_tag)
{
    if (res == (omc_exif_res*)0) {
        return;
    }
    if (res->status != OMC_EXIF_LIMIT) {
        res->limit_reason = reason;
        res->limit_tag = limit_tag;
    }
    omc_ciff_update_status(res, OMC_EXIF_LIMIT);
}

static int
omc_ciff_read_u16le(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 2U) {
        return 0;
    }

    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 0)
                           | (((omc_u16)bytes[(omc_size)offset + 1U]) << 8));
    return 1;
}

static int
omc_ciff_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 2U) {
        return 0;
    }

    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                           | (((omc_u16)bytes[(omc_size)offset + 1U]) << 0));
    return 1;
}

static int
omc_ciff_read_u32le(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 0)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 3U]) << 24);
    return 1;
}

static int
omc_ciff_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    return 1;
}

static int
omc_ciff_read_u16(omc_ciff_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u16* out_value)
{
    if (cfg.little_endian) {
        return omc_ciff_read_u16le(bytes, size, offset, out_value);
    }
    return omc_ciff_read_u16be(bytes, size, offset, out_value);
}

static int
omc_ciff_read_u32(omc_ciff_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u32* out_value)
{
    if (cfg.little_endian) {
        return omc_ciff_read_u32le(bytes, size, offset, out_value);
    }
    return omc_ciff_read_u32be(bytes, size, offset, out_value);
}

static int
omc_ciff_read_i32(omc_ciff_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_s32* out_value)
{
    omc_u32 u32v;

    if (out_value == (omc_s32*)0) {
        return 0;
    }
    if (!omc_ciff_read_u32(cfg, bytes, size, offset, &u32v)) {
        return 0;
    }
    *out_value = (omc_s32)u32v;
    return 1;
}

static omc_u16
omc_ciff_tag_id(omc_u16 tag)
{
    return (omc_u16)(tag & 0x3FFFU);
}

static omc_u16
omc_ciff_type_bits(omc_u16 tag)
{
    return (omc_u16)(tag & 0x3800U);
}

static omc_u16
omc_ciff_loc_bits(omc_u16 tag)
{
    return (omc_u16)(tag & 0xC000U);
}

static int
omc_ciff_is_directory(omc_u16 tag)
{
    omc_u16 type_bits;

    type_bits = omc_ciff_type_bits(tag);
    return type_bits == 0x2800U || type_bits == 0x3000U;
}

static int
omc_ciff_hex_nibble(char c, omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (c >= '0' && c <= '9') {
        *out_value = (omc_u16)(c - '0');
        return 1;
    }
    if (c >= 'a' && c <= 'f') {
        *out_value = (omc_u16)(10 + (c - 'a'));
        return 1;
    }
    if (c >= 'A' && c <= 'F') {
        *out_value = (omc_u16)(10 + (c - 'A'));
        return 1;
    }
    return 0;
}

static int
omc_ciff_parse_dir_id(const char* ifd_token, omc_size ifd_size,
                      omc_u16* out_dir_id)
{
    omc_size i;
    omc_u16 dir_id;

    if (ifd_token == (const char*)0 || out_dir_id == (omc_u16*)0) {
        return 0;
    }
    if (ifd_size < 10U || memcmp(ifd_token, "ciff_", 5U) != 0
        || ifd_token[9] != '_') {
        return 0;
    }

    dir_id = 0U;
    for (i = 5U; i < 9U; ++i) {
        omc_u16 nibble;

        if (!omc_ciff_hex_nibble(ifd_token[i], &nibble)) {
            return 0;
        }
        dir_id = (omc_u16)((dir_id << 4) | nibble);
    }

    *out_dir_id = dir_id;
    return 1;
}

static int
omc_ciff_make_child_token(char* out_name, omc_size out_cap, omc_u16 tag_id,
                          omc_u32 dir_index, omc_size* out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    char digits[16];
    omc_size digit_count;
    omc_size i;
    omc_u32 v;
    omc_size needed;

    if (out_name == (char*)0 || out_size == (omc_size*)0) {
        return 0;
    }

    digits[0] = '0';
    digit_count = 0U;
    v = dir_index;
    do {
        digits[digit_count] = (char)('0' + (v % 10U));
        digit_count += 1U;
        v /= 10U;
    } while (v != 0U && digit_count < sizeof(digits));
    if (v != 0U) {
        return 0;
    }

    needed = 10U + digit_count;
    if (needed + 1U > out_cap) {
        return 0;
    }

    memcpy(out_name, "ciff_", 5U);
    out_name[5] = hex[(tag_id >> 12) & 0x0FU];
    out_name[6] = hex[(tag_id >> 8) & 0x0FU];
    out_name[7] = hex[(tag_id >> 4) & 0x0FU];
    out_name[8] = hex[(tag_id >> 0) & 0x0FU];
    out_name[9] = '_';
    for (i = 0U; i < digit_count; ++i) {
        out_name[10U + i] = digits[digit_count - 1U - i];
    }
    out_name[needed] = '\0';
    *out_size = needed;
    return 1;
}

static int
omc_ciff_make_suffix_token(char* out_name, omc_size out_cap,
                           const char* parent_ifd, omc_size parent_ifd_size,
                           const char* suffix, omc_size suffix_size,
                           omc_size* out_size)
{
    omc_size needed;

    if (out_name == (char*)0 || parent_ifd == (const char*)0
        || suffix == (const char*)0 || out_size == (omc_size*)0) {
        return 0;
    }

    needed = parent_ifd_size + 1U + suffix_size;
    if (needed + 1U > out_cap) {
        return 0;
    }

    memcpy(out_name, parent_ifd, parent_ifd_size);
    out_name[parent_ifd_size] = '_';
    memcpy(out_name + parent_ifd_size + 1U, suffix, suffix_size);
    out_name[needed] = '\0';
    *out_size = needed;
    return 1;
}

static int
omc_ciff_make_ifd0_token(const omc_exif_opts* opts, char* out_name,
                         omc_size out_cap, omc_size* out_size)
{
    const char* prefix;
    omc_size prefix_len;

    if (out_name == (char*)0 || out_size == (omc_size*)0) {
        return 0;
    }

    prefix = (opts != (const omc_exif_opts*)0
                  && opts->tokens.ifd_prefix != (const char*)0)
                 ? opts->tokens.ifd_prefix
                 : "ifd";
    prefix_len = strlen(prefix);
    if (prefix_len + 2U > out_cap) {
        return 0;
    }

    memcpy(out_name, prefix, prefix_len);
    out_name[prefix_len] = '0';
    out_name[prefix_len + 1U] = '\0';
    *out_size = prefix_len + 1U;
    return 1;
}

static omc_u16
omc_ciff_rotation_to_orientation(omc_s32 degrees)
{
    switch (degrees) {
    case 0: return 1U;
    case 180:
    case -180: return 3U;
    case 90:
    case -270: return 6U;
    case 270:
    case -90: return 8U;
    default: return 1U;
    }
}

static int
omc_ciff_localtime_copy(time_t raw_time, struct tm* out_tm)
{
    if (out_tm == (struct tm*)0) {
        return 0;
    }

#if defined(_MSC_VER)
    return localtime_s(out_tm, &raw_time) == 0;
#elif defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) \
    || defined(_GNU_SOURCE)
    return localtime_r(&raw_time, out_tm) != (struct tm*)0;
#else
    {
        struct tm* tm_parts;

        tm_parts = localtime(&raw_time);
        if (tm_parts == (struct tm*)0) {
            return 0;
        }
        *out_tm = *tm_parts;
        return 1;
    }
#endif
}

static int
omc_ciff_format_datetime(omc_u32 unix_seconds, char out_text[20])
{
    time_t raw_time;
    struct tm tm_parts;
    size_t written;

    raw_time = (time_t)unix_seconds;
    if (!omc_ciff_localtime_copy(raw_time, &tm_parts)) {
        return 0;
    }

    written = strftime(out_text, 20U, "%Y:%m:%d %H:%M:%S", &tm_parts);
    return written == 19U;
}

static int
omc_ciff_tag_is_padded_ascii_text(omc_u16 dir_id, omc_u16 tag_id)
{
    switch (dir_id) {
    case 0x2804U: return tag_id == 0x0805U || tag_id == 0x0815U;
    case 0x2807U: return tag_id == 0x0810U;
    case 0x3004U:
        return tag_id == 0x080BU || tag_id == 0x080CU || tag_id == 0x080DU;
    case 0x300AU: return tag_id == 0x0816U || tag_id == 0x0817U;
    default: return 0;
    }
}

static int
omc_ciff_trailing_zero_bytes(const omc_u8* raw, omc_size raw_size,
                             omc_size offset)
{
    omc_size i;

    if (raw == (const omc_u8*)0 || offset > raw_size) {
        return 0;
    }

    for (i = offset; i < raw_size; ++i) {
        if (raw[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

static int
omc_ciff_extract_padded_ascii_text(const omc_u8* raw, omc_size raw_size,
                                   const char** out_text,
                                   omc_size* out_text_size)
{
    omc_size end;
    omc_size i;

    if (raw == (const omc_u8*)0 || out_text == (const char**)0
        || out_text_size == (omc_size*)0) {
        return 0;
    }

    *out_text = (const char*)raw;
    *out_text_size = 0U;
    if (raw_size == 0U) {
        return 1;
    }

    end = 0U;
    while (end < raw_size && raw[end] != 0U) {
        end += 1U;
    }
    if (end < raw_size) {
        for (i = end + 1U; i < raw_size; ++i) {
            if (raw[i] != 0U) {
                return 0;
            }
        }
    }

    *out_text_size = end;
    return 1;
}

static int
omc_ciff_store_bytes(omc_store* store, const void* src, omc_size size,
                     omc_byte_ref* out_ref)
{
    omc_status st;

    if (store == (omc_store*)0 || out_ref == (omc_byte_ref*)0) {
        return 0;
    }

    st = omc_arena_append(&store->arena, src, size, out_ref);
    return st == OMC_STATUS_OK;
}

static int
omc_ciff_add_entry(omc_store* store, omc_block_id block_id,
                   const char* ifd_name, omc_size ifd_size, omc_u16 tag,
                   omc_u32 order_in_block, omc_u16 wire_code,
                   omc_u32 wire_count, omc_entry_flags flags,
                   const omc_val* value, omc_exif_res* res)
{
    omc_entry entry;
    omc_byte_ref ifd_ref;
    omc_status st;

    if (store == (omc_store*)0 || ifd_name == (const char*)0
        || value == (const omc_val*)0 || res == (omc_exif_res*)0) {
        return 0;
    }

    if (!omc_ciff_store_bytes(store, ifd_name, ifd_size, &ifd_ref)) {
        omc_ciff_update_status(res, OMC_EXIF_NOMEM);
        return 0;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, tag);
    entry.value = *value;
    entry.origin.block = block_id;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = wire_code;
    entry.origin.wire_count = wire_count;
    entry.flags = flags;

    st = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    if (st == OMC_STATUS_NO_MEMORY) {
        omc_ciff_update_status(res, OMC_EXIF_NOMEM);
        return 0;
    }
    if (st != OMC_STATUS_OK) {
        omc_ciff_update_status(res, OMC_EXIF_LIMIT);
        return 0;
    }

    res->entries_decoded += 1U;
    omc_ciff_update_status(res, OMC_EXIF_OK);
    return 1;
}

static int
omc_ciff_emit_scalar_u32(omc_store* store, omc_block_id block_id,
                         const char* ifd_name, omc_size ifd_size, omc_u16 tag,
                         omc_u32 order_in_block, omc_u16 wire_code,
                         omc_entry_flags flags, omc_u32 value32,
                         omc_exif_res* res)
{
    omc_val value;

    omc_val_make_u32(&value, value32);
    return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag,
                              order_in_block, wire_code, 1U, flags, &value,
                              res);
}

static int
omc_ciff_emit_scalar_u16(omc_store* store, omc_block_id block_id,
                         const char* ifd_name, omc_size ifd_size, omc_u16 tag,
                         omc_u32 order_in_block, omc_u16 wire_code,
                         omc_entry_flags flags, omc_u16 value16,
                         omc_exif_res* res)
{
    omc_val value;

    omc_val_make_u32(&value, value16);
    value.elem_type = OMC_ELEM_U16;
    return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag,
                              order_in_block, wire_code, 1U, flags, &value,
                              res);
}

static int
omc_ciff_emit_scalar_i16(omc_store* store, omc_block_id block_id,
                         const char* ifd_name, omc_size ifd_size, omc_u16 tag,
                         omc_u32 order_in_block, omc_u16 wire_code,
                         omc_entry_flags flags, omc_s16 value16,
                         omc_exif_res* res)
{
    omc_val value;

    omc_val_make_i64(&value, value16);
    value.elem_type = OMC_ELEM_I16;
    return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag,
                              order_in_block, wire_code, 1U, flags, &value,
                              res);
}

static int
omc_ciff_emit_scalar_i32(omc_store* store, omc_block_id block_id,
                         const char* ifd_name, omc_size ifd_size, omc_u16 tag,
                         omc_u32 order_in_block, omc_u16 wire_code,
                         omc_entry_flags flags, omc_s32 value32,
                         omc_exif_res* res)
{
    omc_val value;

    omc_val_make_i64(&value, value32);
    value.elem_type = OMC_ELEM_I32;
    return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag,
                              order_in_block, wire_code, 1U, flags, &value,
                              res);
}

static int
omc_ciff_emit_scalar_f32_bits(omc_store* store, omc_block_id block_id,
                              const char* ifd_name, omc_size ifd_size,
                              omc_u16 tag, omc_u32 order_in_block,
                              omc_u16 wire_code, omc_entry_flags flags,
                              omc_u32 bits, omc_exif_res* res)
{
    omc_val value;

    omc_val_make_f32_bits(&value, bits);
    return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag,
                              order_in_block, wire_code, 1U, flags, &value,
                              res);
}

static int
omc_ciff_emit_text(omc_store* store, omc_block_id block_id, const char* ifd_name,
                   omc_size ifd_size, omc_u16 tag, omc_u32 order_in_block,
                   omc_u16 wire_code, omc_entry_flags flags,
                   const char* text, omc_size text_size, omc_exif_res* res)
{
    omc_val value;
    omc_byte_ref ref;

    if (text == (const char*)0) {
        return 0;
    }
    if (!omc_ciff_store_bytes(store, text, text_size, &ref)) {
        omc_ciff_update_status(res, OMC_EXIF_NOMEM);
        return 0;
    }

    omc_val_make_text(&value, ref, OMC_TEXT_ASCII);
    return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag,
                              order_in_block, wire_code, (omc_u32)text_size,
                              flags, &value, res);
}

static int
omc_ciff_emit_suffix_scalar_u16(omc_store* store, omc_block_id block_id,
                                const char* parent_ifd,
                                omc_size parent_ifd_size,
                                const char* suffix, omc_size suffix_size,
                                omc_u16 tag, omc_u32 order_in_block,
                                omc_u16 wire_code, omc_entry_flags flags,
                                omc_u16 value16, omc_exif_res* res)
{
    char ifd_name[64];
    omc_size ifd_size;

    if (!omc_ciff_make_suffix_token(ifd_name, sizeof(ifd_name), parent_ifd,
                                    parent_ifd_size, suffix, suffix_size,
                                    &ifd_size)) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, wire_code);
        return 0;
    }

    return omc_ciff_emit_scalar_u16(store, block_id, ifd_name, ifd_size, tag,
                                    order_in_block, wire_code, flags, value16,
                                    res);
}

static int
omc_ciff_emit_suffix_scalar_u32(omc_store* store, omc_block_id block_id,
                                const char* parent_ifd,
                                omc_size parent_ifd_size,
                                const char* suffix, omc_size suffix_size,
                                omc_u16 tag, omc_u32 order_in_block,
                                omc_u16 wire_code, omc_entry_flags flags,
                                omc_u32 value32, omc_exif_res* res)
{
    char ifd_name[64];
    omc_size ifd_size;

    if (!omc_ciff_make_suffix_token(ifd_name, sizeof(ifd_name), parent_ifd,
                                    parent_ifd_size, suffix, suffix_size,
                                    &ifd_size)) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, wire_code);
        return 0;
    }

    return omc_ciff_emit_scalar_u32(store, block_id, ifd_name, ifd_size, tag,
                                    order_in_block, wire_code, flags, value32,
                                    res);
}

static int
omc_ciff_emit_suffix_scalar_i16(omc_store* store, omc_block_id block_id,
                                const char* parent_ifd,
                                omc_size parent_ifd_size,
                                const char* suffix, omc_size suffix_size,
                                omc_u16 tag, omc_u32 order_in_block,
                                omc_u16 wire_code, omc_entry_flags flags,
                                omc_s16 value16, omc_exif_res* res)
{
    char ifd_name[64];
    omc_size ifd_size;

    if (!omc_ciff_make_suffix_token(ifd_name, sizeof(ifd_name), parent_ifd,
                                    parent_ifd_size, suffix, suffix_size,
                                    &ifd_size)) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, wire_code);
        return 0;
    }

    return omc_ciff_emit_scalar_i16(store, block_id, ifd_name, ifd_size, tag,
                                    order_in_block, wire_code, flags, value16,
                                    res);
}

static int
omc_ciff_emit_suffix_scalar_i32(omc_store* store, omc_block_id block_id,
                                const char* parent_ifd,
                                omc_size parent_ifd_size,
                                const char* suffix, omc_size suffix_size,
                                omc_u16 tag, omc_u32 order_in_block,
                                omc_u16 wire_code, omc_entry_flags flags,
                                omc_s32 value32, omc_exif_res* res)
{
    char ifd_name[64];
    omc_size ifd_size;

    if (!omc_ciff_make_suffix_token(ifd_name, sizeof(ifd_name), parent_ifd,
                                    parent_ifd_size, suffix, suffix_size,
                                    &ifd_size)) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, wire_code);
        return 0;
    }

    return omc_ciff_emit_scalar_i32(store, block_id, ifd_name, ifd_size, tag,
                                    order_in_block, wire_code, flags, value32,
                                    res);
}

static int
omc_ciff_emit_suffix_scalar_f32_bits(omc_store* store, omc_block_id block_id,
                                     const char* parent_ifd,
                                     omc_size parent_ifd_size,
                                     const char* suffix, omc_size suffix_size,
                                     omc_u16 tag, omc_u32 order_in_block,
                                     omc_u16 wire_code, omc_entry_flags flags,
                                     omc_u32 bits, omc_exif_res* res)
{
    char ifd_name[64];
    omc_size ifd_size;

    if (!omc_ciff_make_suffix_token(ifd_name, sizeof(ifd_name), parent_ifd,
                                    parent_ifd_size, suffix, suffix_size,
                                    &ifd_size)) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, wire_code);
        return 0;
    }

    return omc_ciff_emit_scalar_f32_bits(store, block_id, ifd_name, ifd_size,
                                         tag, order_in_block, wire_code,
                                         flags, bits, res);
}

static int
omc_ciff_emit_suffix_text(omc_store* store, omc_block_id block_id,
                          const char* parent_ifd, omc_size parent_ifd_size,
                          const char* suffix, omc_size suffix_size,
                          omc_u16 tag, omc_u32 order_in_block,
                          omc_u16 wire_code, omc_entry_flags flags,
                          const char* text, omc_size text_size,
                          omc_exif_res* res)
{
    char ifd_name[64];
    omc_size ifd_size;

    if (!omc_ciff_make_suffix_token(ifd_name, sizeof(ifd_name), parent_ifd,
                                    parent_ifd_size, suffix, suffix_size,
                                    &ifd_size)) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, wire_code);
        return 0;
    }

    return omc_ciff_emit_text(store, block_id, ifd_name, ifd_size, tag,
                              order_in_block, wire_code, flags, text,
                              text_size, res);
}

static int
omc_ciff_contains_nul(const omc_u8* raw, omc_size size)
{
    omc_size i;

    for (i = 0U; i < size; ++i) {
        if (raw[i] == 0U) {
            return 1;
        }
    }
    return 0;
}

static int
omc_ciff_add_raw_entry(omc_ciff_cfg cfg, const omc_exif_opts* opts,
                       const omc_u8* dir_bytes, const char* ifd_name,
                       omc_size ifd_size,
                       omc_u16 raw_tag, omc_u16 tag_id, omc_u64 value_off,
                       omc_u64 value_bytes, omc_store* store,
                       omc_block_id block_id, omc_u32 order_in_block,
                       omc_exif_res* res)
{
    omc_val value;
    omc_byte_ref ref;
    const omc_u8* raw;
    omc_size raw_size;
    omc_u16 type_bits;
    omc_u16 dir_id;
    int have_dir_id;

    (void)opts;

    raw = dir_bytes + (omc_size)value_off;
    raw_size = (omc_size)value_bytes;
    type_bits = omc_ciff_type_bits(raw_tag);
    have_dir_id = omc_ciff_parse_dir_id(ifd_name, ifd_size, &dir_id);
    omc_val_init(&value);

    if (value_bytes > opts->limits.max_value_bytes) {
        value.kind = OMC_VAL_EMPTY;
        value.count = 0U;
        return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag_id,
                                  order_in_block, raw_tag,
                                  (value_bytes > (omc_u64)~(omc_u32)0)
                                      ? ~(omc_u32)0
                                      : (omc_u32)value_bytes,
                                  OMC_ENTRY_FLAG_TRUNCATED, &value, res);
    }

    if (type_bits == 0x0000U) {
        if (raw_size == 1U) {
            omc_val_make_u32(&value, raw[0]);
            value.elem_type = OMC_ELEM_U8;
            return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size,
                                      tag_id, order_in_block, raw_tag, 1U,
                                      OMC_ENTRY_FLAG_NONE, &value, res);
        }
        if (!omc_ciff_store_bytes(store, raw, raw_size, &ref)) {
            omc_ciff_update_status(res, OMC_EXIF_NOMEM);
            return 0;
        }
        value.kind = OMC_VAL_ARRAY;
        value.elem_type = OMC_ELEM_U8;
        value.count = (omc_u32)raw_size;
        value.u.ref = ref;
        return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag_id,
                                  order_in_block, raw_tag, value.count,
                                  OMC_ENTRY_FLAG_NONE, &value, res);
    }

    if (type_bits == 0x0800U) {
        omc_size trimmed;
        const char* padded_text;
        omc_size padded_text_size;

        if (have_dir_id && omc_ciff_tag_is_padded_ascii_text(dir_id, tag_id)
            && omc_ciff_extract_padded_ascii_text(raw, raw_size, &padded_text,
                                                  &padded_text_size)) {
            if (!omc_ciff_store_bytes(store, padded_text, padded_text_size,
                                      &ref)) {
                omc_ciff_update_status(res, OMC_EXIF_NOMEM);
                return 0;
            }
            omc_val_make_text(&value, ref, OMC_TEXT_ASCII);
            return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size,
                                      tag_id, order_in_block, raw_tag,
                                      (raw_size > (omc_size)~(omc_u32)0)
                                          ? ~(omc_u32)0
                                          : (omc_u32)raw_size,
                                      OMC_ENTRY_FLAG_NONE, &value, res);
        }

        trimmed = raw_size;
        if (trimmed > 0U && raw[trimmed - 1U] == 0U) {
            trimmed -= 1U;
        }
        if (trimmed != 0U && !omc_ciff_contains_nul(raw, trimmed)) {
            if (!omc_ciff_store_bytes(store, raw, trimmed, &ref)) {
                omc_ciff_update_status(res, OMC_EXIF_NOMEM);
                return 0;
            }
            omc_val_make_text(&value, ref, OMC_TEXT_ASCII);
        } else {
            if (!omc_ciff_store_bytes(store, raw, raw_size, &ref)) {
                omc_ciff_update_status(res, OMC_EXIF_NOMEM);
                return 0;
            }
            omc_val_make_bytes(&value, ref);
        }
        return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag_id,
                                  order_in_block, raw_tag,
                                  (raw_size > (omc_size)~(omc_u32)0)
                                      ? ~(omc_u32)0
                                      : (omc_u32)raw_size,
                                  OMC_ENTRY_FLAG_NONE, &value, res);
    }

    if (have_dir_id && type_bits == 0x1000U && raw_size >= 2U
        && omc_ciff_trailing_zero_bytes(raw, raw_size, 2U)) {
        omc_u16 value16;
        int matched;

        matched = 0;
        switch (dir_id) {
        case 0x3002U:
            matched = (tag_id == 0x1010U || tag_id == 0x1011U
                       || tag_id == 0x1016U);
            break;
        case 0x3004U: matched = (tag_id == 0x101CU); break;
        case 0x300AU: matched = (tag_id == 0x100AU); break;
        default: break;
        }

        if (matched) {
            if (!omc_ciff_read_u16(cfg, raw, raw_size, 0U, &value16)) {
                omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
                return 0;
            }
            return omc_ciff_emit_scalar_u16(store, block_id, ifd_name,
                                            ifd_size, tag_id, order_in_block,
                                            raw_tag, OMC_ENTRY_FLAG_NONE,
                                            value16, res);
        }
    }

    if (type_bits == 0x1000U && raw_size == 2U) {
        omc_u16 value16;

        if (!omc_ciff_read_u16(cfg, raw, raw_size, 0U, &value16)) {
            omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
            return 0;
        }
        return omc_ciff_emit_scalar_u16(store, block_id, ifd_name, ifd_size,
                                        tag_id, order_in_block, raw_tag,
                                        OMC_ENTRY_FLAG_NONE, value16, res);
    }

    if (have_dir_id && type_bits == 0x1800U && raw_size >= 4U
        && omc_ciff_trailing_zero_bytes(raw, raw_size, 4U)) {
        omc_u32 value32;
        int matched;
        int emit_f32;

        matched = 0;
        emit_f32 = 0;
        switch (dir_id) {
        case 0x3002U:
            if (tag_id == 0x1807U) {
                matched = 1;
                emit_f32 = 1;
            }
            break;
        case 0x3003U:
            if (tag_id == 0x1814U) {
                matched = 1;
                emit_f32 = 1;
            }
            break;
        case 0x3004U:
            matched = (tag_id == 0x1834U || tag_id == 0x183BU);
            break;
        case 0x300AU:
            matched = (tag_id == 0x1804U || tag_id == 0x1806U
                       || tag_id == 0x1817U);
            break;
        default: break;
        }

        if (matched) {
            if (!omc_ciff_read_u32(cfg, raw, raw_size, 0U, &value32)) {
                omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
                return 0;
            }
            if (emit_f32) {
                return omc_ciff_emit_scalar_f32_bits(
                    store, block_id, ifd_name, ifd_size, tag_id,
                    order_in_block, raw_tag, OMC_ENTRY_FLAG_NONE, value32,
                    res);
            }
            return omc_ciff_emit_scalar_u32(store, block_id, ifd_name,
                                            ifd_size, tag_id, order_in_block,
                                            raw_tag, OMC_ENTRY_FLAG_NONE,
                                            value32, res);
        }
    }

    if (type_bits == 0x1800U && raw_size == 4U) {
        omc_u32 value32;

        if (!omc_ciff_read_u32(cfg, raw, raw_size, 0U, &value32)) {
            omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
            return 0;
        }
        return omc_ciff_emit_scalar_u32(store, block_id, ifd_name, ifd_size,
                                        tag_id, order_in_block, raw_tag,
                                        OMC_ENTRY_FLAG_NONE, value32, res);
    }

    if (!omc_ciff_store_bytes(store, raw, raw_size, &ref)) {
        omc_ciff_update_status(res, OMC_EXIF_NOMEM);
        return 0;
    }

    if (type_bits == 0x1000U) {
        value.kind = OMC_VAL_ARRAY;
        value.elem_type = OMC_ELEM_U16;
        value.count = (omc_u32)(raw_size / 2U);
        value.u.ref = ref;
    } else if (type_bits == 0x1800U) {
        value.kind = OMC_VAL_ARRAY;
        value.elem_type = OMC_ELEM_U32;
        value.count = (omc_u32)(raw_size / 4U);
        value.u.ref = ref;
    } else {
        omc_val_make_bytes(&value, ref);
    }

    return omc_ciff_add_entry(store, block_id, ifd_name, ifd_size, tag_id,
                              order_in_block, raw_tag,
                              (raw_size > (omc_size)~(omc_u32)0)
                                  ? ~(omc_u32)0
                                  : (omc_u32)raw_size,
                              OMC_ENTRY_FLAG_NONE, &value, res);
}

static void
omc_ciff_add_derived_entries(omc_ciff_cfg cfg, const omc_exif_opts* opts,
                             const char* ifd_name, omc_size ifd_size,
                             omc_u16 tag_id, const omc_u8* raw,
                             omc_size raw_size, omc_store* store,
                             omc_block_id block_id, omc_u32 order_in_block,
                             omc_exif_res* res)
{
    omc_u16 dir_id;
    char ifd0[32];
    omc_size ifd0_size;
    const char* exif_ifd;
    omc_size exif_ifd_size;
    omc_u32 next_order;
    const char* text;
    omc_size text_size;
    omc_u32 u32v;
    omc_s32 i32v;
    omc_u16 u16v;
    omc_u16 i;
    char datetime_text[20];

    if (!omc_ciff_parse_dir_id(ifd_name, ifd_size, &dir_id)) {
        return;
    }

    if (!omc_ciff_make_ifd0_token(opts, ifd0, sizeof(ifd0), &ifd0_size)) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, tag_id);
        return;
    }
    exif_ifd = (opts->tokens.exif_ifd_token != (const char*)0)
                   ? opts->tokens.exif_ifd_token
                   : "exififd";
    exif_ifd_size = strlen(exif_ifd);
    next_order = order_in_block + 1U;
    text = (const char*)0;
    text_size = 0U;
    u32v = 0U;
    i32v = 0;
    u16v = 0U;

    if (dir_id == 0x2807U && tag_id == 0x080AU) {
        omc_size make_end;
        omc_size model_begin;
        omc_size model_end;

        make_end = 0U;
        while (make_end < raw_size && raw[make_end] != 0U) {
            make_end += 1U;
        }
        if (make_end > 0U) {
            (void)omc_ciff_emit_suffix_text(
                store, block_id, ifd_name, ifd_size, "makemodel", 9U, 0x0000U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE,
                (const char*)raw, make_end, res);
            (void)omc_ciff_emit_text(store, block_id, ifd0, ifd0_size, 0x010FU,
                                     next_order++, tag_id,
                                     OMC_ENTRY_FLAG_NONE,
                                     (const char*)raw, make_end, res);
        }

        model_begin = make_end;
        if (model_begin < raw_size && raw[model_begin] == 0U) {
            model_begin += 1U;
        }
        model_end = model_begin;
        while (model_end < raw_size && raw[model_end] != 0U) {
            model_end += 1U;
        }
        if (model_end > model_begin) {
            (void)omc_ciff_emit_suffix_text(
                store, block_id, ifd_name, ifd_size, "makemodel", 9U, 0x0006U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE,
                (const char*)(raw + model_begin), model_end - model_begin,
                res);
            (void)omc_ciff_emit_text(store, block_id, ifd0, ifd0_size, 0x0110U,
                                     next_order++, tag_id,
                                     OMC_ENTRY_FLAG_NONE,
                                     (const char*)(raw + model_begin),
                                     model_end - model_begin, res);
        }
        return;
    }

    if (dir_id == 0x2804U && tag_id == 0x0805U) {
        if (omc_ciff_extract_padded_ascii_text(raw, raw_size, &text,
                                               &text_size)
            && text_size != 0U) {
            (void)omc_ciff_emit_text(store, block_id, ifd0, ifd0_size, 0x010EU,
                                     next_order++, tag_id,
                                     OMC_ENTRY_FLAG_NONE, text, text_size,
                                     res);
        }
        return;
    }

    if (dir_id == 0x2807U && tag_id == 0x0810U) {
        if (omc_ciff_extract_padded_ascii_text(raw, raw_size, &text,
                                               &text_size)
            && text_size != 0U) {
            (void)omc_ciff_emit_text(store, block_id, exif_ifd, exif_ifd_size,
                                     0xA430U, next_order++, tag_id,
                                     OMC_ENTRY_FLAG_NONE, text, text_size,
                                     res);
        }
        return;
    }

    if (dir_id == 0x300AU && tag_id == 0x180EU && raw_size >= 4U) {
        if (omc_ciff_read_u32(cfg, raw, raw_size, 0U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "timestamp", 9U, 0x0000U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
            if (omc_ciff_format_datetime(u32v, datetime_text)) {
                (void)omc_ciff_emit_text(
                    store, block_id, exif_ifd, exif_ifd_size, 0x9003U,
                    next_order++, tag_id, OMC_ENTRY_FLAG_NONE,
                    datetime_text, 19U, res);
            }
        }
        if (raw_size >= 8U
            && omc_ciff_read_i32(cfg, raw, raw_size, 4U, &i32v)) {
            (void)omc_ciff_emit_suffix_scalar_i32(
                store, block_id, ifd_name, ifd_size, "timestamp", 9U, 0x0001U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, i32v, res);
        }
        if (raw_size >= 12U
            && omc_ciff_read_u32(cfg, raw, raw_size, 8U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "timestamp", 9U, 0x0002U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        return;
    }

    if (dir_id == 0x300AU && tag_id == 0x1810U) {
        if (raw_size >= 4U
            && omc_ciff_read_u32(cfg, raw, raw_size, 0U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "imageinfo", 9U, 0x0000U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
            (void)omc_ciff_emit_scalar_u32(store, block_id, exif_ifd,
                                           exif_ifd_size, 0xA002U,
                                           next_order++, tag_id,
                                           OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        if (raw_size >= 8U
            && omc_ciff_read_u32(cfg, raw, raw_size, 4U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "imageinfo", 9U, 0x0001U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
            (void)omc_ciff_emit_scalar_u32(store, block_id, exif_ifd,
                                           exif_ifd_size, 0xA003U,
                                           next_order++, tag_id,
                                           OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        if (raw_size >= 12U
            && omc_ciff_read_u32(cfg, raw, raw_size, 8U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_f32_bits(
                store, block_id, ifd_name, ifd_size, "imageinfo", 9U, 0x0002U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        if (raw_size >= 16U
            && omc_ciff_read_i32(cfg, raw, raw_size, 12U, &i32v)) {
            (void)omc_ciff_emit_suffix_scalar_i32(
                store, block_id, ifd_name, ifd_size, "imageinfo", 9U, 0x0003U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, i32v, res);
            (void)omc_ciff_emit_scalar_u16(store, block_id, ifd0, ifd0_size,
                                           0x0112U, next_order++, tag_id,
                                           OMC_ENTRY_FLAG_NONE,
                                           omc_ciff_rotation_to_orientation(
                                               i32v),
                                           res);
        }
        if (raw_size >= 20U
            && omc_ciff_read_u32(cfg, raw, raw_size, 16U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "imageinfo", 9U, 0x0004U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        if (raw_size >= 24U
            && omc_ciff_read_u32(cfg, raw, raw_size, 20U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "imageinfo", 9U, 0x0005U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        if (raw_size >= 28U
            && omc_ciff_read_u32(cfg, raw, raw_size, 24U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "imageinfo", 9U, 0x0006U,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        return;
    }

    if (dir_id == 0x300AU && tag_id == 0x1803U && raw_size >= 4U) {
        if (omc_ciff_read_u32(cfg, raw, raw_size, 0U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "imageformat", 11U,
                0x0000U, next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v,
                res);
        }
        if (raw_size >= 8U
            && omc_ciff_read_u32(cfg, raw, raw_size, 4U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_f32_bits(
                store, block_id, ifd_name, ifd_size, "imageformat", 11U,
                0x0001U, next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v,
                res);
        }
        return;
    }

    if (dir_id == 0x3002U && tag_id == 0x1807U && raw_size >= 4U) {
        if (omc_ciff_read_u32(cfg, raw, raw_size, 0U, &u32v)) {
            (void)omc_ciff_emit_scalar_u32(store, block_id, exif_ifd,
                                           exif_ifd_size, 0x9206U,
                                           next_order++, tag_id,
                                           OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        return;
    }

    if (dir_id == 0x3002U && tag_id == 0x1818U && raw_size >= 4U) {
        for (i = 0U; i < 3U; ++i) {
            if (((omc_u64)i * 4U) + 4U > (omc_u64)raw_size) {
                break;
            }
            if (!omc_ciff_read_u32(cfg, raw, raw_size, (omc_u64)i * 4U,
                                   &u32v)) {
                break;
            }
            (void)omc_ciff_emit_suffix_scalar_f32_bits(
                store, block_id, ifd_name, ifd_size, "exposureinfo", 12U, i,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        return;
    }

    if (dir_id == 0x3002U && tag_id == 0x1813U && raw_size >= 4U) {
        for (i = 0U; i < 2U; ++i) {
            if (((omc_u64)i * 4U) + 4U > (omc_u64)raw_size) {
                break;
            }
            if (!omc_ciff_read_u32(cfg, raw, raw_size, (omc_u64)i * 4U,
                                   &u32v)) {
                break;
            }
            (void)omc_ciff_emit_suffix_scalar_f32_bits(
                store, block_id, ifd_name, ifd_size, "flashinfo", 9U, i,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v, res);
        }
        return;
    }

    if (dir_id == 0x3004U && tag_id == 0x1835U && raw_size >= 4U) {
        if (omc_ciff_read_u32(cfg, raw, raw_size, 0U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "decodertable", 12U,
                0x0000U, next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v,
                res);
        }
        if (raw_size >= 12U
            && omc_ciff_read_u32(cfg, raw, raw_size, 8U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "decodertable", 12U,
                0x0002U, next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v,
                res);
        }
        if (raw_size >= 16U
            && omc_ciff_read_u32(cfg, raw, raw_size, 12U, &u32v)) {
            (void)omc_ciff_emit_suffix_scalar_u32(
                store, block_id, ifd_name, ifd_size, "decodertable", 12U,
                0x0003U, next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u32v,
                res);
        }
        return;
    }

    if (dir_id == 0x300BU && tag_id == 0x1029U && raw_size >= 2U) {
        for (i = 0U; i < 4U; ++i) {
            if (((omc_u64)i * 2U) + 2U > (omc_u64)raw_size) {
                break;
            }
            if (!omc_ciff_read_u16(cfg, raw, raw_size, (omc_u64)i * 2U,
                                   &u16v)) {
                break;
            }
            (void)omc_ciff_emit_suffix_scalar_u16(
                store, block_id, ifd_name, ifd_size, "focallength", 11U, i,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u16v, res);
        }
        return;
    }

    if (dir_id == 0x300BU && tag_id == 0x102AU && raw_size >= 2U) {
        for (i = 1U; i <= 10U; ++i) {
            if (((omc_u64)(i - 1U) * 2U) + 2U > (omc_u64)raw_size) {
                break;
            }
            if (!omc_ciff_read_u16(cfg, raw, raw_size,
                                   (omc_u64)(i - 1U) * 2U, &u16v)) {
                break;
            }
            (void)omc_ciff_emit_suffix_scalar_i16(
                store, block_id, ifd_name, ifd_size, "shotinfo", 8U, i,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, (omc_s16)u16v,
                res);
        }
        return;
    }

    if (dir_id == 0x300BU && tag_id == 0x10B5U && raw_size >= 10U) {
        for (i = 1U; i <= 4U; ++i) {
            if (((omc_u64)i * 2U) + 2U > (omc_u64)raw_size) {
                break;
            }
            if (!omc_ciff_read_u16(cfg, raw, raw_size, (omc_u64)i * 2U,
                                   &u16v)) {
                break;
            }
            (void)omc_ciff_emit_suffix_scalar_u16(
                store, block_id, ifd_name, ifd_size, "rawjpginfo", 10U, i,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u16v, res);
        }
        return;
    }

    if (dir_id == 0x300BU && tag_id == 0x1030U && raw_size >= 12U) {
        for (i = 1U; i <= 5U; ++i) {
            if (((omc_u64)i * 2U) + 2U > (omc_u64)raw_size) {
                break;
            }
            if (!omc_ciff_read_u16(cfg, raw, raw_size, (omc_u64)i * 2U,
                                   &u16v)) {
                break;
            }
            (void)omc_ciff_emit_suffix_scalar_u16(
                store, block_id, ifd_name, ifd_size, "whitesample", 11U, i,
                next_order++, tag_id, OMC_ENTRY_FLAG_NONE, u16v, res);
        }
    }
}

static int
omc_ciff_decode_directory(omc_ciff_cfg cfg, const omc_exif_opts* opts,
                          const omc_u8* dir_bytes, omc_size dir_size,
                          const char* ifd_name, omc_size ifd_size,
                          omc_store* store, omc_block_id block_id,
                          omc_exif_res* res, omc_u32 depth,
                          omc_u32* dir_index)
{
    omc_u32 entry_off32;
    omc_u64 entry_off;
    omc_u16 entry_count;
    omc_u64 entries_start;
    omc_u64 needed_bytes;
    omc_u32 i;
    int any;

    if (dir_bytes == (const omc_u8*)0 || ifd_name == (const char*)0
        || store == (omc_store*)0 || res == (omc_exif_res*)0
        || dir_index == (omc_u32*)0) {
        return 0;
    }
    if (dir_size < 6U) {
        omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
        return 0;
    }
    if (depth > 32U) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_IFDS, 0U);
        return 0;
    }
    if (res->ifds_written >= opts->limits.max_ifds) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_IFDS, 0U);
        return 0;
    }

    if (!omc_ciff_read_u32(cfg, dir_bytes, dir_size, (omc_u64)dir_size - 4U,
                           &entry_off32)) {
        omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
        return 0;
    }
    entry_off = entry_off32;
    if (entry_off > (omc_u64)dir_size - 2U) {
        omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
        return 0;
    }

    if (!omc_ciff_read_u16(cfg, dir_bytes, dir_size, entry_off, &entry_count)) {
        omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
        return 0;
    }
    if (entry_count > opts->limits.max_entries_per_ifd) {
        omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_IFD, 0U);
        return 0;
    }

    entries_start = entry_off + 2U;
    needed_bytes = entries_start + ((omc_u64)entry_count * 10U);
    if (needed_bytes > (omc_u64)dir_size) {
        omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
        return 0;
    }

    res->ifds_written += 1U;
    any = 0;
    for (i = 0U; i < (omc_u32)entry_count; ++i) {
        omc_u64 eoff;
        omc_u16 raw_tag;
        omc_u16 tag_id;
        omc_u16 loc_bits;
        omc_u64 value_off;
        omc_u64 value_bytes;
        omc_u32 size32;
        omc_u32 off32;

        eoff = entries_start + ((omc_u64)i * 10U);
        if (!omc_ciff_read_u16(cfg, dir_bytes, dir_size, eoff, &raw_tag)) {
            omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
            break;
        }

        tag_id = omc_ciff_tag_id(raw_tag);
        loc_bits = omc_ciff_loc_bits(raw_tag);
        value_off = 0U;
        value_bytes = 0U;

        if (loc_bits == 0x4000U) {
            value_off = eoff + 2U;
            value_bytes = 8U;
        } else if (loc_bits == 0x0000U) {
            if (!omc_ciff_read_u32(cfg, dir_bytes, dir_size, eoff + 2U, &size32)
                || !omc_ciff_read_u32(cfg, dir_bytes, dir_size, eoff + 6U,
                                      &off32)) {
                omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
                break;
            }
            value_off = off32;
            value_bytes = size32;

            if (value_off < eoff) {
                if (value_bytes > (eoff - value_off)) {
                    omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
                    continue;
                }
            } else if (value_off < eoff + 10U) {
                omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
                continue;
            }
        } else {
            omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
            continue;
        }

        if (value_off > (omc_u64)dir_size
            || value_bytes > ((omc_u64)dir_size - value_off)) {
            omc_ciff_update_status(res, OMC_EXIF_MALFORMED);
            continue;
        }

        if (omc_ciff_is_directory(raw_tag)) {
            char child_name[32];
            omc_size child_name_size;

            if (!omc_ciff_make_child_token(child_name, sizeof(child_name),
                                           tag_id, *dir_index,
                                           &child_name_size)) {
                omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_IFDS, tag_id);
                continue;
            }
            *dir_index += 1U;
            (void)omc_ciff_decode_directory(
                cfg, opts, dir_bytes + (omc_size)value_off, (omc_size)value_bytes,
                child_name, child_name_size, store, block_id, res,
                depth + 1U, dir_index);
            any = 1;
            continue;
        }

        if (res->entries_decoded >= opts->limits.max_total_entries) {
            omc_ciff_set_limit(res, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, tag_id);
            break;
        }

        if (omc_ciff_add_raw_entry(cfg, opts, dir_bytes, ifd_name, ifd_size,
                                   raw_tag, tag_id, value_off, value_bytes,
                                   store, block_id, i, res)) {
            if (value_bytes <= opts->limits.max_value_bytes) {
                omc_ciff_add_derived_entries(cfg, opts, ifd_name, ifd_size,
                                             tag_id, dir_bytes + (omc_size)value_off,
                                             (omc_size)value_bytes, store,
                                             block_id, i, res);
            } else {
                omc_ciff_set_limit(res, OMC_EXIF_LIM_VALUE_COUNT, tag_id);
            }
            any = 1;
        }
    }

    return any;
}

omc_exif_res
omc_ciff_dec(const omc_u8* file_bytes, omc_size file_size,
             omc_store* store, omc_block_id source_block,
             const omc_exif_opts* opts)
{
    omc_exif_opts local_opts;
    const omc_exif_opts* use_opts;
    omc_exif_res res;
    omc_ciff_cfg cfg;
    omc_u32 root_off;
    omc_u32 dir_index;
    int any;

    omc_ciff_init_res(&res);

    if (opts == (const omc_exif_opts*)0) {
        omc_exif_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        use_opts = opts;
    }

    if (file_bytes == (const omc_u8*)0 || store == (omc_store*)0) {
        res.status = OMC_EXIF_MALFORMED;
        return res;
    }
    if (file_size < 14U) {
        return res;
    }

    if (file_bytes[0] == (omc_u8)'I' && file_bytes[1] == (omc_u8)'I') {
        cfg.little_endian = 1;
    } else if (file_bytes[0] == (omc_u8)'M' && file_bytes[1] == (omc_u8)'M') {
        cfg.little_endian = 0;
    } else {
        return res;
    }

    if (memcmp(file_bytes + 6U, "HEAPCCDR", 8U) != 0) {
        return res;
    }

    if (!omc_ciff_read_u32(cfg, file_bytes, file_size, 2U, &root_off)) {
        res.status = OMC_EXIF_MALFORMED;
        return res;
    }
    if (root_off < 14U || (omc_u64)root_off > (omc_u64)file_size) {
        res.status = OMC_EXIF_MALFORMED;
        return res;
    }

    dir_index = 0U;
    any = omc_ciff_decode_directory(cfg, use_opts, file_bytes + root_off,
                                    file_size - (omc_size)root_off,
                                    "ciff_root", 9U, store, source_block,
                                    &res, 0U, &dir_index);
    if (any) {
        omc_ciff_update_status(&res, OMC_EXIF_OK);
    }
    return res;
}
