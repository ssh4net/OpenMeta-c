#include "omc/omc_exr.h"

#include <string.h>

typedef struct omc_exr_span {
    omc_u64 offset;
    omc_u32 size;
} omc_exr_span;

typedef enum omc_exr_parse_str_status {
    OMC_EXR_PARSE_STR_OK = 0,
    OMC_EXR_PARSE_STR_MALFORMED = 1,
    OMC_EXR_PARSE_STR_LIMIT = 2
} omc_exr_parse_str_status;

typedef struct omc_exr_type_code_map {
    const char* name;
    omc_u16 code;
} omc_exr_type_code_map;

static const omc_u32 k_omc_exr_magic = 20000630U;
static const omc_u32 k_omc_exr_version_mask = 0x000000FFU;
static const omc_u32 k_omc_exr_supported_version = 2U;
static const omc_u32 k_omc_exr_multipart_flag = 0x00001000U;
static const omc_u32 k_omc_exr_valid_flags =
    0x00000200U | 0x00000400U | 0x00000800U | 0x00001000U;
static const omc_u16 k_omc_exr_attr_opaque = 31U;

static const omc_exr_type_code_map k_omc_exr_type_code_map[] = {
    { "box2i", 1U },
    { "box2f", 2U },
    { "bytes", 3U },
    { "chlist", 4U },
    { "chromaticities", 5U },
    { "compression", 6U },
    { "double", 7U },
    { "envmap", 8U },
    { "float", 9U },
    { "floatvector", 10U },
    { "int", 11U },
    { "keycode", 12U },
    { "lineOrder", 13U },
    { "m33f", 14U },
    { "m33d", 15U },
    { "m44f", 16U },
    { "m44d", 17U },
    { "preview", 18U },
    { "rational", 19U },
    { "string", 20U },
    { "stringvector", 21U },
    { "tiledesc", 22U },
    { "timecode", 23U },
    { "v2i", 24U },
    { "v2f", 25U },
    { "v2d", 26U },
    { "v3i", 27U },
    { "v3f", 28U },
    { "v3d", 29U },
    { "deepImageState", 30U }
};

typedef struct omc_exr_state {
    const omc_u8* bytes;
    omc_size size;
    omc_store* store;
    omc_entry_flags flags;
    omc_exr_opts opts;
    omc_u32 total_attr_count;
    omc_u64 total_attr_bytes;
    omc_exr_res res;
} omc_exr_state;

static int
omc_exr_read_u32le(const omc_u8* bytes, omc_size size, omc_u64 off,
                   omc_u32* out_value)
{
    omc_u32 value;

    if (bytes == (const omc_u8*)0 || out_value == (omc_u32*)0) {
        return 0;
    }
    if (off + 4U > (omc_u64)size) {
        return 0;
    }

    value = 0U;
    value |= (omc_u32)bytes[(omc_size)off + 0U] << 0;
    value |= (omc_u32)bytes[(omc_size)off + 1U] << 8;
    value |= (omc_u32)bytes[(omc_size)off + 2U] << 16;
    value |= (omc_u32)bytes[(omc_size)off + 3U] << 24;
    *out_value = value;
    return 1;
}

static int
omc_exr_read_u64le(const omc_u8* bytes, omc_size size, omc_u64 off,
                   omc_u64* out_value)
{
    omc_u64 value;
    omc_u32 i;

    if (bytes == (const omc_u8*)0 || out_value == (omc_u64*)0) {
        return 0;
    }
    if (off + 8U > (omc_u64)size) {
        return 0;
    }

    value = 0U;
    for (i = 0U; i < 8U; ++i) {
        value |= (omc_u64)bytes[(omc_size)off + i] << (8U * i);
    }
    *out_value = value;
    return 1;
}

static int
omc_exr_read_i32le(const omc_u8* bytes, omc_size size, omc_u64 off,
                   omc_s32* out_value)
{
    omc_u32 value;

    if (!omc_exr_read_u32le(bytes, size, off, &value)
        || out_value == (omc_s32*)0) {
        return 0;
    }
    *out_value = (omc_s32)value;
    return 1;
}

static omc_exr_parse_str_status
omc_exr_read_cstr_with_first(const omc_u8* bytes, omc_size size,
                             omc_u64* io_offset, omc_u8 first,
                             omc_u32 max_bytes, omc_exr_span* out_span)
{
    omc_u64 start;
    omc_u32 count;

    if (bytes == (const omc_u8*)0 || io_offset == (omc_u64*)0
        || out_span == (omc_exr_span*)0 || first == 0U) {
        return OMC_EXR_PARSE_STR_MALFORMED;
    }

    start = *io_offset - 1U;
    count = 1U;
    if (max_bytes != 0U && count > max_bytes) {
        return OMC_EXR_PARSE_STR_LIMIT;
    }

    while (1) {
        omc_u8 b;

        if (*io_offset >= (omc_u64)size) {
            return OMC_EXR_PARSE_STR_MALFORMED;
        }
        b = bytes[(omc_size)*io_offset];
        *io_offset += 1U;
        if (b == 0U) {
            break;
        }
        count += 1U;
        if (max_bytes != 0U && count > max_bytes) {
            return OMC_EXR_PARSE_STR_LIMIT;
        }
    }

    out_span->offset = start;
    out_span->size = count;
    return OMC_EXR_PARSE_STR_OK;
}

static omc_exr_parse_str_status
omc_exr_read_cstr(const omc_u8* bytes, omc_size size, omc_u64* io_offset,
                  omc_u32 max_bytes, omc_exr_span* out_span)
{
    omc_u8 first;

    if (bytes == (const omc_u8*)0 || io_offset == (omc_u64*)0
        || *io_offset >= (omc_u64)size) {
        return OMC_EXR_PARSE_STR_MALFORMED;
    }

    first = bytes[(omc_size)*io_offset];
    *io_offset += 1U;
    return omc_exr_read_cstr_with_first(bytes, size, io_offset, first,
                                        max_bytes, out_span);
}

static int
omc_exr_span_equals(const omc_u8* bytes, omc_exr_span span, const char* text)
{
    omc_size text_len;

    if (bytes == (const omc_u8*)0 || text == (const char*)0) {
        return 0;
    }
    text_len = strlen(text);
    return span.size == text_len
           && memcmp(bytes + (omc_size)span.offset, text, text_len) == 0;
}

static omc_u16
omc_exr_type_code(const omc_u8* bytes, omc_exr_span type_name)
{
    omc_size i;
    omc_size count;

    count = sizeof(k_omc_exr_type_code_map) / sizeof(k_omc_exr_type_code_map[0]);
    for (i = 0U; i < count; ++i) {
        if (omc_exr_span_equals(bytes, type_name, k_omc_exr_type_code_map[i].name)) {
            return k_omc_exr_type_code_map[i].code;
        }
    }
    return k_omc_exr_attr_opaque;
}

static int
omc_exr_has_nul(const omc_u8* bytes, omc_size size)
{
    omc_size i;

    if (bytes == (const omc_u8*)0) {
        return 0;
    }
    for (i = 0U; i < size; ++i) {
        if (bytes[i] == 0U) {
            return 1;
        }
    }
    return 0;
}

static int
omc_exr_bytes_ascii(const omc_u8* bytes, omc_size size)
{
    omc_size i;

    if (bytes == (const omc_u8*)0) {
        return 0;
    }
    for (i = 0U; i < size; ++i) {
        if (bytes[i] >= 0x80U) {
            return 0;
        }
    }
    return 1;
}

static int
omc_exr_bytes_valid_utf8(const omc_u8* bytes, omc_size size)
{
    omc_size i;

    i = 0U;
    while (i < size) {
        omc_u8 c0;
        omc_u32 needed;
        omc_u32 min_cp;
        omc_u32 cp;
        omc_u32 j;

        c0 = bytes[i];
        if ((c0 & 0x80U) == 0U) {
            i += 1U;
            continue;
        }

        needed = 0U;
        min_cp = 0U;
        cp = 0U;
        if ((c0 & 0xE0U) == 0xC0U) {
            needed = 1U;
            min_cp = 0x80U;
            cp = (omc_u32)(c0 & 0x1FU);
        } else if ((c0 & 0xF0U) == 0xE0U) {
            needed = 2U;
            min_cp = 0x800U;
            cp = (omc_u32)(c0 & 0x0FU);
        } else if ((c0 & 0xF8U) == 0xF0U) {
            needed = 3U;
            min_cp = 0x10000U;
            cp = (omc_u32)(c0 & 0x07U);
        } else {
            return 0;
        }

        if (i + needed >= size) {
            return 0;
        }
        for (j = 0U; j < needed; ++j) {
            omc_u8 cx;

            cx = bytes[i + 1U + j];
            if ((cx & 0xC0U) != 0x80U) {
                return 0;
            }
            cp = (cp << 6) | (omc_u32)(cx & 0x3FU);
        }

        if (cp < min_cp || cp > 0x10FFFFU) {
            return 0;
        }
        if (cp >= 0xD800U && cp <= 0xDFFFU) {
            return 0;
        }
        i += 1U + needed;
    }
    return 1;
}

static omc_text_encoding
omc_exr_classify_text(const omc_u8* bytes, omc_size size)
{
    if (size == 0U) {
        return OMC_TEXT_UTF8;
    }
    if (omc_exr_bytes_ascii(bytes, size)) {
        return OMC_TEXT_ASCII;
    }
    if (omc_exr_bytes_valid_utf8(bytes, size)) {
        return OMC_TEXT_UTF8;
    }
    return OMC_TEXT_UNKNOWN;
}

static void
omc_exr_make_i32(omc_val* value, omc_s32 scalar)
{
    omc_val_init(value);
    if (value == (omc_val*)0) {
        return;
    }
    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_I32;
    value->count = 1U;
    value->u.i64 = scalar;
}

static omc_status
omc_exr_make_array(omc_store* store, const void* values, omc_u32 byte_size,
                   omc_elem_type elem_type, omc_u32 count,
                   omc_val* out_value)
{
    omc_byte_ref ref;
    omc_status status;

    if (store == (omc_store*)0 || values == (const void*)0
        || out_value == (omc_val*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    status = omc_arena_append(&store->arena, values, byte_size, &ref);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    omc_val_init(out_value);
    out_value->kind = OMC_VAL_ARRAY;
    out_value->elem_type = elem_type;
    out_value->count = count;
    out_value->u.ref = ref;
    return OMC_STATUS_OK;
}

static omc_status
omc_exr_make_u32_array(omc_store* store, const omc_u32* values,
                       omc_u32 count, omc_val* out_value)
{
    return omc_exr_make_array(store, values,
                              (omc_u32)((omc_size)count * sizeof(omc_u32)),
                              OMC_ELEM_U32, count, out_value);
}

static omc_status
omc_exr_make_i32_array(omc_store* store, const omc_s32* values,
                       omc_u32 count, omc_val* out_value)
{
    return omc_exr_make_array(store, values,
                              (omc_u32)((omc_size)count * sizeof(omc_s32)),
                              OMC_ELEM_I32, count, out_value);
}

static void
omc_exr_make_srational(omc_val* value, omc_s32 numer, omc_s32 denom)
{
    omc_val_init(value);
    if (value == (omc_val*)0) {
        return;
    }
    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_SRATIONAL;
    value->count = 1U;
    value->u.sr.numer = numer;
    value->u.sr.denom = denom;
}

static int
omc_exr_decode_i32_fixed(const omc_u8* bytes, omc_size size, omc_u32 count,
                         omc_s32* out_values)
{
    omc_u32 i;

    if (bytes == (const omc_u8*)0 || out_values == (omc_s32*)0
        || size != (omc_size)count * 4U) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (!omc_exr_read_i32le(bytes, size, (omc_u64)i * 4U,
                                &out_values[i])) {
            return 0;
        }
    }
    return 1;
}

static int
omc_exr_decode_u32_fixed(const omc_u8* bytes, omc_size size, omc_u32 count,
                         omc_u32* out_values)
{
    omc_u32 i;

    if (bytes == (const omc_u8*)0 || out_values == (omc_u32*)0
        || size != (omc_size)count * 4U) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (!omc_exr_read_u32le(bytes, size, (omc_u64)i * 4U,
                                &out_values[i])) {
            return 0;
        }
    }
    return 1;
}

static omc_exr_status
omc_exr_decode_u32_array(omc_exr_state* st, const omc_u8* value_bytes,
                         omc_u32 value_size, omc_elem_type elem_type,
                         omc_val* out_value)
{
    omc_u32 count;
    omc_byte_ref ref;
    omc_mut_bytes view;
    omc_u32 i;

    if (st == (omc_exr_state*)0 || out_value == (omc_val*)0
        || value_bytes == (const omc_u8*)0 || (value_size % 4U) != 0U) {
        return OMC_EXR_MALFORMED;
    }
    if (st->store == (omc_store*)0) {
        return OMC_EXR_OK;
    }

    count = value_size / 4U;
    if (omc_arena_append(&st->store->arena, value_bytes, value_size, &ref)
        != OMC_STATUS_OK) {
        return OMC_EXR_NOMEM;
    }
    view = omc_arena_view_mut(&st->store->arena, ref);
    for (i = 0U; i < count; ++i) {
        omc_u32 v;
        if (!omc_exr_read_u32le(value_bytes, value_size, (omc_u64)i * 4U, &v)) {
            return OMC_EXR_MALFORMED;
        }
        memcpy(view.data + ((omc_size)i * sizeof(v)), &v, sizeof(v));
    }
    omc_val_init(out_value);
    out_value->kind = OMC_VAL_ARRAY;
    out_value->elem_type = elem_type;
    out_value->count = count;
    out_value->u.ref = ref;
    return OMC_EXR_OK;
}

static omc_exr_status
omc_exr_decode_u64_array(omc_exr_state* st, const omc_u8* value_bytes,
                         omc_u32 value_size, omc_elem_type elem_type,
                         omc_val* out_value)
{
    omc_u32 count;
    omc_byte_ref ref;
    omc_mut_bytes view;
    omc_u32 i;

    if (st == (omc_exr_state*)0 || out_value == (omc_val*)0
        || value_bytes == (const omc_u8*)0 || (value_size % 8U) != 0U) {
        return OMC_EXR_MALFORMED;
    }
    if (st->store == (omc_store*)0) {
        return OMC_EXR_OK;
    }

    count = value_size / 8U;
    if (omc_arena_append(&st->store->arena, value_bytes, value_size, &ref)
        != OMC_STATUS_OK) {
        return OMC_EXR_NOMEM;
    }
    view = omc_arena_view_mut(&st->store->arena, ref);
    for (i = 0U; i < count; ++i) {
        omc_u64 v;
        if (!omc_exr_read_u64le(value_bytes, value_size, (omc_u64)i * 8U, &v)) {
            return OMC_EXR_MALFORMED;
        }
        memcpy(view.data + ((omc_size)i * sizeof(v)), &v, sizeof(v));
    }
    omc_val_init(out_value);
    out_value->kind = OMC_VAL_ARRAY;
    out_value->elem_type = elem_type;
    out_value->count = count;
    out_value->u.ref = ref;
    return OMC_EXR_OK;
}

static omc_status
omc_exr_append_span(omc_store* store, omc_exr_state* st, omc_exr_span span,
                    omc_byte_ref* out_ref)
{
    if (store == (omc_store*)0 || st == (omc_exr_state*)0
        || out_ref == (omc_byte_ref*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    return omc_arena_append(&store->arena, st->bytes + (omc_size)span.offset,
                            span.size, out_ref);
}

static omc_exr_status
omc_exr_decode_value(omc_exr_state* st, omc_exr_span type_name,
                     omc_u64 value_off, omc_u32 value_size,
                     omc_val* out_value)
{
    const omc_u8* value_bytes;

    if (st == (omc_exr_state*)0 || out_value == (omc_val*)0) {
        return OMC_EXR_MALFORMED;
    }
    value_bytes = st->bytes + (omc_size)value_off;

    if (!st->opts.decode_known_types) {
        omc_byte_ref ref;
        if (st->store == (omc_store*)0) {
            return OMC_EXR_OK;
        }
        if (omc_arena_append(&st->store->arena, value_bytes, value_size, &ref)
            != OMC_STATUS_OK) {
            return OMC_EXR_NOMEM;
        }
        omc_val_make_bytes(out_value, ref);
        return OMC_EXR_OK;
    }

    if (omc_exr_span_equals(st->bytes, type_name, "int") && value_size == 4U) {
        omc_s32 v;
        if (omc_exr_read_i32le(value_bytes, value_size, 0U, &v)) {
            omc_exr_make_i32(out_value, v);
            return OMC_EXR_OK;
        }
    }
    if (omc_exr_span_equals(st->bytes, type_name, "float")
        && value_size == 4U) {
        omc_u32 bits;
        if (omc_exr_read_u32le(value_bytes, value_size, 0U, &bits)) {
            omc_val_make_f32_bits(out_value, bits);
            return OMC_EXR_OK;
        }
    }
    if (omc_exr_span_equals(st->bytes, type_name, "double")
        && value_size == 8U) {
        omc_u64 bits;
        if (omc_exr_read_u64le(value_bytes, value_size, 0U, &bits)) {
            omc_val_make_f64_bits(out_value, bits);
            return OMC_EXR_OK;
        }
    }
    if ((omc_exr_span_equals(st->bytes, type_name, "compression")
         || omc_exr_span_equals(st->bytes, type_name, "envmap")
         || omc_exr_span_equals(st->bytes, type_name, "lineOrder")
         || omc_exr_span_equals(st->bytes, type_name, "deepImageState"))
        && value_size == 1U) {
        omc_val_make_u8(out_value, value_bytes[0]);
        return OMC_EXR_OK;
    }
    if (omc_exr_span_equals(st->bytes, type_name, "string")
        && !omc_exr_has_nul(value_bytes, value_size)) {
        omc_byte_ref ref;
        if (st->store == (omc_store*)0) {
            return OMC_EXR_OK;
        }
        if (omc_arena_append(&st->store->arena, value_bytes, value_size, &ref)
            != OMC_STATUS_OK) {
            return OMC_EXR_NOMEM;
        }
        omc_val_make_text(out_value, ref,
                          omc_exr_classify_text(value_bytes, value_size));
        return OMC_EXR_OK;
    }
    if (omc_exr_span_equals(st->bytes, type_name, "rational")
        && value_size == 8U) {
        omc_s32 numer;
        omc_u32 denom;
        if (omc_exr_read_i32le(value_bytes, value_size, 0U, &numer)
            && omc_exr_read_u32le(value_bytes, value_size, 4U, &denom)
            && denom <= 0x7FFFFFFFU) {
            omc_exr_make_srational(out_value, numer, (omc_s32)denom);
            return OMC_EXR_OK;
        }
    }
    if (omc_exr_span_equals(st->bytes, type_name, "floatvector")
        && (value_size % 4U) == 0U) {
        return omc_exr_decode_u32_array(st, value_bytes, value_size,
                                        OMC_ELEM_F32_BITS, out_value);
    }
    if (omc_exr_span_equals(st->bytes, type_name, "box2i")
        && value_size == 16U) {
        omc_s32 values[4];
        if (omc_exr_decode_i32_fixed(value_bytes, value_size, 4U, values)) {
            if (st->store == (omc_store*)0) {
                return OMC_EXR_OK;
            }
            if (omc_exr_make_i32_array(st->store, values, 4U, out_value)
                != OMC_STATUS_OK) {
                return OMC_EXR_NOMEM;
            }
            return OMC_EXR_OK;
        }
    }
    if ((omc_exr_span_equals(st->bytes, type_name, "box2f")
         || omc_exr_span_equals(st->bytes, type_name, "v2f")
         || omc_exr_span_equals(st->bytes, type_name, "v3f")
         || omc_exr_span_equals(st->bytes, type_name, "m33f")
         || omc_exr_span_equals(st->bytes, type_name, "m44f")
         || omc_exr_span_equals(st->bytes, type_name, "chromaticities"))
        && (value_size % 4U) == 0U) {
        return omc_exr_decode_u32_array(st, value_bytes, value_size,
                                        OMC_ELEM_F32_BITS, out_value);
    }
    if (omc_exr_span_equals(st->bytes, type_name, "v2i")
        && value_size == 8U) {
        omc_s32 values[2];
        if (omc_exr_decode_i32_fixed(value_bytes, value_size, 2U, values)) {
            if (st->store == (omc_store*)0) {
                return OMC_EXR_OK;
            }
            if (omc_exr_make_i32_array(st->store, values, 2U, out_value)
                != OMC_STATUS_OK) {
                return OMC_EXR_NOMEM;
            }
            return OMC_EXR_OK;
        }
    }
    if (omc_exr_span_equals(st->bytes, type_name, "v3i")
        && value_size == 12U) {
        omc_s32 values[3];
        if (omc_exr_decode_i32_fixed(value_bytes, value_size, 3U, values)) {
            if (st->store == (omc_store*)0) {
                return OMC_EXR_OK;
            }
            if (omc_exr_make_i32_array(st->store, values, 3U, out_value)
                != OMC_STATUS_OK) {
                return OMC_EXR_NOMEM;
            }
            return OMC_EXR_OK;
        }
    }
    if (omc_exr_span_equals(st->bytes, type_name, "timecode")
        && value_size == 8U) {
        omc_u32 values[2];
        if (omc_exr_decode_u32_fixed(value_bytes, value_size, 2U, values)) {
            if (st->store == (omc_store*)0) {
                return OMC_EXR_OK;
            }
            if (omc_exr_make_u32_array(st->store, values, 2U, out_value)
                != OMC_STATUS_OK) {
                return OMC_EXR_NOMEM;
            }
            return OMC_EXR_OK;
        }
    }
    if ((omc_exr_span_equals(st->bytes, type_name, "v2d")
         || omc_exr_span_equals(st->bytes, type_name, "v3d")
         || omc_exr_span_equals(st->bytes, type_name, "m33d")
         || omc_exr_span_equals(st->bytes, type_name, "m44d"))
        && (value_size % 8U) == 0U) {
        return omc_exr_decode_u64_array(st, value_bytes, value_size,
                                        OMC_ELEM_F64_BITS, out_value);
    }
    if (omc_exr_span_equals(st->bytes, type_name, "keycode")
        && value_size == 28U) {
        omc_s32 values[7];
        if (omc_exr_decode_i32_fixed(value_bytes, value_size, 7U, values)) {
            if (st->store == (omc_store*)0) {
                return OMC_EXR_OK;
            }
            if (omc_exr_make_i32_array(st->store, values, 7U, out_value)
                != OMC_STATUS_OK) {
                return OMC_EXR_NOMEM;
            }
            return OMC_EXR_OK;
        }
    }
    if (omc_exr_span_equals(st->bytes, type_name, "tiledesc")
        && value_size == 9U) {
        omc_u32 base0;
        omc_u32 base1;
        omc_u32 values[3];
        if (omc_exr_read_u32le(value_bytes, value_size, 0U, &base0)
            && omc_exr_read_u32le(value_bytes, value_size, 4U, &base1)) {
            values[0] = base0;
            values[1] = base1;
            values[2] = value_bytes[8];
            if (st->store == (omc_store*)0) {
                return OMC_EXR_OK;
            }
            if (omc_exr_make_u32_array(st->store, values, 3U, out_value)
                != OMC_STATUS_OK) {
                return OMC_EXR_NOMEM;
            }
            return OMC_EXR_OK;
        }
    }

    if (st->store != (omc_store*)0) {
        omc_byte_ref ref;
        if (omc_arena_append(&st->store->arena, value_bytes, value_size, &ref)
            != OMC_STATUS_OK) {
            return OMC_EXR_NOMEM;
        }
        omc_val_make_bytes(out_value, ref);
    }
    return OMC_EXR_OK;
}

static omc_exr_status
omc_exr_add_part_block(omc_exr_state* st, omc_block_id* out_block)
{
    omc_block_info info;
    omc_status status;

    if (out_block == (omc_block_id*)0 || st == (omc_exr_state*)0) {
        return OMC_EXR_MALFORMED;
    }
    *out_block = OMC_INVALID_BLOCK_ID;
    if (st->store == (omc_store*)0) {
        return OMC_EXR_OK;
    }
    memset(&info, 0, sizeof(info));
    info.format = OMC_SCAN_FMT_EXR;
    info.kind = OMC_BLK_UNKNOWN;
    info.outer_size = (omc_u64)st->size;
    info.data_size = (omc_u64)st->size;
    status = omc_store_add_block(st->store, &info, out_block);
    if (status != OMC_STATUS_OK) {
        return OMC_EXR_NOMEM;
    }
    return OMC_EXR_OK;
}

static omc_exr_status
omc_exr_parse_attr_with_first(omc_exr_state* st, omc_u64* io_offset,
                              omc_u8 first_name_char, omc_u32 part_index,
                              omc_block_id part_block,
                              omc_u32* io_order_in_block,
                              omc_u32* io_part_attr_count)
{
    omc_exr_span name;
    omc_exr_span type_name;
    omc_u32 attribute_size;
    omc_u64 next_total;
    omc_exr_parse_str_status str_status;

    if (st == (omc_exr_state*)0 || io_offset == (omc_u64*)0
        || io_order_in_block == (omc_u32*)0
        || io_part_attr_count == (omc_u32*)0) {
        return OMC_EXR_MALFORMED;
    }

    if (st->opts.limits.max_attributes_per_part != 0U
        && *io_part_attr_count >= st->opts.limits.max_attributes_per_part) {
        return OMC_EXR_LIMIT;
    }
    if (st->opts.limits.max_attributes != 0U
        && st->total_attr_count >= st->opts.limits.max_attributes) {
        return OMC_EXR_LIMIT;
    }

    str_status = omc_exr_read_cstr_with_first(
        st->bytes, st->size, io_offset, first_name_char,
        st->opts.limits.max_name_bytes, &name);
    if (str_status == OMC_EXR_PARSE_STR_MALFORMED) {
        return OMC_EXR_MALFORMED;
    }
    if (str_status == OMC_EXR_PARSE_STR_LIMIT) {
        return OMC_EXR_LIMIT;
    }

    str_status = omc_exr_read_cstr(st->bytes, st->size, io_offset,
                                   st->opts.limits.max_type_name_bytes,
                                   &type_name);
    if (str_status == OMC_EXR_PARSE_STR_MALFORMED) {
        return OMC_EXR_MALFORMED;
    }
    if (str_status == OMC_EXR_PARSE_STR_LIMIT) {
        return OMC_EXR_LIMIT;
    }

    if (!omc_exr_read_u32le(st->bytes, st->size, *io_offset, &attribute_size)) {
        return OMC_EXR_MALFORMED;
    }
    *io_offset += 4U;

    if (st->opts.limits.max_attribute_bytes != 0U
        && attribute_size > st->opts.limits.max_attribute_bytes) {
        return OMC_EXR_LIMIT;
    }
    if (*io_offset + (omc_u64)attribute_size > (omc_u64)st->size) {
        return OMC_EXR_MALFORMED;
    }

    next_total = st->total_attr_bytes + (omc_u64)attribute_size;
    if (next_total < st->total_attr_bytes) {
        return OMC_EXR_LIMIT;
    }
    if (st->opts.limits.max_total_attribute_bytes != 0U
        && next_total > st->opts.limits.max_total_attribute_bytes) {
        return OMC_EXR_LIMIT;
    }

    if (st->store != (omc_store*)0) {
        omc_entry entry;
        omc_byte_ref name_ref;
        omc_exr_status value_status;
        omc_u16 wire_code;

        memset(&entry, 0, sizeof(entry));
        if (omc_exr_append_span(st->store, st, name, &name_ref)
            != OMC_STATUS_OK) {
            return OMC_EXR_NOMEM;
        }
        omc_key_make_exr_attr(&entry.key, part_index, name_ref);
        value_status = omc_exr_decode_value(st, type_name, *io_offset,
                                            attribute_size, &entry.value);
        if (value_status != OMC_EXR_OK) {
            return value_status;
        }
        entry.origin.block = part_block;
        entry.origin.order_in_block = *io_order_in_block;
        entry.origin.wire_type.family = OMC_WIRE_OTHER;
        wire_code = omc_exr_type_code(st->bytes, type_name);
        entry.origin.wire_type.code = wire_code;
        entry.origin.wire_count = attribute_size;
        entry.origin.wire_type_name.offset = 0U;
        entry.origin.wire_type_name.size = 0U;
        if (st->opts.preserve_unknown_type_name
            && wire_code == k_omc_exr_attr_opaque) {
            if (omc_exr_append_span(st->store, st, type_name,
                                    &entry.origin.wire_type_name)
                != OMC_STATUS_OK) {
                return OMC_EXR_NOMEM;
            }
        }
        entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
        entry.origin.name_context_variant = 0U;
        entry.flags = st->flags;
        if (omc_store_add_entry(st->store, &entry, (omc_entry_id*)0)
            != OMC_STATUS_OK) {
            return OMC_EXR_NOMEM;
        }
    }

    *io_offset += (omc_u64)attribute_size;
    *io_order_in_block += 1U;
    *io_part_attr_count += 1U;
    st->total_attr_count += 1U;
    st->total_attr_bytes = next_total;
    st->res.entries_decoded += 1U;
    return OMC_EXR_OK;
}

static omc_exr_res
omc_exr_decode_impl(const omc_u8* exr_bytes, omc_size exr_size,
                    omc_store* store, omc_entry_flags flags,
                    const omc_exr_opts* opts)
{
    omc_exr_state st;
    omc_u32 magic;
    omc_u32 version_and_flags;
    omc_u32 version;
    omc_u32 flags_only;
    omc_u64 offset;
    omc_u32 part_index;
    omc_u32 order_in_block;
    omc_u32 part_attr_count;
    int multipart;
    omc_block_id part_block;
    omc_exr_status part_status;

    memset(&st, 0, sizeof(st));
    st.bytes = exr_bytes;
    st.size = exr_size;
    st.store = store;
    st.flags = flags;
    st.res.status = OMC_EXR_UNSUPPORTED;
    if (opts == (const omc_exr_opts*)0) {
        omc_exr_opts_init(&st.opts);
    } else {
        st.opts = *opts;
    }

    if (exr_bytes == (const omc_u8*)0) {
        st.res.status = OMC_EXR_MALFORMED;
        return st.res;
    }
    if (exr_size < 8U) {
        return st.res;
    }
    if (!omc_exr_read_u32le(exr_bytes, exr_size, 0U, &magic)
        || !omc_exr_read_u32le(exr_bytes, exr_size, 4U, &version_and_flags)) {
        return st.res;
    }
    if (magic != k_omc_exr_magic) {
        return st.res;
    }

    version = version_and_flags & k_omc_exr_version_mask;
    if (version != k_omc_exr_supported_version) {
        return st.res;
    }

    flags_only = version_and_flags & ~k_omc_exr_version_mask;
    if ((flags_only & ~k_omc_exr_valid_flags) != 0U) {
        st.res.status = OMC_EXR_MALFORMED;
        return st.res;
    }
    if (st.opts.limits.max_parts == 0U) {
        st.res.status = OMC_EXR_LIMIT;
        return st.res;
    }

    st.res.status = OMC_EXR_OK;
    multipart = (flags_only & k_omc_exr_multipart_flag) != 0U;
    offset = 8U;
    part_index = 0U;
    order_in_block = 0U;
    part_attr_count = 0U;
    part_status = omc_exr_add_part_block(&st, &part_block);
    if (part_status != OMC_EXR_OK) {
        st.res.status = part_status;
        return st.res;
    }
    st.res.parts_decoded = 1U;

    while (1) {
        omc_u8 first;

        if (offset >= (omc_u64)exr_size) {
            st.res.status = OMC_EXR_MALFORMED;
            return st.res;
        }

        first = exr_bytes[(omc_size)offset];
        offset += 1U;

        if (first == 0U) {
            if (!multipart) {
                return st.res;
            }
            if (offset >= (omc_u64)exr_size) {
                st.res.status = OMC_EXR_MALFORMED;
                return st.res;
            }
            first = exr_bytes[(omc_size)offset];
            offset += 1U;
            if (first == 0U) {
                return st.res;
            }
            part_index += 1U;
            if (part_index >= st.opts.limits.max_parts) {
                st.res.status = OMC_EXR_LIMIT;
                return st.res;
            }
            part_status = omc_exr_add_part_block(&st, &part_block);
            if (part_status != OMC_EXR_OK) {
                st.res.status = part_status;
                return st.res;
            }
            st.res.parts_decoded = part_index + 1U;
            order_in_block = 0U;
            part_attr_count = 0U;
        }

        part_status = omc_exr_parse_attr_with_first(
            &st, &offset, first, part_index, part_block, &order_in_block,
            &part_attr_count);
        if (part_status != OMC_EXR_OK) {
            st.res.status = part_status;
            return st.res;
        }
    }
}

void
omc_exr_opts_init(omc_exr_opts* opts)
{
    if (opts == (omc_exr_opts*)0) {
        return;
    }

    opts->decode_known_types = 1;
    opts->preserve_unknown_type_name = 1;
    opts->limits.max_parts = 64U;
    opts->limits.max_attributes_per_part = 1U << 16;
    opts->limits.max_attributes = 200000U;
    opts->limits.max_name_bytes = 1024U;
    opts->limits.max_type_name_bytes = 1024U;
    opts->limits.max_attribute_bytes = 8U * 1024U * 1024U;
    opts->limits.max_total_attribute_bytes =
        (omc_u64)64U * (omc_u64)1024U * (omc_u64)1024U;
}

omc_exr_res
omc_exr_dec(const omc_u8* exr_bytes, omc_size exr_size, omc_store* store,
            omc_entry_flags flags, const omc_exr_opts* opts)
{
    if (store == (omc_store*)0) {
        omc_exr_res res;
        res.status = OMC_EXR_MALFORMED;
        res.parts_decoded = 0U;
        res.entries_decoded = 0U;
        return res;
    }
    return omc_exr_decode_impl(exr_bytes, exr_size, store, flags, opts);
}

omc_exr_res
omc_exr_meas(const omc_u8* exr_bytes, omc_size exr_size,
             const omc_exr_opts* opts)
{
    return omc_exr_decode_impl(exr_bytes, exr_size, (omc_store*)0,
                               OMC_ENTRY_FLAG_NONE, opts);
}
