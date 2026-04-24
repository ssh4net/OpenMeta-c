#ifndef OMC_PREVIEW_H
#define OMC_PREVIEW_H

#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_preview_kind {
    OMC_PREVIEW_EXIF_JPEG_INTERCHANGE = 0,
    OMC_PREVIEW_EXIF_JPG_FROM_RAW = 1,
    OMC_PREVIEW_EXIF_JPG_FROM_RAW2 = 2,
    OMC_PREVIEW_CR3_PRVW_JPEG = 3
} omc_preview_kind;

typedef struct omc_preview_candidate {
    omc_preview_kind kind;
    omc_scan_fmt format;
    omc_u32 block_index;
    omc_u16 offset_tag;
    omc_u16 length_tag;
    omc_u64 file_offset;
    omc_u64 size;
    int has_jpeg_soi_signature;
} omc_preview_candidate;

typedef enum omc_preview_scan_status {
    OMC_PREVIEW_SCAN_OK = 0,
    OMC_PREVIEW_SCAN_TRUNCATED = 1,
    OMC_PREVIEW_SCAN_UNSUPPORTED = 2,
    OMC_PREVIEW_SCAN_MALFORMED = 3,
    OMC_PREVIEW_SCAN_LIMIT = 4
} omc_preview_scan_status;

typedef struct omc_preview_scan_limits {
    omc_u32 max_ifds;
    omc_u32 max_total_entries;
    omc_u64 max_preview_bytes;
} omc_preview_scan_limits;

typedef struct omc_preview_scan_opts {
    int include_exif_jpeg_interchange;
    int include_jpg_from_raw;
    int include_cr3_prvw_jpeg;
    int require_jpeg_soi;
    omc_preview_scan_limits limits;
} omc_preview_scan_opts;

typedef struct omc_preview_scan_res {
    omc_preview_scan_status status;
    omc_u32 written;
    omc_u32 needed;
} omc_preview_scan_res;

typedef enum omc_preview_extract_status {
    OMC_PREVIEW_EXTRACT_OK = 0,
    OMC_PREVIEW_EXTRACT_TRUNCATED = 1,
    OMC_PREVIEW_EXTRACT_MALFORMED = 2,
    OMC_PREVIEW_EXTRACT_LIMIT = 3
} omc_preview_extract_status;

typedef struct omc_preview_extract_opts {
    omc_u64 max_output_bytes;
    int require_jpeg_soi;
} omc_preview_extract_opts;

typedef struct omc_preview_extract_res {
    omc_preview_extract_status status;
    omc_u64 written;
    omc_u64 needed;
} omc_preview_extract_res;

OMC_API void
omc_preview_scan_opts_init(omc_preview_scan_opts* opts);

OMC_API omc_preview_scan_res
omc_preview_find_candidates(const omc_u8* file_bytes, omc_size file_size,
                            const omc_blk_ref* blocks, omc_u32 block_count,
                            omc_preview_candidate* out_candidates,
                            omc_u32 out_cap,
                            const omc_preview_scan_opts* opts);

OMC_API omc_preview_scan_res
omc_preview_scan_candidates(const omc_u8* file_bytes, omc_size file_size,
                            omc_blk_ref* blocks_scratch, omc_u32 block_cap,
                            omc_preview_candidate* out_candidates,
                            omc_u32 out_cap,
                            const omc_preview_scan_opts* opts);

OMC_API void
omc_preview_extract_opts_init(omc_preview_extract_opts* opts);

OMC_API omc_preview_extract_res
omc_preview_extract_candidate(const omc_u8* file_bytes, omc_size file_size,
                              const omc_preview_candidate* candidate,
                              omc_u8* out_bytes, omc_size out_cap,
                              const omc_preview_extract_opts* opts);

OMC_EXTERN_C_END

#endif
