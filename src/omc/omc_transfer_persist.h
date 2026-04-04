#ifndef OMC_TRANSFER_PERSIST_H
#define OMC_TRANSFER_PERSIST_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_transfer.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_transfer_destination_sidecar_mode {
    OMC_TRANSFER_DEST_SIDECAR_PRESERVE_EXISTING = 0,
    OMC_TRANSFER_DEST_SIDECAR_STRIP_EXISTING = 1
} omc_transfer_destination_sidecar_mode;

typedef struct omc_transfer_persist_opts {
    const char* output_path;
    const char* xmp_sidecar_base_path;
    int write_output;
    int overwrite_output;
    omc_u64 prewritten_output_bytes;
    int overwrite_xmp_sidecar;
    int remove_destination_xmp_sidecar;
    omc_transfer_destination_sidecar_mode destination_sidecar_mode;
} omc_transfer_persist_opts;

typedef struct omc_transfer_persist_res {
    omc_transfer_status status;
    omc_byte_ref message;

    omc_transfer_status output_status;
    omc_byte_ref output_message;
    omc_byte_ref output_path;
    omc_u64 output_bytes;

    int xmp_sidecar_requested;
    omc_transfer_status xmp_sidecar_status;
    omc_byte_ref xmp_sidecar_message;
    omc_byte_ref xmp_sidecar_path;
    omc_u64 xmp_sidecar_bytes;

    int xmp_sidecar_cleanup_requested;
    omc_transfer_status xmp_sidecar_cleanup_status;
    omc_byte_ref xmp_sidecar_cleanup_message;
    omc_byte_ref xmp_sidecar_cleanup_path;
    int xmp_sidecar_cleanup_removed;
} omc_transfer_persist_res;

OMC_API void
omc_transfer_persist_opts_init(omc_transfer_persist_opts* opts);

OMC_API omc_status
omc_transfer_persist(const omc_u8* edited_bytes, omc_size edited_size,
                     const omc_u8* sidecar_bytes, omc_size sidecar_size,
                     const omc_transfer_res* transfer,
                     const omc_transfer_persist_opts* opts,
                     omc_arena* meta_out,
                     omc_transfer_persist_res* out_res);

OMC_EXTERN_C_END

#endif
