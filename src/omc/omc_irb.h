#ifndef OMC_IRB_H
#define OMC_IRB_H

#include "omc/omc_base.h"
#include "omc/omc_iptc.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_irb_status {
    OMC_IRB_OK = 0,
    OMC_IRB_UNSUPPORTED = 1,
    OMC_IRB_MALFORMED = 2,
    OMC_IRB_LIMIT = 3,
    OMC_IRB_NOMEM = 4
} omc_irb_status;

typedef struct omc_irb_limits {
    omc_u32 max_resources;
    omc_u64 max_total_bytes;
    omc_u32 max_resource_len;
} omc_irb_limits;

typedef enum omc_irb_str_charset {
    OMC_IRB_STR_LATIN = 0,
    OMC_IRB_STR_ASCII = 1
} omc_irb_str_charset;

typedef struct omc_irb_opts {
    int decode_iptc_iim;
    omc_irb_str_charset string_charset;
    omc_irb_limits limits;
    omc_iptc_opts iptc;
} omc_irb_opts;

typedef struct omc_irb_res {
    omc_irb_status status;
    omc_u32 resources_decoded;
    omc_u32 entries_decoded;
    omc_u32 iptc_entries_decoded;
} omc_irb_res;

OMC_API void
omc_irb_opts_init(omc_irb_opts* opts);

OMC_API omc_irb_res
omc_irb_dec(const omc_u8* irb_bytes, omc_size irb_size, omc_store* store,
            omc_block_id source_block, const omc_irb_opts* opts);

OMC_API omc_irb_res
omc_irb_meas(const omc_u8* irb_bytes, omc_size irb_size,
             const omc_irb_opts* opts);

OMC_EXTERN_C_END

#endif
