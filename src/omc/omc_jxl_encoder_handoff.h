#ifndef OMC_JXL_ENCODER_HANDOFF_H
#define OMC_JXL_ENCODER_HANDOFF_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_span.h"
#include "omc/omc_status.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer.h"

OMC_EXTERN_C_BEGIN

enum {
    OMC_TRANSFER_CONTRACT_VERSION = 1U,
    OMC_JXL_ENCODER_HANDOFF_VERSION = 1U
};

typedef struct omc_jxl_encoder_handoff_opts {
    omc_u32 icc_block_index;
    omc_u32 box_count;
    omc_u64 box_payload_bytes;
} omc_jxl_encoder_handoff_opts;

typedef struct omc_jxl_encoder_handoff {
    omc_u32 contract_version;
    int has_icc_profile;
    omc_u32 icc_block_index;
    omc_u32 box_count;
    omc_u64 box_payload_bytes;
    omc_const_bytes icc_profile;
} omc_jxl_encoder_handoff;

typedef struct omc_jxl_encoder_handoff_io_res {
    omc_transfer_status status;
    omc_u64 bytes;
} omc_jxl_encoder_handoff_io_res;

OMC_API void
omc_jxl_encoder_handoff_opts_init(omc_jxl_encoder_handoff_opts* opts);

OMC_API void
omc_jxl_encoder_handoff_init(omc_jxl_encoder_handoff* handoff);

OMC_API void
omc_jxl_encoder_handoff_io_res_init(omc_jxl_encoder_handoff_io_res* res);

OMC_API omc_status
omc_jxl_encoder_handoff_build(const omc_store* store,
                              const omc_jxl_encoder_handoff_opts* opts,
                              omc_arena* out_icc_profile,
                              omc_jxl_encoder_handoff* out_handoff);

OMC_API omc_status
omc_jxl_encoder_handoff_serialize(
    const omc_jxl_encoder_handoff* handoff, omc_arena* out_bytes,
    omc_jxl_encoder_handoff_io_res* out_res);

/* Zero-copy parse: out_handoff->icc_profile aliases the input byte buffer. */
OMC_API omc_status
omc_jxl_encoder_handoff_parse_view(
    const omc_u8* bytes, omc_size size, omc_jxl_encoder_handoff* out_handoff,
    omc_jxl_encoder_handoff_io_res* out_res);

OMC_API omc_status
omc_jxl_encoder_handoff_deserialize(
    const omc_u8* bytes, omc_size size, omc_arena* out_icc_profile,
    omc_jxl_encoder_handoff* out_handoff,
    omc_jxl_encoder_handoff_io_res* out_res);

OMC_EXTERN_C_END

#endif
