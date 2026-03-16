#include "omc/omc_val.h"

#include <string.h>

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
