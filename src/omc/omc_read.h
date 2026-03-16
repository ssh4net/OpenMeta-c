#ifndef OMC_READ_H
#define OMC_READ_H

#include "omc/omc_exif.h"
#include "omc/omc_icc.h"
#include "omc/omc_iptc.h"
#include "omc/omc_irb.h"
#include "omc/omc_jumbf.h"
#include "omc/omc_pay.h"
#include "omc/omc_scan.h"
#include "omc/omc_store.h"
#include "omc/omc_xmp.h"

OMC_EXTERN_C_BEGIN

typedef struct omc_read_opts {
    omc_exif_opts exif;
    omc_icc_opts icc;
    omc_iptc_opts iptc;
    omc_irb_opts irb;
    omc_jumbf_opts jumbf;
    omc_pay_opts pay;
    omc_xmp_opts xmp;
} omc_read_opts;

typedef struct omc_read_res {
    omc_scan_res scan;
    omc_pay_res pay;
    omc_exif_res exif;
    omc_icc_res icc;
    omc_iptc_res iptc;
    omc_irb_res irb;
    omc_jumbf_res jumbf;
    omc_xmp_res xmp;
    omc_u32 entries_added;
} omc_read_res;

OMC_API void
omc_read_opts_init(omc_read_opts* opts);

OMC_API omc_read_res
omc_read_simple(const omc_u8* file_bytes, omc_size file_size,
                omc_store* store, omc_blk_ref* out_blocks, omc_u32 block_cap,
                omc_exif_ifd_ref* out_ifds, omc_u32 ifd_cap,
                omc_u8* payload, omc_size payload_cap,
                omc_u32* payload_scratch_indices, omc_u32 payload_scratch_cap,
                const omc_read_opts* opts);

OMC_EXTERN_C_END

#endif
