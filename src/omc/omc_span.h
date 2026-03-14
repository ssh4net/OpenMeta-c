#ifndef OMC_SPAN_H
#define OMC_SPAN_H

#include "omc/omc_base.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef struct omc_const_bytes {
    const omc_u8* data;
    omc_size size;
} omc_const_bytes;

typedef struct omc_mut_bytes {
    omc_u8* data;
    omc_size size;
} omc_mut_bytes;

OMC_EXTERN_C_END

#endif
