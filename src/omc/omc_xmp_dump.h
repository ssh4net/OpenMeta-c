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

typedef enum omc_xmp_conflict_policy {
    OMC_XMP_CONFLICT_CURRENT_BEHAVIOR = 0,
    OMC_XMP_CONFLICT_EXISTING_WINS = 1,
    OMC_XMP_CONFLICT_GENERATED_WINS = 2
} omc_xmp_conflict_policy;

typedef struct omc_xmp_sidecar_opts {
    omc_xmp_dump_limits limits;
    int include_existing_xmp;
    int include_exif;
    int include_iptc;
    omc_xmp_conflict_policy conflict_policy;
    int exiftool_gpsdatetime_alias;
} omc_xmp_sidecar_opts;

typedef struct omc_xmp_dump_res {
    omc_xmp_dump_status status;
    omc_u64 written;
    omc_u64 needed;
    omc_u32 entries;
} omc_xmp_dump_res;

typedef omc_xmp_sidecar_opts omc_xmp_portable_opts;

typedef struct omc_xmp_lossless_opts {
    omc_xmp_dump_limits limits;
    int include_origin;
    int include_wire;
    int include_flags;
    int include_names;
} omc_xmp_lossless_opts;

typedef enum omc_xmp_sidecar_format {
    OMC_XMP_SIDECAR_LOSSLESS = 0,
    OMC_XMP_SIDECAR_PORTABLE = 1
} omc_xmp_sidecar_format;

typedef struct omc_xmp_sidecar_cfg {
    omc_xmp_sidecar_format format;
    omc_xmp_lossless_opts lossless;
    omc_xmp_portable_opts portable;
    omc_u64 initial_output_bytes;
} omc_xmp_sidecar_cfg;

typedef struct omc_xmp_sidecar_req {
    omc_xmp_sidecar_format format;
    omc_xmp_dump_limits limits;
    int include_exif;
    int include_iptc;
    int include_existing_xmp;
    omc_xmp_conflict_policy portable_conflict_policy;
    int portable_exiftool_gpsdatetime_alias;
    int include_origin;
    int include_wire;
    int include_flags;
    int include_names;
    omc_u64 initial_output_bytes;
} omc_xmp_sidecar_req;

OMC_API void
omc_xmp_sidecar_opts_init(omc_xmp_sidecar_opts* opts);

OMC_API void
omc_xmp_portable_opts_init(omc_xmp_portable_opts* opts);

OMC_API void
omc_xmp_lossless_opts_init(omc_xmp_lossless_opts* opts);

OMC_API void
omc_xmp_sidecar_cfg_init(omc_xmp_sidecar_cfg* cfg);

OMC_API void
omc_xmp_sidecar_req_init(omc_xmp_sidecar_req* req);

OMC_API void
omc_xmp_sidecar_cfg_from_req(omc_xmp_sidecar_cfg* out_cfg,
                             const omc_xmp_sidecar_req* req);

OMC_API omc_status
omc_xmp_dump_portable(const omc_store* store, omc_u8* out, omc_size out_cap,
                      const omc_xmp_portable_opts* opts,
                      omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_dump_portable_arena(const omc_store* store, omc_arena* out,
                            const omc_xmp_portable_opts* opts,
                            omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_dump_lossless(const omc_store* store, omc_u8* out, omc_size out_cap,
                      const omc_xmp_lossless_opts* opts,
                      omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_dump_sidecar(const omc_store* store, omc_u8* out, omc_size out_cap,
                     const omc_xmp_sidecar_opts* opts,
                     omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_dump_sidecar_arena(const omc_store* store, omc_arena* out,
                           const omc_xmp_sidecar_opts* opts,
                           omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_dump_sidecar_cfg(const omc_store* store, omc_arena* out,
                         const omc_xmp_sidecar_cfg* cfg,
                         omc_xmp_dump_res* out_res);

OMC_API omc_status
omc_xmp_dump_sidecar_req(const omc_store* store, omc_arena* out,
                         const omc_xmp_sidecar_req* req,
                         omc_xmp_dump_res* out_res);

OMC_EXTERN_C_END

#endif
