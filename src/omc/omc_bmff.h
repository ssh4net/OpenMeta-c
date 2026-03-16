#ifndef OMC_BMFF_H
#define OMC_BMFF_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_bmff_status {
    OMC_BMFF_OK = 0,
    OMC_BMFF_MALFORMED = 1,
    OMC_BMFF_LIMIT = 2,
    OMC_BMFF_NOMEM = 3
} omc_bmff_status;

typedef struct omc_bmff_limits {
    omc_u32 max_boxes;
    omc_u32 max_depth;
    omc_u32 max_item_infos;
    omc_u32 max_entries;
} omc_bmff_limits;

typedef struct omc_bmff_opts {
    omc_bmff_limits limits;
} omc_bmff_opts;

typedef struct omc_bmff_res {
    omc_bmff_status status;
    omc_u32 boxes_scanned;
    omc_u32 item_infos;
    omc_u32 entries_decoded;
} omc_bmff_res;

OMC_API void
omc_bmff_opts_init(omc_bmff_opts* opts);

OMC_API omc_bmff_res
omc_bmff_dec(const omc_u8* file_bytes, omc_size file_size, omc_store* store,
             const omc_bmff_opts* opts);

OMC_API omc_bmff_res
omc_bmff_meas(const omc_u8* file_bytes, omc_size file_size,
              const omc_bmff_opts* opts);

OMC_EXTERN_C_END

#endif
