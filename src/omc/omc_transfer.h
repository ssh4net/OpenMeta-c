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

typedef struct omc_transfer_prepare_opts {
    omc_scan_fmt format;
    omc_xmp_writeback_mode writeback_mode;
    omc_xmp_destination_embedded_mode destination_embedded_mode;
    omc_xmp_embed_opts embedded;
    omc_xmp_sidecar_req sidecar;
} omc_transfer_prepare_opts;

typedef struct omc_transfer_bundle {
    omc_transfer_status status;
    omc_scan_fmt format;
    omc_xmp_writeback_mode writeback_mode;
    omc_xmp_destination_embedded_mode destination_embedded_mode;
    omc_transfer_embedded_action embedded_action;
    omc_u32 route_count;
    omc_u32 existing_xmp_blocks;
    int sidecar_requested;
    int embedded_supported;
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
    omc_xmp_writeback_mode writeback_mode;
    omc_u32 route_count;
    omc_transfer_route routes[2];
    omc_xmp_write_opts embedded_write;
    omc_xmp_sidecar_req sidecar;
} omc_transfer_exec;

typedef struct omc_transfer_res {
    omc_transfer_status status;
    omc_scan_fmt format;
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
