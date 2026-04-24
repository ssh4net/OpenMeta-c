#ifndef OMC_TRANSFER_ARTIFACT_H
#define OMC_TRANSFER_ARTIFACT_H

#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_transfer.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_transfer_artifact_kind {
    OMC_TRANSFER_ARTIFACT_UNKNOWN = 0,
    OMC_TRANSFER_ARTIFACT_JXL_ENCODER_HANDOFF = 1,
    OMC_TRANSFER_ARTIFACT_TRANSFER_PAYLOAD_BATCH = 2,
    OMC_TRANSFER_ARTIFACT_TRANSFER_PACKAGE_BATCH = 3
} omc_transfer_artifact_kind;

typedef struct omc_transfer_artifact_info {
    omc_transfer_artifact_kind kind;
    int has_contract_version;
    omc_u32 contract_version;
    int has_target_format;
    omc_scan_fmt target_format;
    omc_u32 entry_count;
    omc_u64 payload_bytes;
    omc_u64 binding_bytes;
    omc_u64 signed_payload_bytes;
    int has_icc_profile;
    omc_u32 icc_block_index;
    omc_u64 icc_profile_bytes;
    omc_u64 box_payload_bytes;
} omc_transfer_artifact_info;

typedef struct omc_transfer_artifact_io_res {
    omc_transfer_status status;
    omc_u64 bytes;
} omc_transfer_artifact_io_res;

OMC_API void
omc_transfer_artifact_info_init(omc_transfer_artifact_info* info);

OMC_API void
omc_transfer_artifact_io_res_init(omc_transfer_artifact_io_res* res);

OMC_API omc_status
omc_transfer_artifact_inspect(const omc_u8* bytes, omc_size size,
                              omc_transfer_artifact_info* out_info,
                              omc_transfer_artifact_io_res* out_res);

OMC_EXTERN_C_END

#endif
