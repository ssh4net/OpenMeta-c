#ifndef OMC_STATUS_H
#define OMC_STATUS_H

#include "omc/omc_base.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_status {
    OMC_STATUS_OK = 0,
    OMC_STATUS_INVALID_ARGUMENT = 1,
    OMC_STATUS_NO_MEMORY = 2,
    OMC_STATUS_OVERFLOW = 3,
    OMC_STATUS_OUT_OF_RANGE = 4,
    OMC_STATUS_STATE = 5
} omc_status;

OMC_EXTERN_C_END

#endif
