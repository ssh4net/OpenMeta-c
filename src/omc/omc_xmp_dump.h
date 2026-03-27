#ifndef OMC_XMP_DUMP_H
#define OMC_XMP_DUMP_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_xmp_dump_status {
    OMC_XMP_DUMP_OK = 0,
    OMC_XMP_DUMP_TRUNCATED = 1,
    OMC_XMP_DUMP_LIMIT = 2
} omc_xmp_dump_status;

typedef struct omc_xmp_dump_limits {
    omc_u64 max_output_bytes;
    omc_u32 max_entries;
} omc_xmp_dump_limits;

typedef struct omc_xmp_sidecar_opts {
    omc_xmp_dump_limits limits;
    int include_existing_xmp;
    int include_exif;
    int include_iptc;
} omc_xmp_sidecar_opts;

typedef struct omc_xmp_dump_res {
    omc_xmp_dump_status status;
    omc_u64 written;
    omc_u64 needed;
    omc_u32 entries;
} omc_xmp_dump_res;

OMC_API void
omc_xmp_sidecar_opts_init(omc_xmp_sidecar_opts* opts);

OMC_API omc_status
omc_xmp_dump_sidecar(const omc_store* store, omc_u8* out, omc_size out_cap,
                     const omc_xmp_sidecar_opts* opts,
                     omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_dump_sidecar_arena(const omc_store* store, omc_arena* out,
                           const omc_xmp_sidecar_opts* opts,
                           omc_xmp_dump_res* out_res);

OMC_EXTERN_C_END

#endif
