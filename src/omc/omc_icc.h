#ifndef OMC_ICC_H
#define OMC_ICC_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_icc_status {
    OMC_ICC_OK = 0,
    OMC_ICC_UNSUPPORTED = 1,
    OMC_ICC_MALFORMED = 2,
    OMC_ICC_LIMIT = 3,
    OMC_ICC_NOMEM = 4
} omc_icc_status;

typedef struct omc_icc_limits {
    omc_u32 max_tags;
    omc_u32 max_tag_bytes;
    omc_u64 max_total_tag_bytes;
} omc_icc_limits;

typedef struct omc_icc_opts {
    omc_icc_limits limits;
} omc_icc_opts;

typedef struct omc_icc_res {
    omc_icc_status status;
    omc_u32 entries_decoded;
} omc_icc_res;

OMC_API void
omc_icc_opts_init(omc_icc_opts* opts);

OMC_API omc_icc_res
omc_icc_dec(const omc_u8* icc_bytes, omc_size icc_size, omc_store* store,
            omc_block_id source_block, const omc_icc_opts* opts);

OMC_API omc_icc_res
omc_icc_meas(const omc_u8* icc_bytes, omc_size icc_size,
             const omc_icc_opts* opts);

OMC_EXTERN_C_END

#endif
