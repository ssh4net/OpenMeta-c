#ifndef OMC_PAY_H
#define OMC_PAY_H

#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_pay_status {
    OMC_PAY_OK = 0,
    OMC_PAY_TRUNCATED = 1,
    OMC_PAY_UNSUPPORTED = 2,
    OMC_PAY_MALFORMED = 3,
    OMC_PAY_LIMIT = 4,
    OMC_PAY_NOMEM = 5
} omc_pay_status;

typedef struct omc_pay_limits {
    omc_u32 max_parts;
    omc_u64 max_output_bytes;
} omc_pay_limits;

typedef struct omc_pay_opts {
    int decompress;
    omc_pay_limits limits;
} omc_pay_opts;

typedef struct omc_pay_res {
    omc_pay_status status;
    omc_u64 written;
    omc_u64 needed;
} omc_pay_res;

OMC_API void
omc_pay_opts_init(omc_pay_opts* opts);

OMC_API omc_pay_res
omc_pay_ext(const omc_u8* file_bytes, omc_size file_size,
            const omc_blk_ref* blocks, omc_u32 block_count,
            omc_u32 seed_index, omc_u8* out_payload, omc_size out_cap,
            omc_u32* scratch_indices, omc_u32 scratch_cap,
            const omc_pay_opts* opts);

OMC_API omc_pay_res
omc_pay_meas(const omc_u8* file_bytes, omc_size file_size,
             const omc_blk_ref* blocks, omc_u32 block_count,
             omc_u32 seed_index, omc_u32* scratch_indices,
             omc_u32 scratch_cap, const omc_pay_opts* opts);

OMC_EXTERN_C_END

#endif
