#ifndef OMC_EXR_H
#define OMC_EXR_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_exr_status {
    OMC_EXR_OK = 0,
    OMC_EXR_UNSUPPORTED = 1,
    OMC_EXR_MALFORMED = 2,
    OMC_EXR_LIMIT = 3,
    OMC_EXR_NOMEM = 4
} omc_exr_status;

typedef struct omc_exr_limits {
    omc_u32 max_parts;
    omc_u32 max_attributes_per_part;
    omc_u32 max_attributes;
    omc_u32 max_name_bytes;
    omc_u32 max_type_name_bytes;
    omc_u32 max_attribute_bytes;
    omc_u64 max_total_attribute_bytes;
} omc_exr_limits;

typedef struct omc_exr_opts {
    int decode_known_types;
    int preserve_unknown_type_name;
    omc_exr_limits limits;
} omc_exr_opts;

typedef struct omc_exr_res {
    omc_exr_status status;
    omc_u32 parts_decoded;
    omc_u32 entries_decoded;
} omc_exr_res;

OMC_API void
omc_exr_opts_init(omc_exr_opts* opts);

OMC_API omc_exr_res
omc_exr_dec(const omc_u8* exr_bytes, omc_size exr_size, omc_store* store,
            omc_entry_flags flags, const omc_exr_opts* opts);

OMC_API omc_exr_res
omc_exr_meas(const omc_u8* exr_bytes, omc_size exr_size,
             const omc_exr_opts* opts);

OMC_EXTERN_C_END

#endif
