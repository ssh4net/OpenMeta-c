#ifndef OMC_IPTC_H
#define OMC_IPTC_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_iptc_status {
    OMC_IPTC_OK = 0,
    OMC_IPTC_UNSUPPORTED = 1,
    OMC_IPTC_MALFORMED = 2,
    OMC_IPTC_LIMIT = 3,
    OMC_IPTC_NOMEM = 4
} omc_iptc_status;

typedef struct omc_iptc_limits {
    omc_u32 max_datasets;
    omc_u32 max_dataset_bytes;
    omc_u64 max_total_bytes;
} omc_iptc_limits;

typedef struct omc_iptc_opts {
    omc_iptc_limits limits;
} omc_iptc_opts;

typedef struct omc_iptc_res {
    omc_iptc_status status;
    omc_u32 entries_decoded;
} omc_iptc_res;

OMC_API void
omc_iptc_opts_init(omc_iptc_opts* opts);

OMC_API omc_iptc_res
omc_iptc_dec(const omc_u8* iptc_bytes, omc_size iptc_size, omc_store* store,
             omc_block_id source_block, omc_entry_flags flags,
             const omc_iptc_opts* opts);

OMC_API omc_iptc_res
omc_iptc_meas(const omc_u8* iptc_bytes, omc_size iptc_size,
              const omc_iptc_opts* opts);

OMC_EXTERN_C_END

#endif
