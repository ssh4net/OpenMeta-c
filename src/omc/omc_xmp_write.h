#ifndef OMC_XMP_WRITE_H
#define OMC_XMP_WRITE_H

#include "omc/omc_arena.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_xmp_embed.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_xmp_write_status {
    OMC_XMP_WRITE_OK = 0,
    OMC_XMP_WRITE_LIMIT = 1,
    OMC_XMP_WRITE_UNSUPPORTED = 2,
    OMC_XMP_WRITE_MALFORMED = 3
} omc_xmp_write_status;

typedef struct omc_xmp_write_opts {
    omc_scan_fmt format;
    int write_embedded_xmp;
    int strip_existing_xmp;
    omc_xmp_embed_opts embed;
} omc_xmp_write_opts;

typedef struct omc_xmp_write_res {
    omc_xmp_write_status status;
    omc_scan_fmt format;
    omc_u64 written;
    omc_u64 needed;
    omc_u32 removed_xmp_blocks;
    omc_u32 inserted_xmp_blocks;
    omc_xmp_dump_res payload;
} omc_xmp_write_res;

OMC_API void
omc_xmp_write_opts_init(omc_xmp_write_opts* opts);

OMC_API omc_status
omc_xmp_write_embedded(const omc_u8* file_bytes, omc_size file_size,
                       const omc_store* store, omc_arena* out,
                       const omc_xmp_write_opts* opts,
                       omc_xmp_write_res* out_res);

OMC_EXTERN_C_END

#endif
