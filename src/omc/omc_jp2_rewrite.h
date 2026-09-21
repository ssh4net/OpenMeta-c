#ifndef OMC_JP2_REWRITE_H
#define OMC_JP2_REWRITE_H

#include "omc/omc_arena.h"
#include "omc/omc_status.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_jp2_rewrite_status {
    OMC_JP2_REWRITE_OK = 0,
    OMC_JP2_REWRITE_LIMIT = 1,
    OMC_JP2_REWRITE_UNSUPPORTED = 2,
    OMC_JP2_REWRITE_MALFORMED = 3
} omc_jp2_rewrite_status;

typedef struct omc_jp2_rewrite_opts {
    int replace_exif;
    int replace_xmp;
} omc_jp2_rewrite_opts;

typedef struct omc_jp2_rewrite_res {
    omc_jp2_rewrite_status status;
    omc_u32 removed_exif;
    omc_u32 removed_xmp;
    omc_u32 inserted_exif;
    omc_u32 inserted_xmp;
    omc_u64 needed;
    omc_u64 written;
} omc_jp2_rewrite_res;

OMC_API void omc_jp2_rewrite_opts_init(omc_jp2_rewrite_opts *opts);

/* Rewrite top-level JP2-family metadata carriers. The input and output are
 * distinct. Selected literal Exif/xml boxes and their standard UUID carriers
 * are removed, replacement literal boxes are inserted before a preserved
 * terminal size-zero box, and all other boxes are copied byte-for-byte. */
OMC_API omc_status omc_jp2_rewrite(
    const omc_u8 *input, omc_size input_size,
    const omc_u8 *exif_payload, omc_size exif_size,
    const omc_u8 *xmp_payload, omc_size xmp_size,
    const omc_jp2_rewrite_opts *opts, omc_arena *out,
    omc_jp2_rewrite_res *result);

OMC_EXTERN_C_END

#endif
