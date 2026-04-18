#ifndef OMC_TRANSFER_H
#define OMC_TRANSFER_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_store.h"
#include "omc/omc_xmp_apply.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_transfer_status {
    OMC_TRANSFER_OK = 0,
    OMC_TRANSFER_UNSUPPORTED = 1,
    OMC_TRANSFER_MALFORMED = 2,
    OMC_TRANSFER_LIMIT = 3
} omc_transfer_status;

typedef enum omc_transfer_route_kind {
    OMC_TRANSFER_ROUTE_EMBEDDED_XMP = 0,
    OMC_TRANSFER_ROUTE_SIDECAR_XMP = 1
} omc_transfer_route_kind;

typedef enum omc_transfer_embedded_action {
    OMC_TRANSFER_EMBEDDED_NONE = 0,
    OMC_TRANSFER_EMBEDDED_STRIP = 1,
    OMC_TRANSFER_EMBEDDED_REWRITE = 2
} omc_transfer_embedded_action;

typedef enum omc_transfer_existing_xmp_mode {
    OMC_TRANSFER_EXISTING_XMP_IGNORE = 0,
    OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT = 1
} omc_transfer_existing_xmp_mode;

typedef enum omc_transfer_existing_xmp_precedence {
    OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING = 0,
    OMC_TRANSFER_EXISTING_XMP_PREFER_SOURCE = 1
} omc_transfer_existing_xmp_precedence;

typedef enum omc_transfer_existing_xmp_carrier_precedence {
    OMC_TRANSFER_EXISTING_XMP_PREFER_SIDECAR = 0,
    OMC_TRANSFER_EXISTING_XMP_PREFER_EMBEDDED = 1
} omc_transfer_existing_xmp_carrier_precedence;

typedef enum omc_dng_target_mode {
    OMC_DNG_TARGET_EXISTING = 0,
    OMC_DNG_TARGET_TEMPLATE = 1,
    OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD = 2
} omc_dng_target_mode;

typedef struct omc_transfer_prepare_opts {
    omc_scan_fmt format;
    omc_dng_target_mode dng_target_mode;
    omc_xmp_writeback_mode writeback_mode;
    omc_xmp_destination_embedded_mode destination_embedded_mode;
    const omc_store* existing_sidecar_xmp_store;
    omc_transfer_existing_xmp_mode existing_sidecar_xmp_mode;
    omc_transfer_existing_xmp_precedence existing_sidecar_xmp_precedence;
    const omc_store* existing_embedded_xmp_store;
    omc_transfer_existing_xmp_mode existing_embedded_xmp_mode;
    omc_transfer_existing_xmp_precedence existing_embedded_xmp_precedence;
    omc_transfer_existing_xmp_carrier_precedence
        existing_xmp_carrier_precedence;
    omc_xmp_embed_opts embedded;
    omc_xmp_sidecar_req sidecar;
} omc_transfer_prepare_opts;

typedef struct omc_transfer_bundle {
    omc_transfer_status status;
    omc_scan_fmt format;
    omc_dng_target_mode dng_target_mode;
    omc_xmp_writeback_mode writeback_mode;
    omc_xmp_destination_embedded_mode destination_embedded_mode;
    omc_transfer_embedded_action embedded_action;
    omc_u32 route_count;
    omc_u32 existing_xmp_blocks;
    int sidecar_requested;
    int embedded_supported;
    const omc_store* existing_sidecar_xmp_store;
    omc_transfer_existing_xmp_mode existing_sidecar_xmp_mode;
    omc_transfer_existing_xmp_precedence existing_sidecar_xmp_precedence;
    const omc_store* existing_embedded_xmp_store;
    omc_transfer_existing_xmp_mode existing_embedded_xmp_mode;
    omc_transfer_existing_xmp_precedence existing_embedded_xmp_precedence;
    omc_transfer_existing_xmp_carrier_precedence
        existing_xmp_carrier_precedence;
    omc_xmp_embed_opts embedded;
    omc_xmp_sidecar_req sidecar;
} omc_transfer_bundle;

typedef struct omc_transfer_route {
    omc_transfer_route_kind kind;
    omc_transfer_embedded_action embedded_action;
} omc_transfer_route;

typedef struct omc_transfer_exec {
    omc_transfer_status status;
    omc_scan_fmt format;
    omc_dng_target_mode dng_target_mode;
    omc_xmp_writeback_mode writeback_mode;
    omc_u32 route_count;
    omc_transfer_route routes[2];
    const omc_store* existing_sidecar_xmp_store;
    omc_transfer_existing_xmp_mode existing_sidecar_xmp_mode;
    omc_transfer_existing_xmp_precedence existing_sidecar_xmp_precedence;
    const omc_store* existing_embedded_xmp_store;
    omc_transfer_existing_xmp_mode existing_embedded_xmp_mode;
    omc_transfer_existing_xmp_precedence existing_embedded_xmp_precedence;
    omc_transfer_existing_xmp_carrier_precedence
        existing_xmp_carrier_precedence;
    omc_xmp_write_opts embedded_write;
    omc_xmp_sidecar_req sidecar;
} omc_transfer_exec;

typedef struct omc_transfer_res {
    omc_transfer_status status;
    omc_scan_fmt format;
    omc_dng_target_mode dng_target_mode;
    omc_xmp_writeback_mode writeback_mode;
    omc_u32 route_count;
    int edited_present;
    int sidecar_present;
    omc_xmp_write_res embedded;
    omc_xmp_dump_res sidecar;
} omc_transfer_res;

OMC_API void
omc_transfer_prepare_opts_init(omc_transfer_prepare_opts* opts);

OMC_API omc_status
omc_transfer_prepare(const omc_u8* file_bytes, omc_size file_size,
                     const omc_store* store,
                     const omc_transfer_prepare_opts* opts,
                     omc_transfer_bundle* out_bundle);

OMC_API omc_status
omc_transfer_compile(const omc_transfer_bundle* bundle,
                     omc_transfer_exec* out_exec);

OMC_API omc_status
omc_transfer_execute(const omc_u8* file_bytes, omc_size file_size,
                     const omc_store* store, omc_arena* edited_out,
                     omc_arena* sidecar_out,
                     const omc_transfer_exec* exec,
                     omc_transfer_res* out_res);

OMC_EXTERN_C_END

#endif
