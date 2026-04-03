#ifndef OMC_XMP_EMBED_H
#define OMC_XMP_EMBED_H

#include "omc/omc_arena.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_xmp_dump.h"

OMC_EXTERN_C_BEGIN

typedef struct omc_xmp_embed_opts {
    omc_scan_fmt format;
    omc_xmp_sidecar_req packet;
} omc_xmp_embed_opts;

OMC_API void
omc_xmp_embed_opts_init(omc_xmp_embed_opts* opts);

OMC_API omc_status
omc_xmp_embed_payload(const omc_store* store, omc_u8* out, omc_size out_cap,
                      const omc_xmp_embed_opts* opts,
                      omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_embed_payload_arena(const omc_store* store, omc_arena* out,
                            const omc_xmp_embed_opts* opts,
                            omc_xmp_dump_res* out_res);

OMC_EXTERN_C_END

#endif
