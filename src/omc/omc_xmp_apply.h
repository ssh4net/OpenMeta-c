#ifndef OMC_XMP_APPLY_H
#define OMC_XMP_APPLY_H

#include "omc/omc_arena.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_xmp_dump.h"
#include "omc/omc_xmp_write.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_xmp_writeback_mode {
    OMC_XMP_WRITEBACK_EMBEDDED_ONLY = 0,
    OMC_XMP_WRITEBACK_SIDECAR_ONLY = 1,
    OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR = 2
} omc_xmp_writeback_mode;

typedef enum omc_xmp_destination_embedded_mode {
    OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING = 0,
    OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING = 1
} omc_xmp_destination_embedded_mode;

typedef struct omc_xmp_apply_opts {
    omc_scan_fmt format;
    omc_xmp_writeback_mode writeback_mode;
    omc_xmp_destination_embedded_mode destination_embedded_mode;
    omc_xmp_embed_opts embedded;
    omc_xmp_sidecar_req sidecar;
} omc_xmp_apply_opts;

typedef struct omc_xmp_apply_res {
    omc_xmp_writeback_mode writeback_mode;
    int sidecar_requested;
    omc_xmp_write_res embedded;
    omc_xmp_dump_res sidecar;
} omc_xmp_apply_res;

OMC_API void
omc_xmp_apply_opts_init(omc_xmp_apply_opts* opts);

OMC_API omc_status
omc_xmp_apply(const omc_u8* file_bytes, omc_size file_size,
              const omc_store* store, omc_arena* edited_out,
              omc_arena* sidecar_out, const omc_xmp_apply_opts* opts,
              omc_xmp_apply_res* out_res);

OMC_EXTERN_C_END

#endif
