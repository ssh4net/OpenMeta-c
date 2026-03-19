#include "omc/omc_irb.h"

#include <string.h>

static int
omc_irb_match(const omc_u8* bytes, omc_size size, omc_u64 off, const char* lit,
              omc_u64 lit_size)
{
    if (off > (omc_u64)size || lit_size > ((omc_u64)size - off)) {
        return 0;
    }
    return memcmp(bytes + (omc_size)off, lit, (omc_size)lit_size) == 0;
}

static int
omc_irb_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                   omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 2U) {
        return 0;
    }
    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                           | ((omc_u16)bytes[(omc_size)offset + 1U]));
    return 1;
}

static int
omc_irb_read_i16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                   omc_s16* out_value)
{
    omc_u16 raw;

    raw = 0U;
    if (out_value == (omc_s16*)0
        || !omc_irb_read_u16be(bytes, size, offset, &raw)) {
        return 0;
    }
    *out_value = (omc_s16)raw;
    return 1;
}

static int
omc_irb_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
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
omc_irb_read_u64be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                   omc_u64* out_value)
{
    omc_u64 v;

    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 8U) {
        return 0;
    }

    v = 0U;
    v |= ((omc_u64)bytes[(omc_size)offset + 0U]) << 56;
    v |= ((omc_u64)bytes[(omc_size)offset + 1U]) << 48;
    v |= ((omc_u64)bytes[(omc_size)offset + 2U]) << 40;
    v |= ((omc_u64)bytes[(omc_size)offset + 3U]) << 32;
    v |= ((omc_u64)bytes[(omc_size)offset + 4U]) << 24;
    v |= ((omc_u64)bytes[(omc_size)offset + 5U]) << 16;
    v |= ((omc_u64)bytes[(omc_size)offset + 6U]) << 8;
    v |= ((omc_u64)bytes[(omc_size)offset + 7U]) << 0;
    *out_value = v;
    return 1;
}

static int
omc_irb_read_f32be_bits(const omc_u8* bytes, omc_size size, omc_u64 offset,
                        omc_u32* out_bits)
{
    return omc_irb_read_u32be(bytes, size, offset, out_bits);
}

static omc_u64
omc_irb_pad2(omc_u64 n)
{
    return (n + 1U) & ~(omc_u64)1U;
}

static omc_size
omc_irb_trailing_zero_start(const omc_u8* bytes, omc_size size)
{
    omc_size end;

    end = size;
    while (end > 0U && bytes[end - 1U] == 0U) {
        end -= 1U;
    }
    return end;
}

static omc_irb_status
omc_irb_status_from_store(omc_status st)
{
    if (st == OMC_STATUS_NO_MEMORY) {
        return OMC_IRB_NOMEM;
    }
    if (st == OMC_STATUS_OK) {
        return OMC_IRB_OK;
    }
    return OMC_IRB_LIMIT;
}

static int
omc_irb_ascii_no_nul(const omc_u8* bytes, omc_size size)
{
    omc_size i;

    for (i = 0U; i < size; ++i) {
        if (bytes[i] == 0U || bytes[i] >= 0x80U) {
            return 0;
        }
    }
    return 1;
}

static omc_u32
omc_irb_utf8_cp_size(omc_u32 cp)
{
    if (cp <= 0x7FU) {
        return 1U;
    }
    if (cp <= 0x7FFU) {
        return 2U;
    }
    if (cp >= 0xD800U && cp <= 0xDFFFU) {
        return 0U;
    }
    if (cp <= 0xFFFFU) {
        return 3U;
    }
    if (cp <= 0x10FFFFU) {
        return 4U;
    }
    return 0U;
}

static int
omc_irb_utf8_write_cp(omc_u32 cp, omc_u8* dst, omc_u32 cap, omc_u32* io_written)
{
    omc_u32 written;
    omc_u32 need;

    if (io_written == (omc_u32*)0) {
        return 0;
    }
    written = *io_written;
    need = omc_irb_utf8_cp_size(cp);
    if (need == 0U || written > cap || need > (cap - written)) {
        return 0;
    }
    if (need == 1U) {
        dst[written + 0U] = (omc_u8)cp;
    } else if (need == 2U) {
        dst[written + 0U] = (omc_u8)(0xC0U | (cp >> 6));
        dst[written + 1U] = (omc_u8)(0x80U | (cp & 0x3FU));
    } else if (need == 3U) {
        dst[written + 0U] = (omc_u8)(0xE0U | (cp >> 12));
        dst[written + 1U] = (omc_u8)(0x80U | ((cp >> 6) & 0x3FU));
        dst[written + 2U] = (omc_u8)(0x80U | (cp & 0x3FU));
    } else {
        dst[written + 0U] = (omc_u8)(0xF0U | (cp >> 18));
        dst[written + 1U] = (omc_u8)(0x80U | ((cp >> 12) & 0x3FU));
        dst[written + 2U] = (omc_u8)(0x80U | ((cp >> 6) & 0x3FU));
        dst[written + 3U] = (omc_u8)(0x80U | (cp & 0x3FU));
    }
    *io_written = written + need;
    return 1;
}

static int
omc_irb_utf16be_measure_utf8(const omc_u8* bytes, omc_size size,
                             omc_u32* out_size)
{
    omc_u64 off;
    omc_u32 total;

    if (out_size == (omc_u32*)0 || (size & 1U) != 0U) {
        return 0;
    }

    off = 0U;
    total = 0U;
    while (off < (omc_u64)size) {
        omc_u16 hi;
        omc_u32 cp;
        omc_u32 add;

        hi = 0U;
        if (!omc_irb_read_u16be(bytes, size, off, &hi)) {
            return 0;
        }
        off += 2U;
        cp = hi;
        if (hi >= 0xD800U && hi <= 0xDBFFU) {
            omc_u16 lo;

            lo = 0U;
            if (!omc_irb_read_u16be(bytes, size, off, &lo)
                || lo < 0xDC00U || lo > 0xDFFFU) {
                return 0;
            }
            off += 2U;
            cp = 0x10000U
                 + ((((omc_u32)hi - 0xD800U) << 10)
                    | ((omc_u32)lo - 0xDC00U));
        } else if (hi >= 0xDC00U && hi <= 0xDFFFU) {
            return 0;
        }
        add = omc_irb_utf8_cp_size(cp);
        if (add == 0U || total > ((omc_u32)~0U - add)) {
            return 0;
        }
        total += add;
    }

    *out_size = total;
    return 1;
}

static int
omc_irb_utf16be_write_utf8(const omc_u8* bytes, omc_size size, omc_u8* dst,
                           omc_u32 cap)
{
    omc_u64 off;
    omc_u32 written;

    if ((size & 1U) != 0U) {
        return 0;
    }

    off = 0U;
    written = 0U;
    while (off < (omc_u64)size) {
        omc_u16 hi;
        omc_u32 cp;

        hi = 0U;
        if (!omc_irb_read_u16be(bytes, size, off, &hi)) {
            return 0;
        }
        off += 2U;
        cp = hi;
        if (hi >= 0xD800U && hi <= 0xDBFFU) {
            omc_u16 lo;

            lo = 0U;
            if (!omc_irb_read_u16be(bytes, size, off, &lo)
                || lo < 0xDC00U || lo > 0xDFFFU) {
                return 0;
            }
            off += 2U;
            cp = 0x10000U
                 + ((((omc_u32)hi - 0xD800U) << 10)
                    | ((omc_u32)lo - 0xDC00U));
        } else if (hi >= 0xDC00U && hi <= 0xDFFFU) {
            return 0;
        }
        if (!omc_irb_utf8_write_cp(cp, dst, cap, &written)) {
            return 0;
        }
    }

    return written == cap;
}

static int
omc_irb_latin1_measure_utf8(const omc_u8* bytes, omc_size size,
                            omc_u32* out_size)
{
    omc_size i;
    omc_u32 total;

    if (out_size == (omc_u32*)0) {
        return 0;
    }

    total = 0U;
    for (i = 0U; i < size; ++i) {
        omc_u32 add;

        if (bytes[i] == 0U) {
            return 0;
        }
        add = (bytes[i] < 0x80U) ? 1U : 2U;
        if (total > ((omc_u32)~0U - add)) {
            return 0;
        }
        total += add;
    }
    *out_size = total;
    return 1;
}

static int
omc_irb_latin1_write_utf8(const omc_u8* bytes, omc_size size, omc_u8* dst,
                          omc_u32 cap)
{
    omc_size i;
    omc_u32 written;

    written = 0U;
    for (i = 0U; i < size; ++i) {
        omc_u32 cp;

        cp = bytes[i];
        if (cp == 0U || !omc_irb_utf8_write_cp(cp, dst, cap, &written)) {
            return 0;
        }
    }
    return written == cap;
}

static omc_irb_status
omc_irb_arena_alloc(omc_arena* arena, omc_size size, omc_byte_ref* out_ref,
                    omc_u8** out_ptr)
{
    omc_size offset;
    omc_size needed;
    omc_status st;

    if (arena == (omc_arena*)0 || out_ref == (omc_byte_ref*)0
        || out_ptr == (omc_u8**)0) {
        return OMC_IRB_LIMIT;
    }
    if (arena->size > (omc_size)(~(omc_u32)0) || size > (omc_size)(~(omc_u32)0)
        || size > ((omc_size)(~(omc_size)0) - arena->size)) {
        return OMC_IRB_LIMIT;
    }

    offset = arena->size;
    needed = offset + size;
    if (needed > (omc_size)(~(omc_u32)0)) {
        return OMC_IRB_LIMIT;
    }

    st = omc_arena_reserve(arena, needed);
    if (st != OMC_STATUS_OK) {
        return omc_irb_status_from_store(st);
    }

    arena->size = needed;
    out_ref->offset = (omc_u32)offset;
    out_ref->size = (omc_u32)size;
    *out_ptr = arena->data + offset;
    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_store_ascii(omc_store* store, const omc_u8* bytes, omc_size size,
                    omc_byte_ref* out_ref)
{
    return omc_irb_status_from_store(
        omc_arena_append(&store->arena, bytes, size, out_ref));
}

static omc_irb_status
omc_irb_store_hex_lower(omc_store* store, const omc_u8* bytes, omc_size size,
                        omc_byte_ref* out_ref)
{
    static const char hex[] = "0123456789abcdef";
    omc_byte_ref ref;
    omc_u8* dst;
    omc_irb_status st;
    omc_size i;

    ref.offset = 0U;
    ref.size = 0U;
    dst = (omc_u8*)0;
    st = omc_irb_arena_alloc(&store->arena, size * 2U, &ref, &dst);
    if (st != OMC_IRB_OK) {
        return st;
    }

    for (i = 0U; i < size; ++i) {
        dst[i * 2U + 0U] = (omc_u8)hex[(bytes[i] >> 4) & 0x0FU];
        dst[i * 2U + 1U] = (omc_u8)hex[(bytes[i] >> 0) & 0x0FU];
    }

    *out_ref = ref;
    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_store_latin1_utf8(omc_store* store, const omc_u8* bytes, omc_size size,
                          omc_byte_ref* out_ref)
{
    omc_u32 utf8_size;
    omc_byte_ref ref;
    omc_u8* dst;
    omc_irb_status st;

    utf8_size = 0U;
    ref.offset = 0U;
    ref.size = 0U;
    dst = (omc_u8*)0;

    if (!omc_irb_latin1_measure_utf8(bytes, size, &utf8_size)) {
        return OMC_IRB_MALFORMED;
    }
    st = omc_irb_arena_alloc(&store->arena, utf8_size, &ref, &dst);
    if (st != OMC_IRB_OK) {
        return st;
    }
    if (!omc_irb_latin1_write_utf8(bytes, size, dst, utf8_size)) {
        return OMC_IRB_MALFORMED;
    }
    *out_ref = ref;
    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_store_utf16be_utf8(omc_store* store, const omc_u8* bytes, omc_size size,
                           omc_byte_ref* out_ref)
{
    omc_u32 utf8_size;
    omc_byte_ref ref;
    omc_u8* dst;
    omc_irb_status st;

    utf8_size = 0U;
    ref.offset = 0U;
    ref.size = 0U;
    dst = (omc_u8*)0;

    if (!omc_irb_utf16be_measure_utf8(bytes, size, &utf8_size)) {
        return OMC_IRB_MALFORMED;
    }
    st = omc_irb_arena_alloc(&store->arena, utf8_size, &ref, &dst);
    if (st != OMC_IRB_OK) {
        return st;
    }
    if (!omc_irb_utf16be_write_utf8(bytes, size, dst, utf8_size)) {
        return OMC_IRB_MALFORMED;
    }
    *out_ref = ref;
    return OMC_IRB_OK;
}

static int
omc_irb_read_var_ustr32_ref(omc_store* store, const omc_u8* bytes,
                            omc_size size, omc_u64* io_offset,
                            omc_byte_ref* out_ref)
{
    omc_u32 code_unit_count;
    omc_u64 text_offset;
    omc_u64 byte_count;
    omc_irb_status st;

    code_unit_count = 0U;
    if (store == (omc_store*)0 || io_offset == (omc_u64*)0
        || out_ref == (omc_byte_ref*)0
        || !omc_irb_read_u32be(bytes, size, *io_offset, &code_unit_count)) {
        return 0;
    }

    text_offset = *io_offset + 4U;
    byte_count = ((omc_u64)code_unit_count) * 2U;
    if (text_offset > (omc_u64)size || byte_count > ((omc_u64)size - text_offset)
        || byte_count > (omc_u64)(~(omc_size)0)) {
        return 0;
    }

    st = omc_irb_store_utf16be_utf8(store, bytes + (omc_size)text_offset,
                                    (omc_size)byte_count, out_ref);
    if (st != OMC_IRB_OK) {
        return 0;
    }

    *io_offset = text_offset + byte_count;
    return 1;
}

static omc_irb_status
omc_irb_emit_field(omc_store* store, omc_block_id source_block, omc_u32 order,
                   omc_u16 resource_id, const char* field, omc_size field_len,
                   const omc_val* value, omc_irb_res* result)
{
    omc_byte_ref field_ref;
    omc_entry entry;
    omc_status st;

    field_ref.offset = 0U;
    field_ref.size = 0U;
    st = omc_arena_append(&store->arena, field, field_len, &field_ref);
    if (st != OMC_STATUS_OK) {
        return omc_irb_status_from_store(st);
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_photoshop_irb_field(&entry.key, resource_id, field_ref);
    entry.value = *value;
    entry.origin.block = source_block;
    entry.origin.order_in_block = order;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = 1U;
    entry.flags = OMC_ENTRY_FLAG_DERIVED;

    st = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    if (st != OMC_STATUS_OK) {
        return omc_irb_status_from_store(st);
    }

    if (result != (omc_irb_res*)0) {
        result->entries_decoded += 1U;
    }
    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_emit_text_field_ref(omc_store* store, omc_block_id source_block,
                            omc_u32 order, omc_u16 resource_id,
                            const char* field, omc_size field_len,
                            omc_byte_ref ref, omc_text_encoding enc,
                            omc_irb_res* result)
{
    omc_val value;

    omc_val_make_text(&value, ref, enc);
    return omc_irb_emit_field(store, source_block, order, resource_id, field,
                              field_len, &value, result);
}

static omc_irb_status
omc_irb_decode_resolution_info(const omc_u8* payload, omc_size payload_size,
                               omc_store* store, omc_block_id source_block,
                               omc_u32 order, omc_irb_res* result)
{
    omc_u32 x_fixed;
    omc_u32 y_fixed;
    omc_u16 units_x;
    omc_u16 units_y;
    omc_u64 x_bits;
    omc_u64 y_bits;
    omc_val value;
    double x;
    double y;
    omc_irb_status st;

    x_fixed = 0U;
    y_fixed = 0U;
    units_x = 0U;
    units_y = 0U;
    if (!omc_irb_read_u32be(payload, payload_size, 0U, &x_fixed)
        || !omc_irb_read_u16be(payload, payload_size, 4U, &units_x)
        || !omc_irb_read_u32be(payload, payload_size, 8U, &y_fixed)
        || !omc_irb_read_u16be(payload, payload_size, 12U, &units_y)) {
        return OMC_IRB_OK;
    }

    x = (double)x_fixed / 65536.0;
    y = (double)y_fixed / 65536.0;
    memcpy(&x_bits, &x, sizeof(x_bits));
    memcpy(&y_bits, &y, sizeof(y_bits));

    omc_val_make_f64_bits(&value, x_bits);
    st = omc_irb_emit_field(store, source_block, order, 0x03EDU,
                            "XResolution", 11U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    omc_val_make_u16(&value, units_x);
    st = omc_irb_emit_field(store, source_block, order, 0x03EDU,
                            "DisplayedUnitsX", 15U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    omc_val_make_f64_bits(&value, y_bits);
    st = omc_irb_emit_field(store, source_block, order, 0x03EDU,
                            "YResolution", 11U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    omc_val_make_u16(&value, units_y);
    return omc_irb_emit_field(store, source_block, order, 0x03EDU,
                              "DisplayedUnitsY", 15U, &value, result);
}

static omc_irb_status
omc_irb_decode_iptc_digest(const omc_u8* payload, omc_size payload_size,
                           omc_store* store, omc_block_id source_block,
                           omc_u32 order, omc_irb_res* result)
{
    omc_byte_ref ref;
    omc_irb_status st;

    ref.offset = 0U;
    ref.size = 0U;
    st = omc_irb_store_hex_lower(store, payload, payload_size, &ref);
    if (st != OMC_IRB_OK) {
        return st;
    }
    return omc_irb_emit_text_field_ref(store, source_block, order, 0x0425U,
                                       "IPTCDigest", 10U, ref,
                                       OMC_TEXT_ASCII, result);
}

static omc_irb_status
omc_irb_decode_u8_scalar(const omc_u8* payload, omc_size payload_size,
                         omc_store* store, omc_block_id source_block,
                         omc_u32 order, omc_u16 resource_id,
                         const char* field, omc_size field_len,
                         omc_irb_res* result)
{
    omc_val value;

    if (payload_size == 0U) {
        return OMC_IRB_OK;
    }
    omc_val_make_u8(&value, payload[0]);
    return omc_irb_emit_field(store, source_block, order, resource_id, field,
                              field_len, &value, result);
}

static omc_irb_status
omc_irb_decode_u16_scalar(const omc_u8* payload, omc_size payload_size,
                          omc_store* store, omc_block_id source_block,
                          omc_u32 order, omc_u16 resource_id,
                          const char* field, omc_size field_len,
                          omc_irb_res* result)
{
    omc_u16 raw;
    omc_val value;

    raw = 0U;
    if (!omc_irb_read_u16be(payload, payload_size, 0U, &raw)) {
        return OMC_IRB_OK;
    }
    omc_val_make_u16(&value, raw);
    return omc_irb_emit_field(store, source_block, order, resource_id, field,
                              field_len, &value, result);
}

static omc_irb_status
omc_irb_decode_u32_scalar(const omc_u8* payload, omc_size payload_size,
                          omc_store* store, omc_block_id source_block,
                          omc_u32 order, omc_u16 resource_id,
                          const char* field, omc_size field_len,
                          omc_irb_res* result)
{
    omc_u32 raw;
    omc_val value;

    raw = 0U;
    if (!omc_irb_read_u32be(payload, payload_size, 0U, &raw)) {
        return OMC_IRB_OK;
    }
    omc_val_make_u32(&value, raw);
    return omc_irb_emit_field(store, source_block, order, resource_id, field,
                              field_len, &value, result);
}

static omc_irb_status
omc_irb_decode_u16_list(const omc_u8* payload, omc_size payload_size,
                        omc_store* store, omc_block_id source_block,
                        omc_u32 order, omc_u16 resource_id,
                        const char* count_field, omc_size count_field_len,
                        const char* item_field, omc_size item_field_len,
                        omc_irb_res* result)
{
    omc_u32 count;
    omc_u32 i;
    omc_val value;
    omc_irb_status st;

    count = (omc_u32)(payload_size / 2U);
    if (count == 0U) {
        return OMC_IRB_OK;
    }

    omc_val_make_u32(&value, count);
    st = omc_irb_emit_field(store, source_block, order, resource_id,
                            count_field, count_field_len, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    for (i = 0U; i < count; ++i) {
        omc_u16 raw;

        raw = 0U;
        if (!omc_irb_read_u16be(payload, payload_size, ((omc_u64)i) * 2U,
                                &raw)) {
            return OMC_IRB_OK;
        }
        omc_val_make_u16(&value, raw);
        st = omc_irb_emit_field(store, source_block, order, resource_id,
                                item_field, item_field_len, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }
    }

    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_decode_ascii_text(const omc_u8* payload, omc_size payload_size,
                          omc_store* store, omc_block_id source_block,
                          omc_u32 order, omc_u16 resource_id,
                          const char* field, omc_size field_len,
                          omc_irb_res* result)
{
    omc_size end;
    omc_byte_ref ref;
    omc_irb_status st;

    end = omc_irb_trailing_zero_start(payload, payload_size);
    if (end == 0U || !omc_irb_ascii_no_nul(payload, end)) {
        return OMC_IRB_OK;
    }

    ref.offset = 0U;
    ref.size = 0U;
    st = omc_irb_store_ascii(store, payload, end, &ref);
    if (st != OMC_IRB_OK) {
        return st;
    }
    return omc_irb_emit_text_field_ref(store, source_block, order, resource_id,
                                       field, field_len, ref, OMC_TEXT_ASCII,
                                       result);
}

static omc_irb_status
omc_irb_decode_unicode_text(const omc_u8* payload, omc_size payload_size,
                            omc_store* store, omc_block_id source_block,
                            omc_u32 order, omc_u16 resource_id,
                            const char* field, omc_size field_len,
                            omc_irb_res* result)
{
    omc_u64 offset;
    omc_byte_ref ref;

    offset = 0U;
    ref.offset = 0U;
    ref.size = 0U;
    if (!omc_irb_read_var_ustr32_ref(store, payload, payload_size, &offset,
                                     &ref)) {
        return OMC_IRB_OK;
    }

    return omc_irb_emit_text_field_ref(store, source_block, order, resource_id,
                                       field, field_len, ref, OMC_TEXT_UTF8,
                                       result);
}

static omc_irb_status
omc_irb_decode_pascal_text(const omc_u8* payload, omc_size payload_size,
                           omc_store* store, omc_block_id source_block,
                           omc_u32 order, omc_u16 resource_id,
                           const char* field, omc_size field_len,
                           omc_irb_str_charset charset, omc_irb_res* result)
{
    omc_size text_len;
    omc_byte_ref ref;
    omc_irb_status st;

    if (payload_size == 0U) {
        return OMC_IRB_OK;
    }
    text_len = payload[0];
    if (text_len == 0U || (1U + text_len) > payload_size) {
        return OMC_IRB_OK;
    }

    ref.offset = 0U;
    ref.size = 0U;
    if (charset == OMC_IRB_STR_ASCII) {
        if (!omc_irb_ascii_no_nul(payload + 1U, text_len)) {
            return OMC_IRB_OK;
        }
        st = omc_irb_store_ascii(store, payload + 1U, text_len, &ref);
        if (st != OMC_IRB_OK) {
            return st;
        }
        return omc_irb_emit_text_field_ref(store, source_block, order,
                                           resource_id, field, field_len, ref,
                                           OMC_TEXT_ASCII, result);
    }

    st = omc_irb_store_latin1_utf8(store, payload + 1U, text_len, &ref);
    if (st != OMC_IRB_OK) {
        return (st == OMC_IRB_MALFORMED) ? OMC_IRB_OK : st;
    }
    return omc_irb_emit_text_field_ref(store, source_block, order, resource_id,
                                       field, field_len, ref, OMC_TEXT_UTF8,
                                       result);
}

static omc_irb_status
omc_irb_decode_channel_options(const omc_u8* payload, omc_size payload_size,
                               omc_store* store, omc_block_id source_block,
                               omc_u32 order, omc_irb_res* result)
{
    omc_u32 count;
    omc_u32 i;
    omc_val value;
    omc_irb_status st;

    count = (omc_u32)(payload_size / 13U);
    if (count == 0U) {
        return OMC_IRB_OK;
    }

    omc_val_make_u32(&value, count);
    st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                            "ChannelOptionsCount", 19U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    for (i = 0U; i < count; ++i) {
        omc_u64 off;
        omc_u16 color_space;
        omc_u16 color_data_0;
        omc_u16 color_data_1;
        omc_u16 color_data_2;
        omc_u16 color_data_3;

        off = ((omc_u64)i) * 13U;
        color_space = 0U;
        color_data_0 = 0U;
        color_data_1 = 0U;
        color_data_2 = 0U;
        color_data_3 = 0U;
        if (!omc_irb_read_u16be(payload, payload_size, off + 0U, &color_space)
            || !omc_irb_read_u16be(payload, payload_size, off + 2U,
                                   &color_data_0)
            || !omc_irb_read_u16be(payload, payload_size, off + 4U,
                                   &color_data_1)
            || !omc_irb_read_u16be(payload, payload_size, off + 6U,
                                   &color_data_2)
            || !omc_irb_read_u16be(payload, payload_size, off + 8U,
                                   &color_data_3)) {
            return OMC_IRB_OK;
        }

        omc_val_make_u32(&value, i);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelIndex", 12U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }

        omc_val_make_u16(&value, color_space);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelColorSpace", 17U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }

        omc_val_make_u16(&value, color_data_0);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelColorData", 16U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }
        omc_val_make_u16(&value, color_data_1);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelColorData", 16U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }
        omc_val_make_u16(&value, color_data_2);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelColorData", 16U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }
        omc_val_make_u16(&value, color_data_3);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelColorData", 16U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }

        omc_val_make_u8(&value, payload[(omc_size)off + 11U]);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelOpacity", 14U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }

        omc_val_make_u8(&value, payload[(omc_size)off + 12U]);
        st = omc_irb_emit_field(store, source_block, order, 0x0435U,
                                "ChannelColorIndicates", 22U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }
    }

    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_decode_print_flags_info(const omc_u8* payload, omc_size payload_size,
                                omc_store* store, omc_block_id source_block,
                                omc_u32 order, omc_irb_res* result)
{
    omc_u16 version;
    omc_u32 bleed_width_value;
    omc_u16 bleed_width_scale;
    omc_val value;
    omc_irb_status st;

    version = 0U;
    bleed_width_value = 0U;
    bleed_width_scale = 0U;
    if (!omc_irb_read_u16be(payload, payload_size, 0U, &version)
        || !omc_irb_read_u32be(payload, payload_size, 4U, &bleed_width_value)
        || !omc_irb_read_u16be(payload, payload_size, 8U,
                               &bleed_width_scale)) {
        return OMC_IRB_OK;
    }

    omc_val_make_u16(&value, version);
    st = omc_irb_emit_field(store, source_block, order, 0x2710U,
                            "PrintFlagsInfoVersion", 21U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    omc_val_make_u8(&value, payload[2U]);
    st = omc_irb_emit_field(store, source_block, order, 0x2710U,
                            "CenterCropMarks", 15U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    omc_val_make_u32(&value, bleed_width_value);
    st = omc_irb_emit_field(store, source_block, order, 0x2710U,
                            "BleedWidthValue", 15U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    omc_val_make_u16(&value, bleed_width_scale);
    return omc_irb_emit_field(store, source_block, order, 0x2710U,
                              "BleedWidthScale", 15U, &value, result);
}

static omc_irb_status
omc_irb_decode_pixel_info(const omc_u8* payload, omc_size payload_size,
                          omc_store* store, omc_block_id source_block,
                          omc_u32 order, omc_irb_res* result)
{
    omc_u64 pixel_aspect;
    omc_val value;

    pixel_aspect = 0U;
    if (!omc_irb_read_u64be(payload, payload_size, 4U, &pixel_aspect)) {
        return OMC_IRB_OK;
    }

    omc_val_make_f64_bits(&value, pixel_aspect);
    return omc_irb_emit_field(store, source_block, order, 0x0428U,
                              "PixelAspectRatio", 16U, &value, result);
}

static omc_irb_status
omc_irb_decode_jpeg_quality(const omc_u8* payload, omc_size payload_size,
                            omc_store* store, omc_block_id source_block,
                            omc_u32 order, omc_irb_res* result)
{
    omc_s16 quality;
    omc_s16 format;
    omc_s16 scans;
    omc_val value;
    omc_irb_status st;

    quality = 0;
    format = 0;
    scans = 0;
    if (!omc_irb_read_i16be(payload, payload_size, 0U, &quality)
        || !omc_irb_read_i16be(payload, payload_size, 2U, &format)) {
        return OMC_IRB_OK;
    }

    omc_val_make_i16(&value, quality);
    st = omc_irb_emit_field(store, source_block, order, 0x0406U,
                            "PhotoshopQuality", 16U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    omc_val_make_i16(&value, format);
    st = omc_irb_emit_field(store, source_block, order, 0x0406U,
                            "PhotoshopFormat", 15U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    if (format == (omc_s16)0x0101
        && omc_irb_read_i16be(payload, payload_size, 4U, &scans)) {
        omc_val_make_i16(&value, scans);
        st = omc_irb_emit_field(store, source_block, order, 0x0406U,
                                "ProgressiveScans", 16U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }
    }

    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_decode_print_scale_info(const omc_u8* payload, omc_size payload_size,
                                omc_store* store, omc_block_id source_block,
                                omc_u32 order, omc_irb_res* result)
{
    omc_u16 print_style;
    omc_u32 position_x_bits;
    omc_u32 position_y_bits;
    omc_u32 print_scale_bits;
    omc_val value;
    omc_irb_status st;

    print_style = 0U;
    position_x_bits = 0U;
    position_y_bits = 0U;
    print_scale_bits = 0U;
    if (!omc_irb_read_u16be(payload, payload_size, 0U, &print_style)
        || !omc_irb_read_f32be_bits(payload, payload_size, 2U,
                                    &position_x_bits)
        || !omc_irb_read_f32be_bits(payload, payload_size, 6U,
                                    &position_y_bits)
        || !omc_irb_read_f32be_bits(payload, payload_size, 10U,
                                    &print_scale_bits)) {
        return OMC_IRB_OK;
    }

    omc_val_make_u16(&value, print_style);
    st = omc_irb_emit_field(store, source_block, order, 0x0426U,
                            "PrintStyle", 10U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    omc_val_make_f32_bits(&value, position_x_bits);
    st = omc_irb_emit_field(store, source_block, order, 0x0426U,
                            "PrintPositionX", 14U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    omc_val_make_f32_bits(&value, position_y_bits);
    st = omc_irb_emit_field(store, source_block, order, 0x0426U,
                            "PrintPositionY", 14U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    omc_val_make_f32_bits(&value, print_scale_bits);
    return omc_irb_emit_field(store, source_block, order, 0x0426U,
                              "PrintScale", 10U, &value, result);
}

static omc_irb_status
omc_irb_decode_version_info(const omc_u8* payload, omc_size payload_size,
                            omc_store* store, omc_block_id source_block,
                            omc_u32 order, omc_irb_res* result)
{
    omc_u32 version;
    omc_u64 offset;
    omc_byte_ref writer_ref;
    omc_byte_ref reader_ref;
    omc_u32 file_version;
    omc_val value;
    omc_irb_status st;

    version = 0U;
    offset = 5U;
    writer_ref.offset = 0U;
    writer_ref.size = 0U;
    reader_ref.offset = 0U;
    reader_ref.size = 0U;
    file_version = 0U;

    if (payload_size < 5U || !omc_irb_read_u32be(payload, payload_size, 0U,
                                                 &version)) {
        return OMC_IRB_OK;
    }
    if (!omc_irb_read_var_ustr32_ref(store, payload, payload_size, &offset,
                                     &writer_ref)
        || !omc_irb_read_var_ustr32_ref(store, payload, payload_size, &offset,
                                        &reader_ref)
        || !omc_irb_read_u32be(payload, payload_size, offset, &file_version)) {
        return OMC_IRB_OK;
    }
    (void)version;
    (void)file_version;

    omc_val_make_u8(&value, payload[4U]);
    st = omc_irb_emit_field(store, source_block, order, 0x0421U,
                            "HasRealMergedData", 17U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    st = omc_irb_emit_text_field_ref(store, source_block, order, 0x0421U,
                                     "WriterName", 10U, writer_ref,
                                     OMC_TEXT_UTF8, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    return omc_irb_emit_text_field_ref(store, source_block, order, 0x0421U,
                                       "ReaderName", 10U, reader_ref,
                                       OMC_TEXT_UTF8, result);
}

static omc_irb_status
omc_irb_decode_layer_selection_ids(const omc_u8* payload, omc_size payload_size,
                                   omc_store* store, omc_block_id source_block,
                                   omc_u32 order, omc_irb_res* result)
{
    omc_u16 declared_count;
    omc_u32 available_count;
    omc_u32 emit_count;
    omc_u32 i;
    omc_val value;
    omc_irb_status st;

    declared_count = 0U;
    if (!omc_irb_read_u16be(payload, payload_size, 0U, &declared_count)) {
        return OMC_IRB_OK;
    }

    available_count = (payload_size >= 2U)
                          ? (omc_u32)((payload_size - 2U) / 4U)
                          : 0U;
    emit_count = ((omc_u32)declared_count < available_count)
                     ? (omc_u32)declared_count
                     : available_count;

    omc_val_make_u32(&value, emit_count);
    st = omc_irb_emit_field(store, source_block, order, 0x042DU,
                            "LayerSelectionIDCount", 21U, &value, result);
    if (st != OMC_IRB_OK) {
        return st;
    }

    for (i = 0U; i < emit_count; ++i) {
        omc_u32 item_id;

        item_id = 0U;
        if (!omc_irb_read_u32be(payload, payload_size,
                                2U + ((omc_u64)i) * 4U, &item_id)) {
            return OMC_IRB_OK;
        }
        omc_val_make_u32(&value, item_id);
        st = omc_irb_emit_field(store, source_block, order, 0x042DU,
                                "LayerSelectionID", 16U, &value, result);
        if (st != OMC_IRB_OK) {
            return st;
        }
    }

    return OMC_IRB_OK;
}

static omc_irb_status
omc_irb_decode_url_list(const omc_u8* payload, omc_size payload_size,
                        omc_store* store, omc_block_id source_block,
                        omc_u32 order, omc_irb_res* result)
{
    omc_u32 declared_count;
    omc_u64 offset;
    omc_u32 emitted_count;
    omc_u32 i;
    omc_irb_status st;
    omc_val value;

    declared_count = 0U;
    offset = 4U;
    emitted_count = 0U;
    if (!omc_irb_read_u32be(payload, payload_size, 0U, &declared_count)) {
        return OMC_IRB_OK;
    }

    for (i = 0U; i < declared_count; ++i) {
        omc_byte_ref url_ref;

        url_ref.offset = 0U;
        url_ref.size = 0U;
        if (offset > (omc_u64)payload_size
            || ((omc_u64)payload_size - offset) < 8U) {
            break;
        }
        offset += 8U;
        if (!omc_irb_read_var_ustr32_ref(store, payload, payload_size, &offset,
                                         &url_ref)) {
            break;
        }
        st = omc_irb_emit_text_field_ref(store, source_block, order, 0x041EU,
                                         "URL", 3U, url_ref, OMC_TEXT_UTF8,
                                         result);
        if (st != OMC_IRB_OK) {
            return st;
        }
        emitted_count += 1U;
    }

    omc_val_make_u32(&value, emitted_count);
    return omc_irb_emit_field(store, source_block, order, 0x041EU,
                              "URLListCount", 12U, &value, result);
}

static omc_irb_status
omc_irb_decode_slice_info(const omc_u8* payload, omc_size payload_size,
                          omc_store* store, omc_block_id source_block,
                          omc_u32 order, omc_irb_res* result)
{
    omc_u64 offset;
    omc_byte_ref group_ref;
    omc_u32 num_slices;
    omc_irb_status st;
    omc_val value;

    offset = 20U;
    group_ref.offset = 0U;
    group_ref.size = 0U;
    num_slices = 0U;

    if (payload_size < 20U
        || !omc_irb_read_var_ustr32_ref(store, payload, payload_size, &offset,
                                        &group_ref)
        || !omc_irb_read_u32be(payload, payload_size, offset, &num_slices)) {
        return OMC_IRB_OK;
    }

    st = omc_irb_emit_text_field_ref(store, source_block, order, 0x041AU,
                                     "SlicesGroupName", 15U, group_ref,
                                     OMC_TEXT_UTF8, result);
    if (st != OMC_IRB_OK) {
        return st;
    }
    omc_val_make_u32(&value, num_slices);
    return omc_irb_emit_field(store, source_block, order, 0x041AU,
                              "NumSlices", 9U, &value, result);
}

static omc_irb_status
omc_irb_decode_known_fields(const omc_u8* payload, omc_size payload_size,
                            omc_u16 resource_id, const omc_irb_opts* opts,
                            omc_store* store, omc_block_id source_block,
                            omc_u32 order, omc_irb_res* result)
{
    switch (resource_id) {
        case 0x03EDU:
            return omc_irb_decode_resolution_info(payload, payload_size, store,
                                                  source_block, order, result);
        case 0x0421U:
            return omc_irb_decode_version_info(payload, payload_size, store,
                                               source_block, order, result);
        case 0x03F3U:
            return omc_irb_decode_u8_scalar(payload, payload_size, store,
                                            source_block, order, resource_id,
                                            "PrintFlags", 10U, result);
        case 0x03FBU:
            return omc_irb_decode_u8_scalar(payload, payload_size, store,
                                            source_block, order, resource_id,
                                            "EffectiveBW", 11U, result);
        case 0x0400U:
            return omc_irb_decode_u16_scalar(payload, payload_size, store,
                                             source_block, order, resource_id,
                                             "TargetLayerID", 13U, result);
        case 0x0402U:
            return omc_irb_decode_u16_list(payload, payload_size, store,
                                           source_block, order, resource_id,
                                           "LayersGroupInfoCount", 20U,
                                           "LayersGroupInfo", 15U, result);
        case 0x0406U:
            return omc_irb_decode_jpeg_quality(payload, payload_size, store,
                                               source_block, order, result);
        case 0x040AU:
            return omc_irb_decode_u8_scalar(payload, payload_size, store,
                                            source_block, order, resource_id,
                                            "CopyrightFlag", 13U, result);
        case 0x040BU:
            return omc_irb_decode_ascii_text(payload, payload_size, store,
                                             source_block, order, resource_id,
                                             "URL", 3U, result);
        case 0x040DU:
            return omc_irb_decode_u32_scalar(payload, payload_size, store,
                                             source_block, order, resource_id,
                                             "GlobalAngle", 11U, result);
        case 0x0410U:
            return omc_irb_decode_u8_scalar(payload, payload_size, store,
                                            source_block, order, resource_id,
                                            "Watermark", 9U, result);
        case 0x0411U:
            return omc_irb_decode_u8_scalar(payload, payload_size, store,
                                            source_block, order, resource_id,
                                            "ICC_Untagged", 12U, result);
        case 0x0412U:
            return omc_irb_decode_u8_scalar(payload, payload_size, store,
                                            source_block, order, resource_id,
                                            "EffectsVisible", 14U, result);
        case 0x0414U:
            return omc_irb_decode_u32_scalar(payload, payload_size, store,
                                             source_block, order, resource_id,
                                             "IDsBaseValue", 12U, result);
        case 0x0416U:
            return omc_irb_decode_u16_scalar(
                payload, payload_size, store, source_block, order, resource_id,
                "IndexedColorTableCount", 22U, result);
        case 0x0417U:
            return omc_irb_decode_u16_scalar(payload, payload_size, store,
                                             source_block, order, resource_id,
                                             "TransparentIndex", 16U, result);
        case 0x0419U:
            return omc_irb_decode_u32_scalar(payload, payload_size, store,
                                             source_block, order, resource_id,
                                             "GlobalAltitude", 14U, result);
        case 0x041AU:
            return omc_irb_decode_slice_info(payload, payload_size, store,
                                             source_block, order, result);
        case 0x041BU:
            return omc_irb_decode_unicode_text(payload, payload_size, store,
                                               source_block, order, resource_id,
                                               "WorkflowURL", 11U, result);
        case 0x041EU:
            return omc_irb_decode_url_list(payload, payload_size, store,
                                           source_block, order, result);
        case 0x0425U:
            return omc_irb_decode_iptc_digest(payload, payload_size, store,
                                              source_block, order, result);
        case 0x0426U:
            return omc_irb_decode_print_scale_info(payload, payload_size, store,
                                                   source_block, order, result);
        case 0x0428U:
            return omc_irb_decode_pixel_info(payload, payload_size, store,
                                             source_block, order, result);
        case 0x042DU:
            return omc_irb_decode_layer_selection_ids(payload, payload_size,
                                                      store, source_block,
                                                      order, result);
        case 0x0430U:
            return omc_irb_decode_u8_scalar(
                payload, payload_size, store, source_block, order, resource_id,
                "LayerGroupsEnabledID", 20U, result);
        case 0x0435U:
            return omc_irb_decode_channel_options(payload, payload_size, store,
                                                  source_block, order, result);
        case 0x0BB7U:
            return omc_irb_decode_pascal_text(payload, payload_size, store,
                                              source_block, order, resource_id,
                                              "ClippingPathName", 16U,
                                              opts->string_charset, result);
        case 0x2710U:
            return omc_irb_decode_print_flags_info(payload, payload_size, store,
                                                   source_block, order, result);
        default: break;
    }

    return OMC_IRB_OK;
}

void
omc_irb_opts_init(omc_irb_opts* opts)
{
    if (opts == (omc_irb_opts*)0) {
        return;
    }

    opts->decode_iptc_iim = 1;
    opts->string_charset = OMC_IRB_STR_LATIN;
    opts->limits.max_resources = 1U << 16;
    opts->limits.max_total_bytes = 64U * 1024U * 1024U;
    opts->limits.max_resource_len = 32U * 1024U * 1024U;
    omc_iptc_opts_init(&opts->iptc);
}

omc_irb_res
omc_irb_dec(const omc_u8* irb_bytes, omc_size irb_size, omc_store* store,
            omc_block_id source_block, const omc_irb_opts* opts)
{
    omc_irb_opts local_opts;
    const omc_irb_opts* use_opts;
    omc_irb_res res;
    omc_size trailing_zero_start;
    omc_u64 total_value_bytes;
    omc_u64 p;
    omc_u32 order;

    res.status = OMC_IRB_OK;
    res.resources_decoded = 0U;
    res.entries_decoded = 0U;
    res.iptc_entries_decoded = 0U;

    if (opts == (const omc_irb_opts*)0) {
        omc_irb_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        use_opts = opts;
    }

    if (irb_bytes == (const omc_u8*)0 || store == (omc_store*)0) {
        res.status = OMC_IRB_MALFORMED;
        return res;
    }
    if (irb_size == 0U || !omc_irb_match(irb_bytes, irb_size, 0U, "8BIM", 4U)) {
        res.status = OMC_IRB_UNSUPPORTED;
        return res;
    }
    if (use_opts->limits.max_total_bytes != 0U
        && (omc_u64)irb_size > use_opts->limits.max_total_bytes) {
        res.status = OMC_IRB_LIMIT;
        return res;
    }

    trailing_zero_start = omc_irb_trailing_zero_start(irb_bytes, irb_size);
    total_value_bytes = 0U;
    p = 0U;
    order = 0U;
    while (p < (omc_u64)irb_size) {
        omc_u16 resource_id;
        omc_u64 name_total;
        omc_u32 data_len32;
        omc_u64 data_len;
        omc_u64 data_off;
        omc_u64 padded_len;
        omc_byte_ref ref;
        omc_entry entry;
        omc_status st;
        omc_irb_status irb_st;

        resource_id = 0U;
        name_total = 0U;
        data_len32 = 0U;
        data_len = 0U;
        data_off = 0U;
        padded_len = 0U;
        ref.offset = 0U;
        ref.size = 0U;

        if (order >= use_opts->limits.max_resources) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }
        if ((omc_u64)irb_size - p < 4U) {
            break;
        }
        if (!omc_irb_match(irb_bytes, irb_size, p, "8BIM", 4U)) {
            if ((omc_size)p >= trailing_zero_start) {
                break;
            }
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += 4U;

        if (!omc_irb_read_u16be(irb_bytes, irb_size, p, &resource_id)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += 2U;

        if (p >= (omc_u64)irb_size) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        name_total = omc_irb_pad2(1U + irb_bytes[(omc_size)p]);
        if (name_total > ((omc_u64)irb_size - p)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += name_total;

        if (!omc_irb_read_u32be(irb_bytes, irb_size, p, &data_len32)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += 4U;
        if (data_len32 > use_opts->limits.max_resource_len) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }

        data_len = data_len32;
        data_off = p;
        padded_len = omc_irb_pad2(data_len);
        if (padded_len > ((omc_u64)irb_size - data_off)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        if (data_len > ((omc_u64)(~(omc_u64)0) - total_value_bytes)) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }

        total_value_bytes += data_len;
        if (use_opts->limits.max_total_bytes != 0U
            && total_value_bytes > use_opts->limits.max_total_bytes) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }

        st = omc_arena_append(&store->arena, irb_bytes + (omc_size)data_off,
                              (omc_size)data_len, &ref);
        if (st != OMC_STATUS_OK) {
            res.status = omc_irb_status_from_store(st);
            return res;
        }

        memset(&entry, 0, sizeof(entry));
        omc_key_make_photoshop_irb(&entry.key, resource_id);
        omc_val_make_bytes(&entry.value, ref);
        entry.origin.block = source_block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type.family = OMC_WIRE_OTHER;
        entry.origin.wire_type.code = 0U;
        entry.origin.wire_count = (omc_u32)data_len;
        entry.flags = OMC_ENTRY_FLAG_NONE;

        st = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
        if (st != OMC_STATUS_OK) {
            res.status = omc_irb_status_from_store(st);
            return res;
        }

        res.resources_decoded += 1U;
        res.entries_decoded += 1U;

        irb_st = omc_irb_decode_known_fields(irb_bytes + (omc_size)data_off,
                                             (omc_size)data_len, resource_id,
                                             use_opts, store, source_block,
                                             order, &res);
        if (irb_st == OMC_IRB_LIMIT || irb_st == OMC_IRB_NOMEM) {
            res.status = irb_st;
            return res;
        }

        if (use_opts->decode_iptc_iim && resource_id == 0x0404U) {
            omc_iptc_res iptc_res;

            iptc_res = omc_iptc_dec(irb_bytes + (omc_size)data_off,
                                    (omc_size)data_len, store, source_block,
                                    OMC_ENTRY_FLAG_DERIVED, &use_opts->iptc);
            if (iptc_res.status == OMC_IPTC_OK) {
                res.iptc_entries_decoded += iptc_res.entries_decoded;
            } else if (iptc_res.status == OMC_IPTC_NOMEM) {
                res.status = OMC_IRB_NOMEM;
                return res;
            } else if (iptc_res.status == OMC_IPTC_LIMIT) {
                res.status = OMC_IRB_LIMIT;
                return res;
            }
        }

        order += 1U;
        p = data_off + padded_len;
    }

    return res;
}

omc_irb_res
omc_irb_meas(const omc_u8* irb_bytes, omc_size irb_size,
             const omc_irb_opts* opts)
{
    omc_store scratch;
    omc_irb_res res;

    omc_store_init(&scratch);
    res = omc_irb_dec(irb_bytes, irb_size, &scratch, OMC_INVALID_BLOCK_ID,
                      opts);
    omc_store_fini(&scratch);
    return res;
}
