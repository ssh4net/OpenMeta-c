#ifndef OMC_XMP_H
#define OMC_XMP_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_xmp_status {
    OMC_XMP_OK = 0,
    OMC_XMP_TRUNCATED = 1,
    OMC_XMP_UNSUPPORTED = 2,
    OMC_XMP_MALFORMED = 3,
    OMC_XMP_LIMIT = 4,
    OMC_XMP_NOMEM = 5
} omc_xmp_status;

typedef enum omc_xmp_malformed_mode {
    OMC_XMP_MALFORMED_STRICT = 0,
    OMC_XMP_MALFORMED_TRUNCATED = 1
} omc_xmp_malformed_mode;

typedef struct omc_xmp_limits {
    omc_u32 max_depth;
    omc_u32 max_properties;
    omc_u64 max_input_bytes;
    omc_u32 max_path_bytes;
    omc_u32 max_value_bytes;
    omc_u64 max_total_value_bytes;
} omc_xmp_limits;

typedef struct omc_xmp_opts {
    int decode_description_attributes;
    omc_xmp_malformed_mode malformed_mode;
    omc_xmp_limits limits;
} omc_xmp_opts;

typedef struct omc_xmp_res {
    omc_xmp_status status;
    omc_u32 entries_decoded;
} omc_xmp_res;

OMC_API void
omc_xmp_opts_init(omc_xmp_opts* opts);

OMC_API omc_xmp_res
omc_xmp_dec(const omc_u8* xmp_bytes, omc_size xmp_size, omc_store* store,
            omc_block_id source_block, omc_entry_flags flags,
            const omc_xmp_opts* opts);

OMC_API omc_xmp_res
omc_xmp_meas(const omc_u8* xmp_bytes, omc_size xmp_size,
             const omc_xmp_opts* opts);

OMC_EXTERN_C_END

#endif
