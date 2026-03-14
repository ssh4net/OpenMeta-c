#ifndef OMC_VAL_H
#define OMC_VAL_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef struct omc_urational {
    omc_u32 numer;
    omc_u32 denom;
} omc_urational;

typedef struct omc_srational {
    omc_s32 numer;
    omc_s32 denom;
} omc_srational;

typedef enum omc_val_kind {
    OMC_VAL_EMPTY = 0,
    OMC_VAL_SCALAR = 1,
    OMC_VAL_ARRAY = 2,
    OMC_VAL_BYTES = 3,
    OMC_VAL_TEXT = 4
} omc_val_kind;

typedef enum omc_elem_type {
    OMC_ELEM_U8 = 0,
    OMC_ELEM_I8 = 1,
    OMC_ELEM_U16 = 2,
    OMC_ELEM_I16 = 3,
    OMC_ELEM_U32 = 4,
    OMC_ELEM_I32 = 5,
    OMC_ELEM_U64 = 6,
    OMC_ELEM_I64 = 7,
    OMC_ELEM_F32_BITS = 8,
    OMC_ELEM_F64_BITS = 9,
    OMC_ELEM_URATIONAL = 10,
    OMC_ELEM_SRATIONAL = 11
} omc_elem_type;

typedef enum omc_text_encoding {
    OMC_TEXT_UNKNOWN = 0,
    OMC_TEXT_ASCII = 1,
    OMC_TEXT_UTF8 = 2,
    OMC_TEXT_UTF16LE = 3,
    OMC_TEXT_UTF16BE = 4
} omc_text_encoding;

typedef struct omc_val {
    omc_val_kind kind;
    omc_elem_type elem_type;
    omc_text_encoding text_encoding;
    omc_u32 count;
    union {
        omc_u64 u64;
        omc_s64 i64;
        omc_u32 f32_bits;
        omc_u64 f64_bits;
        omc_urational ur;
        omc_srational sr;
        omc_byte_ref ref;
    } u;
} omc_val;

OMC_API void
omc_val_init(omc_val* value);

OMC_API void
omc_val_make_u32(omc_val* value, omc_u32 scalar);

OMC_API void
omc_val_make_u64(omc_val* value, omc_u64 scalar);

OMC_API void
omc_val_make_i64(omc_val* value, omc_s64 scalar);

OMC_API void
omc_val_make_bytes(omc_val* value, omc_byte_ref ref);

OMC_API void
omc_val_make_text(omc_val* value, omc_byte_ref ref, omc_text_encoding enc);

OMC_EXTERN_C_END

#endif
