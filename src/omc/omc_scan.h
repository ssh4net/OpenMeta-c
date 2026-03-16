#ifndef OMC_SCAN_H
#define OMC_SCAN_H

#include "omc/omc_base.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_scan_status {
    OMC_SCAN_OK = 0,
    OMC_SCAN_TRUNCATED = 1,
    OMC_SCAN_UNSUPPORTED = 2,
    OMC_SCAN_MALFORMED = 3
} omc_scan_status;

typedef enum omc_scan_fmt {
    OMC_SCAN_FMT_UNKNOWN = 0,
    OMC_SCAN_FMT_JPEG = 1,
    OMC_SCAN_FMT_PNG = 2,
    OMC_SCAN_FMT_WEBP = 3,
    OMC_SCAN_FMT_GIF = 4,
    OMC_SCAN_FMT_TIFF = 5,
    OMC_SCAN_FMT_JP2 = 6,
    OMC_SCAN_FMT_JXL = 7,
    OMC_SCAN_FMT_HEIF = 8,
    OMC_SCAN_FMT_AVIF = 9,
    OMC_SCAN_FMT_CR3 = 10,
    OMC_SCAN_FMT_CRW = 11,
    OMC_SCAN_FMT_RAF = 12,
    OMC_SCAN_FMT_X3F = 13
} omc_scan_fmt;

typedef enum omc_blk_kind {
    OMC_BLK_UNKNOWN = 0,
    OMC_BLK_EXIF = 1,
    OMC_BLK_CIFF = 2,
    OMC_BLK_MAKERNOTE = 3,
    OMC_BLK_XMP = 4,
    OMC_BLK_XMP_EXT = 5,
    OMC_BLK_JUMBF = 6,
    OMC_BLK_ICC = 7,
    OMC_BLK_IPTC_IIM = 8,
    OMC_BLK_PS_IRB = 9,
    OMC_BLK_MPF = 10,
    OMC_BLK_COMMENT = 11,
    OMC_BLK_TEXT = 12,
    OMC_BLK_COMP_METADATA = 13
} omc_blk_kind;

typedef enum omc_blk_comp {
    OMC_BLK_COMP_NONE = 0,
    OMC_BLK_COMP_DEFLATE = 1,
    OMC_BLK_COMP_BROTLI = 2
} omc_blk_comp;

typedef enum omc_blk_chunk {
    OMC_BLK_CHUNK_NONE = 0,
    OMC_BLK_CHUNK_JPEG_APP2_SEQ = 1,
    OMC_BLK_CHUNK_JPEG_APP11_SEQ = 2,
    OMC_BLK_CHUNK_JPEG_XMP_EXT = 3,
    OMC_BLK_CHUNK_GIF_SUB = 4,
    OMC_BLK_CHUNK_BMFF_EXIF_OFF = 5,
    OMC_BLK_CHUNK_BROB_REALTYPE = 6,
    OMC_BLK_CHUNK_JP2_UUID = 7,
    OMC_BLK_CHUNK_PS_IRB_8BIM = 8
} omc_blk_chunk;

typedef struct omc_blk_ref {
    omc_scan_fmt format;
    omc_blk_kind kind;
    omc_blk_comp compression;
    omc_blk_chunk chunking;
    omc_u64 outer_offset;
    omc_u64 outer_size;
    omc_u64 data_offset;
    omc_u64 data_size;
    omc_u32 id;
    omc_u32 part_index;
    omc_u32 part_count;
    omc_u64 logical_offset;
    omc_u64 logical_size;
    omc_u64 group;
    omc_u32 aux_u32;
} omc_blk_ref;

typedef struct omc_scan_res {
    omc_scan_status status;
    omc_u32 written;
    omc_u32 needed;
} omc_scan_res;

#define OMC_FOURCC(a, b, c, d) \
    ((((omc_u32)((omc_u8)(a))) << 24) \
     | (((omc_u32)((omc_u8)(b))) << 16) \
     | (((omc_u32)((omc_u8)(c))) << 8) \
     | (((omc_u32)((omc_u8)(d))) << 0))

OMC_API omc_scan_res
omc_scan_auto(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_auto(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_jpeg(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_jpeg(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_tiff(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_tiff(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_crw(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_crw(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_raf(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_raf(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_x3f(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_x3f(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_jp2(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_jp2(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_jxl(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_jxl(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_png(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_png(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_webp(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_webp(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_gif(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_gif(const omc_u8* bytes, omc_size size);

OMC_API omc_scan_res
omc_scan_bmff(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap);

OMC_API omc_scan_res
omc_scan_meas_bmff(const omc_u8* bytes, omc_size size);

OMC_EXTERN_C_END

#endif
