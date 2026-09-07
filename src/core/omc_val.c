#include "omc/omc_val.h"
#include "omc_value_internal.h"

#include <string.h>

int
omc_ref_valid(const omc_arena* arena, omc_byte_ref ref)
{
    return ref.offset <= arena->size && ref.size <= arena->size - ref.offset
           && (ref.size == 0U || arena->data != NULL);
}

int
omc_store_shape_valid(const omc_store* store)
{
    return store != NULL && store->entry_count <= store->entry_capacity
           && store->block_count <= store->block_capacity
           && store->arena.size <= store->arena.capacity
           && (store->entry_count == 0U || store->entries != NULL)
           && (store->block_count == 0U || store->blocks != NULL)
           && (store->arena.size == 0U || store->arena.data != NULL);
}

int
omc_value_shape_valid(const omc_val* v, const omc_arena* arena)
{
    omc_u32 width;
    width = omc_elem_size(v->elem_type);
    if (width == 0U || v->text_encoding < OMC_TEXT_UNKNOWN
        || v->text_encoding > OMC_TEXT_UTF16BE
        || v->byte_order < OMC_BYTE_ORDER_NATIVE
        || v->byte_order > OMC_BYTE_ORDER_BIG) {
        return 0;
    }
    switch (v->kind) {
    case OMC_VAL_EMPTY: return v->count == 0U;
    case OMC_VAL_SCALAR: return v->count == 1U;
    case OMC_VAL_ARRAY:
        return omc_ref_valid(arena, v->u.ref)
               && (omc_u64)v->count * width == v->u.ref.size;
    case OMC_VAL_TEXT:
    case OMC_VAL_BYTES:
        return omc_ref_valid(arena, v->u.ref) && v->count == v->u.ref.size;
    }
    return 0;
}

omc_u16
omc_value_tiff_type(const omc_val* v)
{
    static const omc_u16 types[] = {1U,6U,3U,8U,4U,9U,0U,0U,11U,12U,5U,10U};
    if (v->kind == OMC_VAL_TEXT) return 2U;
    if (v->kind == OMC_VAL_BYTES) return 7U;
    if ((v->kind == OMC_VAL_SCALAR || v->kind == OMC_VAL_ARRAY)
        && v->elem_type >= OMC_ELEM_U8 && v->elem_type <= OMC_ELEM_SRATIONAL) {
        return types[v->elem_type];
    }
    return 0U;
}

static omc_u64
omc_array_word(const omc_u8* p, omc_u32 width, int little)
{
    omc_u64 word;
    omc_u32 i;
    word = 0U;
    for (i = 0U; i < width; ++i) {
        word |= (omc_u64)p[little ? i : width - i - 1U] << (8U * i);
    }
    return word;
}

void
omc_value_array_scalar(const omc_val* v, const omc_arena* arena,
                        omc_u32 index, omc_val* scalar)
{
    const omc_u8* p;
    omc_u32 width;
    omc_u64 word;
    omc_u16 native;
    int little;
    native = 1U;
    little = v->byte_order == OMC_BYTE_ORDER_LITTLE
             || (v->byte_order == OMC_BYTE_ORDER_NATIVE
                 && *(const omc_u8*)&native == 1U);
    width = omc_elem_size(v->elem_type);
    p = arena->data + v->u.ref.offset + (omc_size)index * width;
    omc_val_init(scalar);
    scalar->kind = OMC_VAL_SCALAR;
    scalar->elem_type = v->elem_type;
    scalar->count = 1U;
    if (v->elem_type == OMC_ELEM_URATIONAL
        || v->elem_type == OMC_ELEM_SRATIONAL) {
        omc_u32 n;
        omc_u32 d;
        n = (omc_u32)omc_array_word(p, 4U, little);
        d = (omc_u32)omc_array_word(p + 4U, 4U, little);
        if (v->elem_type == OMC_ELEM_URATIONAL) {
            scalar->u.ur.numer = n;
            scalar->u.ur.denom = d;
        } else {
            scalar->u.sr.numer = n <= 0x7FFFFFFFU ? (omc_s32)n
                                 : -1 - (omc_s32)(~n);
            scalar->u.sr.denom = d <= 0x7FFFFFFFU ? (omc_s32)d
                                 : -1 - (omc_s32)(~d);
        }
        return;
    }
    word = omc_array_word(p, width, little);
    switch (v->elem_type) {
    case OMC_ELEM_I8:
    case OMC_ELEM_I16:
    case OMC_ELEM_I32:
    case OMC_ELEM_I64:
        if ((word & ((omc_u64)1U << (width * 8U - 1U))) != 0U) {
            if (width < 8U) word |= ~(omc_u64)0 << (width * 8U);
            scalar->u.i64 = -1 - (omc_s64)(~word);
        } else {
            scalar->u.i64 = (omc_s64)word;
        }
        break;
    case OMC_ELEM_F32_BITS: scalar->u.f32_bits = (omc_u32)word; break;
    case OMC_ELEM_F64_BITS: scalar->u.f64_bits = word; break;
    default: scalar->u.u64 = word; break;
    }
}

void
omc_val_make_i8(omc_val* value, omc_s8 scalar)
{
    if (value == NULL) {
        return;
    }
    omc_val_init(value);
    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_I8;
    value->count = 1U;
    value->u.i64 = scalar;
}

void
omc_val_make_i32(omc_val* value, omc_s32 scalar)
{
    if (value == NULL) {
        return;
    }
    omc_val_init(value);
    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_I32;
    value->count = 1U;
    value->u.i64 = scalar;
}

void
omc_val_make_urational(omc_val* value, omc_urational scalar)
{
    if (value == NULL) {
        return;
    }
    omc_val_init(value);
    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_URATIONAL;
    value->count = 1U;
    value->u.ur = scalar;
}

void
omc_val_make_srational(omc_val* value, omc_srational scalar)
{
    if (value == NULL) {
        return;
    }
    omc_val_init(value);
    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_SRATIONAL;
    value->count = 1U;
    value->u.sr = scalar;
}

void
omc_val_make_array(omc_val* value, omc_elem_type type, omc_u32 count,
                    omc_byte_ref ref, omc_byte_order order)
{
    if (value == NULL) {
        return;
    }
    omc_val_init(value);
    value->kind = OMC_VAL_ARRAY;
    value->elem_type = type;
    value->count = count;
    value->u.ref = ref;
    value->byte_order = order;
}

omc_u32
omc_elem_size(omc_elem_type type)
{
    switch (type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_I8: return 1U;
    case OMC_ELEM_U16:
    case OMC_ELEM_I16: return 2U;
    case OMC_ELEM_U32:
    case OMC_ELEM_I32:
    case OMC_ELEM_F32_BITS: return 4U;
    case OMC_ELEM_U64:
    case OMC_ELEM_I64:
    case OMC_ELEM_F64_BITS:
    case OMC_ELEM_URATIONAL:
    case OMC_ELEM_SRATIONAL: return 8U;
    }
    return 0U;
}

void
omc_val_init(omc_val* value)
{
    if (value == NULL) {
        return;
    }

    memset(value, 0, sizeof(*value));
    value->kind = OMC_VAL_EMPTY;
    value->elem_type = OMC_ELEM_U8;
    value->text_encoding = OMC_TEXT_UNKNOWN;
}

void
omc_val_make_u8(omc_val* value, omc_u8 scalar)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_U8;
    value->count = 1U;
    value->u.u64 = scalar;
}

void
omc_val_make_u16(omc_val* value, omc_u16 scalar)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_U16;
    value->count = 1U;
    value->u.u64 = scalar;
}

void
omc_val_make_u32(omc_val* value, omc_u32 scalar)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_U32;
    value->count = 1U;
    value->u.u64 = scalar;
}

void
omc_val_make_u64(omc_val* value, omc_u64 scalar)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_U64;
    value->count = 1U;
    value->u.u64 = scalar;
}

void
omc_val_make_i16(omc_val* value, omc_s16 scalar)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_I16;
    value->count = 1U;
    value->u.i64 = scalar;
}

void
omc_val_make_i64(omc_val* value, omc_s64 scalar)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_I64;
    value->count = 1U;
    value->u.i64 = scalar;
}

void
omc_val_make_f32_bits(omc_val* value, omc_u32 bits)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_F32_BITS;
    value->count = 1U;
    value->u.f32_bits = bits;
}

void
omc_val_make_f64_bits(omc_val* value, omc_u64 bits)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_SCALAR;
    value->elem_type = OMC_ELEM_F64_BITS;
    value->count = 1U;
    value->u.f64_bits = bits;
}

void
omc_val_make_bytes(omc_val* value, omc_byte_ref ref)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_BYTES;
    value->elem_type = OMC_ELEM_U8;
    value->count = ref.size;
    value->u.ref = ref;
}

void
omc_val_make_text(omc_val* value, omc_byte_ref ref, omc_text_encoding enc)
{
    omc_val_init(value);
    if (value == NULL) {
        return;
    }

    value->kind = OMC_VAL_TEXT;
    value->elem_type = OMC_ELEM_U8;
    value->text_encoding = enc;
    value->count = ref.size;
    value->u.ref = ref;
}
