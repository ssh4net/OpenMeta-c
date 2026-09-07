#ifndef OMC_EXIF_TIFF_SERIALIZE_H
#define OMC_EXIF_TIFF_SERIALIZE_H
#include "omc/omc_store.h"
OMC_EXTERN_C_BEGIN

#define OMC_EXIF_TIFF_SERIALIZE_CONTRACT_VERSION 1U

typedef enum omc_exif_tiff_status {
    OMC_EXIF_TIFF_OK = 0,
    OMC_EXIF_TIFF_OUTPUT_TRUNCATED,
    OMC_EXIF_TIFF_INVALID_OPTIONS,
    OMC_EXIF_TIFF_INVALID_METADATA,
    OMC_EXIF_TIFF_NO_EXIF_DATA,
    OMC_EXIF_TIFF_LIMIT,
    OMC_EXIF_TIFF_NO_MEMORY
} omc_exif_tiff_status;

typedef struct omc_exif_tiff_opts {
    int include_subifds;
    int inject_minimal_dng_version;
    int validate;
    int honor_wire_type_hints;
    int preserve_opaque_makernote;
    omc_u64 max_output_bytes;
} omc_exif_tiff_opts;

typedef struct omc_exif_tiff_res {
    omc_exif_tiff_status status;
    omc_u64 written;
    omc_u64 needed;
    omc_u32 entries_serialized;
    omc_u32 entries_skipped;
} omc_exif_tiff_res;

OMC_API void omc_exif_tiff_opts_init(omc_exif_tiff_opts *opts);

/* Target-neutral little-endian classic TIFF, starting with II/42.
 * Empty output measures; short output receives the deterministic prefix.
 * Other failures leave output bytes unchanged. Input/output must not overlap.
 * The caller keeps the store immutable for the duration of the call. */
OMC_API omc_exif_tiff_res omc_serialize_exif_tiff(const omc_store *store,
                                                  omc_mut_bytes output,
                                                  const omc_exif_tiff_opts *opts);

OMC_EXTERN_C_END
#endif
