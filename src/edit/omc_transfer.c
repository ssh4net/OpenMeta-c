#include "omc/omc_transfer.h"
#include "omc/omc_transfer_artifact.h"
#include "omc/omc_transfer_package.h"
#include "omc/omc_transfer_payload.h"
#include "omc/omc_cfg.h"
#include "omc/omc_jxl_encoder_handoff.h"
#include "omc/omc_xmp_embed.h"
#include "omc_exif_write.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if OMC_HAVE_ZLIB
#include <zlib.h>
#endif

static const omc_u8 k_omc_transfer_png_sig[8] = {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
};
static const omc_u8 k_omc_transfer_jp2_sig[12] = {
    0x00U, 0x00U, 0x00U, 0x0CU, 0x6AU, 0x50U, 0x20U, 0x20U,
    0x0DU, 0x0AU, 0x87U, 0x0AU
};
static const omc_u8 k_omc_transfer_jxl_sig[12] = {
    0x00U, 0x00U, 0x00U, 0x0CU, 0x4AU, 0x58U, 0x4CU, 0x20U,
    0x0DU, 0x0AU, 0x87U, 0x0AU
};
static const omc_u8 k_omc_transfer_jxl_encoder_handoff_magic[8] = {
    (omc_u8)'O', (omc_u8)'M', (omc_u8)'J', (omc_u8)'X',
    (omc_u8)'I', (omc_u8)'C', (omc_u8)'C', (omc_u8)'1'
};
static const omc_u8 k_omc_transfer_payload_batch_magic[8] = {
    (omc_u8)'O', (omc_u8)'M', (omc_u8)'T', (omc_u8)'P',
    (omc_u8)'L', (omc_u8)'D', (omc_u8)'0', (omc_u8)'1'
};
static const omc_u8 k_omc_transfer_package_batch_magic[8] = {
    (omc_u8)'O', (omc_u8)'M', (omc_u8)'T', (omc_u8)'P',
    (omc_u8)'K', (omc_u8)'G', (omc_u8)'0', (omc_u8)'1'
};
static const omc_u8 k_omc_transfer_jpeg_xmp_prefix[]
    = "http://ns.adobe.com/xap/1.0/\0";
static const omc_u8 k_omc_transfer_jpeg_photoshop_prefix[]
    = "Photoshop 3.0\0";
static const omc_u8 k_omc_transfer_webp_vp8x_icc_bit = 0x20U;

static omc_status
omc_transfer_append_jp2_colr_icc_box(omc_arena* out, const omc_u8* profile,
                                     omc_size profile_size);

static omc_status
omc_transfer_build_png_iccp_payload(const omc_u8* profile,
                                    omc_size profile_size,
                                    omc_arena* payload_out,
                                    omc_transfer_status* out_status);

static omc_status
omc_transfer_build_iptc_iim(const omc_store* store, omc_arena* out,
                            int* out_has_iptc, int* out_supported);

static omc_status
omc_transfer_build_photoshop_iptc_irb(const omc_u8* iim_bytes,
                                      omc_size iim_size, omc_arena* out);

static omc_status
omc_transfer_append_jpeg_photoshop_segment(omc_arena* out,
                                           const omc_u8* payload,
                                           omc_size payload_size);

static omc_status
omc_transfer_append_png_chunk(omc_arena* out, const char* type,
                              const omc_u8* payload, omc_size payload_size);

static omc_status
omc_transfer_append_webp_chunk(omc_arena* out, const char* type,
                               const omc_u8* payload, omc_size payload_size);

static omc_status
omc_transfer_append_jp2_box(omc_arena* out, omc_u32 type,
                            const omc_u8* payload, omc_size payload_size);

static omc_status
omc_transfer_package_build_chunk_bytes(
    const omc_transfer_payload* payload,
    omc_transfer_package_chunk_kind* out_kind, omc_arena* out);

static omc_status
omc_transfer_append_u16be_arena(omc_arena* out, omc_u16 value);

static omc_status
omc_transfer_append_u32be_arena(omc_arena* out, omc_u32 value);

static omc_status
omc_transfer_append_u64be_arena(omc_arena* out, omc_u64 value);

static void
omc_transfer_store_u32be(omc_u8* dst, omc_u32 value);

static void
omc_transfer_bundle_init(omc_transfer_bundle* bundle)
{
    if (bundle == (omc_transfer_bundle*)0) {
        return;
    }
    memset(bundle, 0, sizeof(*bundle));
    bundle->status = OMC_TRANSFER_UNSUPPORTED;
    bundle->format = OMC_SCAN_FMT_UNKNOWN;
    bundle->dng_target_mode = OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD;
    bundle->writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    bundle->destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;
    bundle->embedded_action = OMC_TRANSFER_EMBEDDED_NONE;
    bundle->existing_sidecar_xmp_mode =
        OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT;
    bundle->existing_sidecar_xmp_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING;
    bundle->existing_embedded_xmp_mode =
        OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT;
    bundle->existing_embedded_xmp_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING;
    bundle->existing_xmp_carrier_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_SIDECAR;
}

static void
omc_transfer_exec_init(omc_transfer_exec* exec)
{
    if (exec == (omc_transfer_exec*)0) {
        return;
    }
    memset(exec, 0, sizeof(*exec));
    exec->status = OMC_TRANSFER_UNSUPPORTED;
    exec->format = OMC_SCAN_FMT_UNKNOWN;
    exec->dng_target_mode = OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD;
    exec->writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    exec->existing_sidecar_xmp_mode =
        OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT;
    exec->existing_sidecar_xmp_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING;
    exec->existing_embedded_xmp_mode =
        OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT;
    exec->existing_embedded_xmp_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING;
    exec->existing_xmp_carrier_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_SIDECAR;
    omc_xmp_write_opts_init(&exec->embedded_write);
    omc_xmp_sidecar_req_init(&exec->sidecar);
}

static void
omc_transfer_res_init(omc_transfer_res* res)
{
    if (res == (omc_transfer_res*)0) {
        return;
    }
    memset(res, 0, sizeof(*res));
    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->format = OMC_SCAN_FMT_UNKNOWN;
    res->dng_target_mode = OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD;
    res->writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    res->embedded.status = OMC_XMP_WRITE_UNSUPPORTED;
    res->sidecar.status = OMC_XMP_DUMP_LIMIT;
}

static omc_u32
omc_transfer_read_u32le(const omc_u8* src)
{
    return ((omc_u32)src[3] << 24) | ((omc_u32)src[2] << 16)
           | ((omc_u32)src[1] << 8) | (omc_u32)src[0];
}

static omc_u16
omc_transfer_read_u16le(const omc_u8* src)
{
    return (omc_u16)(((omc_u16)src[1] << 8) | (omc_u16)src[0]);
}

static omc_u16
omc_transfer_read_u16be(const omc_u8* src)
{
    return (omc_u16)(((omc_u16)src[0] << 8) | (omc_u16)src[1]);
}

static omc_u32
omc_transfer_read_u32be(const omc_u8* src)
{
    return ((omc_u32)src[0] << 24) | ((omc_u32)src[1] << 16)
           | ((omc_u32)src[2] << 8) | (omc_u32)src[3];
}

static omc_u64
omc_transfer_read_u64le(const omc_u8* src)
{
    return ((omc_u64)src[7] << 56) | ((omc_u64)src[6] << 48)
           | ((omc_u64)src[5] << 40) | ((omc_u64)src[4] << 32)
           | ((omc_u64)src[3] << 24) | ((omc_u64)src[2] << 16)
           | ((omc_u64)src[1] << 8) | (omc_u64)src[0];
}

static omc_u64
omc_transfer_read_u64be(const omc_u8* src)
{
    return ((omc_u64)src[0] << 56) | ((omc_u64)src[1] << 48)
           | ((omc_u64)src[2] << 40) | ((omc_u64)src[3] << 32)
           | ((omc_u64)src[4] << 24) | ((omc_u64)src[5] << 16)
           | ((omc_u64)src[6] << 8) | (omc_u64)src[7];
}

static omc_status
omc_transfer_build_minimal_dng_scaffold(omc_arena* out)
{
    static const omc_u8 k_minimal_dng[] = {
        'I', 'I', 42U, 0U, 8U, 0U, 0U, 0U, 1U, 0U,
        0x12U, 0xC6U, 1U, 0U, 4U, 0U, 0U, 0U, 1U, 6U, 0U, 0U,
        0U, 0U, 0U, 0U
    };
    omc_byte_ref ref;

    if (out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_arena_reset(out);
    return omc_arena_append(out, k_minimal_dng, sizeof(k_minimal_dng), &ref);
}

static omc_u32
omc_transfer_fourcc(char a, char b, char c, char d)
{
    return ((omc_u32)(omc_u8)a << 24) | ((omc_u32)(omc_u8)b << 16)
           | ((omc_u32)(omc_u8)c << 8) | (omc_u32)(omc_u8)d;
}

static int
omc_transfer_tiff_has_dng_version(const omc_u8* bytes, omc_size size)
{
    int little_endian;
    int big_tiff;
    omc_u64 ifd_off;
    omc_u64 count_off;
    omc_u64 count;
    omc_u64 max_count;
    omc_u64 entry_size;
    omc_u64 i;

    if (bytes == (const omc_u8*)0 || size < 8U) {
        return 0;
    }
    if (bytes[0] == 'I' && bytes[1] == 'I') {
        little_endian = 1;
    } else if (bytes[0] == 'M' && bytes[1] == 'M') {
        little_endian = 0;
    } else {
        return 0;
    }

    if ((little_endian ? omc_transfer_read_u16le(bytes + 2U)
                       : omc_transfer_read_u16be(bytes + 2U))
        == 42U) {
        big_tiff = 0;
        ifd_off = (omc_u64)(little_endian ? omc_transfer_read_u32le(bytes + 4U)
                                          : omc_transfer_read_u32be(bytes + 4U));
    } else if ((little_endian ? omc_transfer_read_u16le(bytes + 2U)
                              : omc_transfer_read_u16be(bytes + 2U))
               == 43U) {
        if (size < 16U) {
            return 0;
        }
        if ((little_endian ? omc_transfer_read_u16le(bytes + 4U)
                           : omc_transfer_read_u16be(bytes + 4U))
                != 8U
            || (little_endian ? omc_transfer_read_u16le(bytes + 6U)
                              : omc_transfer_read_u16be(bytes + 6U))
                   != 0U) {
            return 0;
        }
        big_tiff = 1;
        ifd_off = little_endian ? omc_transfer_read_u64le(bytes + 8U)
                                : omc_transfer_read_u64be(bytes + 8U);
    } else {
        return 0;
    }

    if (ifd_off >= (omc_u64)size) {
        return 0;
    }

    if (!big_tiff) {
        if (ifd_off + 2U > (omc_u64)size) {
            return 0;
        }
        count = (omc_u64)(little_endian
                               ? omc_transfer_read_u16le(bytes + (omc_size)ifd_off)
                               : omc_transfer_read_u16be(bytes + (omc_size)ifd_off));
        count_off = ifd_off + 2U;
        entry_size = 12U;
    } else {
        if (ifd_off + 8U > (omc_u64)size) {
            return 0;
        }
        count = little_endian ? omc_transfer_read_u64le(bytes + (omc_size)ifd_off)
                              : omc_transfer_read_u64be(bytes + (omc_size)ifd_off);
        count_off = ifd_off + 8U;
        entry_size = 20U;
    }

    if (count_off > (omc_u64)size) {
        return 0;
    }
    max_count = ((omc_u64)size - count_off) / entry_size;
    if (count > max_count) {
        count = max_count;
    }

    for (i = 0U; i < count; ++i) {
        omc_u64 entry_off;
        omc_u16 tag;

        entry_off = count_off + i * entry_size;
        tag = little_endian
                  ? omc_transfer_read_u16le(bytes + (omc_size)entry_off)
                  : omc_transfer_read_u16be(bytes + (omc_size)entry_off);
        if (tag == 0xC612U) {
            return 1;
        }
    }
    return 0;
}

static int
omc_transfer_dng_target_requires_existing_target(omc_dng_target_mode mode)
{
    return mode == OMC_DNG_TARGET_EXISTING
           || mode == OMC_DNG_TARGET_TEMPLATE;
}

typedef struct omc_transfer_bmff_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
} omc_transfer_bmff_box;

static int
omc_transfer_parse_bmff_box(const omc_u8* bytes, omc_size size,
                            omc_u64 offset, omc_u64 limit,
                            omc_transfer_bmff_box* out_box)
{
    omc_u32 size32;
    omc_u64 box_size;
    omc_u64 header_size;

    if (out_box == (omc_transfer_bmff_box*)0) {
        return 0;
    }
    memset(out_box, 0, sizeof(*out_box));
    if (offset > limit || limit > (omc_u64)size || offset + 8U > limit) {
        return 0;
    }

    size32 = omc_transfer_read_u32be(bytes + (omc_size)offset);
    out_box->type = omc_transfer_read_u32be(bytes + (omc_size)offset + 4U);
    header_size = 8U;
    if (size32 == 1U) {
        if (offset + 16U > limit) {
            return 0;
        }
        box_size = omc_transfer_read_u64be(bytes + (omc_size)offset + 8U);
        header_size = 16U;
    } else if (size32 == 0U) {
        box_size = limit - offset;
    } else {
        box_size = (omc_u64)size32;
    }

    if (box_size < header_size || offset + box_size > limit) {
        return 0;
    }

    out_box->offset = offset;
    out_box->size = box_size;
    out_box->header_size = header_size;
    return 1;
}

static omc_scan_fmt
omc_transfer_bmff_format_from_ftyp(const omc_u8* bytes, omc_size size,
                                   const omc_transfer_bmff_box* ftyp)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u64 off;
    omc_u32 brand;
    int is_heif;
    int is_avif;
    int is_cr3;

    if (ftyp == (const omc_transfer_bmff_box*)0 || ftyp->size < 16U) {
        return OMC_SCAN_FMT_UNKNOWN;
    }

    payload_off = ftyp->offset + ftyp->header_size;
    payload_end = ftyp->offset + ftyp->size;
    if (payload_off + 8U > payload_end || payload_end > (omc_u64)size) {
        return OMC_SCAN_FMT_UNKNOWN;
    }

    is_heif = 0;
    is_avif = 0;
    is_cr3 = 0;

#define OMC_TRANSFER_NOTE_BMFF_BRAND(v) \
    do { \
        if ((v) == omc_transfer_fourcc('c', 'r', 'x', ' ') \
            || (v) == omc_transfer_fourcc('C', 'R', '3', ' ')) { \
            is_cr3 = 1; \
        } \
        if ((v) == omc_transfer_fourcc('a', 'v', 'i', 'f') \
            || (v) == omc_transfer_fourcc('a', 'v', 'i', 's')) { \
            is_avif = 1; \
        } \
        if ((v) == omc_transfer_fourcc('m', 'i', 'f', '1') \
            || (v) == omc_transfer_fourcc('m', 's', 'f', '1') \
            || (v) == omc_transfer_fourcc('h', 'e', 'i', 'c') \
            || (v) == omc_transfer_fourcc('h', 'e', 'i', 'x') \
            || (v) == omc_transfer_fourcc('h', 'e', 'v', 'c') \
            || (v) == omc_transfer_fourcc('h', 'e', 'v', 'x')) { \
            is_heif = 1; \
        } \
    } while (0)

    brand = omc_transfer_read_u32be(bytes + (omc_size)payload_off);
    OMC_TRANSFER_NOTE_BMFF_BRAND(brand);
    off = payload_off + 8U;
    while (off + 4U <= payload_end) {
        brand = omc_transfer_read_u32be(bytes + (omc_size)off);
        OMC_TRANSFER_NOTE_BMFF_BRAND(brand);
        off += 4U;
    }

#undef OMC_TRANSFER_NOTE_BMFF_BRAND

    if (is_cr3) {
        return OMC_SCAN_FMT_CR3;
    }
    if (is_avif) {
        return OMC_SCAN_FMT_AVIF;
    }
    if (is_heif) {
        return OMC_SCAN_FMT_HEIF;
    }
    return OMC_SCAN_FMT_UNKNOWN;
}

static omc_scan_fmt
omc_transfer_detect_format(const omc_u8* file_bytes, omc_size file_size)
{
    omc_u64 off;
    omc_u64 limit;

    if (file_bytes == (const omc_u8*)0 || file_size == 0U) {
        return OMC_SCAN_FMT_UNKNOWN;
    }
    if (file_size >= 2U && file_bytes[0] == 0xFFU && file_bytes[1] == 0xD8U) {
        return OMC_SCAN_FMT_JPEG;
    }
    if (file_size >= sizeof(k_omc_transfer_png_sig)
        && memcmp(file_bytes, k_omc_transfer_png_sig,
                  sizeof(k_omc_transfer_png_sig)) == 0) {
        return OMC_SCAN_FMT_PNG;
    }
    if (file_size >= 6U && memcmp(file_bytes, "GIF87a", 6U) == 0) {
        return OMC_SCAN_FMT_GIF;
    }
    if (file_size >= 6U && memcmp(file_bytes, "GIF89a", 6U) == 0) {
        return OMC_SCAN_FMT_GIF;
    }
    if (file_size >= 8U
        && ((file_bytes[0] == 'I' && file_bytes[1] == 'I')
            || (file_bytes[0] == 'M' && file_bytes[1] == 'M'))) {
        if (omc_transfer_tiff_has_dng_version(file_bytes, file_size)) {
            return OMC_SCAN_FMT_DNG;
        }
        return OMC_SCAN_FMT_TIFF;
    }
    if (file_size >= 12U && memcmp(file_bytes, "RIFF", 4U) == 0
        && memcmp(file_bytes + 8U, "WEBP", 4U) == 0) {
        return OMC_SCAN_FMT_WEBP;
    }
    if (file_size >= sizeof(k_omc_transfer_jp2_sig)
        && memcmp(file_bytes, k_omc_transfer_jp2_sig,
                  sizeof(k_omc_transfer_jp2_sig)) == 0) {
        return OMC_SCAN_FMT_JP2;
    }
    if (file_size >= sizeof(k_omc_transfer_jxl_sig)
        && memcmp(file_bytes, k_omc_transfer_jxl_sig,
                  sizeof(k_omc_transfer_jxl_sig)) == 0) {
        return OMC_SCAN_FMT_JXL;
    }

    off = 0U;
    limit = (omc_u64)file_size;
    while (off + 8U <= limit) {
        omc_transfer_bmff_box box;

        if (!omc_transfer_parse_bmff_box(file_bytes, file_size, off, limit,
                                         &box)) {
            break;
        }
        if (box.type == omc_transfer_fourcc('f', 't', 'y', 'p')) {
            return omc_transfer_bmff_format_from_ftyp(file_bytes, file_size,
                                                      &box);
        }
        if (box.size == 0U) {
            break;
        }
        off += box.size;
    }

    return OMC_SCAN_FMT_UNKNOWN;
}

static int
omc_transfer_embedded_supported(omc_scan_fmt format)
{
    return format == OMC_SCAN_FMT_JPEG || format == OMC_SCAN_FMT_PNG
           || format == OMC_SCAN_FMT_TIFF || format == OMC_SCAN_FMT_WEBP
           || format == OMC_SCAN_FMT_JP2 || format == OMC_SCAN_FMT_JXL
           || format == OMC_SCAN_FMT_HEIF || format == OMC_SCAN_FMT_AVIF
           || format == OMC_SCAN_FMT_CR3 || format == OMC_SCAN_FMT_DNG;
}

static omc_u32
omc_transfer_count_existing_xmp_blocks(const omc_u8* file_bytes,
                                       omc_size file_size)
{
    omc_blk_ref blocks[128];
    omc_scan_res scan_res;
    omc_u32 i;
    omc_u32 count;

    if (file_bytes == (const omc_u8*)0 || file_size == 0U) {
        return 0U;
    }

    scan_res = omc_scan_auto(file_bytes, file_size, blocks, 128U);
    if (scan_res.status == OMC_SCAN_MALFORMED) {
        return 0U;
    }

    count = 0U;
    for (i = 0U; i < scan_res.written; ++i) {
        if (blocks[i].kind == OMC_BLK_XMP || blocks[i].kind == OMC_BLK_XMP_EXT) {
            count += 1U;
        }
    }
    return count;
}

static omc_transfer_status
omc_transfer_status_from_write(omc_xmp_write_status status)
{
    if (status == OMC_XMP_WRITE_OK) {
        return OMC_TRANSFER_OK;
    }
    if (status == OMC_XMP_WRITE_LIMIT) {
        return OMC_TRANSFER_LIMIT;
    }
    if (status == OMC_XMP_WRITE_MALFORMED) {
        return OMC_TRANSFER_MALFORMED;
    }
    return OMC_TRANSFER_UNSUPPORTED;
}

static omc_transfer_status
omc_transfer_status_from_dump(omc_xmp_dump_status status)
{
    if (status == OMC_XMP_DUMP_OK) {
        return OMC_TRANSFER_OK;
    }
    return OMC_TRANSFER_LIMIT;
}

static omc_transfer_status
omc_transfer_status_from_exif(omc_exif_write_status status)
{
    if (status == OMC_EXIF_WRITE_OK) {
        return OMC_TRANSFER_OK;
    }
    if (status == OMC_EXIF_WRITE_LIMIT) {
        return OMC_TRANSFER_LIMIT;
    }
    if (status == OMC_EXIF_WRITE_MALFORMED) {
        return OMC_TRANSFER_MALFORMED;
    }
    return OMC_TRANSFER_UNSUPPORTED;
}

static omc_status
omc_transfer_append_bytes(omc_arena* out, const void* src, omc_size size)
{
    omc_byte_ref ref;

    return omc_arena_append(out, src, size, &ref);
}

typedef struct omc_transfer_icc_header_item {
    omc_u32 offset;
    omc_byte_ref bytes;
} omc_transfer_icc_header_item;

typedef struct omc_transfer_icc_tag_item {
    omc_u32 signature;
    omc_byte_ref bytes;
} omc_transfer_icc_tag_item;

typedef enum omc_transfer_jumbf_proj_node_kind {
    OMC_TRANSFER_JUMBF_PROJ_UNKNOWN = 0,
    OMC_TRANSFER_JUMBF_PROJ_LEAF = 1,
    OMC_TRANSFER_JUMBF_PROJ_MAP = 2,
    OMC_TRANSFER_JUMBF_PROJ_ARRAY = 3
} omc_transfer_jumbf_proj_node_kind;

typedef struct omc_transfer_jumbf_proj_child {
    omc_u32 node_index;
    int array_child;
    omc_u32 array_index;
    omc_const_bytes map_key;
} omc_transfer_jumbf_proj_child;

typedef struct omc_transfer_jumbf_proj_node {
    omc_transfer_jumbf_proj_node_kind kind;
    int has_tag;
    omc_u64 tag;
    const omc_entry* leaf;
    omc_transfer_jumbf_proj_child* children;
    omc_u32 child_count;
    omc_u32 child_capacity;
} omc_transfer_jumbf_proj_node;

typedef struct omc_transfer_jumbf_proj_tree {
    omc_transfer_jumbf_proj_node* nodes;
    omc_u32 node_count;
    omc_u32 node_capacity;
    omc_const_bytes root_prefix;
} omc_transfer_jumbf_proj_tree;

static int
omc_transfer_bytes_eq_literal(omc_const_bytes view, const char* literal)
{
    omc_size literal_size;

    if (view.data == (const omc_u8*)0 || literal == (const char*)0) {
        return 0;
    }
    literal_size = (omc_size)strlen(literal);
    return view.size == literal_size
           && memcmp(view.data, literal, literal_size) == 0;
}

static int
omc_transfer_bytes_starts_with_literal(omc_const_bytes view,
                                       const char* literal)
{
    omc_size literal_size;

    if (view.data == (const omc_u8*)0 || literal == (const char*)0) {
        return 0;
    }
    literal_size = (omc_size)strlen(literal);
    return view.size >= literal_size
           && memcmp(view.data, literal, literal_size) == 0;
}

static int
omc_transfer_decimal_text(omc_const_bytes view)
{
    omc_size i;

    if (view.data == (const omc_u8*)0 || view.size == 0U) {
        return 0;
    }
    for (i = 0U; i < view.size; ++i) {
        if (view.data[i] < (omc_u8)'0' || view.data[i] > (omc_u8)'9') {
            return 0;
        }
    }
    return 1;
}

static int
omc_transfer_parse_u32_decimal_view(omc_const_bytes view, omc_u32* out_value)
{
    omc_size i;
    omc_u32 value;

    if (out_value == (omc_u32*)0 || !omc_transfer_decimal_text(view)) {
        return 0;
    }

    value = 0U;
    for (i = 0U; i < view.size; ++i) {
        omc_u32 digit;

        digit = (omc_u32)(view.data[i] - (omc_u8)'0');
        if (value > (omc_u32)(~(omc_u32)0) / 10U
            || (value == (omc_u32)(~(omc_u32)0) / 10U
                && digit > (omc_u32)(~(omc_u32)0) % 10U)) {
            return 0;
        }
        value = value * 10U + digit;
    }
    *out_value = value;
    return 1;
}

static int
omc_transfer_text_is_ascii(omc_const_bytes view)
{
    omc_size i;

    if (view.data == (const omc_u8*)0) {
        return 0;
    }
    for (i = 0U; i < view.size; ++i) {
        if (view.data[i] > 0x7FU) {
            return 0;
        }
    }
    return 1;
}

static int
omc_transfer_jumbf_path_separator(omc_u8 ch)
{
    return ch == (omc_u8)'.' || ch == (omc_u8)'[' || ch == (omc_u8)']'
           || ch == (omc_u8)'@';
}

static int
omc_transfer_jumbf_key_has_segment(omc_const_bytes key, const char* segment)
{
    omc_size segment_size;
    omc_size i;

    if (key.data == (const omc_u8*)0 || segment == (const char*)0) {
        return 0;
    }
    segment_size = (omc_size)strlen(segment);
    if (segment_size == 0U || key.size < segment_size) {
        return 0;
    }

    for (i = 0U; i + segment_size <= key.size; ++i) {
        int left_ok;
        int right_ok;
        omc_size end;

        if (memcmp(key.data + i, segment, segment_size) != 0) {
            continue;
        }
        left_ok = i == 0U || omc_transfer_jumbf_path_separator(key.data[i - 1U]);
        end = i + segment_size;
        right_ok = end == key.size
                   || omc_transfer_jumbf_path_separator(key.data[end]);
        if (left_ok && right_ok) {
            return 1;
        }
    }
    return 0;
}

static int
omc_transfer_find_jumbf_cbor_root_prefix(omc_const_bytes key,
                                         omc_const_bytes* out_root,
                                         omc_const_bytes* out_suffix)
{
    omc_size pos;

    if (key.data == (const omc_u8*)0 || out_root == (omc_const_bytes*)0
        || out_suffix == (omc_const_bytes*)0) {
        return 0;
    }

    pos = 0U;
    while (pos + 5U <= key.size) {
        omc_size end;

        if (memcmp(key.data + pos, ".cbor", 5U) != 0) {
            pos += 1U;
            continue;
        }
        end = pos + 5U;
        if (end == key.size || key.data[end] == (omc_u8)'.'
            || key.data[end] == (omc_u8)'[' || key.data[end] == (omc_u8)'@') {
            out_root->data = key.data;
            out_root->size = end;
            out_suffix->data = key.data + end;
            out_suffix->size = key.size - end;
            return 1;
        }
        pos = end;
    }
    return 0;
}

static omc_status
omc_transfer_cbor_append_major_u64(omc_arena* out, omc_u8 major,
                                   omc_u64 value)
{
    omc_u8 head;
    omc_status status;

    if (out == (omc_arena*)0 || major > 7U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (value < 24U) {
        head = (omc_u8)(((omc_u32)major << 5) | (omc_u8)value);
        return omc_transfer_append_bytes(out, &head, 1U);
    }
    if (value <= 0xFFU) {
        head = (omc_u8)(((omc_u32)major << 5) | 24U);
        status = omc_transfer_append_bytes(out, &head, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        head = (omc_u8)value;
        return omc_transfer_append_bytes(out, &head, 1U);
    }
    if (value <= 0xFFFFU) {
        head = (omc_u8)(((omc_u32)major << 5) | 25U);
        status = omc_transfer_append_bytes(out, &head, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        return omc_transfer_append_u16be_arena(out, (omc_u16)value);
    }
    if (value <= (omc_u64)0xFFFFFFFFU) {
        head = (omc_u8)(((omc_u32)major << 5) | 26U);
        status = omc_transfer_append_bytes(out, &head, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        return omc_transfer_append_u32be_arena(out, (omc_u32)value);
    }
    head = (omc_u8)(((omc_u32)major << 5) | 27U);
    status = omc_transfer_append_bytes(out, &head, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_transfer_append_u64be_arena(out, value);
}

static omc_status
omc_transfer_cbor_append_text(omc_arena* out, omc_const_bytes text)
{
    omc_status status;

    if (out == (omc_arena*)0 || (text.data == (const omc_u8*)0 && text.size != 0U)) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    status = omc_transfer_cbor_append_major_u64(out, 3U, (omc_u64)text.size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (text.size == 0U) {
        return OMC_STATUS_OK;
    }
    return omc_transfer_append_bytes(out, text.data, text.size);
}

static omc_status
omc_transfer_cbor_append_bytes(omc_arena* out, omc_const_bytes bytes)
{
    omc_status status;

    if (out == (omc_arena*)0
        || (bytes.data == (const omc_u8*)0 && bytes.size != 0U)) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    status = omc_transfer_cbor_append_major_u64(out, 2U, (omc_u64)bytes.size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (bytes.size == 0U) {
        return OMC_STATUS_OK;
    }
    return omc_transfer_append_bytes(out, bytes.data, bytes.size);
}

static omc_status
omc_transfer_cbor_append_f32_bits(omc_arena* out, omc_u32 bits)
{
    omc_u8 head;
    omc_status status;

    if (out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    head = 0xFAU;
    status = omc_transfer_append_bytes(out, &head, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_transfer_append_u32be_arena(out, bits);
}

static omc_status
omc_transfer_cbor_append_f64_bits(omc_arena* out, omc_u64 bits)
{
    omc_u8 head;
    omc_status status;

    if (out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    head = 0xFBU;
    status = omc_transfer_append_bytes(out, &head, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_transfer_append_u64be_arena(out, bits);
}

static omc_status
omc_transfer_append_bmff_box_arena(omc_arena* out, omc_u32 type,
                                   const omc_u8* payload,
                                   omc_size payload_size)
{
    omc_status status;

    if (out == (omc_arena*)0
        || (payload == (const omc_u8*)0 && payload_size != 0U)) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if ((omc_u64)payload_size > (omc_u64)0xFFFFFFFFU - (omc_u64)8U) {
        return OMC_STATUS_OVERFLOW;
    }
    status = omc_transfer_append_u32be_arena(out, (omc_u32)(payload_size + 8U));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_u32be_arena(out, type);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size == 0U) {
        return OMC_STATUS_OK;
    }
    return omc_transfer_append_bytes(out, payload, payload_size);
}

static int
omc_transfer_meta_scalar_to_u64(const omc_val* value, omc_u64* out_value)
{
    if (value == (const omc_val*)0 || out_value == (omc_u64*)0
        || value->kind != OMC_VAL_SCALAR) {
        return 0;
    }

    switch (value->elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_U16:
    case OMC_ELEM_U32:
    case OMC_ELEM_U64:
        *out_value = value->u.u64;
        return 1;
    case OMC_ELEM_I8:
    case OMC_ELEM_I16:
    case OMC_ELEM_I32:
    case OMC_ELEM_I64:
        if (value->u.i64 < 0) {
            return 0;
        }
        *out_value = (omc_u64)value->u.i64;
        return 1;
    default: break;
    }
    return 0;
}

static int
omc_transfer_projected_text_supported(const omc_val* value,
                                      omc_const_bytes text)
{
    if (value == (const omc_val*)0 || value->kind != OMC_VAL_TEXT) {
        return 0;
    }
    if (value->text_encoding == OMC_TEXT_ASCII
        || value->text_encoding == OMC_TEXT_UTF8) {
        return 1;
    }
    if (value->text_encoding == OMC_TEXT_UNKNOWN) {
        return omc_transfer_text_is_ascii(text);
    }
    return 0;
}

static int
omc_transfer_projected_text_looks_simple(omc_const_bytes text)
{
    omc_const_bytes digits;

    if (!omc_transfer_bytes_starts_with_literal(text, "simple(")
        || text.size < 9U || text.data[text.size - 1U] != (omc_u8)')') {
        return 0;
    }
    digits.data = text.data + 7U;
    digits.size = text.size - 8U;
    return omc_transfer_decimal_text(digits);
}

static int
omc_transfer_projected_text_looks_large_negative(omc_const_bytes text)
{
    omc_const_bytes digits;

    if (!omc_transfer_bytes_starts_with_literal(text, "-(1+")
        || text.size < 7U || text.data[text.size - 1U] != (omc_u8)')') {
        return 0;
    }
    digits.data = text.data + 4U;
    digits.size = text.size - 5U;
    return omc_transfer_decimal_text(digits);
}

static void
omc_transfer_jumbf_proj_tree_init(omc_transfer_jumbf_proj_tree* tree)
{
    if (tree == (omc_transfer_jumbf_proj_tree*)0) {
        return;
    }
    memset(tree, 0, sizeof(*tree));
}

static void
omc_transfer_jumbf_proj_tree_fini(omc_transfer_jumbf_proj_tree* tree)
{
    omc_u32 i;

    if (tree == (omc_transfer_jumbf_proj_tree*)0) {
        return;
    }
    for (i = 0U; i < tree->node_count; ++i) {
        free(tree->nodes[i].children);
    }
    free(tree->nodes);
    memset(tree, 0, sizeof(*tree));
}

static int
omc_transfer_jumbf_proj_tree_reserve_nodes(omc_transfer_jumbf_proj_tree* tree,
                                           omc_u32 needed)
{
    omc_u32 capacity;
    void* new_mem;

    if (tree == (omc_transfer_jumbf_proj_tree*)0) {
        return 0;
    }
    if (needed <= tree->node_capacity) {
        return 1;
    }
    capacity = tree->node_capacity == 0U ? 8U : tree->node_capacity;
    while (capacity < needed) {
        if (capacity > (omc_u32)(~(omc_u32)0) / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    if ((omc_u64)capacity
        > ((omc_u64)(~(omc_size)0)
           / (omc_u64)sizeof(omc_transfer_jumbf_proj_node))) {
        return 0;
    }
    new_mem = realloc(tree->nodes,
                      (omc_size)capacity * sizeof(omc_transfer_jumbf_proj_node));
    if (new_mem == (void*)0) {
        return 0;
    }
    tree->nodes = (omc_transfer_jumbf_proj_node*)new_mem;
    while (tree->node_capacity < capacity) {
        memset(&tree->nodes[tree->node_capacity], 0,
               sizeof(omc_transfer_jumbf_proj_node));
        tree->node_capacity += 1U;
    }
    return 1;
}

static int
omc_transfer_jumbf_proj_tree_append_node(omc_transfer_jumbf_proj_tree* tree,
                                         omc_u32* out_index)
{
    if (tree == (omc_transfer_jumbf_proj_tree*)0
        || out_index == (omc_u32*)0) {
        return 0;
    }
    if (!omc_transfer_jumbf_proj_tree_reserve_nodes(tree,
                                                    tree->node_count + 1U)) {
        return 0;
    }
    memset(&tree->nodes[tree->node_count], 0,
           sizeof(omc_transfer_jumbf_proj_node));
    *out_index = tree->node_count;
    tree->node_count += 1U;
    return 1;
}

static int
omc_transfer_jumbf_proj_node_reserve_children(
    omc_transfer_jumbf_proj_node* node, omc_u32 needed)
{
    omc_u32 capacity;
    void* new_mem;

    if (node == (omc_transfer_jumbf_proj_node*)0) {
        return 0;
    }
    if (needed <= node->child_capacity) {
        return 1;
    }
    capacity = node->child_capacity == 0U ? 4U : node->child_capacity;
    while (capacity < needed) {
        if (capacity > (omc_u32)(~(omc_u32)0) / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    if ((omc_u64)capacity
        > ((omc_u64)(~(omc_size)0)
           / (omc_u64)sizeof(omc_transfer_jumbf_proj_child))) {
        return 0;
    }
    new_mem = realloc(node->children,
                      (omc_size)capacity * sizeof(omc_transfer_jumbf_proj_child));
    if (new_mem == (void*)0) {
        return 0;
    }
    node->children = (omc_transfer_jumbf_proj_child*)new_mem;
    node->child_capacity = capacity;
    return 1;
}

static int
omc_transfer_jumbf_proj_assign_tag(omc_transfer_jumbf_proj_tree* tree,
                                   omc_u32 node_index,
                                   const omc_entry* entry)
{
    omc_transfer_jumbf_proj_node* node;
    omc_u64 tag;

    if (tree == (omc_transfer_jumbf_proj_tree*)0
        || entry == (const omc_entry*)0 || node_index >= tree->node_count) {
        return 0;
    }
    if (!omc_transfer_meta_scalar_to_u64(&entry->value, &tag)) {
        return 0;
    }
    node = &tree->nodes[node_index];
    if (node->has_tag && node->tag != tag) {
        return 0;
    }
    node->has_tag = 1;
    node->tag = tag;
    return 1;
}

static int
omc_transfer_jumbf_proj_assign_leaf(omc_transfer_jumbf_proj_tree* tree,
                                    omc_u32 node_index,
                                    const omc_entry* entry)
{
    omc_transfer_jumbf_proj_node* node;

    if (tree == (omc_transfer_jumbf_proj_tree*)0
        || entry == (const omc_entry*)0 || node_index >= tree->node_count) {
        return 0;
    }
    node = &tree->nodes[node_index];
    if (node->child_count != 0U) {
        return 0;
    }
    if (node->leaf != (const omc_entry*)0 && node->leaf != entry) {
        return 0;
    }
    node->kind = OMC_TRANSFER_JUMBF_PROJ_LEAF;
    node->leaf = entry;
    return 1;
}

static int
omc_transfer_jumbf_proj_find_or_add_map_child(
    omc_transfer_jumbf_proj_tree* tree, omc_u32 parent_index,
    omc_const_bytes map_key, omc_u32* out_child_index)
{
    omc_transfer_jumbf_proj_node* parent;
    omc_u32 i;
    omc_u32 child_index;

    if (tree == (omc_transfer_jumbf_proj_tree*)0
        || out_child_index == (omc_u32*)0 || parent_index >= tree->node_count
        || map_key.data == (const omc_u8*)0 || map_key.size == 0U
        || omc_transfer_bytes_eq_literal(map_key, "@tag")
        || omc_transfer_decimal_text(map_key)) {
        return 0;
    }

    parent = &tree->nodes[parent_index];
    if (parent->kind == OMC_TRANSFER_JUMBF_PROJ_LEAF
        || parent->leaf != (const omc_entry*)0) {
        return 0;
    }
    if (parent->kind == OMC_TRANSFER_JUMBF_PROJ_UNKNOWN) {
        parent->kind = OMC_TRANSFER_JUMBF_PROJ_MAP;
    } else if (parent->kind != OMC_TRANSFER_JUMBF_PROJ_MAP) {
        return 0;
    }

    for (i = 0U; i < parent->child_count; ++i) {
        omc_transfer_jumbf_proj_child* child;

        child = &parent->children[i];
        if (!child->array_child && child->map_key.size == map_key.size
            && memcmp(child->map_key.data, map_key.data, map_key.size) == 0) {
            *out_child_index = child->node_index;
            return 1;
        }
    }

    if (!omc_transfer_jumbf_proj_tree_append_node(tree, &child_index)
        || !omc_transfer_jumbf_proj_node_reserve_children(parent,
                                                          parent->child_count
                                                              + 1U)) {
        return 0;
    }
    parent->children[parent->child_count].node_index = child_index;
    parent->children[parent->child_count].array_child = 0;
    parent->children[parent->child_count].array_index = 0U;
    parent->children[parent->child_count].map_key = map_key;
    parent->child_count += 1U;
    *out_child_index = child_index;
    return 1;
}

static int
omc_transfer_jumbf_proj_find_or_add_array_child(
    omc_transfer_jumbf_proj_tree* tree, omc_u32 parent_index,
    omc_u32 array_index, omc_u32* out_child_index)
{
    omc_transfer_jumbf_proj_node* parent;
    omc_u32 i;
    omc_u32 insert_at;
    omc_u32 child_index;

    if (tree == (omc_transfer_jumbf_proj_tree*)0
        || out_child_index == (omc_u32*)0 || parent_index >= tree->node_count) {
        return 0;
    }

    parent = &tree->nodes[parent_index];
    if (parent->kind == OMC_TRANSFER_JUMBF_PROJ_LEAF
        || parent->leaf != (const omc_entry*)0) {
        return 0;
    }
    if (parent->kind == OMC_TRANSFER_JUMBF_PROJ_UNKNOWN) {
        parent->kind = OMC_TRANSFER_JUMBF_PROJ_ARRAY;
    } else if (parent->kind != OMC_TRANSFER_JUMBF_PROJ_ARRAY) {
        return 0;
    }

    insert_at = parent->child_count;
    for (i = 0U; i < parent->child_count; ++i) {
        omc_transfer_jumbf_proj_child* child;

        child = &parent->children[i];
        if (!child->array_child) {
            continue;
        }
        if (child->array_index == array_index) {
            *out_child_index = child->node_index;
            return 1;
        }
        if (child->array_index > array_index) {
            insert_at = i;
            break;
        }
    }

    if (!omc_transfer_jumbf_proj_tree_append_node(tree, &child_index)
        || !omc_transfer_jumbf_proj_node_reserve_children(parent,
                                                          parent->child_count
                                                              + 1U)) {
        return 0;
    }
    if (insert_at < parent->child_count) {
        memmove(parent->children + insert_at + 1U, parent->children + insert_at,
                (omc_size)(parent->child_count - insert_at)
                    * sizeof(omc_transfer_jumbf_proj_child));
    }
    parent->children[insert_at].node_index = child_index;
    parent->children[insert_at].array_child = 1;
    parent->children[insert_at].array_index = array_index;
    parent->children[insert_at].map_key.data = (const omc_u8*)0;
    parent->children[insert_at].map_key.size = 0U;
    parent->child_count += 1U;
    *out_child_index = child_index;
    return 1;
}

static int
omc_transfer_build_projected_cbor_tree(const omc_store* store,
                                       omc_const_bytes root_prefix,
                                       omc_transfer_jumbf_proj_tree* out_tree)
{
    omc_size i;
    omc_u32 root_index;
    int matched_any;

    if (store == (const omc_store*)0
        || out_tree == (omc_transfer_jumbf_proj_tree*)0
        || root_prefix.data == (const omc_u8*)0 || root_prefix.size == 0U) {
        return 0;
    }

    omc_transfer_jumbf_proj_tree_init(out_tree);
    out_tree->root_prefix = root_prefix;
    if (!omc_transfer_jumbf_proj_tree_append_node(out_tree, &root_index)
        || root_index != 0U) {
        return 0;
    }

    matched_any = 0;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        omc_const_bytes relative;
        omc_u32 current_node;
        omc_u32 depth;
        omc_size pos;
        int tag_only;

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_arena_view(&store->arena, entry->key.u.jumbf_cbor_key.key);
        if (omc_transfer_jumbf_key_has_segment(key, "c2pa")
            || key.size < root_prefix.size
            || memcmp(key.data, root_prefix.data, root_prefix.size) != 0) {
            continue;
        }

        matched_any = 1;
        relative.data = key.data + root_prefix.size;
        relative.size = key.size - root_prefix.size;
        current_node = 0U;
        depth = 0U;
        pos = 0U;
        tag_only = 0;

        while (pos < relative.size) {
            depth += 1U;
            if (depth > 64U) {
                return 0;
            }
            if (relative.data[pos] == (omc_u8)'.') {
                omc_size end;
                omc_const_bytes map_key;

                pos += 1U;
                if (pos >= relative.size) {
                    return 0;
                }
                if (relative.size - pos == 4U
                    && memcmp(relative.data + pos, "@tag", 4U) == 0) {
                    tag_only = 1;
                    pos = relative.size;
                    break;
                }
                end = pos;
                while (end < relative.size && relative.data[end] != (omc_u8)'.'
                       && relative.data[end] != (omc_u8)'[') {
                    end += 1U;
                }
                map_key.data = relative.data + pos;
                map_key.size = end - pos;
                if (!omc_transfer_jumbf_proj_find_or_add_map_child(
                        out_tree, current_node, map_key, &current_node)) {
                    return 0;
                }
                pos = end;
                continue;
            }
            if (relative.data[pos] == (omc_u8)'[') {
                omc_size index_begin;
                omc_const_bytes index_text;
                omc_u32 array_index;

                pos += 1U;
                index_begin = pos;
                while (pos < relative.size
                       && relative.data[pos] != (omc_u8)']') {
                    pos += 1U;
                }
                if (pos >= relative.size || pos == index_begin) {
                    return 0;
                }
                index_text.data = relative.data + index_begin;
                index_text.size = pos - index_begin;
                if (!omc_transfer_parse_u32_decimal_view(index_text,
                                                         &array_index)
                    || !omc_transfer_jumbf_proj_find_or_add_array_child(
                           out_tree, current_node, array_index,
                           &current_node)) {
                    return 0;
                }
                pos += 1U;
                continue;
            }
            return 0;
        }

        if (tag_only) {
            if (!omc_transfer_jumbf_proj_assign_tag(out_tree, current_node,
                                                    entry)) {
                return 0;
            }
            continue;
        }
        if (!omc_transfer_jumbf_proj_assign_leaf(out_tree, current_node,
                                                 entry)) {
            return 0;
        }
    }

    return matched_any;
}

static omc_status
omc_transfer_emit_projected_cbor_leaf(const omc_store* store,
                                      const omc_entry* entry,
                                      omc_arena* out)
{
    omc_const_bytes view;
    const omc_val* value;
    omc_u64 cbor_negative;

    if (store == (const omc_store*)0 || entry == (const omc_entry*)0
        || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    value = &entry->value;
    if (value->kind == OMC_VAL_SCALAR) {
        switch (value->elem_type) {
        case OMC_ELEM_U8: return OMC_STATUS_STATE;
        case OMC_ELEM_U16:
        case OMC_ELEM_U32:
        case OMC_ELEM_U64:
            return omc_transfer_cbor_append_major_u64(out, 0U, value->u.u64);
        case OMC_ELEM_I8:
        case OMC_ELEM_I16:
        case OMC_ELEM_I32:
        case OMC_ELEM_I64:
            if (value->u.i64 >= 0) {
                return omc_transfer_cbor_append_major_u64(
                    out, 0U, (omc_u64)value->u.i64);
            }
            cbor_negative = (omc_u64)(-(value->u.i64 + 1));
            return omc_transfer_cbor_append_major_u64(out, 1U,
                                                      cbor_negative);
        case OMC_ELEM_F32_BITS:
            return omc_transfer_cbor_append_f32_bits(out, value->u.f32_bits);
        case OMC_ELEM_F64_BITS:
            return omc_transfer_cbor_append_f64_bits(out, value->u.f64_bits);
        case OMC_ELEM_URATIONAL:
        case OMC_ELEM_SRATIONAL: break;
        }
    } else if (value->kind == OMC_VAL_BYTES) {
        view = omc_arena_view(&store->arena, value->u.ref);
        return omc_transfer_cbor_append_bytes(out, view);
    } else if (value->kind == OMC_VAL_TEXT) {
        view = omc_arena_view(&store->arena, value->u.ref);
        if (!omc_transfer_projected_text_supported(value, view)
            || omc_transfer_bytes_eq_literal(view, "null")
            || omc_transfer_bytes_eq_literal(view, "undefined")
            || omc_transfer_projected_text_looks_simple(view)
            || omc_transfer_projected_text_looks_large_negative(view)) {
            return OMC_STATUS_STATE;
        }
        return omc_transfer_cbor_append_text(out, view);
    }

    return OMC_STATUS_STATE;
}

static omc_status
omc_transfer_emit_projected_cbor_node(const omc_store* store,
                                      const omc_transfer_jumbf_proj_tree* tree,
                                      omc_u32 node_index, omc_u32 depth,
                                      omc_arena* out)
{
    const omc_transfer_jumbf_proj_node* node;
    omc_status status;
    omc_u32 i;

    if (store == (const omc_store*)0
        || tree == (const omc_transfer_jumbf_proj_tree*)0
        || out == (omc_arena*)0 || node_index >= tree->node_count
        || depth > 64U) {
        return OMC_STATUS_STATE;
    }

    node = &tree->nodes[node_index];
    if (node->has_tag) {
        status = omc_transfer_cbor_append_major_u64(out, 6U, node->tag);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    switch (node->kind) {
    case OMC_TRANSFER_JUMBF_PROJ_LEAF:
        if (node->leaf == (const omc_entry*)0) {
            return OMC_STATUS_STATE;
        }
        return omc_transfer_emit_projected_cbor_leaf(store, node->leaf, out);
    case OMC_TRANSFER_JUMBF_PROJ_MAP:
        status = omc_transfer_cbor_append_major_u64(out, 5U,
                                                    (omc_u64)node->child_count);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        for (i = 0U; i < node->child_count; ++i) {
            if (node->children[i].array_child) {
                return OMC_STATUS_STATE;
            }
            status = omc_transfer_cbor_append_text(out,
                                                   node->children[i].map_key);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            status = omc_transfer_emit_projected_cbor_node(
                store, tree, node->children[i].node_index, depth + 1U, out);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        return OMC_STATUS_OK;
    case OMC_TRANSFER_JUMBF_PROJ_ARRAY:
        status = omc_transfer_cbor_append_major_u64(out, 4U,
                                                    (omc_u64)node->child_count);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        for (i = 0U; i < node->child_count; ++i) {
            if (!node->children[i].array_child
                || node->children[i].array_index != i) {
                return OMC_STATUS_STATE;
            }
            status = omc_transfer_emit_projected_cbor_node(
                store, tree, node->children[i].node_index, depth + 1U, out);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        return OMC_STATUS_OK;
    case OMC_TRANSFER_JUMBF_PROJ_UNKNOWN: break;
    }
    return OMC_STATUS_STATE;
}

static omc_status
omc_transfer_build_projected_jumbf_logical_payload_for_root(
    const omc_store* store, omc_const_bytes root_prefix, omc_arena* out_payload)
{
    omc_transfer_jumbf_proj_tree tree;
    omc_arena cbor_payload;
    omc_arena jumd_payload;
    omc_arena jumb_children;
    omc_status status;
    static const char k_proj_label[] = "openmeta.projected.";

    if (store == (const omc_store*)0 || out_payload == (omc_arena*)0
        || root_prefix.data == (const omc_u8*)0 || root_prefix.size == 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_jumbf_proj_tree_init(&tree);
    omc_arena_init(&cbor_payload);
    omc_arena_init(&jumd_payload);
    omc_arena_init(&jumb_children);
    omc_arena_reset(out_payload);

    if (!omc_transfer_build_projected_cbor_tree(store, root_prefix, &tree)) {
        status = OMC_STATUS_STATE;
        goto omc_transfer_build_projected_jumbf_logical_payload_done;
    }

    status = omc_transfer_emit_projected_cbor_node(store, &tree, 0U, 0U,
                                                   &cbor_payload);
    if (status != OMC_STATUS_OK) {
        goto omc_transfer_build_projected_jumbf_logical_payload_done;
    }

    status = omc_transfer_append_bytes(&jumd_payload, k_proj_label,
                                       sizeof(k_proj_label) - 1U);
    if (status != OMC_STATUS_OK) {
        goto omc_transfer_build_projected_jumbf_logical_payload_done;
    }
    status = omc_transfer_append_bytes(&jumd_payload, root_prefix.data,
                                       root_prefix.size);
    if (status != OMC_STATUS_OK) {
        goto omc_transfer_build_projected_jumbf_logical_payload_done;
    }
    status = omc_transfer_append_bytes(&jumd_payload, "", 1U);
    if (status != OMC_STATUS_OK) {
        goto omc_transfer_build_projected_jumbf_logical_payload_done;
    }

    status = omc_transfer_append_bmff_box_arena(
        &jumb_children, omc_transfer_fourcc('j', 'u', 'm', 'd'),
        jumd_payload.data, jumd_payload.size);
    if (status != OMC_STATUS_OK) {
        goto omc_transfer_build_projected_jumbf_logical_payload_done;
    }
    status = omc_transfer_append_bmff_box_arena(
        &jumb_children, omc_transfer_fourcc('c', 'b', 'o', 'r'),
        cbor_payload.data, cbor_payload.size);
    if (status != OMC_STATUS_OK) {
        goto omc_transfer_build_projected_jumbf_logical_payload_done;
    }
    status = omc_transfer_append_bmff_box_arena(
        out_payload, omc_transfer_fourcc('j', 'u', 'm', 'b'),
        jumb_children.data, jumb_children.size);

omc_transfer_build_projected_jumbf_logical_payload_done:
    omc_arena_fini(&jumb_children);
    omc_arena_fini(&jumd_payload);
    omc_arena_fini(&cbor_payload);
    omc_transfer_jumbf_proj_tree_fini(&tree);
    return status;
}

static omc_status
omc_transfer_build_projected_jumbf_payloads(const omc_store* store,
                                            omc_arena* out_payload_storage,
                                            omc_byte_ref** out_payload_refs,
                                            omc_u32* out_payload_count)
{
    omc_const_bytes* roots;
    omc_u32 root_count;
    omc_u32 root_capacity;
    omc_byte_ref* payload_refs;
    omc_arena temp_payload;
    omc_size i;
    omc_status status;

    if (store == (const omc_store*)0 || out_payload_storage == (omc_arena*)0
        || out_payload_refs == (omc_byte_ref**)0
        || out_payload_count == (omc_u32*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_payload_refs = (omc_byte_ref*)0;
    *out_payload_count = 0U;
    omc_arena_reset(out_payload_storage);
    roots = (omc_const_bytes*)0;
    root_count = 0U;
    root_capacity = 0U;
    payload_refs = (omc_byte_ref*)0;
    omc_arena_init(&temp_payload);

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        omc_const_bytes root;
        omc_const_bytes suffix;
        omc_u32 j;
        int duplicate;
        void* new_mem;

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_arena_view(&store->arena, entry->key.u.jumbf_cbor_key.key);
        if (omc_transfer_jumbf_key_has_segment(key, "c2pa")
            || !omc_transfer_find_jumbf_cbor_root_prefix(key, &root, &suffix)) {
            continue;
        }
        (void)suffix;

        duplicate = 0;
        for (j = 0U; j < root_count; ++j) {
            if (roots[j].size == root.size
                && memcmp(roots[j].data, root.data, root.size) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (root_count == root_capacity) {
            omc_u32 new_capacity;

            new_capacity = root_capacity == 0U ? 4U : root_capacity * 2U;
            new_mem = realloc(roots,
                              (omc_size)new_capacity * sizeof(omc_const_bytes));
            if (new_mem == (void*)0) {
                status = OMC_STATUS_NO_MEMORY;
                goto omc_transfer_build_projected_jumbf_payloads_done;
            }
            roots = (omc_const_bytes*)new_mem;
            root_capacity = new_capacity;
        }
        roots[root_count] = root;
        root_count += 1U;
    }

    if (root_count == 0U) {
        status = OMC_STATUS_OK;
        goto omc_transfer_build_projected_jumbf_payloads_done;
    }

    payload_refs
        = (omc_byte_ref*)calloc(root_count, sizeof(omc_byte_ref));
    if (payload_refs == (omc_byte_ref*)0) {
        status = OMC_STATUS_NO_MEMORY;
        goto omc_transfer_build_projected_jumbf_payloads_done;
    }

    for (i = 0U; i < root_count; ++i) {
        status = omc_transfer_build_projected_jumbf_logical_payload_for_root(
            store, roots[i], &temp_payload);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_build_projected_jumbf_payloads_done;
        }
        status = omc_arena_append(out_payload_storage, temp_payload.data,
                                  temp_payload.size, &payload_refs[i]);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_build_projected_jumbf_payloads_done;
        }
        omc_arena_reset(&temp_payload);
    }

    *out_payload_refs = payload_refs;
    *out_payload_count = root_count;
    payload_refs = (omc_byte_ref*)0;
    status = OMC_STATUS_OK;

omc_transfer_build_projected_jumbf_payloads_done:
    if (status != OMC_STATUS_OK) {
        omc_arena_reset(out_payload_storage);
    }
    omc_arena_fini(&temp_payload);
    free(payload_refs);
    free(roots);
    return status;
}

static omc_transfer_status
omc_transfer_jumbf_projected_status_from_store_status(omc_status status)
{
    if (status == OMC_STATUS_OK) {
        return OMC_TRANSFER_OK;
    }
    if (status == OMC_STATUS_NO_MEMORY || status == OMC_STATUS_OVERFLOW) {
        return OMC_TRANSFER_LIMIT;
    }
    return OMC_TRANSFER_UNSUPPORTED;
}

static int
omc_transfer_count_jpeg_jumbf_segments(const omc_u8* logical_payload,
                                       omc_size logical_size,
                                       omc_u32* out_count)
{
    omc_transfer_bmff_box root_box;
    omc_u64 fixed_overhead;
    omc_u64 body_size;
    omc_u64 max_chunk;
    omc_u64 count;

    if (logical_payload == (const omc_u8*)0 || out_count == (omc_u32*)0) {
        return 0;
    }
    if (!omc_transfer_parse_bmff_box(logical_payload, logical_size, 0U,
                                     (omc_u64)logical_size, &root_box)
        || root_box.offset != 0U || root_box.size != (omc_u64)logical_size) {
        return 0;
    }
    fixed_overhead = 8U + root_box.header_size;
    if (fixed_overhead > 65533U) {
        return 0;
    }
    body_size = root_box.size - root_box.header_size;
    max_chunk = 65533U - fixed_overhead;
    if (max_chunk == 0U) {
        return 0;
    }
    count = body_size == 0U ? 1U : ((body_size + max_chunk - 1U) / max_chunk);
    if (count > (omc_u64)(~(omc_u32)0)) {
        return 0;
    }
    *out_count = (omc_u32)count;
    return 1;
}

static omc_status
omc_transfer_build_jpeg_jumbf_segment_payload(const omc_u8* logical_payload,
                                              omc_size logical_size,
                                              omc_u32 seq_no,
                                              omc_u32 seq_count,
                                              omc_arena* out)
{
    omc_transfer_bmff_box root_box;
    omc_u64 fixed_overhead;
    omc_u64 max_chunk;
    omc_u64 body_size;
    omc_u64 chunk_off;
    omc_u64 chunk_size;
    omc_u8 zero_bytes[2];
    omc_u8 seq_buf[4];
    omc_status status;

    if (logical_payload == (const omc_u8*)0 || out == (omc_arena*)0
        || seq_no == 0U || seq_count == 0U || seq_no > seq_count) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (!omc_transfer_parse_bmff_box(logical_payload, logical_size, 0U,
                                     (omc_u64)logical_size, &root_box)
        || root_box.offset != 0U || root_box.size != (omc_u64)logical_size) {
        return OMC_STATUS_STATE;
    }
    fixed_overhead = 8U + root_box.header_size;
    if (fixed_overhead > 65533U) {
        return OMC_STATUS_OVERFLOW;
    }
    body_size = root_box.size - root_box.header_size;
    max_chunk = 65533U - fixed_overhead;
    if (max_chunk == 0U) {
        return OMC_STATUS_OVERFLOW;
    }
    chunk_off = max_chunk * (omc_u64)(seq_no - 1U);
    if (seq_no < seq_count && chunk_off >= body_size) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (chunk_off > body_size) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (body_size == 0U) {
        chunk_size = 0U;
    } else {
        chunk_size = body_size - chunk_off;
        if (chunk_size > max_chunk) {
            chunk_size = max_chunk;
        }
    }

    omc_arena_reset(out);
    zero_bytes[0] = 0U;
    zero_bytes[1] = 0U;
    status = omc_transfer_append_bytes(out, "JP", 2U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, zero_bytes, sizeof(zero_bytes));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    omc_transfer_store_u32be(seq_buf, seq_no);
    status = omc_transfer_append_bytes(out, seq_buf, sizeof(seq_buf));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, logical_payload,
                                       (omc_size)root_box.header_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (chunk_size == 0U) {
        return OMC_STATUS_OK;
    }
    return omc_transfer_append_bytes(
        out, logical_payload + (omc_size)root_box.header_size + (omc_size)chunk_off,
        (omc_size)chunk_size);
}

static int
omc_transfer_store_has_icc(const omc_store* store)
{
    omc_size i;

    if (store == (const omc_store*)0) {
        return 0;
    }

    for (i = 0U; i < store->entry_count; ++i) {
        if ((store->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        if (store->entries[i].key.kind == OMC_KEY_ICC_HEADER_FIELD
            || store->entries[i].key.kind == OMC_KEY_ICC_TAG) {
            return 1;
        }
    }
    return 0;
}

static int
omc_transfer_store_has_iptc(const omc_store* store)
{
    omc_size i;

    if (store == (const omc_store*)0) {
        return 0;
    }

    for (i = 0U; i < store->entry_count; ++i) {
        if ((store->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        if (store->entries[i].key.kind == OMC_KEY_IPTC_DATASET) {
            return 1;
        }
    }
    return 0;
}

static omc_status
omc_transfer_append_u16be_arena(omc_arena* out, omc_u16 value)
{
    omc_u8 buf[2];

    buf[0] = (omc_u8)((value >> 8) & 0xFFU);
    buf[1] = (omc_u8)(value & 0xFFU);
    return omc_transfer_append_bytes(out, buf, sizeof(buf));
}

static omc_status
omc_transfer_append_u32be_arena(omc_arena* out, omc_u32 value)
{
    omc_u8 buf[4];

    buf[0] = (omc_u8)((value >> 24) & 0xFFU);
    buf[1] = (omc_u8)((value >> 16) & 0xFFU);
    buf[2] = (omc_u8)((value >> 8) & 0xFFU);
    buf[3] = (omc_u8)(value & 0xFFU);
    return omc_transfer_append_bytes(out, buf, sizeof(buf));
}

static omc_status
omc_transfer_append_u64be_arena(omc_arena* out, omc_u64 value)
{
    omc_u8 buf[8];

    buf[0] = (omc_u8)((value >> 56) & 0xFFU);
    buf[1] = (omc_u8)((value >> 48) & 0xFFU);
    buf[2] = (omc_u8)((value >> 40) & 0xFFU);
    buf[3] = (omc_u8)((value >> 32) & 0xFFU);
    buf[4] = (omc_u8)((value >> 24) & 0xFFU);
    buf[5] = (omc_u8)((value >> 16) & 0xFFU);
    buf[6] = (omc_u8)((value >> 8) & 0xFFU);
    buf[7] = (omc_u8)(value & 0xFFU);
    return omc_transfer_append_bytes(out, buf, sizeof(buf));
}

static int
omc_transfer_icc_elem_size(omc_elem_type elem_type, omc_size* out_size)
{
    if (out_size == (omc_size*)0) {
        return 0;
    }

    switch (elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_I8:
        *out_size = 1U;
        return 1;
    case OMC_ELEM_U16:
    case OMC_ELEM_I16:
        *out_size = 2U;
        return 1;
    case OMC_ELEM_U32:
    case OMC_ELEM_I32:
    case OMC_ELEM_F32_BITS:
        *out_size = 4U;
        return 1;
    case OMC_ELEM_U64:
    case OMC_ELEM_I64:
    case OMC_ELEM_F64_BITS:
    case OMC_ELEM_URATIONAL:
    case OMC_ELEM_SRATIONAL:
        *out_size = 8U;
        return 1;
    default:
        *out_size = 0U;
        return 0;
    }
}

static omc_status
omc_transfer_value_to_icc_bytes(const omc_store* store, const omc_val* value,
                                omc_arena* out, omc_byte_ref* out_ref,
                                int* out_supported)
{
    omc_const_bytes view;
    omc_status status;
    omc_size elem_size;
    omc_u32 count;
    omc_u32 i;

    if (out_supported == (int*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_supported = 0;

    if (store == (const omc_store*)0 || value == (const omc_val*)0
        || out == (omc_arena*)0 || out_ref == (omc_byte_ref*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    out_ref->offset = 0U;
    out_ref->size = 0U;

    if (value->kind == OMC_VAL_BYTES || value->kind == OMC_VAL_TEXT) {
        view = omc_arena_view(&store->arena, value->u.ref);
        status = omc_arena_append(out, view.data, view.size, out_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        *out_supported = 1;
        return OMC_STATUS_OK;
    }

    if (value->kind == OMC_VAL_ARRAY) {
        view = omc_arena_view(&store->arena, value->u.ref);
        if (!omc_transfer_icc_elem_size(value->elem_type, &elem_size)) {
            return OMC_STATUS_OK;
        }
        count = value->count;
        if (count == 0U) {
            if (elem_size == 0U || (view.size % elem_size) != 0U) {
                return OMC_STATUS_OK;
            }
            count = (omc_u32)(view.size / elem_size);
        } else if ((omc_u64)count * (omc_u64)elem_size != (omc_u64)view.size) {
            return OMC_STATUS_OK;
        }

        status = omc_arena_append(out, "", 0U, out_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        if (value->elem_type == OMC_ELEM_U8 || value->elem_type == OMC_ELEM_I8) {
            status = omc_arena_append(out, view.data, view.size, out_ref);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            *out_supported = 1;
            return OMC_STATUS_OK;
        }

        for (i = 0U; i < count; ++i) {
            const omc_u8* src;

            src = view.data + (omc_size)i * elem_size;
            if (value->elem_type == OMC_ELEM_U16
                || value->elem_type == OMC_ELEM_I16) {
                omc_u16 x;

                memcpy(&x, src, sizeof(x));
                status = omc_transfer_append_u16be_arena(out, x);
            } else if (value->elem_type == OMC_ELEM_U32
                       || value->elem_type == OMC_ELEM_I32
                       || value->elem_type == OMC_ELEM_F32_BITS) {
                omc_u32 x;

                memcpy(&x, src, sizeof(x));
                status = omc_transfer_append_u32be_arena(out, x);
            } else if (value->elem_type == OMC_ELEM_URATIONAL
                       || value->elem_type == OMC_ELEM_SRATIONAL) {
                omc_u32 a;
                omc_u32 b;

                memcpy(&a, src, sizeof(a));
                memcpy(&b, src + 4U, sizeof(b));
                status = omc_transfer_append_u32be_arena(out, a);
                if (status == OMC_STATUS_OK) {
                    status = omc_transfer_append_u32be_arena(out, b);
                }
            } else {
                omc_u64 x;

                memcpy(&x, src, sizeof(x));
                status = omc_transfer_append_u64be_arena(out, x);
            }
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }

        out_ref->size = (omc_u32)(out->size - out_ref->offset);
        *out_supported = 1;
        return OMC_STATUS_OK;
    }

    if (value->kind != OMC_VAL_SCALAR) {
        return OMC_STATUS_OK;
    }

    status = omc_arena_append(out, "", 0U, out_ref);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    switch (value->elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_I8: {
        omc_u8 x;

        x = (omc_u8)(value->u.u64 & 0xFFU);
        status = omc_transfer_append_bytes(out, &x, 1U);
        break;
    }
    case OMC_ELEM_U16:
    case OMC_ELEM_I16:
        status = omc_transfer_append_u16be_arena(out,
                                                 (omc_u16)value->u.u64);
        break;
    case OMC_ELEM_U32:
    case OMC_ELEM_I32:
    case OMC_ELEM_F32_BITS:
        status = omc_transfer_append_u32be_arena(out,
                                                 (omc_u32)value->u.u64);
        break;
    case OMC_ELEM_U64:
    case OMC_ELEM_I64:
    case OMC_ELEM_F64_BITS:
        status = omc_transfer_append_u64be_arena(out, value->u.u64);
        break;
    case OMC_ELEM_URATIONAL:
        status = omc_transfer_append_u32be_arena(out, value->u.ur.numer);
        if (status == OMC_STATUS_OK) {
            status = omc_transfer_append_u32be_arena(out, value->u.ur.denom);
        }
        break;
    case OMC_ELEM_SRATIONAL:
        status = omc_transfer_append_u32be_arena(
            out, (omc_u32)value->u.sr.numer);
        if (status == OMC_STATUS_OK) {
            status = omc_transfer_append_u32be_arena(
                out, (omc_u32)value->u.sr.denom);
        }
        break;
    default:
        return OMC_STATUS_OK;
    }
    if (status != OMC_STATUS_OK) {
        return status;
    }

    out_ref->size = (omc_u32)(out->size - out_ref->offset);
    *out_supported = 1;
    return OMC_STATUS_OK;
}

static omc_size
omc_transfer_align_up(omc_size value, omc_size align)
{
    omc_size rem;

    if (align == 0U) {
        return value;
    }
    rem = value % align;
    if (rem == 0U) {
        return value;
    }
    return value + (align - rem);
}

static void
omc_transfer_store_u16be(omc_u8* dst, omc_u16 value)
{
    dst[0] = (omc_u8)((value >> 8) & 0xFFU);
    dst[1] = (omc_u8)(value & 0xFFU);
}

static void
omc_transfer_store_u32be(omc_u8* dst, omc_u32 value)
{
    dst[0] = (omc_u8)((value >> 24) & 0xFFU);
    dst[1] = (omc_u8)((value >> 16) & 0xFFU);
    dst[2] = (omc_u8)((value >> 8) & 0xFFU);
    dst[3] = (omc_u8)(value & 0xFFU);
}

static void
omc_transfer_store_u32le(omc_u8* dst, omc_u32 value)
{
    dst[0] = (omc_u8)(value & 0xFFU);
    dst[1] = (omc_u8)((value >> 8) & 0xFFU);
    dst[2] = (omc_u8)((value >> 16) & 0xFFU);
    dst[3] = (omc_u8)((value >> 24) & 0xFFU);
}

static void
omc_transfer_store_u64le(omc_u8* dst, omc_u64 value)
{
    dst[0] = (omc_u8)(value & 0xFFU);
    dst[1] = (omc_u8)((value >> 8) & 0xFFU);
    dst[2] = (omc_u8)((value >> 16) & 0xFFU);
    dst[3] = (omc_u8)((value >> 24) & 0xFFU);
    dst[4] = (omc_u8)((value >> 32) & 0xFFU);
    dst[5] = (omc_u8)((value >> 40) & 0xFFU);
    dst[6] = (omc_u8)((value >> 48) & 0xFFU);
    dst[7] = (omc_u8)((value >> 56) & 0xFFU);
}

static void
omc_transfer_tiff_store_u16(omc_u8* dst, int little_endian, omc_u16 value)
{
    if (little_endian) {
        dst[0] = (omc_u8)(value & 0xFFU);
        dst[1] = (omc_u8)((value >> 8) & 0xFFU);
    } else {
        dst[0] = (omc_u8)((value >> 8) & 0xFFU);
        dst[1] = (omc_u8)(value & 0xFFU);
    }
}

static void
omc_transfer_tiff_store_u32(omc_u8* dst, int little_endian, omc_u32 value)
{
    if (little_endian) {
        dst[0] = (omc_u8)(value & 0xFFU);
        dst[1] = (omc_u8)((value >> 8) & 0xFFU);
        dst[2] = (omc_u8)((value >> 16) & 0xFFU);
        dst[3] = (omc_u8)((value >> 24) & 0xFFU);
    } else {
        omc_transfer_store_u32be(dst, value);
    }
}

static void
omc_transfer_tiff_store_u64(omc_u8* dst, int little_endian, omc_u64 value)
{
    if (little_endian) {
        dst[0] = (omc_u8)(value & 0xFFU);
        dst[1] = (omc_u8)((value >> 8) & 0xFFU);
        dst[2] = (omc_u8)((value >> 16) & 0xFFU);
        dst[3] = (omc_u8)((value >> 24) & 0xFFU);
        dst[4] = (omc_u8)((value >> 32) & 0xFFU);
        dst[5] = (omc_u8)((value >> 40) & 0xFFU);
        dst[6] = (omc_u8)((value >> 48) & 0xFFU);
        dst[7] = (omc_u8)((value >> 56) & 0xFFU);
    } else {
        dst[0] = (omc_u8)((value >> 56) & 0xFFU);
        dst[1] = (omc_u8)((value >> 48) & 0xFFU);
        dst[2] = (omc_u8)((value >> 40) & 0xFFU);
        dst[3] = (omc_u8)((value >> 32) & 0xFFU);
        dst[4] = (omc_u8)((value >> 24) & 0xFFU);
        dst[5] = (omc_u8)((value >> 16) & 0xFFU);
        dst[6] = (omc_u8)((value >> 8) & 0xFFU);
        dst[7] = (omc_u8)(value & 0xFFU);
    }
}

static omc_u16
omc_transfer_tiff_read_u16(const omc_u8* src, int little_endian)
{
    return little_endian ? omc_transfer_read_u16le(src)
                         : omc_transfer_read_u16be(src);
}

static omc_u32
omc_transfer_tiff_read_u32(const omc_u8* src, int little_endian)
{
    return little_endian ? omc_transfer_read_u32le(src)
                         : omc_transfer_read_u32be(src);
}

static omc_u64
omc_transfer_tiff_read_u64(const omc_u8* src, int little_endian)
{
    return little_endian ? omc_transfer_read_u64le(src)
                         : omc_transfer_read_u64be(src);
}

static void
omc_transfer_tiff_insertion_sort(omc_u8* entries, omc_u32 count,
                                 omc_size entry_size, int little_endian)
{
    omc_u32 i;

    for (i = 1U; i < count; ++i) {
        omc_u8 tmp[20];
        omc_u32 j;

        memcpy(tmp, entries + (omc_size)i * entry_size, entry_size);
        j = i;
        while (j > 0U) {
            omc_u8* prev;
            omc_u16 prev_tag;
            omc_u16 tmp_tag;

            prev = entries + (omc_size)(j - 1U) * entry_size;
            prev_tag = omc_transfer_tiff_read_u16(prev, little_endian);
            tmp_tag = omc_transfer_tiff_read_u16(tmp, little_endian);
            if (prev_tag <= tmp_tag) {
                break;
            }
            memcpy(entries + (omc_size)j * entry_size, prev, entry_size);
            --j;
        }
        memcpy(entries + (omc_size)j * entry_size, tmp, entry_size);
    }
}

static void
omc_transfer_icc_tag_insertion_sort(omc_transfer_icc_tag_item* tags,
                                    omc_u32 count)
{
    omc_u32 i;

    for (i = 1U; i < count; ++i) {
        omc_transfer_icc_tag_item tmp;
        omc_u32 j;

        tmp = tags[i];
        j = i;
        while (j > 0U && tags[j - 1U].signature > tmp.signature) {
            tags[j] = tags[j - 1U];
            --j;
        }
        tags[j] = tmp;
    }
}

static omc_status
omc_transfer_build_icc_profile(const omc_store* store, omc_arena* out_profile,
                               int* out_has_profile)
{
    omc_arena tmp;
    omc_transfer_icc_header_item* headers;
    omc_transfer_icc_tag_item* tags;
    omc_size header_cap;
    omc_size tag_cap;
    omc_size header_count;
    omc_size tag_count;
    omc_size i;
    omc_status status;

    if (out_has_profile == (int*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_has_profile = 0;

    if (store == (const omc_store*)0 || out_profile == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    header_cap = 0U;
    tag_cap = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        if ((store->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        if (store->entries[i].key.kind == OMC_KEY_ICC_HEADER_FIELD) {
            header_cap += 1U;
        } else if (store->entries[i].key.kind == OMC_KEY_ICC_TAG) {
            tag_cap += 1U;
        }
    }
    if (tag_cap == 0U) {
        omc_arena_reset(out_profile);
        return OMC_STATUS_OK;
    }
    if (header_cap > ((omc_size)(~(omc_u32)0)
                      / sizeof(omc_transfer_icc_header_item))
        || tag_cap > ((omc_size)(~(omc_u32)0)
                      / sizeof(omc_transfer_icc_tag_item))) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_init(&tmp);
    headers = (omc_transfer_icc_header_item*)0;
    tags = (omc_transfer_icc_tag_item*)0;
    if (header_cap != 0U) {
        headers = (omc_transfer_icc_header_item*)malloc(header_cap
                                                        * sizeof(*headers));
        if (headers == (omc_transfer_icc_header_item*)0) {
            omc_arena_fini(&tmp);
            return OMC_STATUS_NO_MEMORY;
        }
        memset(headers, 0, header_cap * sizeof(*headers));
    }
    tags = (omc_transfer_icc_tag_item*)malloc(tag_cap * sizeof(*tags));
    if (tags == (omc_transfer_icc_tag_item*)0) {
        free(headers);
        omc_arena_fini(&tmp);
        return OMC_STATUS_NO_MEMORY;
    }
    {
        memset(tags, 0, tag_cap * sizeof(*tags));
    }

    header_count = 0U;
    tag_count = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_byte_ref bytes_ref;
        int supported;
        omc_size j;
        int exists;

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        if (entry->key.kind != OMC_KEY_ICC_HEADER_FIELD
            && entry->key.kind != OMC_KEY_ICC_TAG) {
            continue;
        }
        status = omc_transfer_value_to_icc_bytes(store, &entry->value, &tmp,
                                                 &bytes_ref, &supported);
        if (status != OMC_STATUS_OK) {
            free(tags);
            free(headers);
            omc_arena_fini(&tmp);
            return status;
        }
        if (!supported) {
            continue;
        }
        if (entry->key.kind == OMC_KEY_ICC_HEADER_FIELD) {
            exists = 0;
            for (j = 0U; j < header_count; ++j) {
                if (headers[j].offset
                    == entry->key.u.icc_header_field.offset) {
                    exists = 1;
                    break;
                }
            }
            if (!exists && header_count < header_cap) {
                headers[header_count].offset =
                    entry->key.u.icc_header_field.offset;
                headers[header_count].bytes = bytes_ref;
                header_count += 1U;
            }
        } else if (bytes_ref.size != 0U) {
            exists = 0;
            for (j = 0U; j < tag_count; ++j) {
                if (tags[j].signature == entry->key.u.icc_tag.signature) {
                    exists = 1;
                    break;
                }
            }
            if (!exists && tag_count < tag_cap) {
                tags[tag_count].signature = entry->key.u.icc_tag.signature;
                tags[tag_count].bytes = bytes_ref;
                tag_count += 1U;
            }
        }
    }

    if (tag_count == 0U) {
        omc_arena_reset(out_profile);
        free(tags);
        free(headers);
        omc_arena_fini(&tmp);
        return OMC_STATUS_OK;
    }

    omc_transfer_icc_tag_insertion_sort(tags, (omc_u32)tag_count);
    {
        omc_size table_offset;
        omc_size data_offset;
        omc_size cursor;
        omc_u32 count_u32;

        count_u32 = (omc_u32)tag_count;
        table_offset = 128U;
        data_offset = table_offset + 4U + (omc_size)count_u32 * 12U;
        data_offset = omc_transfer_align_up(data_offset, 4U);
        cursor = data_offset;
        for (i = 0U; i < tag_count; ++i) {
            cursor = omc_transfer_align_up(cursor, 4U);
            if ((omc_u64)cursor + (omc_u64)tags[i].bytes.size
                > (omc_u64)(~(omc_size)0)) {
                free(tags);
                free(headers);
                omc_arena_fini(&tmp);
                return OMC_STATUS_OVERFLOW;
            }
            cursor += tags[i].bytes.size;
        }

        omc_arena_reset(out_profile);
        status = omc_arena_reserve(out_profile, cursor);
        if (status != OMC_STATUS_OK) {
            free(tags);
            free(headers);
            omc_arena_fini(&tmp);
            return status;
        }
        out_profile->size = cursor;
        memset(out_profile->data, 0, cursor);

        for (i = 0U; i < header_count; ++i) {
            omc_const_bytes header_view;
            omc_size copy_size;

            if (headers[i].offset >= 128U || headers[i].bytes.size == 0U) {
                continue;
            }
            header_view = omc_arena_view(&tmp, headers[i].bytes);
            copy_size = header_view.size;
            if ((omc_u64)headers[i].offset + (omc_u64)copy_size > 128U) {
                copy_size = 128U - (omc_size)headers[i].offset;
            }
            memcpy(out_profile->data + headers[i].offset, header_view.data,
                   copy_size);
        }

        out_profile->data[36] = (omc_u8)'a';
        out_profile->data[37] = (omc_u8)'c';
        out_profile->data[38] = (omc_u8)'s';
        out_profile->data[39] = (omc_u8)'p';
        omc_transfer_store_u32be(out_profile->data + 128U, count_u32);

        cursor = data_offset;
        for (i = 0U; i < tag_count; ++i) {
            omc_const_bytes tag_view;
            omc_size table_pos;

            cursor = omc_transfer_align_up(cursor, 4U);
            table_pos = 132U + i * 12U;
            tag_view = omc_arena_view(&tmp, tags[i].bytes);
            omc_transfer_store_u32be(out_profile->data + table_pos,
                                     tags[i].signature);
            omc_transfer_store_u32be(out_profile->data + table_pos + 4U,
                                     (omc_u32)cursor);
            omc_transfer_store_u32be(out_profile->data + table_pos + 8U,
                                     (omc_u32)tag_view.size);
            if (tag_view.size != 0U) {
                memcpy(out_profile->data + cursor, tag_view.data,
                       tag_view.size);
                cursor += tag_view.size;
            }
        }
        omc_transfer_store_u32be(out_profile->data + 0U,
                                 (omc_u32)out_profile->size);
    }

    *out_has_profile = 1;
    free(tags);
    free(headers);
    omc_arena_fini(&tmp);
    return OMC_STATUS_OK;
}

void
omc_jxl_encoder_handoff_opts_init(omc_jxl_encoder_handoff_opts* opts)
{
    if (opts == (omc_jxl_encoder_handoff_opts*)0) {
        return;
    }

    opts->icc_block_index = 0U;
    opts->box_count = 0U;
    opts->box_payload_bytes = 0U;
}

void
omc_jxl_encoder_handoff_init(omc_jxl_encoder_handoff* handoff)
{
    if (handoff == (omc_jxl_encoder_handoff*)0) {
        return;
    }

    handoff->contract_version = OMC_TRANSFER_CONTRACT_VERSION;
    handoff->has_icc_profile = 0;
    handoff->icc_block_index = 0xFFFFFFFFU;
    handoff->box_count = 0U;
    handoff->box_payload_bytes = 0U;
    handoff->icc_profile.data = (const omc_u8*)0;
    handoff->icc_profile.size = 0U;
}

void
omc_jxl_encoder_handoff_io_res_init(omc_jxl_encoder_handoff_io_res* res)
{
    if (res == (omc_jxl_encoder_handoff_io_res*)0) {
        return;
    }

    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->bytes = 0U;
}

void
omc_transfer_payload_build_opts_init(omc_transfer_payload_build_opts* opts)
{
    if (opts == (omc_transfer_payload_build_opts*)0) {
        return;
    }

    opts->format = OMC_SCAN_FMT_JPEG;
    opts->include_exif = 1;
    opts->include_xmp = 1;
    opts->include_icc = 1;
    opts->include_iptc = 1;
    opts->include_jumbf = 1;
    opts->skip_empty_payloads = 1;
    opts->stop_on_error = 1;
    omc_xmp_sidecar_req_init(&opts->xmp_packet);
    opts->xmp_packet.format = OMC_XMP_SIDECAR_PORTABLE;
}

void
omc_transfer_payload_batch_init(omc_transfer_payload_batch* batch)
{
    if (batch == (omc_transfer_payload_batch*)0) {
        return;
    }

    batch->contract_version = OMC_TRANSFER_CONTRACT_VERSION;
    batch->target_format = OMC_SCAN_FMT_UNKNOWN;
    batch->skip_empty_payloads = 1;
    batch->stop_on_error = 1;
    batch->payload_count = 0U;
    batch->payloads = (const omc_transfer_payload*)0;
}

void
omc_transfer_package_build_opts_init(omc_transfer_package_build_opts* opts)
{
    if (opts == (omc_transfer_package_build_opts*)0) {
        return;
    }

    opts->format = OMC_SCAN_FMT_JPEG;
    opts->include_exif = 1;
    opts->include_xmp = 1;
    opts->include_icc = 1;
    opts->include_iptc = 1;
    opts->include_jumbf = 1;
    opts->skip_empty_chunks = 1;
    opts->stop_on_error = 1;
    omc_xmp_sidecar_req_init(&opts->xmp_packet);
    opts->xmp_packet.format = OMC_XMP_SIDECAR_PORTABLE;
}

void
omc_transfer_package_batch_init(omc_transfer_package_batch* batch)
{
    if (batch == (omc_transfer_package_batch*)0) {
        return;
    }

    batch->contract_version = OMC_TRANSFER_CONTRACT_VERSION;
    batch->target_format = OMC_SCAN_FMT_UNKNOWN;
    batch->input_size = 0U;
    batch->output_size = 0U;
    batch->chunk_count = 0U;
    batch->chunks = (const omc_transfer_package_chunk*)0;
}

void
omc_transfer_payload_io_res_init(omc_transfer_payload_io_res* res)
{
    if (res == (omc_transfer_payload_io_res*)0) {
        return;
    }

    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->bytes = 0U;
    res->payload_count = 0U;
}

void
omc_transfer_package_io_res_init(omc_transfer_package_io_res* res)
{
    if (res == (omc_transfer_package_io_res*)0) {
        return;
    }

    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->bytes = 0U;
    res->chunk_count = 0U;
}

void
omc_transfer_payload_replay_res_init(omc_transfer_payload_replay_res* res)
{
    if (res == (omc_transfer_payload_replay_res*)0) {
        return;
    }

    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->replayed = 0U;
    res->failed_payload_index = 0xFFFFFFFFU;
}

void
omc_transfer_package_replay_res_init(omc_transfer_package_replay_res* res)
{
    if (res == (omc_transfer_package_replay_res*)0) {
        return;
    }

    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->replayed = 0U;
    res->failed_chunk_index = 0xFFFFFFFFU;
}

static int
omc_transfer_route_eq(const omc_u8* route, omc_size route_size,
                      const char* literal)
{
    omc_size literal_size;

    if (route == (const omc_u8*)0 || literal == (const char*)0) {
        return 0;
    }
    literal_size = (omc_size)strlen(literal);
    return literal_size == route_size
           && memcmp(route, literal, literal_size) == 0;
}

static int
omc_transfer_route_view_eq(omc_const_bytes route, const char* literal)
{
    return omc_transfer_route_eq(route.data, route.size, literal);
}

static omc_const_bytes
omc_transfer_route_static(const char* literal)
{
    omc_const_bytes out;

    out.data = (const omc_u8*)literal;
    out.size = literal == (const char*)0 ? 0U : (omc_size)strlen(literal);
    return out;
}

OMC_API omc_transfer_semantic_kind
omc_transfer_classify_route_semantic_kind(const omc_u8* route,
                                          omc_size route_size)
{
    if (omc_transfer_route_eq(route, route_size, "jpeg:app1-exif")
        || omc_transfer_route_eq(route, route_size, "tiff:ifd-exif-app1")
        || omc_transfer_route_eq(route, route_size, "png:chunk-exif")
        || omc_transfer_route_eq(route, route_size, "jxl:box-exif")
        || omc_transfer_route_eq(route, route_size, "jp2:box-exif")
        || omc_transfer_route_eq(route, route_size, "webp:chunk-exif")
        || omc_transfer_route_eq(route, route_size, "bmff:item-exif")) {
        return OMC_TRANSFER_SEMANTIC_EXIF;
    }
    if (omc_transfer_route_eq(route, route_size, "jpeg:app1-xmp")
        || omc_transfer_route_eq(route, route_size, "tiff:tag-700-xmp")
        || omc_transfer_route_eq(route, route_size, "png:chunk-xmp")
        || omc_transfer_route_eq(route, route_size, "jxl:box-xml")
        || omc_transfer_route_eq(route, route_size, "jp2:box-xml")
        || omc_transfer_route_eq(route, route_size, "webp:chunk-xmp")
        || omc_transfer_route_eq(route, route_size, "bmff:item-xmp")) {
        return OMC_TRANSFER_SEMANTIC_XMP;
    }
    if (omc_transfer_route_eq(route, route_size, "jpeg:app2-icc")
        || omc_transfer_route_eq(route, route_size, "tiff:tag-34675-icc")
        || omc_transfer_route_eq(route, route_size, "png:chunk-iccp")
        || omc_transfer_route_eq(route, route_size, "jxl:icc-profile")
        || omc_transfer_route_eq(route, route_size, "jp2:box-jp2h-colr")
        || omc_transfer_route_eq(route, route_size, "webp:chunk-iccp")
        || omc_transfer_route_eq(route, route_size,
                                 "bmff:property-colr-icc")) {
        return OMC_TRANSFER_SEMANTIC_ICC;
    }
    if (omc_transfer_route_eq(route, route_size, "jpeg:app13-iptc")
        || omc_transfer_route_eq(route, route_size, "tiff:tag-33723-iptc")) {
        return OMC_TRANSFER_SEMANTIC_IPTC;
    }
    if (omc_transfer_route_eq(route, route_size, "jpeg:app11-jumbf")
        || omc_transfer_route_eq(route, route_size, "jxl:box-jumb")
        || omc_transfer_route_eq(route, route_size, "bmff:item-jumb")) {
        return OMC_TRANSFER_SEMANTIC_JUMBF;
    }
    if (omc_transfer_route_eq(route, route_size, "jpeg:app11-c2pa")
        || omc_transfer_route_eq(route, route_size, "jxl:box-c2pa")
        || omc_transfer_route_eq(route, route_size, "webp:chunk-c2pa")
        || omc_transfer_route_eq(route, route_size, "bmff:item-c2pa")) {
        return OMC_TRANSFER_SEMANTIC_C2PA;
    }
    return OMC_TRANSFER_SEMANTIC_UNKNOWN;
}

OMC_API const char*
omc_transfer_semantic_kind_name(omc_transfer_semantic_kind kind)
{
    switch (kind) {
    case OMC_TRANSFER_SEMANTIC_EXIF: return "Exif";
    case OMC_TRANSFER_SEMANTIC_XMP: return "XMP";
    case OMC_TRANSFER_SEMANTIC_ICC: return "ICC";
    case OMC_TRANSFER_SEMANTIC_IPTC: return "IPTC";
    case OMC_TRANSFER_SEMANTIC_JUMBF: return "JUMBF";
    case OMC_TRANSFER_SEMANTIC_C2PA: return "C2PA";
    case OMC_TRANSFER_SEMANTIC_UNKNOWN: break;
    }
    return "Unknown";
}

static int
omc_transfer_payload_target_code_from_format(omc_scan_fmt format,
                                             omc_u8* out_code)
{
    if (out_code == (omc_u8*)0) {
        return 0;
    }

    switch (format) {
    case OMC_SCAN_FMT_JPEG:
        *out_code = 0U;
        return 1;
    case OMC_SCAN_FMT_TIFF:
        *out_code = 1U;
        return 1;
    case OMC_SCAN_FMT_JXL:
        *out_code = 2U;
        return 1;
    case OMC_SCAN_FMT_WEBP:
        *out_code = 3U;
        return 1;
    case OMC_SCAN_FMT_HEIF:
        *out_code = 4U;
        return 1;
    case OMC_SCAN_FMT_AVIF:
        *out_code = 5U;
        return 1;
    case OMC_SCAN_FMT_CR3:
        *out_code = 6U;
        return 1;
    case OMC_SCAN_FMT_EXR:
        *out_code = 7U;
        return 1;
    case OMC_SCAN_FMT_PNG:
        *out_code = 8U;
        return 1;
    case OMC_SCAN_FMT_JP2:
        *out_code = 9U;
        return 1;
    case OMC_SCAN_FMT_DNG:
        *out_code = 10U;
        return 1;
    default: break;
    }
    return 0;
}

static int
omc_transfer_payload_format_from_target_code(omc_u8 code,
                                             omc_scan_fmt* out_format)
{
    if (out_format == (omc_scan_fmt*)0) {
        return 0;
    }

    switch (code) {
    case 0U:
        *out_format = OMC_SCAN_FMT_JPEG;
        return 1;
    case 1U:
        *out_format = OMC_SCAN_FMT_TIFF;
        return 1;
    case 2U:
        *out_format = OMC_SCAN_FMT_JXL;
        return 1;
    case 3U:
        *out_format = OMC_SCAN_FMT_WEBP;
        return 1;
    case 4U:
        *out_format = OMC_SCAN_FMT_HEIF;
        return 1;
    case 5U:
        *out_format = OMC_SCAN_FMT_AVIF;
        return 1;
    case 6U:
        *out_format = OMC_SCAN_FMT_CR3;
        return 1;
    case 7U:
        *out_format = OMC_SCAN_FMT_EXR;
        return 1;
    case 8U:
        *out_format = OMC_SCAN_FMT_PNG;
        return 1;
    case 9U:
        *out_format = OMC_SCAN_FMT_JP2;
        return 1;
    case 10U:
        *out_format = OMC_SCAN_FMT_DNG;
        return 1;
    default: break;
    }
    return 0;
}

static int
omc_transfer_payload_jpeg_marker_from_route(omc_const_bytes route,
                                            omc_u8* out_marker)
{
    if (out_marker == (omc_u8*)0) {
        return 0;
    }
    if (omc_transfer_route_view_eq(route, "jpeg:app1-exif")
        || omc_transfer_route_view_eq(route, "jpeg:app1-xmp")) {
        *out_marker = 0xE1U;
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jpeg:app2-icc")) {
        *out_marker = 0xE2U;
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jpeg:app11-jumbf")
        || omc_transfer_route_view_eq(route, "jpeg:app11-c2pa")) {
        *out_marker = 0xEBU;
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jpeg:app13-iptc")) {
        *out_marker = 0xEDU;
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_tiff_tag_from_route(omc_const_bytes route,
                                         omc_u16* out_tag)
{
    if (out_tag == (omc_u16*)0) {
        return 0;
    }
    if (omc_transfer_route_view_eq(route, "tiff:tag-700-xmp")) {
        *out_tag = 700U;
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "tiff:ifd-exif-app1")) {
        *out_tag = 34665U;
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "tiff:tag-34675-icc")) {
        *out_tag = 34675U;
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "tiff:tag-33723-iptc")) {
        *out_tag = 33723U;
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_jxl_box_from_route(omc_const_bytes route,
                                        omc_u8 out_box_type[4],
                                        int* out_compress)
{
    if (out_box_type == (omc_u8*)0 || out_compress == (int*)0) {
        return 0;
    }

    *out_compress = 0;
    if (omc_transfer_route_view_eq(route, "jxl:box-exif")) {
        memcpy(out_box_type, "Exif", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jxl:box-xml")) {
        memcpy(out_box_type, "xml ", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jxl:box-jumb")) {
        memcpy(out_box_type, "jumb", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jxl:box-c2pa")) {
        memcpy(out_box_type, "c2pa", 4U);
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_webp_chunk_from_route(omc_const_bytes route,
                                           omc_u8 out_chunk_type[4])
{
    if (out_chunk_type == (omc_u8*)0) {
        return 0;
    }
    if (omc_transfer_route_view_eq(route, "webp:chunk-exif")) {
        memcpy(out_chunk_type, "EXIF", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "webp:chunk-xmp")) {
        memcpy(out_chunk_type, "XMP ", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "webp:chunk-iccp")) {
        memcpy(out_chunk_type, "ICCP", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "webp:chunk-c2pa")) {
        memcpy(out_chunk_type, "C2PA", 4U);
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_png_chunk_from_route(omc_const_bytes route,
                                          omc_u8 out_chunk_type[4])
{
    if (out_chunk_type == (omc_u8*)0) {
        return 0;
    }
    if (omc_transfer_route_view_eq(route, "png:chunk-exif")) {
        memcpy(out_chunk_type, "eXIf", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "png:chunk-xmp")) {
        memcpy(out_chunk_type, "iTXt", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "png:chunk-iccp")) {
        memcpy(out_chunk_type, "iCCP", 4U);
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_jp2_box_from_route(omc_const_bytes route,
                                        omc_u8 out_box_type[4])
{
    if (out_box_type == (omc_u8*)0) {
        return 0;
    }
    if (omc_transfer_route_view_eq(route, "jp2:box-exif")) {
        memcpy(out_box_type, "Exif", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jp2:box-xml")) {
        memcpy(out_box_type, "xml ", 4U);
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "jp2:box-jp2h-colr")) {
        memcpy(out_box_type, "jp2h", 4U);
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_bmff_item_from_route(omc_const_bytes route,
                                          omc_u32* out_item_type,
                                          int* out_mime_xmp)
{
    if (out_item_type == (omc_u32*)0 || out_mime_xmp == (int*)0) {
        return 0;
    }

    *out_mime_xmp = 0;
    if (omc_transfer_route_view_eq(route, "bmff:item-exif")) {
        *out_item_type = omc_transfer_fourcc('E', 'x', 'i', 'f');
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "bmff:item-xmp")) {
        *out_item_type = omc_transfer_fourcc('m', 'i', 'm', 'e');
        *out_mime_xmp = 1;
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "bmff:item-jumb")) {
        *out_item_type = omc_transfer_fourcc('j', 'u', 'm', 'b');
        return 1;
    }
    if (omc_transfer_route_view_eq(route, "bmff:item-c2pa")) {
        *out_item_type = omc_transfer_fourcc('c', '2', 'p', 'a');
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_bmff_property_from_route(omc_const_bytes route,
                                              omc_u32* out_property_type,
                                              omc_u32* out_property_subtype)
{
    if (out_property_type == (omc_u32*)0
        || out_property_subtype == (omc_u32*)0) {
        return 0;
    }
    if (omc_transfer_route_view_eq(route, "bmff:property-colr-icc")) {
        *out_property_type = omc_transfer_fourcc('c', 'o', 'l', 'r');
        *out_property_subtype = omc_transfer_fourcc('p', 'r', 'o', 'f');
        return 1;
    }
    return 0;
}

static int
omc_transfer_payload_exr_attribute_route(omc_const_bytes route)
{
    return omc_transfer_route_view_eq(route, "exr:attribute-string");
}

static void
omc_transfer_payload_op_init(omc_transfer_payload_op* op)
{
    if (op == (omc_transfer_payload_op*)0) {
        return;
    }
    memset(op, 0, sizeof(*op));
    op->kind = OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER;
}

static omc_transfer_status
omc_transfer_payload_validate_batch(const omc_transfer_payload_batch* batch)
{
    omc_u8 target_code;
    omc_u32 i;

    if (batch == (const omc_transfer_payload_batch*)0) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (batch->contract_version != OMC_TRANSFER_CONTRACT_VERSION) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (!omc_transfer_payload_target_code_from_format(batch->target_format,
                                                      &target_code)) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (batch->payload_count != 0U
        && batch->payloads == (const omc_transfer_payload*)0) {
        return OMC_TRANSFER_MALFORMED;
    }

    for (i = 0U; i < batch->payload_count; ++i) {
        const omc_transfer_payload* payload;
        omc_transfer_semantic_kind semantic_kind;

        payload = &batch->payloads[i];
        if (payload->route.data == (const omc_u8*)0) {
            return OMC_TRANSFER_MALFORMED;
        }
        if (payload->payload.size != 0U
            && payload->payload.data == (const omc_u8*)0) {
            return OMC_TRANSFER_MALFORMED;
        }
        if (payload->op.payload_size != (omc_u64)payload->payload.size) {
            return OMC_TRANSFER_MALFORMED;
        }

        semantic_kind = omc_transfer_classify_route_semantic_kind(
            payload->route.data, payload->route.size);
        if (payload->semantic_kind != semantic_kind) {
            return OMC_TRANSFER_MALFORMED;
        }

        switch (payload->op.kind) {
        case OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER: {
            omc_u8 marker;
            if (!omc_transfer_payload_jpeg_marker_from_route(payload->route,
                                                             &marker)
                || marker != payload->op.jpeg_marker_code) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        case OMC_TRANSFER_PAYLOAD_OP_TIFF_TAG_BYTES: {
            omc_u16 tag;
            if (!omc_transfer_payload_tiff_tag_from_route(payload->route, &tag)
                || tag != payload->op.tiff_tag) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        case OMC_TRANSFER_PAYLOAD_OP_JXL_BOX: {
            omc_u8 box_type[4];
            int compress;
            if (!omc_transfer_payload_jxl_box_from_route(payload->route,
                                                         box_type, &compress)
                || memcmp(box_type, payload->op.box_type, 4U) != 0
                || compress != payload->op.compress) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        case OMC_TRANSFER_PAYLOAD_OP_JXL_ICC_PROFILE:
            if (!omc_transfer_route_view_eq(payload->route,
                                            "jxl:icc-profile")) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        case OMC_TRANSFER_PAYLOAD_OP_WEBP_CHUNK: {
            omc_u8 chunk_type[4];
            if (!omc_transfer_payload_webp_chunk_from_route(payload->route,
                                                            chunk_type)
                || memcmp(chunk_type, payload->op.chunk_type, 4U) != 0) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        case OMC_TRANSFER_PAYLOAD_OP_PNG_CHUNK: {
            omc_u8 chunk_type[4];
            if (!omc_transfer_payload_png_chunk_from_route(payload->route,
                                                           chunk_type)
                || memcmp(chunk_type, payload->op.chunk_type, 4U) != 0) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        case OMC_TRANSFER_PAYLOAD_OP_JP2_BOX: {
            omc_u8 box_type[4];
            if (!omc_transfer_payload_jp2_box_from_route(payload->route,
                                                         box_type)
                || memcmp(box_type, payload->op.box_type, 4U) != 0) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        case OMC_TRANSFER_PAYLOAD_OP_EXR_ATTRIBUTE:
            if (!omc_transfer_payload_exr_attribute_route(payload->route)) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        case OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM: {
            omc_u32 item_type;
            int mime_xmp;
            if (!omc_transfer_payload_bmff_item_from_route(payload->route,
                                                           &item_type,
                                                           &mime_xmp)
                || item_type != payload->op.bmff_item_type
                || mime_xmp != payload->op.bmff_mime_xmp) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        case OMC_TRANSFER_PAYLOAD_OP_BMFF_PROPERTY: {
            omc_u32 property_type;
            omc_u32 property_subtype;
            if (!omc_transfer_payload_bmff_property_from_route(
                    payload->route, &property_type, &property_subtype)
                || property_type != payload->op.bmff_property_type
                || property_subtype != payload->op.bmff_property_subtype) {
                return OMC_TRANSFER_MALFORMED;
            }
            break;
        }
        default: return OMC_TRANSFER_MALFORMED;
        }
    }

    return OMC_TRANSFER_OK;
}

static omc_transfer_status
omc_transfer_package_validate_batch(const omc_transfer_package_batch* batch)
{
    omc_u8 target_code;
    omc_u32 i;
    omc_u64 next_offset;

    if (batch == (const omc_transfer_package_batch*)0) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (batch->contract_version != OMC_TRANSFER_CONTRACT_VERSION) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (!omc_transfer_payload_target_code_from_format(batch->target_format,
                                                      &target_code)) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (batch->chunk_count != 0U
        && batch->chunks == (const omc_transfer_package_chunk*)0) {
        return OMC_TRANSFER_MALFORMED;
    }

    next_offset = 0U;
    for (i = 0U; i < batch->chunk_count; ++i) {
        const omc_transfer_package_chunk* chunk;

        chunk = &batch->chunks[i];
        if (chunk->route.size != 0U
            && chunk->route.data == (const omc_u8*)0) {
            return OMC_TRANSFER_MALFORMED;
        }
        if (chunk->bytes.size != 0U
            && chunk->bytes.data == (const omc_u8*)0) {
            return OMC_TRANSFER_MALFORMED;
        }
        if (chunk->output_offset != next_offset) {
            return OMC_TRANSFER_MALFORMED;
        }
        switch (chunk->kind) {
        case OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE:
        case OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK:
        case OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT:
        case OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES: break;
        default: return OMC_TRANSFER_MALFORMED;
        }
        if ((omc_u64)chunk->bytes.size > ~(omc_u64)0 - next_offset) {
            return OMC_TRANSFER_LIMIT;
        }
        next_offset += (omc_u64)chunk->bytes.size;
    }
    if (next_offset != batch->output_size) {
        return OMC_TRANSFER_MALFORMED;
    }

    return OMC_TRANSFER_OK;
}

static omc_status
omc_transfer_payload_append_u16le(omc_arena* out, omc_u16 value)
{
    omc_u8 buf[2];

    buf[0] = (omc_u8)(value & 0xFFU);
    buf[1] = (omc_u8)((value >> 8) & 0xFFU);
    return omc_transfer_append_bytes(out, buf, sizeof(buf));
}

static omc_status
omc_transfer_payload_append_u32le(omc_arena* out, omc_u32 value)
{
    omc_u8 buf[4];

    omc_transfer_store_u32le(buf, value);
    return omc_transfer_append_bytes(out, buf, sizeof(buf));
}

static omc_status
omc_transfer_payload_append_u64le(omc_arena* out, omc_u64 value)
{
    omc_u8 buf[8];

    omc_transfer_store_u64le(buf, value);
    return omc_transfer_append_bytes(out, buf, sizeof(buf));
}

static omc_status
omc_transfer_payload_append_blob_le(omc_arena* out, const omc_u8* bytes,
                                    omc_size size)
{
    omc_status status;

    status = omc_transfer_payload_append_u64le(out, (omc_u64)size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (size == 0U) {
        return OMC_STATUS_OK;
    }
    return omc_transfer_append_bytes(out, bytes, size);
}

static int
omc_transfer_payload_read_u8(const omc_u8* bytes, omc_size size,
                             omc_size* io_off, omc_u8* out_value)
{
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_value == (omc_u8*)0) {
        return 0;
    }
    off = *io_off;
    if (off > size || size - off < 1U) {
        return 0;
    }
    *out_value = bytes[off];
    *io_off = off + 1U;
    return 1;
}

static int
omc_transfer_payload_read_u16le(const omc_u8* bytes, omc_size size,
                                omc_size* io_off, omc_u16* out_value)
{
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_value == (omc_u16*)0) {
        return 0;
    }
    off = *io_off;
    if (off > size || size - off < 2U) {
        return 0;
    }
    *out_value = omc_transfer_read_u16le(bytes + off);
    *io_off = off + 2U;
    return 1;
}

static int
omc_transfer_payload_read_u32le(const omc_u8* bytes, omc_size size,
                                omc_size* io_off, omc_u32* out_value)
{
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_value == (omc_u32*)0) {
        return 0;
    }
    off = *io_off;
    if (off > size || size - off < 4U) {
        return 0;
    }
    *out_value = omc_transfer_read_u32le(bytes + off);
    *io_off = off + 4U;
    return 1;
}

static int
omc_transfer_payload_read_u64le(const omc_u8* bytes, omc_size size,
                                omc_size* io_off, omc_u64* out_value)
{
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_value == (omc_u64*)0) {
        return 0;
    }
    off = *io_off;
    if (off > size || size - off < 8U) {
        return 0;
    }
    *out_value = ((omc_u64)bytes[off + 7U] << 56)
                 | ((omc_u64)bytes[off + 6U] << 48)
                 | ((omc_u64)bytes[off + 5U] << 40)
                 | ((omc_u64)bytes[off + 4U] << 32)
                 | ((omc_u64)bytes[off + 3U] << 24)
                 | ((omc_u64)bytes[off + 2U] << 16)
                 | ((omc_u64)bytes[off + 1U] << 8)
                 | (omc_u64)bytes[off + 0U];
    *io_off = off + 8U;
    return 1;
}

static int
omc_transfer_payload_read_blob(const omc_u8* bytes, omc_size size,
                               omc_size* io_off, omc_const_bytes* out_blob)
{
    omc_u64 blob_size;
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_blob == (omc_const_bytes*)0) {
        return 0;
    }
    if (!omc_transfer_payload_read_u64le(bytes, size, io_off, &blob_size)) {
        return 0;
    }
    if (blob_size > (omc_u64)(~(omc_size)0)) {
        return 0;
    }
    off = *io_off;
    if ((omc_u64)off > (omc_u64)size
        || blob_size > (omc_u64)((omc_u64)size - (omc_u64)off)) {
        return 0;
    }
    out_blob->data = bytes + off;
    out_blob->size = (omc_size)blob_size;
    *io_off = off + (omc_size)blob_size;
    return 1;
}

static omc_transfer_payload*
omc_transfer_payload_entry_ptr(omc_arena* storage, omc_byte_ref entries_ref)
{
    omc_mut_bytes view;

    view = omc_arena_view_mut(storage, entries_ref);
    return (omc_transfer_payload*)view.data;
}

static omc_status
omc_transfer_payload_store_entry(omc_arena* storage, omc_byte_ref entries_ref,
                                 omc_u32 index,
                                 omc_transfer_semantic_kind semantic_kind,
                                 omc_const_bytes route,
                                 const omc_transfer_payload_op* op,
                                 const omc_u8* payload_bytes,
                                 omc_size payload_size)
{
    omc_transfer_payload* entries;
    omc_transfer_payload_op stored_op;
    omc_const_bytes payload_view;
    omc_byte_ref payload_ref;
    omc_status status;

    if (storage == (omc_arena*)0 || op == (const omc_transfer_payload_op*)0
        || route.data == (const omc_u8*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (payload_bytes == (const omc_u8*)0 && payload_size != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    payload_view.data = (const omc_u8*)0;
    payload_view.size = 0U;
    if (payload_size != 0U) {
        status = omc_arena_append(storage, payload_bytes, payload_size,
                                  &payload_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        payload_view = omc_arena_view(storage, payload_ref);
    }

    entries = omc_transfer_payload_entry_ptr(storage, entries_ref);
    if (entries == (omc_transfer_payload*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    stored_op = *op;
    stored_op.block_index = index;
    stored_op.payload_size = (omc_u64)payload_size;
    if (stored_op.serialized_size == 0U) {
        stored_op.serialized_size = (omc_u64)payload_size;
    }

    entries[index].semantic_kind = semantic_kind;
    entries[index].route = route;
    entries[index].op = stored_op;
    entries[index].payload = payload_view;
    return OMC_STATUS_OK;
}

static omc_transfer_package_chunk*
omc_transfer_package_entry_ptr(omc_arena* storage, omc_byte_ref entries_ref)
{
    omc_mut_bytes view;

    view = omc_arena_view_mut(storage, entries_ref);
    return (omc_transfer_package_chunk*)view.data;
}

static omc_status
omc_transfer_package_store_entry(omc_arena* storage,
                                 omc_byte_ref entries_ref, omc_u32 index,
                                 omc_transfer_package_chunk_kind kind,
                                 omc_const_bytes route,
                                 omc_u64 output_offset,
                                 omc_u64 source_offset,
                                 omc_u32 block_index,
                                 omc_u8 jpeg_marker_code,
                                 const omc_u8* bytes, omc_size size)
{
    omc_transfer_package_chunk* entries;
    omc_const_bytes route_view;
    omc_const_bytes bytes_view;
    omc_byte_ref route_ref;
    omc_byte_ref bytes_ref;
    omc_status status;

    if (storage == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    route_view.data = (const omc_u8*)0;
    route_view.size = 0U;
    if (route.size != 0U && route.data == (const omc_u8*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (route.size != 0U) {
        status = omc_arena_append(storage, route.data, route.size, &route_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        route_view = omc_arena_view(storage, route_ref);
    }

    bytes_view.data = (const omc_u8*)0;
    bytes_view.size = 0U;
    if (size != 0U) {
        status = omc_arena_append(storage, bytes, size, &bytes_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        bytes_view = omc_arena_view(storage, bytes_ref);
    }

    entries = omc_transfer_package_entry_ptr(storage, entries_ref);
    if (entries == (omc_transfer_package_chunk*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    entries[index].kind = kind;
    entries[index].route = route_view;
    entries[index].output_offset = output_offset;
    entries[index].source_offset = source_offset;
    entries[index].block_index = block_index;
    entries[index].jpeg_marker_code = jpeg_marker_code;
    entries[index].bytes = bytes_view;
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_build_jpeg_icc_payload(const omc_u8* chunk, omc_size chunk_size,
                                    omc_u8 seq_no, omc_u8 chunk_count,
                                    omc_arena* out)
{
    omc_status status;

    if (chunk == (const omc_u8*)0 || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (chunk_size > 65519U) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_transfer_append_bytes(out, "ICC_PROFILE\0", 12U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, &seq_no, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, &chunk_count, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_transfer_append_bytes(out, chunk, chunk_size);
}

static int
omc_transfer_payload_make_exif_op(omc_scan_fmt format,
                                  omc_const_bytes* out_route,
                                  omc_transfer_payload_op* out_op)
{
    if (out_route == (omc_const_bytes*)0
        || out_op == (omc_transfer_payload_op*)0) {
        return 0;
    }

    omc_transfer_payload_op_init(out_op);
    switch (format) {
    case OMC_SCAN_FMT_JPEG:
        *out_route = omc_transfer_route_static("jpeg:app1-exif");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER;
        out_op->jpeg_marker_code = 0xE1U;
        return 1;
    case OMC_SCAN_FMT_TIFF:
    case OMC_SCAN_FMT_DNG:
        *out_route = omc_transfer_route_static("tiff:ifd-exif-app1");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_TIFF_TAG_BYTES;
        out_op->tiff_tag = 34665U;
        return 1;
    case OMC_SCAN_FMT_PNG:
        *out_route = omc_transfer_route_static("png:chunk-exif");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_PNG_CHUNK;
        memcpy(out_op->chunk_type, "eXIf", 4U);
        return 1;
    case OMC_SCAN_FMT_WEBP:
        *out_route = omc_transfer_route_static("webp:chunk-exif");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_WEBP_CHUNK;
        memcpy(out_op->chunk_type, "EXIF", 4U);
        return 1;
    case OMC_SCAN_FMT_JP2:
        *out_route = omc_transfer_route_static("jp2:box-exif");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JP2_BOX;
        memcpy(out_op->box_type, "Exif", 4U);
        return 1;
    case OMC_SCAN_FMT_JXL:
        *out_route = omc_transfer_route_static("jxl:box-exif");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JXL_BOX;
        memcpy(out_op->box_type, "Exif", 4U);
        return 1;
    case OMC_SCAN_FMT_HEIF:
    case OMC_SCAN_FMT_AVIF:
    case OMC_SCAN_FMT_CR3:
        *out_route = omc_transfer_route_static("bmff:item-exif");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM;
        out_op->bmff_item_type = omc_transfer_fourcc('E', 'x', 'i', 'f');
        return 1;
    default: break;
    }
    return 0;
}

static int
omc_transfer_payload_make_xmp_op(omc_scan_fmt format,
                                 omc_const_bytes* out_route,
                                 omc_transfer_payload_op* out_op)
{
    if (out_route == (omc_const_bytes*)0
        || out_op == (omc_transfer_payload_op*)0) {
        return 0;
    }

    omc_transfer_payload_op_init(out_op);
    switch (format) {
    case OMC_SCAN_FMT_JPEG:
        *out_route = omc_transfer_route_static("jpeg:app1-xmp");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER;
        out_op->jpeg_marker_code = 0xE1U;
        return 1;
    case OMC_SCAN_FMT_TIFF:
    case OMC_SCAN_FMT_DNG:
        *out_route = omc_transfer_route_static("tiff:tag-700-xmp");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_TIFF_TAG_BYTES;
        out_op->tiff_tag = 700U;
        return 1;
    case OMC_SCAN_FMT_PNG:
        *out_route = omc_transfer_route_static("png:chunk-xmp");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_PNG_CHUNK;
        memcpy(out_op->chunk_type, "iTXt", 4U);
        return 1;
    case OMC_SCAN_FMT_WEBP:
        *out_route = omc_transfer_route_static("webp:chunk-xmp");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_WEBP_CHUNK;
        memcpy(out_op->chunk_type, "XMP ", 4U);
        return 1;
    case OMC_SCAN_FMT_JP2:
        *out_route = omc_transfer_route_static("jp2:box-xml");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JP2_BOX;
        memcpy(out_op->box_type, "xml ", 4U);
        return 1;
    case OMC_SCAN_FMT_JXL:
        *out_route = omc_transfer_route_static("jxl:box-xml");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JXL_BOX;
        memcpy(out_op->box_type, "xml ", 4U);
        return 1;
    case OMC_SCAN_FMT_HEIF:
    case OMC_SCAN_FMT_AVIF:
    case OMC_SCAN_FMT_CR3:
        *out_route = omc_transfer_route_static("bmff:item-xmp");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM;
        out_op->bmff_item_type = omc_transfer_fourcc('m', 'i', 'm', 'e');
        out_op->bmff_mime_xmp = 1;
        return 1;
    default: break;
    }
    return 0;
}

static int
omc_transfer_payload_make_icc_op(omc_scan_fmt format,
                                 omc_const_bytes* out_route,
                                 omc_transfer_payload_op* out_op)
{
    if (out_route == (omc_const_bytes*)0
        || out_op == (omc_transfer_payload_op*)0) {
        return 0;
    }

    omc_transfer_payload_op_init(out_op);
    switch (format) {
    case OMC_SCAN_FMT_JPEG:
        *out_route = omc_transfer_route_static("jpeg:app2-icc");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER;
        out_op->jpeg_marker_code = 0xE2U;
        return 1;
    case OMC_SCAN_FMT_TIFF:
    case OMC_SCAN_FMT_DNG:
        *out_route = omc_transfer_route_static("tiff:tag-34675-icc");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_TIFF_TAG_BYTES;
        out_op->tiff_tag = 34675U;
        return 1;
    case OMC_SCAN_FMT_PNG:
        *out_route = omc_transfer_route_static("png:chunk-iccp");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_PNG_CHUNK;
        memcpy(out_op->chunk_type, "iCCP", 4U);
        return 1;
    case OMC_SCAN_FMT_WEBP:
        *out_route = omc_transfer_route_static("webp:chunk-iccp");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_WEBP_CHUNK;
        memcpy(out_op->chunk_type, "ICCP", 4U);
        return 1;
    case OMC_SCAN_FMT_JP2:
        *out_route = omc_transfer_route_static("jp2:box-jp2h-colr");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JP2_BOX;
        memcpy(out_op->box_type, "jp2h", 4U);
        return 1;
    case OMC_SCAN_FMT_JXL:
        *out_route = omc_transfer_route_static("jxl:icc-profile");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JXL_ICC_PROFILE;
        return 1;
    case OMC_SCAN_FMT_HEIF:
    case OMC_SCAN_FMT_AVIF:
    case OMC_SCAN_FMT_CR3:
        *out_route = omc_transfer_route_static("bmff:property-colr-icc");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_BMFF_PROPERTY;
        out_op->bmff_property_type = omc_transfer_fourcc('c', 'o', 'l', 'r');
        out_op->bmff_property_subtype = omc_transfer_fourcc('p', 'r', 'o',
                                                            'f');
        return 1;
    default: break;
    }
    return 0;
}

static int
omc_transfer_payload_make_iptc_op(omc_scan_fmt format,
                                  omc_const_bytes* out_route,
                                  omc_transfer_payload_op* out_op)
{
    if (out_route == (omc_const_bytes*)0
        || out_op == (omc_transfer_payload_op*)0) {
        return 0;
    }

    omc_transfer_payload_op_init(out_op);
    switch (format) {
    case OMC_SCAN_FMT_JPEG:
        *out_route = omc_transfer_route_static("jpeg:app13-iptc");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER;
        out_op->jpeg_marker_code = 0xEDU;
        return 1;
    case OMC_SCAN_FMT_TIFF:
    case OMC_SCAN_FMT_DNG:
        *out_route = omc_transfer_route_static("tiff:tag-33723-iptc");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_TIFF_TAG_BYTES;
        out_op->tiff_tag = 33723U;
        return 1;
    default: break;
    }
    return 0;
}

static int
omc_transfer_payload_make_jumbf_op(omc_scan_fmt format,
                                   omc_const_bytes* out_route,
                                   omc_transfer_payload_op* out_op)
{
    if (out_route == (omc_const_bytes*)0
        || out_op == (omc_transfer_payload_op*)0) {
        return 0;
    }

    omc_transfer_payload_op_init(out_op);
    switch (format) {
    case OMC_SCAN_FMT_JPEG:
        *out_route = omc_transfer_route_static("jpeg:app11-jumbf");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER;
        out_op->jpeg_marker_code = 0xEBU;
        return 1;
    case OMC_SCAN_FMT_JXL:
        *out_route = omc_transfer_route_static("jxl:box-jumb");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_JXL_BOX;
        memcpy(out_op->box_type, "jumb", 4U);
        return 1;
    case OMC_SCAN_FMT_HEIF:
    case OMC_SCAN_FMT_AVIF:
    case OMC_SCAN_FMT_CR3:
        *out_route = omc_transfer_route_static("bmff:item-jumb");
        out_op->kind = OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM;
        out_op->bmff_item_type = omc_transfer_fourcc('j', 'u', 'm', 'b');
        return 1;
    default: break;
    }
    return 0;
}

static omc_transfer_status
omc_transfer_jxl_encoder_handoff_validate(
    const omc_jxl_encoder_handoff* handoff)
{
    if (handoff == (const omc_jxl_encoder_handoff*)0) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (handoff->contract_version != OMC_TRANSFER_CONTRACT_VERSION) {
        return OMC_TRANSFER_UNSUPPORTED;
    }
    if (!handoff->has_icc_profile) {
        if (handoff->icc_block_index != 0xFFFFFFFFU
            || handoff->icc_profile.data != (const omc_u8*)0
            || handoff->icc_profile.size != 0U) {
            return OMC_TRANSFER_MALFORMED;
        }
        return OMC_TRANSFER_OK;
    }
    if (handoff->icc_profile.data == (const omc_u8*)0
        || handoff->icc_profile.size == 0U) {
        return OMC_TRANSFER_MALFORMED;
    }
    return OMC_TRANSFER_OK;
}

static int
omc_transfer_jxl_encoder_handoff_read_u32le(const omc_u8* bytes,
                                            omc_size size, omc_size* io_off,
                                            omc_u32* out_value)
{
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_value == (omc_u32*)0) {
        return 0;
    }
    off = *io_off;
    if (off > size || size - off < 4U) {
        return 0;
    }
    *out_value = omc_transfer_read_u32le(bytes + off);
    *io_off = off + 4U;
    return 1;
}

static int
omc_transfer_jxl_encoder_handoff_read_u64le(const omc_u8* bytes,
                                            omc_size size, omc_size* io_off,
                                            omc_u64* out_value)
{
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_value == (omc_u64*)0) {
        return 0;
    }
    off = *io_off;
    if (off > size || size - off < 8U) {
        return 0;
    }
    *out_value = omc_transfer_read_u64le(bytes + off);
    *io_off = off + 8U;
    return 1;
}

static int
omc_transfer_jxl_encoder_handoff_read_blob(
    const omc_u8* bytes, omc_size size, omc_size* io_off,
    omc_const_bytes* out_blob)
{
    omc_u64 blob_size;
    omc_size off;

    if (bytes == (const omc_u8*)0 || io_off == (omc_size*)0
        || out_blob == (omc_const_bytes*)0) {
        return 0;
    }
    if (!omc_transfer_jxl_encoder_handoff_read_u64le(bytes, size, io_off,
                                                     &blob_size)) {
        return 0;
    }
    if (blob_size > (omc_u64)(~(omc_size)0)) {
        return 0;
    }
    off = *io_off;
    if ((omc_u64)off > (omc_u64)size
        || blob_size > (omc_u64)((omc_u64)size - (omc_u64)off)) {
        return 0;
    }
    out_blob->data = bytes + off;
    out_blob->size = (omc_size)blob_size;
    *io_off = off + (omc_size)blob_size;
    return 1;
}

OMC_API omc_status
omc_jxl_encoder_handoff_parse_view(
    const omc_u8* bytes, omc_size size, omc_jxl_encoder_handoff* out_handoff,
    omc_jxl_encoder_handoff_io_res* out_res)
{
    omc_size off;
    omc_u32 version;
    omc_u32 contract_version;
    omc_u32 icc_block_index;
    omc_u32 box_count;
    omc_u64 box_payload_bytes;
    omc_const_bytes icc_blob;
    omc_transfer_status validate_status;
    int has_icc;

    if (bytes == (const omc_u8*)0 || out_handoff == (omc_jxl_encoder_handoff*)0
        || out_res == (omc_jxl_encoder_handoff_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_jxl_encoder_handoff_io_res_init(out_res);
    omc_jxl_encoder_handoff_init(out_handoff);

    if (size < 12U) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (memcmp(bytes, k_omc_transfer_jxl_encoder_handoff_magic,
               sizeof(k_omc_transfer_jxl_encoder_handoff_magic))
        != 0) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    off = 8U;
    if (!omc_transfer_jxl_encoder_handoff_read_u32le(bytes, size, &off,
                                                     &version)) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (version != OMC_JXL_ENCODER_HANDOFF_VERSION) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }
    if (!omc_transfer_jxl_encoder_handoff_read_u32le(bytes, size, &off,
                                                     &contract_version)
        || off >= size) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    has_icc = bytes[off] != 0U;
    off += 1U;
    if (!omc_transfer_jxl_encoder_handoff_read_u32le(bytes, size, &off,
                                                     &icc_block_index)
        || !omc_transfer_jxl_encoder_handoff_read_u32le(bytes, size, &off,
                                                        &box_count)
        || !omc_transfer_jxl_encoder_handoff_read_u64le(bytes, size, &off,
                                                        &box_payload_bytes)
        || !omc_transfer_jxl_encoder_handoff_read_blob(bytes, size, &off,
                                                       &icc_blob)) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    out_handoff->contract_version = contract_version;
    out_handoff->has_icc_profile = has_icc;
    out_handoff->icc_block_index = icc_block_index;
    out_handoff->box_count = box_count;
    out_handoff->box_payload_bytes = box_payload_bytes;
    if (has_icc) {
        out_handoff->icc_profile = icc_blob;
    }

    if (off != size) {
        omc_jxl_encoder_handoff_init(out_handoff);
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (!has_icc && icc_blob.size != 0U) {
        omc_jxl_encoder_handoff_init(out_handoff);
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    validate_status = omc_transfer_jxl_encoder_handoff_validate(out_handoff);
    if (validate_status != OMC_TRANSFER_OK) {
        omc_jxl_encoder_handoff_init(out_handoff);
        out_res->status = validate_status;
        return OMC_STATUS_OK;
    }

    out_res->status = OMC_TRANSFER_OK;
    out_res->bytes = (omc_u64)size;
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_jxl_encoder_handoff_build(const omc_store* store,
                              const omc_jxl_encoder_handoff_opts* opts,
                              omc_arena* out_icc_profile,
                              omc_jxl_encoder_handoff* out_handoff)
{
    omc_jxl_encoder_handoff_opts local_opts;
    int has_profile;
    omc_transfer_status validate_status;
    omc_status status;

    if (store == (const omc_store*)0 || out_icc_profile == (omc_arena*)0
        || out_handoff == (omc_jxl_encoder_handoff*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_jxl_encoder_handoff_opts*)0) {
        omc_jxl_encoder_handoff_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_jxl_encoder_handoff_init(out_handoff);
    if (opts->box_payload_bytes > (omc_u64)(~(omc_size)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    has_profile = 0;
    omc_arena_reset(out_icc_profile);
    status = omc_transfer_build_icc_profile(store, out_icc_profile,
                                            &has_profile);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    out_handoff->box_count = opts->box_count;
    out_handoff->box_payload_bytes = opts->box_payload_bytes;
    if (has_profile) {
        out_handoff->has_icc_profile = 1;
        out_handoff->icc_block_index = opts->icc_block_index;
        out_handoff->icc_profile.data = out_icc_profile->data;
        out_handoff->icc_profile.size = out_icc_profile->size;
    }

    validate_status = omc_transfer_jxl_encoder_handoff_validate(out_handoff);
    if (validate_status != OMC_TRANSFER_OK) {
        omc_arena_reset(out_icc_profile);
        omc_jxl_encoder_handoff_init(out_handoff);
    }
    return validate_status == OMC_TRANSFER_OK ? OMC_STATUS_OK
                                              : OMC_STATUS_INVALID_ARGUMENT;
}

OMC_API omc_status
omc_jxl_encoder_handoff_serialize(
    const omc_jxl_encoder_handoff* handoff, omc_arena* out_bytes,
    omc_jxl_encoder_handoff_io_res* out_res)
{
    omc_transfer_status validate_status;
    omc_size needed;
    omc_u8 u32_buf[4];
    omc_u8 u64_buf[8];
    omc_u8 has_icc;
    omc_status status;

    if (handoff == (const omc_jxl_encoder_handoff*)0
        || out_bytes == (omc_arena*)0
        || out_res == (omc_jxl_encoder_handoff_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_jxl_encoder_handoff_io_res_init(out_res);
    omc_arena_reset(out_bytes);

    validate_status = omc_transfer_jxl_encoder_handoff_validate(handoff);
    if (validate_status != OMC_TRANSFER_OK) {
        out_res->status = validate_status;
        return OMC_STATUS_OK;
    }

    needed = 41U;
    if ((omc_u64)handoff->icc_profile.size
        > (omc_u64)((omc_u64)(~(omc_size)0) - (omc_u64)needed)) {
        return OMC_STATUS_OVERFLOW;
    }
    needed += handoff->icc_profile.size;
    status = omc_arena_reserve(out_bytes, needed);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    status = omc_transfer_append_bytes(
        out_bytes, k_omc_transfer_jxl_encoder_handoff_magic,
        sizeof(k_omc_transfer_jxl_encoder_handoff_magic));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    omc_transfer_store_u32le(u32_buf, OMC_JXL_ENCODER_HANDOFF_VERSION);
    status = omc_transfer_append_bytes(out_bytes, u32_buf, sizeof(u32_buf));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    omc_transfer_store_u32le(u32_buf, handoff->contract_version);
    status = omc_transfer_append_bytes(out_bytes, u32_buf, sizeof(u32_buf));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    has_icc = handoff->has_icc_profile ? (omc_u8)1U : (omc_u8)0U;
    status = omc_transfer_append_bytes(out_bytes, &has_icc, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    omc_transfer_store_u32le(u32_buf, handoff->icc_block_index);
    status = omc_transfer_append_bytes(out_bytes, u32_buf, sizeof(u32_buf));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    omc_transfer_store_u32le(u32_buf, handoff->box_count);
    status = omc_transfer_append_bytes(out_bytes, u32_buf, sizeof(u32_buf));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    omc_transfer_store_u64le(u64_buf, handoff->box_payload_bytes);
    status = omc_transfer_append_bytes(out_bytes, u64_buf, sizeof(u64_buf));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    omc_transfer_store_u64le(u64_buf, (omc_u64)handoff->icc_profile.size);
    status = omc_transfer_append_bytes(out_bytes, u64_buf, sizeof(u64_buf));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    if (handoff->icc_profile.size != 0U) {
        status = omc_transfer_append_bytes(out_bytes, handoff->icc_profile.data,
                                           handoff->icc_profile.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    out_res->status = OMC_TRANSFER_OK;
    out_res->bytes = (omc_u64)out_bytes->size;
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_jxl_encoder_handoff_deserialize(
    const omc_u8* bytes, omc_size size, omc_arena* out_icc_profile,
    omc_jxl_encoder_handoff* out_handoff,
    omc_jxl_encoder_handoff_io_res* out_res)
{
    omc_jxl_encoder_handoff parsed;
    omc_byte_ref icc_ref;
    omc_status status;

    if (bytes == (const omc_u8*)0 || out_icc_profile == (omc_arena*)0
        || out_handoff == (omc_jxl_encoder_handoff*)0
        || out_res == (omc_jxl_encoder_handoff_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_jxl_encoder_handoff_io_res_init(out_res);
    omc_jxl_encoder_handoff_init(out_handoff);
    omc_arena_reset(out_icc_profile);

    status = omc_jxl_encoder_handoff_parse_view(bytes, size, &parsed, out_res);
    if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
        return status;
    }

    *out_handoff = parsed;
    if (parsed.has_icc_profile) {
        status = omc_arena_append(out_icc_profile, parsed.icc_profile.data,
                                  parsed.icc_profile.size,
                                  &icc_ref);
        if (status != OMC_STATUS_OK) {
            omc_jxl_encoder_handoff_init(out_handoff);
            omc_arena_reset(out_icc_profile);
            return status;
        }
        out_handoff->icc_profile = omc_arena_view(out_icc_profile, icc_ref);
    }
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_transfer_payload_batch_build(const omc_store* store,
                                 const omc_transfer_payload_build_opts* opts,
                                 omc_arena* out_storage,
                                 omc_transfer_payload_batch* out_batch,
                                 omc_transfer_payload_io_res* out_res)
{
    omc_transfer_payload_build_opts local_opts;
    omc_arena exif_payload;
    omc_arena xmp_payload;
    omc_arena icc_profile;
    omc_arena icc_payload;
    omc_arena iptc_iim;
    omc_arena iptc_payload;
    omc_arena jpeg_icc_payload;
    omc_arena jumbf_payload_storage;
    omc_arena jpeg_jumbf_payload;
    omc_xmp_embed_opts xmp_opts;
    omc_xmp_dump_res xmp_res;
    omc_byte_ref entries_ref;
    omc_byte_ref* jumbf_payload_refs;
    omc_transfer_payload_op op;
    omc_const_bytes route;
    omc_status status;
    omc_exif_write_res exif_res;
    omc_transfer_status validate_status;
    omc_u32 payload_count;
    omc_u32 payload_index;
    omc_u32 jumbf_payload_root_count;
    omc_u32 jpeg_jumbf_segment_count;
    int have_exif;
    int have_xmp;
    int have_icc;
    int have_iptc;
    int have_jumbf;
    int iptc_supported;
    int has_profile;
    omc_size jpeg_icc_chunk_count;

    if (store == (const omc_store*)0 || out_storage == (omc_arena*)0
        || out_batch == (omc_transfer_payload_batch*)0
        || out_res == (omc_transfer_payload_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_transfer_payload_build_opts*)0) {
        omc_transfer_payload_build_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_transfer_payload_batch_init(out_batch);
    omc_transfer_payload_io_res_init(out_res);
    omc_arena_reset(out_storage);
    out_batch->target_format = opts->format;
    out_batch->skip_empty_payloads = opts->skip_empty_payloads != 0;
    out_batch->stop_on_error = opts->stop_on_error != 0;

    omc_arena_init(&exif_payload);
    omc_arena_init(&xmp_payload);
    omc_arena_init(&icc_profile);
    omc_arena_init(&icc_payload);
    omc_arena_init(&iptc_iim);
    omc_arena_init(&iptc_payload);
    omc_arena_init(&jpeg_icc_payload);
    omc_arena_init(&jumbf_payload_storage);
    omc_arena_init(&jpeg_jumbf_payload);

    have_exif = 0;
    have_xmp = 0;
    have_icc = 0;
    have_iptc = 0;
    have_jumbf = 0;
    iptc_supported = 1;
    has_profile = 0;
    jpeg_icc_chunk_count = 0U;
    jumbf_payload_refs = (omc_byte_ref*)0;
    jumbf_payload_root_count = 0U;
    jpeg_jumbf_segment_count = 0U;

    if (opts->include_exif) {
        status = omc_exif_write_build_transfer_payload(store, &exif_payload,
                                                       opts->format,
                                                       &exif_res);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        out_res->status = omc_transfer_status_from_exif(exif_res.status);
        if (exif_res.status != OMC_EXIF_WRITE_OK) {
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        have_exif = exif_payload.size != 0U;
    }

    if (opts->include_xmp) {
        omc_xmp_embed_opts_init(&xmp_opts);
        xmp_opts.format = opts->format;
        xmp_opts.packet = opts->xmp_packet;
        status = omc_xmp_embed_payload_arena(store, &xmp_payload, &xmp_opts,
                                             &xmp_res);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        out_res->status = omc_transfer_status_from_dump(xmp_res.status);
        if (xmp_res.status != OMC_XMP_DUMP_OK) {
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        have_xmp = xmp_payload.size != 0U;
    }

    if (opts->include_icc) {
        status = omc_transfer_build_icc_profile(store, &icc_profile,
                                                &has_profile);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        if (has_profile) {
            have_icc = 1;
            if (opts->format == OMC_SCAN_FMT_PNG) {
                status = omc_transfer_build_png_iccp_payload(
                    icc_profile.data, icc_profile.size, &icc_payload,
                    &out_res->status);
                if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
                    if (status == OMC_STATUS_OK) {
                        status = OMC_STATUS_OK;
                    }
                    goto omc_transfer_payload_build_done;
                }
            } else if (opts->format == OMC_SCAN_FMT_JP2) {
                omc_arena_reset(&icc_payload);
                status = omc_transfer_append_jp2_colr_icc_box(
                    &icc_payload, icc_profile.data, icc_profile.size);
                if (status != OMC_STATUS_OK) {
                    goto omc_transfer_payload_build_done;
                }
            } else if (opts->format == OMC_SCAN_FMT_HEIF
                       || opts->format == OMC_SCAN_FMT_AVIF
                       || opts->format == OMC_SCAN_FMT_CR3) {
                omc_arena_reset(&icc_payload);
                status = omc_transfer_append_bytes(&icc_payload, "prof", 4U);
                if (status != OMC_STATUS_OK) {
                    goto omc_transfer_payload_build_done;
                }
                status = omc_transfer_append_bytes(&icc_payload,
                                                   icc_profile.data,
                                                   icc_profile.size);
                if (status != OMC_STATUS_OK) {
                    goto omc_transfer_payload_build_done;
                }
            } else if (opts->format == OMC_SCAN_FMT_JPEG) {
                jpeg_icc_chunk_count
                    = (icc_profile.size + 65519U - 1U) / 65519U;
                if (jpeg_icc_chunk_count == 0U
                    || jpeg_icc_chunk_count > 255U) {
                    out_res->status = OMC_TRANSFER_LIMIT;
                    status = OMC_STATUS_OK;
                    goto omc_transfer_payload_build_done;
                }
            }
        }
    }

    if (opts->include_iptc) {
        status = omc_transfer_build_iptc_iim(store, &iptc_iim, &have_iptc,
                                             &iptc_supported);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        if (have_iptc && !iptc_supported) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        if (have_iptc && opts->format == OMC_SCAN_FMT_JPEG) {
            status = omc_transfer_build_photoshop_iptc_irb(
                iptc_iim.data, iptc_iim.size, &iptc_payload);
            if (status != OMC_STATUS_OK) {
                goto omc_transfer_payload_build_done;
            }
        }
    }

    if (opts->include_jumbf) {
        status = omc_transfer_build_projected_jumbf_payloads(
            store, &jumbf_payload_storage, &jumbf_payload_refs,
            &jumbf_payload_root_count);
        if (status == OMC_STATUS_STATE) {
            out_res->status
                = omc_transfer_jumbf_projected_status_from_store_status(status);
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        have_jumbf = jumbf_payload_root_count != 0U;
        if (have_jumbf && opts->format == OMC_SCAN_FMT_JPEG) {
            omc_u32 i;

            for (i = 0U; i < jumbf_payload_root_count; ++i) {
                omc_const_bytes logical_payload;
                omc_u32 segment_count;

                logical_payload = omc_arena_view(&jumbf_payload_storage,
                                                 jumbf_payload_refs[i]);
                if (!omc_transfer_count_jpeg_jumbf_segments(
                        logical_payload.data, logical_payload.size,
                        &segment_count)) {
                    out_res->status = OMC_TRANSFER_UNSUPPORTED;
                    status = OMC_STATUS_OK;
                    goto omc_transfer_payload_build_done;
                }
                if ((omc_u64)jpeg_jumbf_segment_count
                    + (omc_u64)segment_count
                    > (omc_u64)(~(omc_u32)0)) {
                    out_res->status = OMC_TRANSFER_LIMIT;
                    status = OMC_STATUS_OK;
                    goto omc_transfer_payload_build_done;
                }
                jpeg_jumbf_segment_count += segment_count;
            }
        }
    }

    payload_count = 0U;
    if (have_exif) {
        payload_count += 1U;
    }
    if (have_xmp) {
        payload_count += 1U;
    }
    if (have_icc) {
        if (opts->format == OMC_SCAN_FMT_JPEG) {
            payload_count += (omc_u32)jpeg_icc_chunk_count;
        } else {
            payload_count += 1U;
        }
    }
    if (have_iptc && (opts->format == OMC_SCAN_FMT_JPEG
                      || opts->format == OMC_SCAN_FMT_TIFF
                      || opts->format == OMC_SCAN_FMT_DNG)) {
        payload_count += 1U;
    }
    if (have_jumbf) {
        if (opts->format == OMC_SCAN_FMT_JPEG) {
            payload_count += jpeg_jumbf_segment_count;
        } else {
            payload_count += jumbf_payload_root_count;
        }
    }

    if (payload_count != 0U) {
        omc_transfer_payload* zero_entries;

        zero_entries = (omc_transfer_payload*)calloc(
            payload_count, sizeof(omc_transfer_payload));
        if (zero_entries == (omc_transfer_payload*)0) {
            status = OMC_STATUS_NO_MEMORY;
            goto omc_transfer_payload_build_done;
        }
        status = omc_arena_append(out_storage, zero_entries,
                                  payload_count
                                      * sizeof(omc_transfer_payload),
                                  &entries_ref);
        free(zero_entries);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
    } else {
        entries_ref.offset = 0U;
        entries_ref.size = 0U;
    }

    payload_index = 0U;
    if (have_exif) {
        if (!omc_transfer_payload_make_exif_op(opts->format, &route, &op)) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        status = omc_transfer_payload_store_entry(
            out_storage, entries_ref, payload_index,
            OMC_TRANSFER_SEMANTIC_EXIF, route, &op, exif_payload.data,
            exif_payload.size);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        payload_index += 1U;
    }

    if (have_xmp) {
        if (!omc_transfer_payload_make_xmp_op(opts->format, &route, &op)) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        status = omc_transfer_payload_store_entry(
            out_storage, entries_ref, payload_index, OMC_TRANSFER_SEMANTIC_XMP,
            route, &op, xmp_payload.data, xmp_payload.size);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        payload_index += 1U;
    }

    if (have_icc) {
        if (!omc_transfer_payload_make_icc_op(opts->format, &route, &op)) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        if (opts->format == OMC_SCAN_FMT_JPEG) {
            omc_size chunk_off;
            omc_u8 chunk_index;
            omc_u8 chunk_count_u8;

            chunk_off = 0U;
            chunk_index = 0U;
            chunk_count_u8 = (omc_u8)jpeg_icc_chunk_count;
            while (chunk_index < chunk_count_u8) {
                omc_size remaining;
                omc_size chunk_size;

                remaining = icc_profile.size - chunk_off;
                chunk_size = remaining > 65519U ? 65519U : remaining;
                status = omc_transfer_build_jpeg_icc_payload(
                    icc_profile.data + chunk_off, chunk_size,
                    (omc_u8)(chunk_index + 1U), chunk_count_u8,
                    &jpeg_icc_payload);
                if (status != OMC_STATUS_OK) {
                    goto omc_transfer_payload_build_done;
                }
                status = omc_transfer_payload_store_entry(
                    out_storage, entries_ref, payload_index,
                    OMC_TRANSFER_SEMANTIC_ICC, route, &op,
                    jpeg_icc_payload.data, jpeg_icc_payload.size);
                if (status != OMC_STATUS_OK) {
                    goto omc_transfer_payload_build_done;
                }
                payload_index += 1U;
                chunk_off += chunk_size;
                chunk_index = (omc_u8)(chunk_index + 1U);
            }
        } else {
            const omc_u8* payload_data;
            omc_size payload_size;

            payload_data = icc_profile.data;
            payload_size = icc_profile.size;
            if (opts->format == OMC_SCAN_FMT_PNG || opts->format == OMC_SCAN_FMT_JP2
                || opts->format == OMC_SCAN_FMT_HEIF
                || opts->format == OMC_SCAN_FMT_AVIF
                || opts->format == OMC_SCAN_FMT_CR3) {
                payload_data = icc_payload.data;
                payload_size = icc_payload.size;
            }
            status = omc_transfer_payload_store_entry(
                out_storage, entries_ref, payload_index,
                OMC_TRANSFER_SEMANTIC_ICC, route, &op, payload_data,
                payload_size);
            if (status != OMC_STATUS_OK) {
                goto omc_transfer_payload_build_done;
            }
            payload_index += 1U;
        }
    }

    if (have_iptc && (opts->format == OMC_SCAN_FMT_JPEG
                      || opts->format == OMC_SCAN_FMT_TIFF
                      || opts->format == OMC_SCAN_FMT_DNG)) {
        const omc_u8* payload_data;
        omc_size payload_size;

        if (!omc_transfer_payload_make_iptc_op(opts->format, &route, &op)) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        if (opts->format == OMC_SCAN_FMT_JPEG) {
            payload_data = iptc_payload.data;
            payload_size = iptc_payload.size;
        } else {
            payload_data = iptc_iim.data;
            payload_size = iptc_iim.size;
        }
        status = omc_transfer_payload_store_entry(
            out_storage, entries_ref, payload_index,
            OMC_TRANSFER_SEMANTIC_IPTC, route, &op, payload_data,
            payload_size);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_payload_build_done;
        }
        payload_index += 1U;
    }

    if (have_jumbf) {
        if (!omc_transfer_payload_make_jumbf_op(opts->format, &route, &op)) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            status = OMC_STATUS_OK;
            goto omc_transfer_payload_build_done;
        }
        if (opts->format == OMC_SCAN_FMT_JPEG) {
            omc_u32 i;

            for (i = 0U; i < jumbf_payload_root_count; ++i) {
                omc_const_bytes logical_payload;
                omc_u32 segment_count;
                omc_u32 seq_no;

                logical_payload = omc_arena_view(&jumbf_payload_storage,
                                                 jumbf_payload_refs[i]);
                if (!omc_transfer_count_jpeg_jumbf_segments(
                        logical_payload.data, logical_payload.size,
                        &segment_count)) {
                    out_res->status = OMC_TRANSFER_UNSUPPORTED;
                    status = OMC_STATUS_OK;
                    goto omc_transfer_payload_build_done;
                }
                for (seq_no = 1U; seq_no <= segment_count; ++seq_no) {
                    status = omc_transfer_build_jpeg_jumbf_segment_payload(
                        logical_payload.data, logical_payload.size, seq_no,
                        segment_count, &jpeg_jumbf_payload);
                    if (status != OMC_STATUS_OK) {
                        if (status == OMC_STATUS_STATE) {
                            out_res->status = OMC_TRANSFER_UNSUPPORTED;
                            status = OMC_STATUS_OK;
                        }
                        goto omc_transfer_payload_build_done;
                    }
                    status = omc_transfer_payload_store_entry(
                        out_storage, entries_ref, payload_index,
                        OMC_TRANSFER_SEMANTIC_JUMBF, route, &op,
                        jpeg_jumbf_payload.data, jpeg_jumbf_payload.size);
                    if (status != OMC_STATUS_OK) {
                        goto omc_transfer_payload_build_done;
                    }
                    payload_index += 1U;
                }
            }
        } else if (opts->format == OMC_SCAN_FMT_JXL) {
            omc_u32 i;

            for (i = 0U; i < jumbf_payload_root_count; ++i) {
                omc_const_bytes logical_payload;
                omc_transfer_bmff_box root_box;

                logical_payload = omc_arena_view(&jumbf_payload_storage,
                                                 jumbf_payload_refs[i]);
                if (!omc_transfer_parse_bmff_box(logical_payload.data,
                                                 logical_payload.size, 0U,
                                                 (omc_u64)logical_payload.size,
                                                 &root_box)
                    || root_box.offset != 0U
                    || root_box.size != (omc_u64)logical_payload.size
                    || root_box.type
                           != omc_transfer_fourcc('j', 'u', 'm', 'b')) {
                    out_res->status = OMC_TRANSFER_UNSUPPORTED;
                    status = OMC_STATUS_OK;
                    goto omc_transfer_payload_build_done;
                }
                status = omc_transfer_payload_store_entry(
                    out_storage, entries_ref, payload_index,
                    OMC_TRANSFER_SEMANTIC_JUMBF, route, &op,
                    logical_payload.data + (omc_size)root_box.header_size,
                    logical_payload.size - (omc_size)root_box.header_size);
                if (status != OMC_STATUS_OK) {
                    goto omc_transfer_payload_build_done;
                }
                payload_index += 1U;
            }
        } else {
            omc_u32 i;

            for (i = 0U; i < jumbf_payload_root_count; ++i) {
                omc_const_bytes logical_payload;
                omc_transfer_bmff_box root_box;

                logical_payload = omc_arena_view(&jumbf_payload_storage,
                                                 jumbf_payload_refs[i]);
                if (!omc_transfer_parse_bmff_box(logical_payload.data,
                                                 logical_payload.size, 0U,
                                                 (omc_u64)logical_payload.size,
                                                 &root_box)
                    || root_box.offset != 0U
                    || root_box.size != (omc_u64)logical_payload.size
                    || root_box.type
                           != omc_transfer_fourcc('j', 'u', 'm', 'b')) {
                    out_res->status = OMC_TRANSFER_UNSUPPORTED;
                    status = OMC_STATUS_OK;
                    goto omc_transfer_payload_build_done;
                }
                status = omc_transfer_payload_store_entry(
                    out_storage, entries_ref, payload_index,
                    OMC_TRANSFER_SEMANTIC_JUMBF, route, &op,
                    logical_payload.data, logical_payload.size);
                if (status != OMC_STATUS_OK) {
                    goto omc_transfer_payload_build_done;
                }
                payload_index += 1U;
            }
        }
    }

    out_batch->payload_count = payload_index;
    if (payload_index != 0U) {
        out_batch->payloads = (const omc_transfer_payload*)omc_arena_view_mut(
                                  out_storage, entries_ref)
                                  .data;
    }
    validate_status = omc_transfer_payload_validate_batch(out_batch);
    out_res->status = validate_status;
    out_res->bytes = (omc_u64)out_storage->size;
    out_res->payload_count = payload_index;
    status = OMC_STATUS_OK;

omc_transfer_payload_build_done:
    if (status == OMC_STATUS_OK && out_res->status != OMC_TRANSFER_OK) {
        omc_transfer_payload_batch_init(out_batch);
        omc_arena_reset(out_storage);
        out_batch->target_format = opts->format;
        out_batch->skip_empty_payloads = opts->skip_empty_payloads != 0;
        out_batch->stop_on_error = opts->stop_on_error != 0;
        out_res->bytes = 0U;
        out_res->payload_count = 0U;
    }

    omc_arena_fini(&exif_payload);
    omc_arena_fini(&jpeg_icc_payload);
    free(jumbf_payload_refs);
    omc_arena_fini(&jpeg_jumbf_payload);
    omc_arena_fini(&jumbf_payload_storage);
    omc_arena_fini(&iptc_payload);
    omc_arena_fini(&iptc_iim);
    omc_arena_fini(&icc_payload);
    omc_arena_fini(&icc_profile);
    omc_arena_fini(&xmp_payload);
    return status;
}

OMC_API omc_status
omc_transfer_payload_batch_serialize(
    const omc_transfer_payload_batch* batch, omc_arena* out_bytes,
    omc_transfer_payload_io_res* out_res)
{
    omc_transfer_status validate_status;
    omc_u8 target_code;
    omc_u8 emit_u8;
    omc_u8 i8_buf[8];
    omc_u32 i;
    omc_status status;

    if (batch == (const omc_transfer_payload_batch*)0
        || out_bytes == (omc_arena*)0
        || out_res == (omc_transfer_payload_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_payload_io_res_init(out_res);
    omc_arena_reset(out_bytes);

    validate_status = omc_transfer_payload_validate_batch(batch);
    out_res->status = validate_status;
    if (validate_status != OMC_TRANSFER_OK) {
        return OMC_STATUS_OK;
    }
    if (!omc_transfer_payload_target_code_from_format(batch->target_format,
                                                      &target_code)) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    status = omc_transfer_append_bytes(out_bytes,
                                       k_omc_transfer_payload_batch_magic,
                                       sizeof(k_omc_transfer_payload_batch_magic));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u32le(
        out_bytes, OMC_TRANSFER_PAYLOAD_BATCH_VERSION);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u32le(out_bytes,
                                               batch->contract_version);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out_bytes, &target_code, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    emit_u8 = batch->skip_empty_payloads ? (omc_u8)1U : (omc_u8)0U;
    status = omc_transfer_append_bytes(out_bytes, &emit_u8, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    emit_u8 = batch->stop_on_error ? (omc_u8)1U : (omc_u8)0U;
    status = omc_transfer_append_bytes(out_bytes, &emit_u8, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u32le(out_bytes,
                                               batch->payload_count);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    for (i = 0U; i < batch->payload_count; ++i) {
        const omc_transfer_payload* payload;

        payload = &batch->payloads[i];
        status = omc_transfer_payload_append_blob_le(out_bytes,
                                                     payload->route.data,
                                                     payload->route.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        emit_u8 = (omc_u8)payload->op.kind;
        status = omc_transfer_append_bytes(out_bytes, &emit_u8, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u32le(
            out_bytes, payload->op.block_index);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u64le(
            out_bytes, payload->op.payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u64le(
            out_bytes, payload->op.serialized_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_append_bytes(out_bytes,
                                           &payload->op.jpeg_marker_code, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u16le(out_bytes,
                                                   payload->op.tiff_tag);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_append_bytes(out_bytes, payload->op.box_type,
                                           4U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_append_bytes(out_bytes, payload->op.chunk_type,
                                           4U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u32le(
            out_bytes, payload->op.bmff_item_type);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u32le(
            out_bytes, payload->op.bmff_property_type);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u32le(
            out_bytes, payload->op.bmff_property_subtype);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        i8_buf[0] = payload->op.bmff_mime_xmp ? (omc_u8)1U : (omc_u8)0U;
        status = omc_transfer_append_bytes(out_bytes, i8_buf, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        i8_buf[0] = payload->op.compress ? (omc_u8)1U : (omc_u8)0U;
        status = omc_transfer_append_bytes(out_bytes, i8_buf, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_blob_le(out_bytes,
                                                     payload->payload.data,
                                                     payload->payload.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    out_res->status = OMC_TRANSFER_OK;
    out_res->bytes = (omc_u64)out_bytes->size;
    out_res->payload_count = batch->payload_count;
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_transfer_payload_batch_deserialize(
    const omc_u8* bytes, omc_size size, omc_arena* out_storage,
    omc_transfer_payload_batch* out_batch,
    omc_transfer_payload_io_res* out_res)
{
    omc_size off;
    omc_u32 version;
    omc_u32 contract_version;
    omc_u8 target_code;
    omc_u8 skip_empty;
    omc_u8 stop_on_error;
    omc_u32 count;
    omc_byte_ref entries_ref;
    omc_status status;
    omc_u32 i;
    omc_transfer_status validate_status;

    if (bytes == (const omc_u8*)0 || out_storage == (omc_arena*)0
        || out_batch == (omc_transfer_payload_batch*)0
        || out_res == (omc_transfer_payload_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_payload_batch_init(out_batch);
    omc_transfer_payload_io_res_init(out_res);
    omc_arena_reset(out_storage);

    if (size < 8U) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (memcmp(bytes, k_omc_transfer_payload_batch_magic,
               sizeof(k_omc_transfer_payload_batch_magic))
        != 0) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    off = 8U;
    if (!omc_transfer_payload_read_u32le(bytes, size, &off, &version)
        || version != OMC_TRANSFER_PAYLOAD_BATCH_VERSION
        || !omc_transfer_payload_read_u32le(bytes, size, &off,
                                            &contract_version)
        || !omc_transfer_payload_read_u8(bytes, size, &off, &target_code)
        || !omc_transfer_payload_read_u8(bytes, size, &off, &skip_empty)
        || skip_empty > 1U
        || !omc_transfer_payload_read_u8(bytes, size, &off, &stop_on_error)
        || stop_on_error > 1U
        || !omc_transfer_payload_read_u32le(bytes, size, &off, &count)) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (!omc_transfer_payload_format_from_target_code(target_code,
                                                      &out_batch->target_format)) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    out_batch->contract_version = contract_version;
    out_batch->skip_empty_payloads = skip_empty != 0U;
    out_batch->stop_on_error = stop_on_error != 0U;

    if (count != 0U) {
        omc_transfer_payload* zero_entries;

        zero_entries = (omc_transfer_payload*)calloc(
            count, sizeof(omc_transfer_payload));
        if (zero_entries == (omc_transfer_payload*)0) {
            return OMC_STATUS_NO_MEMORY;
        }
        status = omc_arena_append(out_storage, zero_entries,
                                  count * sizeof(omc_transfer_payload),
                                  &entries_ref);
        free(zero_entries);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    } else {
        entries_ref.offset = 0U;
        entries_ref.size = 0U;
    }

    for (i = 0U; i < count; ++i) {
        omc_const_bytes route_view;
        omc_const_bytes payload_view;
        omc_byte_ref route_ref;
        omc_transfer_payload_op op;
        omc_u8 op_kind;
        omc_u8 bool_u8;
        omc_transfer_semantic_kind semantic_kind;
        omc_transfer_payload* entries;

        omc_transfer_payload_op_init(&op);
        if (!omc_transfer_payload_read_blob(bytes, size, &off, &route_view)
            || !omc_transfer_payload_read_u8(bytes, size, &off, &op_kind)
            || !omc_transfer_payload_read_u32le(bytes, size, &off,
                                                &op.block_index)
            || !omc_transfer_payload_read_u64le(bytes, size, &off,
                                                &op.payload_size)
            || !omc_transfer_payload_read_u64le(bytes, size, &off,
                                                &op.serialized_size)
            || !omc_transfer_payload_read_u8(bytes, size, &off,
                                             &op.jpeg_marker_code)
            || !omc_transfer_payload_read_u16le(bytes, size, &off,
                                                &op.tiff_tag)
            || off > size || size - off < 4U) {
            out_res->status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        memcpy(op.box_type, bytes + off, 4U);
        off += 4U;
        if (off > size || size - off < 4U) {
            out_res->status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        memcpy(op.chunk_type, bytes + off, 4U);
        off += 4U;
        if (!omc_transfer_payload_read_u32le(bytes, size, &off,
                                             &op.bmff_item_type)
            || !omc_transfer_payload_read_u32le(bytes, size, &off,
                                                &op.bmff_property_type)
            || !omc_transfer_payload_read_u32le(bytes, size, &off,
                                                &op.bmff_property_subtype)
            || !omc_transfer_payload_read_u8(bytes, size, &off, &bool_u8)
            || bool_u8 > 1U) {
            out_res->status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        op.bmff_mime_xmp = bool_u8 != 0U;
        if (!omc_transfer_payload_read_u8(bytes, size, &off, &bool_u8)
            || bool_u8 > 1U
            || !omc_transfer_payload_read_blob(bytes, size, &off,
                                               &payload_view)) {
            out_res->status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        op.compress = bool_u8 != 0U;
        op.kind = (omc_transfer_payload_op_kind)op_kind;

        status = omc_arena_append(out_storage, route_view.data, route_view.size,
                                  &route_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        route_view = omc_arena_view(out_storage, route_ref);
        semantic_kind = omc_transfer_classify_route_semantic_kind(
            route_view.data, route_view.size);
        status = omc_transfer_payload_store_entry(
            out_storage, entries_ref, i, semantic_kind, route_view, &op,
            payload_view.data, payload_view.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        entries = omc_transfer_payload_entry_ptr(out_storage, entries_ref);
        if (entries == (omc_transfer_payload*)0) {
            return OMC_STATUS_INVALID_ARGUMENT;
        }
        entries[i].route = omc_arena_view(out_storage, route_ref);
        entries[i].op.block_index = op.block_index;
        entries[i].op.serialized_size = op.serialized_size;
        entries[i].op.payload_size = op.payload_size;
    }

    if (off != size) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    out_batch->payload_count = count;
    if (count != 0U) {
        out_batch->payloads
            = (const omc_transfer_payload*)omc_arena_view_mut(out_storage,
                                                              entries_ref)
                  .data;
    }

    validate_status = omc_transfer_payload_validate_batch(out_batch);
    out_res->status = validate_status;
    if (validate_status != OMC_TRANSFER_OK) {
        omc_transfer_payload_batch_init(out_batch);
        omc_arena_reset(out_storage);
        return OMC_STATUS_OK;
    }

    out_res->bytes = (omc_u64)size;
    out_res->payload_count = count;
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_transfer_payload_batch_replay(
    const omc_transfer_payload_batch* batch,
    const omc_transfer_payload_replay_callbacks* callbacks,
    omc_transfer_payload_replay_res* out_res)
{
    omc_transfer_status validate_status;
    omc_u32 i;

    if (batch == (const omc_transfer_payload_batch*)0
        || callbacks == (const omc_transfer_payload_replay_callbacks*)0
        || out_res == (omc_transfer_payload_replay_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_payload_replay_res_init(out_res);
    validate_status = omc_transfer_payload_validate_batch(batch);
    out_res->status = validate_status;
    if (validate_status != OMC_TRANSFER_OK) {
        return OMC_STATUS_OK;
    }
    if (callbacks->emit_payload == (omc_transfer_payload_emit_fn)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (callbacks->begin_batch != (omc_transfer_payload_begin_batch_fn)0) {
        out_res->status = callbacks->begin_batch(callbacks->user,
                                                 batch->target_format,
                                                 batch->payload_count);
        if (out_res->status != OMC_TRANSFER_OK) {
            return OMC_STATUS_OK;
        }
    }

    for (i = 0U; i < batch->payload_count; ++i) {
        out_res->status = callbacks->emit_payload(callbacks->user,
                                                  &batch->payloads[i]);
        if (out_res->status != OMC_TRANSFER_OK) {
            out_res->failed_payload_index = i;
            return OMC_STATUS_OK;
        }
        out_res->replayed = i + 1U;
    }

    if (callbacks->end_batch != (omc_transfer_payload_end_batch_fn)0) {
        out_res->status = callbacks->end_batch(callbacks->user,
                                               batch->target_format);
        return OMC_STATUS_OK;
    }

    out_res->status = OMC_TRANSFER_OK;
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_transfer_package_batch_build(
    const omc_store* store, const omc_transfer_package_build_opts* opts,
    omc_arena* out_storage, omc_transfer_package_batch* out_batch,
    omc_transfer_package_io_res* out_res)
{
    omc_transfer_package_build_opts local_opts;
    omc_transfer_payload_build_opts payload_opts;
    omc_transfer_payload_batch payload_batch;
    omc_transfer_payload_io_res payload_res;
    omc_arena payload_storage;
    omc_arena chunk_bytes;
    omc_transfer_status validate_status;
    omc_byte_ref entries_ref;
    omc_status status;
    omc_u32 i;
    omc_u64 output_offset;

    if (store == (const omc_store*)0 || out_storage == (omc_arena*)0
        || out_batch == (omc_transfer_package_batch*)0
        || out_res == (omc_transfer_package_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_transfer_package_build_opts*)0) {
        omc_transfer_package_build_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_transfer_package_batch_init(out_batch);
    omc_transfer_package_io_res_init(out_res);
    omc_arena_reset(out_storage);
    omc_arena_init(&payload_storage);
    omc_arena_init(&chunk_bytes);

    omc_transfer_payload_build_opts_init(&payload_opts);
    payload_opts.format = opts->format;
    payload_opts.include_exif = opts->include_exif;
    payload_opts.include_xmp = opts->include_xmp;
    payload_opts.include_icc = opts->include_icc;
    payload_opts.include_iptc = opts->include_iptc;
    payload_opts.include_jumbf = opts->include_jumbf;
    payload_opts.skip_empty_payloads = opts->skip_empty_chunks;
    payload_opts.stop_on_error = opts->stop_on_error;
    payload_opts.xmp_packet = opts->xmp_packet;

    omc_transfer_payload_batch_init(&payload_batch);
    omc_transfer_payload_io_res_init(&payload_res);
    status = omc_transfer_payload_batch_build(store, &payload_opts,
                                              &payload_storage, &payload_batch,
                                              &payload_res);
    if (status != OMC_STATUS_OK) {
        goto omc_transfer_package_batch_build_done;
    }
    out_res->status = payload_res.status;
    if (payload_res.status != OMC_TRANSFER_OK) {
        status = OMC_STATUS_OK;
        goto omc_transfer_package_batch_build_done;
    }

    if (payload_batch.payload_count != 0U) {
        omc_transfer_package_chunk* zero_entries;

        zero_entries = (omc_transfer_package_chunk*)calloc(
            payload_batch.payload_count, sizeof(omc_transfer_package_chunk));
        if (zero_entries == (omc_transfer_package_chunk*)0) {
            status = OMC_STATUS_NO_MEMORY;
            goto omc_transfer_package_batch_build_done;
        }
        status = omc_arena_append(out_storage, zero_entries,
                                  payload_batch.payload_count
                                      * sizeof(omc_transfer_package_chunk),
                                  &entries_ref);
        free(zero_entries);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_package_batch_build_done;
        }
    } else {
        entries_ref.offset = 0U;
        entries_ref.size = 0U;
    }

    output_offset = 0U;
    for (i = 0U; i < payload_batch.payload_count; ++i) {
        omc_transfer_package_chunk_kind chunk_kind;
        const omc_transfer_payload* payload;

        payload = &payload_batch.payloads[i];
        status = omc_transfer_package_build_chunk_bytes(
            payload, &chunk_kind, &chunk_bytes);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_package_batch_build_done;
        }
        status = omc_transfer_package_store_entry(
            out_storage, entries_ref, i, chunk_kind, payload->route,
            output_offset, 0U, 0xFFFFFFFFU,
            chunk_kind == OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT
                ? payload->op.jpeg_marker_code
                : (omc_u8)0U,
            chunk_bytes.data, chunk_bytes.size);
        if (status != OMC_STATUS_OK) {
            goto omc_transfer_package_batch_build_done;
        }
        if ((omc_u64)chunk_bytes.size
            > (omc_u64)(~(omc_u64)0) - output_offset) {
            status = OMC_STATUS_OVERFLOW;
            goto omc_transfer_package_batch_build_done;
        }
        output_offset += (omc_u64)chunk_bytes.size;
    }

    out_batch->contract_version = OMC_TRANSFER_CONTRACT_VERSION;
    out_batch->target_format = payload_batch.target_format;
    out_batch->input_size = 0U;
    out_batch->output_size = output_offset;
    out_batch->chunk_count = payload_batch.payload_count;
    if (payload_batch.payload_count != 0U) {
        out_batch->chunks = (const omc_transfer_package_chunk*)
                                omc_arena_view_mut(out_storage, entries_ref)
                                    .data;
    }

    validate_status = omc_transfer_package_validate_batch(out_batch);
    out_res->status = validate_status;
    if (validate_status != OMC_TRANSFER_OK) {
        status = OMC_STATUS_OK;
        omc_transfer_package_batch_init(out_batch);
        goto omc_transfer_package_batch_build_done;
    }
    out_res->bytes = output_offset;
    out_res->chunk_count = out_batch->chunk_count;
    status = OMC_STATUS_OK;

omc_transfer_package_batch_build_done:
    omc_arena_fini(&chunk_bytes);
    omc_arena_fini(&payload_storage);
    if (status != OMC_STATUS_OK) {
        omc_transfer_package_batch_init(out_batch);
        omc_transfer_package_io_res_init(out_res);
    }
    return status;
}

OMC_API omc_status
omc_transfer_package_batch_serialize(
    const omc_transfer_package_batch* batch, omc_arena* out_bytes,
    omc_transfer_package_io_res* out_res)
{
    omc_transfer_status validate_status;
    omc_u8 target_code;
    omc_u8 emit_u8;
    omc_status status;
    omc_u32 i;

    if (batch == (const omc_transfer_package_batch*)0
        || out_bytes == (omc_arena*)0
        || out_res == (omc_transfer_package_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_package_io_res_init(out_res);
    omc_arena_reset(out_bytes);

    validate_status = omc_transfer_package_validate_batch(batch);
    out_res->status = validate_status;
    if (validate_status != OMC_TRANSFER_OK) {
        return OMC_STATUS_OK;
    }
    if (!omc_transfer_payload_target_code_from_format(batch->target_format,
                                                      &target_code)) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    status = omc_transfer_append_bytes(out_bytes,
                                       k_omc_transfer_package_batch_magic,
                                       sizeof(k_omc_transfer_package_batch_magic));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u32le(
        out_bytes, OMC_TRANSFER_PACKAGE_BATCH_VERSION);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u32le(out_bytes,
                                               batch->contract_version);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out_bytes, &target_code, 1U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u64le(out_bytes, batch->input_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u64le(out_bytes, batch->output_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_payload_append_u32le(out_bytes, batch->chunk_count);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    for (i = 0U; i < batch->chunk_count; ++i) {
        const omc_transfer_package_chunk* chunk;

        chunk = &batch->chunks[i];
        emit_u8 = (omc_u8)chunk->kind;
        status = omc_transfer_append_bytes(out_bytes, &emit_u8, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u64le(out_bytes,
                                                   chunk->output_offset);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u64le(out_bytes,
                                                   chunk->source_offset);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_u32le(out_bytes,
                                                   chunk->block_index);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_append_bytes(out_bytes,
                                           &chunk->jpeg_marker_code, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_blob_le(out_bytes,
                                                     chunk->route.data,
                                                     chunk->route.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_transfer_payload_append_blob_le(out_bytes,
                                                     chunk->bytes.data,
                                                     chunk->bytes.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    out_res->status = OMC_TRANSFER_OK;
    out_res->bytes = (omc_u64)out_bytes->size;
    out_res->chunk_count = batch->chunk_count;
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_transfer_package_batch_deserialize(
    const omc_u8* bytes, omc_size size, omc_arena* out_storage,
    omc_transfer_package_batch* out_batch,
    omc_transfer_package_io_res* out_res)
{
    omc_size off;
    omc_u32 version;
    omc_u32 contract_version;
    omc_u8 target_code;
    omc_u64 input_size;
    omc_u64 output_size;
    omc_u32 count;
    omc_byte_ref entries_ref;
    omc_status status;
    omc_u32 i;
    omc_transfer_status validate_status;

    if (bytes == (const omc_u8*)0 || out_storage == (omc_arena*)0
        || out_batch == (omc_transfer_package_batch*)0
        || out_res == (omc_transfer_package_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_package_batch_init(out_batch);
    omc_transfer_package_io_res_init(out_res);
    omc_arena_reset(out_storage);

    if (size < 8U) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (memcmp(bytes, k_omc_transfer_package_batch_magic,
               sizeof(k_omc_transfer_package_batch_magic))
        != 0) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    off = 8U;
    if (!omc_transfer_payload_read_u32le(bytes, size, &off, &version)
        || !omc_transfer_payload_read_u32le(bytes, size, &off,
                                            &contract_version)
        || !omc_transfer_payload_read_u8(bytes, size, &off, &target_code)
        || !omc_transfer_payload_read_u64le(bytes, size, &off, &input_size)
        || !omc_transfer_payload_read_u64le(bytes, size, &off, &output_size)
        || !omc_transfer_payload_read_u32le(bytes, size, &off, &count)) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (version != OMC_TRANSFER_PACKAGE_BATCH_VERSION) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (!omc_transfer_payload_format_from_target_code(target_code,
                                                      &out_batch->target_format)) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    out_batch->contract_version = contract_version;
    out_batch->input_size = input_size;
    out_batch->output_size = output_size;
    out_batch->chunk_count = count;
    if (count != 0U
        && ((omc_u64)(size - off) / (omc_u64)(1U + 8U + 8U + 4U + 1U + 8U + 8U))
               < (omc_u64)count) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (count != 0U) {
        omc_transfer_package_chunk* zero_entries;

        zero_entries = (omc_transfer_package_chunk*)calloc(
            count, sizeof(omc_transfer_package_chunk));
        if (zero_entries == (omc_transfer_package_chunk*)0) {
            return OMC_STATUS_NO_MEMORY;
        }
        status = omc_arena_append(out_storage, zero_entries,
                                  count
                                      * sizeof(omc_transfer_package_chunk),
                                  &entries_ref);
        free(zero_entries);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    } else {
        entries_ref.offset = 0U;
        entries_ref.size = 0U;
    }

    for (i = 0U; i < count; ++i) {
        omc_const_bytes route_view;
        omc_const_bytes bytes_view;
        omc_u8 chunk_kind_u8;
        omc_u64 output_offset;
        omc_u64 source_offset;
        omc_u32 block_index;
        omc_u8 jpeg_marker_code;
        omc_transfer_package_chunk_kind chunk_kind;

        if (!omc_transfer_payload_read_u8(bytes, size, &off, &chunk_kind_u8)
            || !omc_transfer_payload_read_u64le(bytes, size, &off,
                                                &output_offset)
            || !omc_transfer_payload_read_u64le(bytes, size, &off,
                                                &source_offset)
            || !omc_transfer_payload_read_u32le(bytes, size, &off,
                                                &block_index)
            || !omc_transfer_payload_read_u8(bytes, size, &off,
                                             &jpeg_marker_code)
            || !omc_transfer_payload_read_blob(bytes, size, &off, &route_view)
            || !omc_transfer_payload_read_blob(bytes, size, &off,
                                               &bytes_view)) {
            out_res->status = OMC_TRANSFER_MALFORMED;
            omc_transfer_package_batch_init(out_batch);
            return OMC_STATUS_OK;
        }
        chunk_kind = (omc_transfer_package_chunk_kind)chunk_kind_u8;
        status = omc_transfer_package_store_entry(
            out_storage, entries_ref, i, chunk_kind, route_view,
            output_offset, source_offset, block_index, jpeg_marker_code,
            bytes_view.data,
            bytes_view.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if (off != size) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        omc_transfer_package_batch_init(out_batch);
        return OMC_STATUS_OK;
    }

    if (count != 0U) {
        out_batch->chunks = (const omc_transfer_package_chunk*)
                                omc_arena_view_mut(out_storage, entries_ref)
                                    .data;
    }
    validate_status = omc_transfer_package_validate_batch(out_batch);
    out_res->status = validate_status;
    if (validate_status != OMC_TRANSFER_OK) {
        omc_transfer_package_batch_init(out_batch);
        return OMC_STATUS_OK;
    }

    out_res->bytes = (omc_u64)size;
    out_res->chunk_count = out_batch->chunk_count;
    return OMC_STATUS_OK;
}

OMC_API omc_status
omc_transfer_package_batch_replay(
    const omc_transfer_package_batch* batch,
    const omc_transfer_package_replay_callbacks* callbacks,
    omc_transfer_package_replay_res* out_res)
{
    omc_transfer_status validate_status;
    omc_u32 i;

    if (batch == (const omc_transfer_package_batch*)0
        || callbacks == (const omc_transfer_package_replay_callbacks*)0
        || out_res == (omc_transfer_package_replay_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_package_replay_res_init(out_res);
    validate_status = omc_transfer_package_validate_batch(batch);
    out_res->status = validate_status;
    if (validate_status != OMC_TRANSFER_OK) {
        return OMC_STATUS_OK;
    }
    if (callbacks->emit_chunk == (omc_transfer_package_emit_chunk_fn)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (callbacks->begin_batch != (omc_transfer_package_begin_batch_fn)0) {
        out_res->status = callbacks->begin_batch(callbacks->user,
                                                 batch->target_format,
                                                 batch->chunk_count);
        if (out_res->status != OMC_TRANSFER_OK) {
            return OMC_STATUS_OK;
        }
    }

    for (i = 0U; i < batch->chunk_count; ++i) {
        out_res->status = callbacks->emit_chunk(callbacks->user,
                                                &batch->chunks[i]);
        if (out_res->status != OMC_TRANSFER_OK) {
            out_res->failed_chunk_index = i;
            return OMC_STATUS_OK;
        }
        out_res->replayed = i + 1U;
    }

    if (callbacks->end_batch != (omc_transfer_package_end_batch_fn)0) {
        out_res->status = callbacks->end_batch(callbacks->user,
                                               batch->target_format);
        return OMC_STATUS_OK;
    }

    out_res->status = OMC_TRANSFER_OK;
    return OMC_STATUS_OK;
}

void
omc_transfer_artifact_info_init(omc_transfer_artifact_info* info)
{
    if (info == (omc_transfer_artifact_info*)0) {
        return;
    }

    memset(info, 0, sizeof(*info));
    info->kind = OMC_TRANSFER_ARTIFACT_UNKNOWN;
    info->target_format = OMC_SCAN_FMT_UNKNOWN;
    info->icc_block_index = 0xFFFFFFFFU;
}

void
omc_transfer_artifact_io_res_init(omc_transfer_artifact_io_res* res)
{
    if (res == (omc_transfer_artifact_io_res*)0) {
        return;
    }

    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->bytes = 0U;
}

OMC_API omc_status
omc_transfer_artifact_inspect(const omc_u8* bytes, omc_size size,
                              omc_transfer_artifact_info* out_info,
                              omc_transfer_artifact_io_res* out_res)
{
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res batch_res;
    omc_arena batch_storage;
    omc_transfer_package_batch package_batch;
    omc_transfer_package_io_res package_res;
    omc_arena package_storage;
    omc_jxl_encoder_handoff handoff;
    omc_jxl_encoder_handoff_io_res handoff_res;
    omc_status status;
    omc_u32 i;
    omc_u64 payload_bytes;

    if (bytes == (const omc_u8*)0
        || out_info == (omc_transfer_artifact_info*)0
        || out_res == (omc_transfer_artifact_io_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_artifact_info_init(out_info);
    omc_transfer_artifact_io_res_init(out_res);

    if (size < 8U) {
        out_res->status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (memcmp(bytes, k_omc_transfer_payload_batch_magic,
               sizeof(k_omc_transfer_payload_batch_magic))
        == 0) {
        omc_transfer_payload_batch_init(&batch);
        omc_transfer_payload_io_res_init(&batch_res);
        omc_arena_init(&batch_storage);
        status = omc_transfer_payload_batch_deserialize(
            bytes, size, &batch_storage, &batch, &batch_res);
        if (status == OMC_STATUS_OK && batch_res.status == OMC_TRANSFER_OK) {
            payload_bytes = 0U;
            for (i = 0U; i < batch.payload_count; ++i) {
                payload_bytes += (omc_u64)batch.payloads[i].payload.size;
            }
            out_info->kind = OMC_TRANSFER_ARTIFACT_TRANSFER_PAYLOAD_BATCH;
            out_info->has_contract_version = 1;
            out_info->contract_version = batch.contract_version;
            out_info->has_target_format = 1;
            out_info->target_format = batch.target_format;
            out_info->entry_count = batch.payload_count;
            out_info->payload_bytes = payload_bytes;
            out_info->binding_bytes = (omc_u64)size - payload_bytes;
        }
        out_res->status = batch_res.status;
        out_res->bytes = batch_res.bytes;
        omc_arena_fini(&batch_storage);
        return status;
    }
    if (memcmp(bytes, k_omc_transfer_package_batch_magic,
               sizeof(k_omc_transfer_package_batch_magic))
        == 0) {
        omc_transfer_package_batch_init(&package_batch);
        omc_transfer_package_io_res_init(&package_res);
        omc_arena_init(&package_storage);
        status = omc_transfer_package_batch_deserialize(
            bytes, size, &package_storage, &package_batch, &package_res);
        if (status == OMC_STATUS_OK && package_res.status == OMC_TRANSFER_OK) {
            payload_bytes = 0U;
            for (i = 0U; i < package_batch.chunk_count; ++i) {
                payload_bytes += (omc_u64)package_batch.chunks[i].bytes.size;
            }
            out_info->kind = OMC_TRANSFER_ARTIFACT_TRANSFER_PACKAGE_BATCH;
            out_info->has_contract_version = 1;
            out_info->contract_version = package_batch.contract_version;
            out_info->has_target_format = 1;
            out_info->target_format = package_batch.target_format;
            out_info->entry_count = package_batch.chunk_count;
            out_info->payload_bytes = payload_bytes;
            out_info->binding_bytes = (omc_u64)size - payload_bytes;
        }
        out_res->status = package_res.status;
        out_res->bytes = package_res.bytes;
        omc_arena_fini(&package_storage);
        return status;
    }
    if (memcmp(bytes, k_omc_transfer_jxl_encoder_handoff_magic,
               sizeof(k_omc_transfer_jxl_encoder_handoff_magic))
        != 0) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    status = omc_jxl_encoder_handoff_parse_view(bytes, size, &handoff,
                                                &handoff_res);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (handoff_res.status == OMC_TRANSFER_OK) {
        out_info->kind = OMC_TRANSFER_ARTIFACT_JXL_ENCODER_HANDOFF;
        out_info->has_contract_version = 1;
        out_info->contract_version = handoff.contract_version;
        out_info->has_target_format = 1;
        out_info->target_format = OMC_SCAN_FMT_JXL;
        out_info->entry_count = handoff.box_count;
        out_info->has_icc_profile = handoff.has_icc_profile;
        out_info->icc_block_index = handoff.icc_block_index;
        out_info->icc_profile_bytes = (omc_u64)handoff.icc_profile.size;
        out_info->box_payload_bytes = handoff.box_payload_bytes;
    }
    out_res->status = handoff_res.status;
    out_res->bytes = handoff_res.bytes;
    return OMC_STATUS_OK;
}

static int
omc_transfer_jpeg_is_exif_app1(const omc_u8* seg_data, omc_size seg_data_size)
{
    return seg_data_size >= 6U && memcmp(seg_data, "Exif\0\0", 6U) == 0;
}

static int
omc_transfer_jpeg_is_icc_app2(const omc_u8* seg_data, omc_size seg_data_size)
{
    return seg_data_size >= 14U
           && memcmp(seg_data, "ICC_PROFILE\0", 12U) == 0;
}

static int
omc_transfer_jpeg_is_xmp_app1(const omc_u8* seg_data, omc_size seg_data_size)
{
    return seg_data_size >= sizeof(k_omc_transfer_jpeg_xmp_prefix) - 1U
           && memcmp(seg_data, k_omc_transfer_jpeg_xmp_prefix,
                     sizeof(k_omc_transfer_jpeg_xmp_prefix) - 1U)
                  == 0;
}

static int
omc_transfer_jpeg_is_photoshop_app13(const omc_u8* seg_data,
                                     omc_size seg_data_size)
{
    return seg_data_size >= sizeof(k_omc_transfer_jpeg_photoshop_prefix) - 1U
           && memcmp(seg_data, k_omc_transfer_jpeg_photoshop_prefix,
                     sizeof(k_omc_transfer_jpeg_photoshop_prefix) - 1U)
                  == 0;
}

static omc_status
omc_transfer_append_jpeg_icc_segment(omc_arena* out, omc_u8 seq_no,
                                     omc_u8 chunk_count,
                                     const omc_u8* payload,
                                     omc_size payload_size)
{
    omc_u8 header[18];
    omc_status status;

    if (payload_size > 65519U) {
        return OMC_STATUS_OVERFLOW;
    }

    header[0] = 0xFFU;
    header[1] = 0xE2U;
    omc_transfer_store_u16be(header + 2U, (omc_u16)(payload_size + 16U));
    memcpy(header + 4U, "ICC_PROFILE\0", 12U);
    header[16] = seq_no;
    header[17] = chunk_count;
    status = omc_transfer_append_bytes(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_transfer_append_bytes(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_append_iptc_length(omc_arena* out, omc_size value_size)
{
    omc_u8 buf[6];

    if (out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (value_size <= 0x7FFFU) {
        omc_transfer_store_u16be(buf, (omc_u16)value_size);
        return omc_transfer_append_bytes(out, buf, 2U);
    }
    if ((omc_u64)value_size > 0xFFFFFFFFU) {
        return OMC_STATUS_OVERFLOW;
    }

    buf[0] = 0x80U;
    buf[1] = 0x04U;
    omc_transfer_store_u32be(buf + 2U, (omc_u32)value_size);
    return omc_transfer_append_bytes(out, buf, sizeof(buf));
}

static omc_status
omc_transfer_append_iptc_value(omc_arena* out, const omc_store* store,
                               const omc_val* value, int* out_supported)
{
    omc_const_bytes view;
    omc_u8 byte_value;

    if (out_supported == (int*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_supported = 0;
    if (out == (omc_arena*)0 || store == (const omc_store*)0
        || value == (const omc_val*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (value->kind == OMC_VAL_BYTES || value->kind == OMC_VAL_TEXT) {
        view = omc_arena_view(&store->arena, value->u.ref);
        *out_supported = 1;
        return omc_transfer_append_bytes(out, view.data, view.size);
    }
    if (value->kind == OMC_VAL_ARRAY
        && (value->elem_type == OMC_ELEM_U8
            || value->elem_type == OMC_ELEM_I8)) {
        view = omc_arena_view(&store->arena, value->u.ref);
        *out_supported = 1;
        return omc_transfer_append_bytes(out, view.data, view.size);
    }
    if (value->kind == OMC_VAL_SCALAR
        && (value->elem_type == OMC_ELEM_U8
            || value->elem_type == OMC_ELEM_I8)) {
        byte_value = (omc_u8)(value->u.u64 & 0xFFU);
        *out_supported = 1;
        return omc_transfer_append_bytes(out, &byte_value, 1U);
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_build_iptc_iim(const omc_store* store, omc_arena* out,
                            int* out_has_iptc, int* out_supported)
{
    omc_size i;
    omc_status status;
    omc_arena value_bytes;

    if (out_has_iptc == (int*)0 || out_supported == (int*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_has_iptc = 0;
    *out_supported = 1;
    if (store == (const omc_store*)0 || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_arena_reset(out);
    omc_arena_init(&value_bytes);
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        int value_supported;
        omc_u8 header[3];

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_IPTC_DATASET) {
            continue;
        }

        if (entry->key.u.iptc_dataset.record > 255U
            || entry->key.u.iptc_dataset.dataset > 255U) {
            *out_has_iptc = 1;
            *out_supported = 0;
            return OMC_STATUS_OK;
        }

        header[0] = 0x1CU;
        header[1] = (omc_u8)entry->key.u.iptc_dataset.record;
        header[2] = (omc_u8)entry->key.u.iptc_dataset.dataset;
        omc_arena_reset(&value_bytes);
        status = omc_transfer_append_iptc_value(&value_bytes, store,
                                                &entry->value,
                                                &value_supported);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&value_bytes);
            return status;
        }
        if (!value_supported) {
            *out_has_iptc = 1;
            *out_supported = 0;
            omc_arena_fini(&value_bytes);
            return OMC_STATUS_OK;
        }
        status = omc_transfer_append_bytes(out, header, sizeof(header));
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&value_bytes);
            return status;
        }
        status = omc_transfer_append_iptc_length(out, value_bytes.size);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&value_bytes);
            return status;
        }
        if (value_bytes.size != 0U) {
            status = omc_transfer_append_bytes(out, value_bytes.data,
                                               value_bytes.size);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&value_bytes);
                return status;
            }
        }
        *out_has_iptc = 1;
    }
    omc_arena_fini(&value_bytes);
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_build_photoshop_iptc_irb(const omc_u8* iim_bytes,
                                      omc_size iim_size, omc_arena* out)
{
    omc_status status;
    static const omc_u8 k_empty_name[2] = { 0U, 0U };

    if (iim_bytes == (const omc_u8*)0 || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if ((omc_u64)iim_size > 0xFFFFFFFFU) {
        return OMC_STATUS_OVERFLOW;
    }
    if ((omc_u64)iim_size + 13U > (omc_u64)(~(omc_size)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, iim_size + 13U + (iim_size & 1U));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    status = omc_transfer_append_bytes(out, "8BIM", 4U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_u16be_arena(out, 0x0404U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, k_empty_name,
                                       sizeof(k_empty_name));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_u32be_arena(out, (omc_u32)iim_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (iim_size != 0U) {
        status = omc_transfer_append_bytes(out, iim_bytes, iim_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if ((iim_size & 1U) != 0U) {
        static const omc_u8 k_pad = 0U;

        status = omc_transfer_append_bytes(out, &k_pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_append_jpeg_photoshop_segment(omc_arena* out,
                                           const omc_u8* payload,
                                           omc_size payload_size)
{
    omc_u8 header[18];
    omc_status status;

    if (out == (omc_arena*)0 || payload == (const omc_u8*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (payload_size > 65519U) {
        return OMC_STATUS_OVERFLOW;
    }

    header[0] = 0xFFU;
    header[1] = 0xEDU;
    omc_transfer_store_u16be(
        header + 2U,
        (omc_u16)(payload_size
                  + sizeof(k_omc_transfer_jpeg_photoshop_prefix) - 1U + 2U));
    memcpy(header + 4U, k_omc_transfer_jpeg_photoshop_prefix,
           sizeof(k_omc_transfer_jpeg_photoshop_prefix) - 1U);
    status = omc_transfer_append_bytes(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_transfer_append_bytes(out, payload, payload_size);
}

static int
omc_transfer_irb_resource_span(const omc_u8* bytes, omc_size size,
                               omc_size offset, omc_u16* out_resource_id,
                               omc_size* out_span_size)
{
    omc_size p;
    omc_u8 name_len;
    omc_u32 data_size;

    if (out_resource_id == (omc_u16*)0 || out_span_size == (omc_size*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || offset > size || size - offset < 12U) {
        return 0;
    }
    if (memcmp(bytes + offset, "8BIM", 4U) != 0) {
        return 0;
    }

    p = offset + 4U;
    *out_resource_id = omc_transfer_read_u16be(bytes + p);
    p += 2U;
    name_len = bytes[p];
    p += 1U;
    if ((omc_size)name_len > size - p) {
        return 0;
    }
    p += (omc_size)name_len;
    if (((omc_size)name_len + 1U) & 1U) {
        if (p >= size) {
            return 0;
        }
        p += 1U;
    }
    if (size - p < 4U) {
        return 0;
    }
    data_size = omc_transfer_read_u32be(bytes + p);
    p += 4U;
    if ((omc_u64)data_size > (omc_u64)(size - p)) {
        return 0;
    }
    p += (omc_size)data_size;
    if ((data_size & 1U) != 0U) {
        if (p >= size) {
            return 0;
        }
        p += 1U;
    }
    *out_span_size = p - offset;
    return 1;
}

static omc_status
omc_transfer_append_irb_without_iptc(const omc_u8* irb_bytes, omc_size irb_size,
                                     omc_arena* out, int* out_malformed)
{
    omc_size offset;

    if (out_malformed == (int*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_malformed = 0;
    if (irb_bytes == (const omc_u8*)0 || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    offset = 0U;
    while (offset < irb_size) {
        omc_u16 resource_id;
        omc_size span_size;
        omc_status status;

        if (!omc_transfer_irb_resource_span(irb_bytes, irb_size, offset,
                                            &resource_id, &span_size)) {
            *out_malformed = 1;
            return OMC_STATUS_OK;
        }
        if (resource_id != 0x0404U) {
            status = omc_transfer_append_bytes(out, irb_bytes + offset,
                                               span_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        offset += span_size;
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_rewrite_jpeg_iptc(const omc_u8* file_bytes, omc_size file_size,
                               const omc_u8* irb_payload,
                               omc_size irb_payload_size, omc_arena* out,
                               omc_transfer_status* out_status)
{
    omc_size offset;
    omc_status status;
    int inserted;
    omc_arena merged_irb;

    if (out_status == (omc_transfer_status*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_status = OMC_TRANSFER_UNSUPPORTED;
    if (file_bytes == (const omc_u8*)0 || irb_payload == (const omc_u8*)0
        || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (file_size < 2U || file_bytes[0] != 0xFFU || file_bytes[1] != 0xD8U) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    omc_arena_init(&merged_irb);
    offset = 2U;
    while (offset + 2U <= file_size) {
        omc_u8 marker;

        if (file_bytes[offset] != 0xFFU) {
            omc_arena_fini(&merged_irb);
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        offset += 1U;
        while (offset < file_size && file_bytes[offset] == 0xFFU) {
            offset += 1U;
        }
        if (offset >= file_size) {
            omc_arena_fini(&merged_irb);
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }

        marker = file_bytes[offset];
        offset += 1U;
        if (marker == 0xD9U) {
            break;
        }
        if (marker == 0xDAU) {
            if (offset + 2U > file_size) {
                omc_arena_fini(&merged_irb);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            break;
        }
        if (marker >= 0xD0U && marker <= 0xD7U) {
            continue;
        }

        {
            omc_u16 seg_len;
            omc_size segment_end;
            const omc_u8* seg_data;
            omc_size seg_data_size;

            if (offset + 2U > file_size) {
                omc_arena_fini(&merged_irb);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            seg_len = omc_transfer_read_u16be(file_bytes + offset);
            if (seg_len < 2U || (omc_size)seg_len > file_size - offset) {
                omc_arena_fini(&merged_irb);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            segment_end = offset + (omc_size)seg_len;
            seg_data = file_bytes + offset + 2U;
            seg_data_size = (omc_size)seg_len - 2U;

            if (marker == 0xEDU
                && omc_transfer_jpeg_is_photoshop_app13(seg_data,
                                                        seg_data_size)) {
                int malformed;

                status = omc_transfer_append_irb_without_iptc(
                    seg_data + sizeof(k_omc_transfer_jpeg_photoshop_prefix) - 1U,
                    seg_data_size
                        - (sizeof(k_omc_transfer_jpeg_photoshop_prefix) - 1U),
                    &merged_irb, &malformed);
                if (status != OMC_STATUS_OK) {
                    omc_arena_fini(&merged_irb);
                    return status;
                }
                if (malformed) {
                    omc_arena_fini(&merged_irb);
                    *out_status = OMC_TRANSFER_MALFORMED;
                    return OMC_STATUS_OK;
                }
            }
            offset = segment_end;
        }
    }

    status = omc_transfer_append_bytes(&merged_irb, irb_payload,
                                       irb_payload_size);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&merged_irb);
        return status;
    }
    if (merged_irb.size > 65519U) {
        omc_arena_fini(&merged_irb);
        *out_status = OMC_TRANSFER_LIMIT;
        return OMC_STATUS_OK;
    }
    if ((omc_u64)file_size + (omc_u64)merged_irb.size + 18U
        > (omc_u64)(~(omc_size)0)) {
        omc_arena_fini(&merged_irb);
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + merged_irb.size + 18U);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&merged_irb);
        return status;
    }
    status = omc_transfer_append_bytes(out, file_bytes, 2U);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&merged_irb);
        return status;
    }

    inserted = 0;
    offset = 2U;
    while (offset + 2U <= file_size) {
        omc_size marker_start;
        omc_u8 marker;

        marker_start = offset;
        if (file_bytes[offset] != 0xFFU) {
            omc_arena_fini(&merged_irb);
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        offset += 1U;
        while (offset < file_size && file_bytes[offset] == 0xFFU) {
            offset += 1U;
        }
        if (offset >= file_size) {
            omc_arena_fini(&merged_irb);
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }

        marker = file_bytes[offset];
        offset += 1U;
        if (marker == 0xD9U) {
            if (!inserted) {
                status = omc_transfer_append_jpeg_photoshop_segment(
                    out, merged_irb.data, merged_irb.size);
                if (status != OMC_STATUS_OK) {
                    omc_arena_fini(&merged_irb);
                    return status;
                }
                inserted = 1;
            }
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&merged_irb);
                return status;
            }
            omc_arena_fini(&merged_irb);
            *out_status = OMC_TRANSFER_OK;
            return OMC_STATUS_OK;
        }
        if (marker == 0xDAU) {
            omc_u16 seg_len;

            if (offset + 2U > file_size) {
                omc_arena_fini(&merged_irb);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            seg_len = omc_transfer_read_u16be(file_bytes + offset);
            if (seg_len < 2U || offset + (omc_size)seg_len > file_size) {
                omc_arena_fini(&merged_irb);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            if (!inserted) {
                status = omc_transfer_append_jpeg_photoshop_segment(
                    out, merged_irb.data, merged_irb.size);
                if (status != OMC_STATUS_OK) {
                    omc_arena_fini(&merged_irb);
                    return status;
                }
                inserted = 1;
            }
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&merged_irb);
                return status;
            }
            omc_arena_fini(&merged_irb);
            *out_status = OMC_TRANSFER_OK;
            return OMC_STATUS_OK;
        }
        if (marker >= 0xD0U && marker <= 0xD7U) {
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               offset - marker_start);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&merged_irb);
                return status;
            }
            continue;
        }

        {
            omc_u16 seg_len;
            omc_size segment_end;
            const omc_u8* seg_data;
            omc_size seg_data_size;
            int is_photoshop;
            int is_leading;

            if (offset + 2U > file_size) {
                omc_arena_fini(&merged_irb);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            seg_len = omc_transfer_read_u16be(file_bytes + offset);
            if (seg_len < 2U || (omc_size)seg_len > file_size - offset) {
                omc_arena_fini(&merged_irb);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }

            segment_end = offset + (omc_size)seg_len;
            seg_data = file_bytes + offset + 2U;
            seg_data_size = (omc_size)seg_len - 2U;
            is_photoshop =
                marker == 0xEDU
                && omc_transfer_jpeg_is_photoshop_app13(seg_data,
                                                        seg_data_size);
            is_leading = marker == 0xE0U
                         || (marker == 0xE1U
                             && (omc_transfer_jpeg_is_exif_app1(
                                     seg_data, seg_data_size)
                                 || omc_transfer_jpeg_is_xmp_app1(
                                     seg_data, seg_data_size)))
                         || (marker == 0xE2U
                             && omc_transfer_jpeg_is_icc_app2(
                                 seg_data, seg_data_size));

            if (is_photoshop) {
                offset = segment_end;
                continue;
            }
            if (!inserted && !is_leading) {
                status = omc_transfer_append_jpeg_photoshop_segment(
                    out, merged_irb.data, merged_irb.size);
                if (status != OMC_STATUS_OK) {
                    omc_arena_fini(&merged_irb);
                    return status;
                }
                inserted = 1;
            }
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               segment_end - marker_start);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&merged_irb);
                return status;
            }
            offset = segment_end;
        }
    }

    omc_arena_fini(&merged_irb);
    *out_status = OMC_TRANSFER_MALFORMED;
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_rewrite_jpeg_icc(const omc_u8* file_bytes, omc_size file_size,
                              const omc_u8* profile, omc_size profile_size,
                              omc_arena* out, omc_transfer_status* out_status)
{
    omc_size offset;
    omc_status status;
    omc_u32 chunk_count;
    omc_u32 insert_extra;
    omc_u32 chunk_index;
    omc_size consumed;
    int inserted;

    if (out_status == (omc_transfer_status*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_status = OMC_TRANSFER_UNSUPPORTED;

    if (file_bytes == (const omc_u8*)0 || profile == (const omc_u8*)0
        || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (file_size < 2U || file_bytes[0] != 0xFFU || file_bytes[1] != 0xD8U) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (profile_size == 0U) {
        *out_status = OMC_TRANSFER_OK;
        omc_arena_reset(out);
        status = omc_arena_reserve(out, file_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        return omc_transfer_append_bytes(out, file_bytes, file_size);
    }

    chunk_count = (omc_u32)((profile_size + 65519U - 1U) / 65519U);
    if (chunk_count == 0U || chunk_count > 255U) {
        *out_status = OMC_TRANSFER_LIMIT;
        return OMC_STATUS_OK;
    }
    if ((omc_u64)chunk_count * 18U + (omc_u64)profile_size
        > (omc_u64)(~(omc_size)0) - (omc_u64)file_size) {
        return OMC_STATUS_OVERFLOW;
    }
    insert_extra = chunk_count * 18U + (omc_u32)profile_size;

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + insert_extra);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, file_bytes, 2U);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    offset = 2U;
    consumed = 0U;
    inserted = 0;
    while (offset + 2U <= file_size) {
        omc_size marker_start;
        omc_u8 marker;

        marker_start = offset;
        if (file_bytes[offset] != 0xFFU) {
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        offset += 1U;
        while (offset < file_size && file_bytes[offset] == 0xFFU) {
            offset += 1U;
        }
        if (offset >= file_size) {
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }

        marker = file_bytes[offset];
        offset += 1U;
        if (marker == 0xD9U) {
            if (!inserted) {
                for (chunk_index = 0U; chunk_index < chunk_count; ++chunk_index) {
                    omc_size chunk_size;

                    chunk_size = profile_size - consumed;
                    if (chunk_size > 65519U) {
                        chunk_size = 65519U;
                    }
                    status = omc_transfer_append_jpeg_icc_segment(
                        out, (omc_u8)(chunk_index + 1U),
                        (omc_u8)chunk_count, profile + consumed, chunk_size);
                    if (status != OMC_STATUS_OK) {
                        return status;
                    }
                    consumed += chunk_size;
                }
                inserted = 1;
            }
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            *out_status = OMC_TRANSFER_OK;
            return OMC_STATUS_OK;
        }
        if (marker == 0xDAU) {
            omc_u16 seg_len;

            if (offset + 2U > file_size) {
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            seg_len = omc_transfer_read_u16be(file_bytes + offset);
            if (seg_len < 2U || offset + (omc_size)seg_len > file_size) {
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            if (!inserted) {
                for (chunk_index = 0U; chunk_index < chunk_count; ++chunk_index) {
                    omc_size chunk_size;

                    chunk_size = profile_size - consumed;
                    if (chunk_size > 65519U) {
                        chunk_size = 65519U;
                    }
                    status = omc_transfer_append_jpeg_icc_segment(
                        out, (omc_u8)(chunk_index + 1U),
                        (omc_u8)chunk_count, profile + consumed, chunk_size);
                    if (status != OMC_STATUS_OK) {
                        return status;
                    }
                    consumed += chunk_size;
                }
                inserted = 1;
            }
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            *out_status = OMC_TRANSFER_OK;
            return OMC_STATUS_OK;
        }

        if (marker >= 0xD0U && marker <= 0xD7U) {
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               offset - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            continue;
        }

        {
            omc_u16 seg_len;
            omc_size segment_end;
            const omc_u8* seg_data;
            omc_size seg_data_size;
            int is_icc;
            int is_leading;

            if (offset + 2U > file_size) {
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            seg_len = omc_transfer_read_u16be(file_bytes + offset);
            if (seg_len < 2U || (omc_size)seg_len > file_size - offset) {
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }

            segment_end = offset + (omc_size)seg_len;
            seg_data = file_bytes + offset + 2U;
            seg_data_size = (omc_size)seg_len - 2U;
            is_icc = marker == 0xE2U
                     && omc_transfer_jpeg_is_icc_app2(seg_data, seg_data_size);
            is_leading = marker == 0xE0U
                         || (marker == 0xE1U
                             && omc_transfer_jpeg_is_exif_app1(
                                    seg_data, seg_data_size));

            if (is_icc) {
                offset = segment_end;
                continue;
            }
            if (!inserted && !is_leading) {
                for (chunk_index = 0U; chunk_index < chunk_count; ++chunk_index) {
                    omc_size chunk_size;

                    chunk_size = profile_size - consumed;
                    if (chunk_size > 65519U) {
                        chunk_size = 65519U;
                    }
                    status = omc_transfer_append_jpeg_icc_segment(
                        out, (omc_u8)(chunk_index + 1U),
                        (omc_u8)chunk_count, profile + consumed, chunk_size);
                    if (status != OMC_STATUS_OK) {
                        return status;
                    }
                    consumed += chunk_size;
                }
                inserted = 1;
            }
            status = omc_transfer_append_bytes(out,
                                               file_bytes + marker_start,
                                               segment_end - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            offset = segment_end;
        }
    }

    *out_status = OMC_TRANSFER_MALFORMED;
    return OMC_STATUS_OK;
}

#if OMC_HAVE_ZLIB
static omc_u32
omc_transfer_crc32_update(omc_u32 crc, const omc_u8* bytes, omc_size size)
{
    return (omc_u32)crc32((uLong)crc, (const Bytef*)bytes, (uInt)size);
}
#endif

static omc_status
omc_transfer_append_png_chunk(omc_arena* out, const char* type,
                              const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_u8 crc_bytes[4];
    omc_status status;
    omc_u32 crc;

    if (type == (const char*)0 || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (payload_size > 0xFFFFFFFFU) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_transfer_store_u32be(header, (omc_u32)payload_size);
    memcpy(header + 4U, type, 4U);
    status = omc_transfer_append_bytes(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_transfer_append_bytes(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
#if OMC_HAVE_ZLIB
    crc = omc_transfer_crc32_update(0U, (const omc_u8*)type, 4U);
    crc = omc_transfer_crc32_update(crc, payload, payload_size);
    omc_transfer_store_u32be(crc_bytes, crc);
#else
    memset(crc_bytes, 0, sizeof(crc_bytes));
#endif
    return omc_transfer_append_bytes(out, crc_bytes, sizeof(crc_bytes));
}

static omc_status
omc_transfer_append_webp_chunk(omc_arena* out, const char* type,
                               const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_status status;

    if (out == (omc_arena*)0 || type == (const char*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (payload_size > (omc_size)(~(omc_u32)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    memcpy(header, type, 4U);
    omc_transfer_store_u32le(header + 4U, (omc_u32)payload_size);
    status = omc_transfer_append_bytes(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_transfer_append_bytes(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if ((payload_size & 1U) != 0U) {
        static const omc_u8 k_pad = 0U;

        status = omc_transfer_append_bytes(out, &k_pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_append_jp2_box(omc_arena* out, omc_u32 type,
                            const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_status status;

    if (out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (payload_size > (omc_size)(0xFFFFFFFFU - 8U)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_transfer_store_u32be(header, (omc_u32)(payload_size + 8U));
    omc_transfer_store_u32be(header + 4U, type);
    status = omc_transfer_append_bytes(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_transfer_append_bytes(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_append_jp2_colr_icc_box(omc_arena* out, const omc_u8* profile,
                                     omc_size profile_size)
{
    static const omc_u8 k_prefix[3] = { 0x02U, 0x00U, 0x00U };
    omc_u8 header[8];
    omc_status status;

    if (out == (omc_arena*)0 || profile == (const omc_u8*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (profile_size > (omc_size)(0xFFFFFFFFU - 11U)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_transfer_store_u32be(header, (omc_u32)(profile_size + 11U));
    omc_transfer_store_u32be(header + 4U,
                             omc_transfer_fourcc('c', 'o', 'l', 'r'));
    status = omc_transfer_append_bytes(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, k_prefix, sizeof(k_prefix));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_transfer_append_bytes(out, profile, profile_size);
}

static omc_status
omc_transfer_package_append_jpeg_segment(omc_arena* out, omc_u8 marker_code,
                                         const omc_u8* payload,
                                         omc_size payload_size)
{
    omc_u8 header[4];
    omc_status status;

    if (out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (payload_size > 65533U) {
        return OMC_STATUS_OVERFLOW;
    }

    header[0] = 0xFFU;
    header[1] = marker_code;
    omc_transfer_store_u16be(header + 2U, (omc_u16)(payload_size + 2U));
    status = omc_transfer_append_bytes(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size == 0U) {
        return OMC_STATUS_OK;
    }
    return omc_transfer_append_bytes(out, payload, payload_size);
}

static omc_status
omc_transfer_package_copy_bytes(omc_arena* out, const omc_u8* payload,
                                omc_size payload_size)
{
    omc_arena_reset(out);
    if (payload_size == 0U) {
        return OMC_STATUS_OK;
    }
    return omc_transfer_append_bytes(out, payload, payload_size);
}

static omc_status
omc_transfer_package_build_chunk_bytes(
    const omc_transfer_payload* payload,
    omc_transfer_package_chunk_kind* out_kind, omc_arena* out)
{
    if (payload == (const omc_transfer_payload*)0
        || out_kind == (omc_transfer_package_chunk_kind*)0
        || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    switch (payload->op.kind) {
    case OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER:
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT;
        omc_arena_reset(out);
        if (omc_transfer_route_view_eq(payload->route, "jpeg:app13-iptc")) {
            return omc_transfer_append_jpeg_photoshop_segment(
                out, payload->payload.data, payload->payload.size);
        }
        return omc_transfer_package_append_jpeg_segment(
            out, payload->op.jpeg_marker_code, payload->payload.data,
            payload->payload.size);
    case OMC_TRANSFER_PAYLOAD_OP_TIFF_TAG_BYTES:
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK;
        return omc_transfer_package_copy_bytes(out, payload->payload.data,
                                               payload->payload.size);
    case OMC_TRANSFER_PAYLOAD_OP_JXL_BOX:
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES;
        omc_arena_reset(out);
        return omc_transfer_append_bmff_box_arena(
            out,
            omc_transfer_fourcc((char)payload->op.box_type[0],
                                (char)payload->op.box_type[1],
                                (char)payload->op.box_type[2],
                                (char)payload->op.box_type[3]),
            payload->payload.data, payload->payload.size);
    case OMC_TRANSFER_PAYLOAD_OP_JXL_ICC_PROFILE:
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK;
        return omc_transfer_package_copy_bytes(out, payload->payload.data,
                                               payload->payload.size);
    case OMC_TRANSFER_PAYLOAD_OP_WEBP_CHUNK:
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES;
        omc_arena_reset(out);
        return omc_transfer_append_webp_chunk(
            out, (const char*)payload->op.chunk_type, payload->payload.data,
            payload->payload.size);
    case OMC_TRANSFER_PAYLOAD_OP_PNG_CHUNK:
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES;
        omc_arena_reset(out);
        return omc_transfer_append_png_chunk(
            out, (const char*)payload->op.chunk_type, payload->payload.data,
            payload->payload.size);
    case OMC_TRANSFER_PAYLOAD_OP_JP2_BOX:
        if (omc_transfer_route_view_eq(payload->route,
                                       "jp2:box-jp2h-colr")) {
            *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK;
            return omc_transfer_package_copy_bytes(out, payload->payload.data,
                                                   payload->payload.size);
        }
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES;
        omc_arena_reset(out);
        return omc_transfer_append_jp2_box(
            out,
            omc_transfer_fourcc((char)payload->op.box_type[0],
                                (char)payload->op.box_type[1],
                                (char)payload->op.box_type[2],
                                (char)payload->op.box_type[3]),
            payload->payload.data, payload->payload.size);
    case OMC_TRANSFER_PAYLOAD_OP_EXR_ATTRIBUTE:
    case OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM:
    case OMC_TRANSFER_PAYLOAD_OP_BMFF_PROPERTY:
        *out_kind = OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK;
        return omc_transfer_package_copy_bytes(out, payload->payload.data,
                                               payload->payload.size);
    default: break;
    }

    return OMC_STATUS_STATE;
}

static omc_status
omc_transfer_build_png_iccp_payload(const omc_u8* profile,
                                    omc_size profile_size,
                                    omc_arena* payload_out,
                                    omc_transfer_status* out_status)
{
#if OMC_HAVE_ZLIB
    omc_arena compressed;
    omc_status status;
    uLongf compressed_size;
    uLong src_size;
    int zret;
#endif
    static const char k_name[] = "icc";

    if (out_status == (omc_transfer_status*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_status = OMC_TRANSFER_UNSUPPORTED;

    if (profile == (const omc_u8*)0 || payload_out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
#if !OMC_HAVE_ZLIB
    (void)profile;
    (void)profile_size;
    omc_arena_reset(payload_out);
    return OMC_STATUS_OK;
#else
    if ((omc_u64)profile_size > (omc_u64)ULONG_MAX) {
        *out_status = OMC_TRANSFER_LIMIT;
        omc_arena_reset(payload_out);
        return OMC_STATUS_OK;
    }

    src_size = (uLong)profile_size;
    compressed_size = compressBound(src_size);
    if ((omc_u64)compressed_size > (omc_u64)(~(omc_size)0)
        || (omc_u64)compressed_size + 5U + 3U
               > (omc_u64)(~(omc_size)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_init(&compressed);
    status = omc_arena_reserve(&compressed, (omc_size)compressed_size);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&compressed);
        return status;
    }
    zret = compress2((Bytef*)compressed.data, &compressed_size,
                     (const Bytef*)profile, src_size, Z_BEST_SPEED);
    if (zret == Z_MEM_ERROR) {
        omc_arena_fini(&compressed);
        return OMC_STATUS_NO_MEMORY;
    }
    if (zret != Z_OK) {
        omc_arena_fini(&compressed);
        omc_arena_reset(payload_out);
        return OMC_STATUS_OK;
    }
    compressed.size = (omc_size)compressed_size;

    omc_arena_reset(payload_out);
    status = omc_arena_reserve(payload_out, 5U + 3U + compressed.size);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&compressed);
        return status;
    }
    status = omc_transfer_append_bytes(payload_out, k_name, 3U);
    if (status == OMC_STATUS_OK) {
        static const omc_u8 k_zero[2] = { 0U, 0U };

        status = omc_transfer_append_bytes(payload_out, k_zero,
                                           sizeof(k_zero));
    }
    if (status == OMC_STATUS_OK) {
        status = omc_transfer_append_bytes(payload_out, compressed.data,
                                           compressed.size);
    }
    omc_arena_fini(&compressed);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    *out_status = OMC_TRANSFER_OK;
    return OMC_STATUS_OK;
#endif
}

static omc_status
omc_transfer_rewrite_png_icc(const omc_u8* file_bytes, omc_size file_size,
                             const omc_u8* profile, omc_size profile_size,
                             omc_arena* out, omc_transfer_status* out_status)
{
    omc_arena payload;
    omc_size offset;
    omc_status status;
    int saw_ihdr;

    if (out_status == (omc_transfer_status*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_status = OMC_TRANSFER_UNSUPPORTED;

    if (file_bytes == (const omc_u8*)0 || profile == (const omc_u8*)0
        || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (file_size < sizeof(k_omc_transfer_png_sig)
        || memcmp(file_bytes, k_omc_transfer_png_sig,
                  sizeof(k_omc_transfer_png_sig))
               != 0) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    omc_arena_init(&payload);
    status = omc_transfer_build_png_iccp_payload(profile, profile_size,
                                                 &payload, out_status);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&payload);
        return status;
    }
    if (*out_status != OMC_TRANSFER_OK) {
        omc_arena_fini(&payload);
        return OMC_STATUS_OK;
    }

    omc_arena_reset(out);
    if ((omc_u64)file_size + 12U + (omc_u64)payload.size
        > (omc_u64)(~(omc_size)0)) {
        omc_arena_fini(&payload);
        return OMC_STATUS_OVERFLOW;
    }
    status = omc_arena_reserve(out, file_size + 12U + payload.size);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&payload);
        return status;
    }
    status = omc_transfer_append_bytes(out, file_bytes,
                                       sizeof(k_omc_transfer_png_sig));
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&payload);
        return status;
    }

    saw_ihdr = 0;
    offset = sizeof(k_omc_transfer_png_sig);
    while (offset + 12U <= file_size) {
        omc_u32 chunk_len;
        omc_size chunk_size;
        const omc_u8* chunk_type;

        chunk_len = omc_transfer_read_u32be(file_bytes + offset);
        if ((omc_u64)chunk_len + 12U > (omc_u64)(file_size - offset)) {
            *out_status = OMC_TRANSFER_MALFORMED;
            omc_arena_fini(&payload);
            return OMC_STATUS_OK;
        }
        chunk_size = (omc_size)chunk_len + 12U;
        chunk_type = file_bytes + offset + 4U;
        if (!saw_ihdr) {
            if (memcmp(chunk_type, "IHDR", 4U) != 0) {
                *out_status = OMC_TRANSFER_MALFORMED;
                omc_arena_fini(&payload);
                return OMC_STATUS_OK;
            }
            saw_ihdr = 1;
            status = omc_transfer_append_bytes(out, file_bytes + offset,
                                               chunk_size);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&payload);
                return status;
            }
            status = omc_transfer_append_png_chunk(out, "iCCP", payload.data,
                                                   payload.size);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&payload);
                return status;
            }
        } else if (memcmp(chunk_type, "iCCP", 4U) == 0) {
        } else {
            status = omc_transfer_append_bytes(out, file_bytes + offset,
                                               chunk_size);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&payload);
                return status;
            }
            if (memcmp(chunk_type, "IEND", 4U) == 0) {
                *out_status = OMC_TRANSFER_OK;
                omc_arena_fini(&payload);
                return OMC_STATUS_OK;
            }
        }
        offset += chunk_size;
    }

    *out_status = OMC_TRANSFER_MALFORMED;
    omc_arena_fini(&payload);
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_rewrite_webp_icc(const omc_u8* file_bytes, omc_size file_size,
                              const omc_u8* profile, omc_size profile_size,
                              omc_arena* out, omc_transfer_status* out_status)
{
    omc_size offset;
    omc_status status;
    int inserted;
    int saw_vp8x;
    omc_u32 riff_size;

    if (out_status == (omc_transfer_status*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_status = OMC_TRANSFER_UNSUPPORTED;

    if (file_bytes == (const omc_u8*)0 || profile == (const omc_u8*)0
        || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (profile_size > (omc_size)(~(omc_u32)0)) {
        *out_status = OMC_TRANSFER_LIMIT;
        return OMC_STATUS_OK;
    }
    if (file_size < 12U || memcmp(file_bytes, "RIFF", 4U) != 0
        || memcmp(file_bytes + 8U, "WEBP", 4U) != 0) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    riff_size = omc_transfer_read_u32le(file_bytes + 4U);
    if ((omc_u64)riff_size + 8U != (omc_u64)file_size) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if ((omc_u64)file_size + (omc_u64)profile_size + 9U
        > (omc_u64)(~(omc_size)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + profile_size + 9U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, file_bytes, 12U);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    inserted = 0;
    saw_vp8x = 0;
    offset = 12U;
    while (offset + 8U <= file_size) {
        omc_u32 chunk_size;
        omc_size padded_size;
        const omc_u8* chunk_type;

        chunk_type = file_bytes + offset;
        chunk_size = omc_transfer_read_u32le(file_bytes + offset + 4U);
        if ((omc_u64)chunk_size + 8U > (omc_u64)(file_size - offset)) {
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        padded_size = 8U + (omc_size)chunk_size + (chunk_size & 1U);
        if (offset + padded_size > file_size) {
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }

        if (memcmp(chunk_type, "ICCP", 4U) == 0) {
        } else if (memcmp(chunk_type, "VP8X", 4U) == 0) {
            omc_u8 vp8x[18];

            if (chunk_size != 10U) {
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            memset(vp8x, 0, sizeof(vp8x));
            memcpy(vp8x, file_bytes + offset, padded_size);
            vp8x[8] = (omc_u8)(vp8x[8] | k_omc_transfer_webp_vp8x_icc_bit);
            status = omc_transfer_append_bytes(out, vp8x, padded_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            saw_vp8x = 1;
            if (!inserted) {
                status = omc_transfer_append_webp_chunk(out, "ICCP", profile,
                                                        profile_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                inserted = 1;
            }
        } else {
            if (!inserted && !saw_vp8x) {
                status = omc_transfer_append_webp_chunk(out, "ICCP", profile,
                                                        profile_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                inserted = 1;
            }
            status = omc_transfer_append_bytes(out, file_bytes + offset,
                                               padded_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        offset += padded_size;
    }

    if (offset != file_size) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (!inserted) {
        status = omc_transfer_append_webp_chunk(out, "ICCP", profile,
                                                profile_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if (out->size < 8U || out->size - 8U > 0xFFFFFFFFU) {
        *out_status = OMC_TRANSFER_LIMIT;
        return OMC_STATUS_OK;
    }
    omc_transfer_store_u32le(out->data + 4U, (omc_u32)(out->size - 8U));
    *out_status = OMC_TRANSFER_OK;
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_rewrite_jp2_icc(const omc_u8* file_bytes, omc_size file_size,
                             const omc_u8* profile, omc_size profile_size,
                             omc_arena* out, omc_transfer_status* out_status)
{
    omc_u64 offset;
    omc_u64 limit;
    omc_status status;
    int saw_signature;
    int found_jp2h;

    if (out_status == (omc_transfer_status*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_status = OMC_TRANSFER_UNSUPPORTED;

    if (file_bytes == (const omc_u8*)0 || profile == (const omc_u8*)0
        || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (file_size < sizeof(k_omc_transfer_jp2_sig)
        || memcmp(file_bytes, k_omc_transfer_jp2_sig,
                  sizeof(k_omc_transfer_jp2_sig))
               != 0) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (profile_size > (omc_size)(0xFFFFFFFFU - 11U)) {
        *out_status = OMC_TRANSFER_LIMIT;
        return OMC_STATUS_OK;
    }
    if ((omc_u64)file_size + (omc_u64)profile_size + 32U
        > (omc_u64)(~(omc_size)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + profile_size + 32U);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    saw_signature = 0;
    found_jp2h = 0;
    offset = 0U;
    limit = (omc_u64)file_size;
    while (offset + 8U <= limit) {
        omc_transfer_bmff_box box;

        if (!omc_transfer_parse_bmff_box(file_bytes, file_size, offset, limit,
                                         &box)) {
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (box.type == omc_transfer_fourcc('j', 'P', ' ', ' ')) {
            saw_signature = 1;
        }

        if (box.type == omc_transfer_fourcc('j', 'p', '2', 'h')) {
            omc_arena rebuilt_payload;
            omc_u64 child_off;
            omc_u64 child_end;
            int inserted_icc;

            if (found_jp2h) {
                *out_status = OMC_TRANSFER_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            found_jp2h = 1;

            omc_arena_init(&rebuilt_payload);
            status = omc_arena_reserve(&rebuilt_payload,
                                       (omc_size)(box.size - box.header_size)
                                           + profile_size + 16U);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&rebuilt_payload);
                return status;
            }

            child_off = box.offset + box.header_size;
            child_end = box.offset + box.size;
            inserted_icc = 0;
            while (child_off + 8U <= child_end) {
                omc_transfer_bmff_box child;

                if (!omc_transfer_parse_bmff_box(file_bytes, file_size,
                                                 child_off, child_end,
                                                 &child)) {
                    omc_arena_fini(&rebuilt_payload);
                    *out_status = OMC_TRANSFER_MALFORMED;
                    return OMC_STATUS_OK;
                }
                if (child.type == omc_transfer_fourcc('c', 'o', 'l', 'r')) {
                    if (!inserted_icc) {
                        status = omc_transfer_append_jp2_colr_icc_box(
                            &rebuilt_payload, profile, profile_size);
                        if (status != OMC_STATUS_OK) {
                            omc_arena_fini(&rebuilt_payload);
                            return status;
                        }
                        inserted_icc = 1;
                    }
                } else {
                    status = omc_transfer_append_bytes(
                        &rebuilt_payload, file_bytes + (omc_size)child.offset,
                        (omc_size)child.size);
                    if (status != OMC_STATUS_OK) {
                        omc_arena_fini(&rebuilt_payload);
                        return status;
                    }
                }
                if (child.size == 0U) {
                    break;
                }
                child_off += child.size;
            }
            if (child_off != child_end) {
                omc_arena_fini(&rebuilt_payload);
                *out_status = OMC_TRANSFER_MALFORMED;
                return OMC_STATUS_OK;
            }
            if (!inserted_icc) {
                status = omc_transfer_append_jp2_colr_icc_box(
                    &rebuilt_payload, profile, profile_size);
                if (status != OMC_STATUS_OK) {
                    omc_arena_fini(&rebuilt_payload);
                    return status;
                }
            }
            status = omc_transfer_append_jp2_box(
                out, omc_transfer_fourcc('j', 'p', '2', 'h'),
                rebuilt_payload.data, rebuilt_payload.size);
            omc_arena_fini(&rebuilt_payload);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        } else {
            status = omc_transfer_append_bytes(out,
                                               file_bytes + (omc_size)box.offset,
                                               (omc_size)box.size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        if (box.size == 0U) {
            break;
        }
        offset += box.size;
    }

    if (!saw_signature || offset != limit) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (!found_jp2h) {
        *out_status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    *out_status = OMC_TRANSFER_OK;
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_rewrite_tiff_tag_bytes(const omc_u8* file_bytes,
                                    omc_size file_size, omc_u16 target_tag,
                                    omc_u16 tiff_type,
                                    const omc_u8* payload,
                                    omc_size payload_size, omc_arena* out,
                                    omc_transfer_status* out_status)
{
    int little_endian;
    int big_tiff;
    omc_u16 magic;
    omc_size count_size;
    omc_size entry_size;
    omc_size next_size;
    omc_size inline_size;
    omc_u64 ifd0_off;
    omc_u64 count_u64;
    omc_u64 next_ifd;
    omc_size entries_off;
    omc_size next_ifd_off;
    omc_size i;
    omc_u32 removed;
    omc_u32 new_count_u32;
    omc_size align;
    omc_size new_ifd_offset;
    omc_size new_ifd_size;
    omc_size entry_table_size;
    omc_size payload_offset;
    omc_u8 entry_buf[20];
    omc_u8* sorted_entries;
    omc_status status;

    if (out_status == (omc_transfer_status*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    *out_status = OMC_TRANSFER_UNSUPPORTED;

    if (file_bytes == (const omc_u8*)0 || out == (omc_arena*)0
        || (payload == (const omc_u8*)0 && payload_size != 0U)) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (file_size < 8U) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (file_bytes[0] == 'I' && file_bytes[1] == 'I') {
        little_endian = 1;
    } else if (file_bytes[0] == 'M' && file_bytes[1] == 'M') {
        little_endian = 0;
    } else {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    magic = omc_transfer_tiff_read_u16(file_bytes + 2U, little_endian);
    if (magic == 43U) {
        omc_u16 off_size;
        omc_u16 reserved;

        big_tiff = 1;
        count_size = 8U;
        entry_size = 20U;
        next_size = 8U;
        inline_size = 8U;
        align = 8U;
        if (file_size < 16U) {
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        off_size = omc_transfer_tiff_read_u16(file_bytes + 4U,
                                              little_endian);
        reserved = omc_transfer_tiff_read_u16(file_bytes + 6U,
                                              little_endian);
        if (off_size != 8U || reserved != 0U) {
            *out_status = OMC_TRANSFER_MALFORMED;
            return OMC_STATUS_OK;
        }
        ifd0_off = omc_transfer_tiff_read_u64(file_bytes + 8U,
                                              little_endian);
    } else if (magic == 42U) {
        big_tiff = 0;
        count_size = 2U;
        entry_size = 12U;
        next_size = 4U;
        inline_size = 4U;
        align = 2U;
        ifd0_off = (omc_u64)omc_transfer_tiff_read_u32(file_bytes + 4U,
                                                       little_endian);
    } else {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (ifd0_off >= (omc_u64)file_size || ifd0_off + count_size > (omc_u64)file_size) {
        *out_status = OMC_TRANSFER_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (big_tiff) {
        count_u64 = omc_transfer_tiff_read_u64(file_bytes + (omc_size)ifd0_off,
                                               little_endian);
        entries_off = (omc_size)ifd0_off + 8U;
    } else {
        count_u64 = (omc_u64)omc_transfer_tiff_read_u16(
            file_bytes + (omc_size)ifd0_off, little_endian);
        entries_off = (omc_size)ifd0_off + 2U;
    }
    if (count_u64 > 0xFFFFU
        || count_u64 * (omc_u64)entry_size > (omc_u64)file_size
        || (omc_u64)entries_off + count_u64 * (omc_u64)entry_size
               + (omc_u64)next_size > (omc_u64)file_size) {
        *out_status = OMC_TRANSFER_LIMIT;
        return OMC_STATUS_OK;
    }
    next_ifd_off = entries_off + (omc_size)count_u64 * entry_size;
    next_ifd = big_tiff
                   ? omc_transfer_tiff_read_u64(file_bytes + next_ifd_off,
                                                little_endian)
                   : (omc_u64)omc_transfer_tiff_read_u32(
                         file_bytes + next_ifd_off, little_endian);

    removed = 0U;
    for (i = 0U; i < (omc_size)count_u64; ++i) {
        const omc_u8* entry;
        omc_u16 entry_tag;

        entry = file_bytes + entries_off + i * entry_size;
        entry_tag = omc_transfer_tiff_read_u16(entry, little_endian);
        if (entry_tag == target_tag) {
            removed += 1U;
        }
    }

    new_count_u32 = (omc_u32)count_u64 - removed + 1U;
    if (new_count_u32 > 0xFFFFU) {
        *out_status = OMC_TRANSFER_LIMIT;
        return OMC_STATUS_OK;
    }

    new_ifd_offset = omc_transfer_align_up(file_size, align);
    entry_table_size = (omc_size)new_count_u32 * entry_size;
    new_ifd_size = count_size + entry_table_size + next_size;
    if (payload_size > inline_size) {
        payload_offset = omc_transfer_align_up(new_ifd_offset + new_ifd_size,
                                               align);
    } else {
        payload_offset = new_ifd_offset + count_size + entry_table_size;
    }
    if ((omc_u64)payload_offset + (omc_u64)payload_size
        > (omc_u64)(~(omc_size)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, payload_offset + payload_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_append_bytes(out, file_bytes, file_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    while (out->size < new_ifd_offset) {
        static const omc_u8 k_pad = 0U;

        status = omc_transfer_append_bytes(out, &k_pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (big_tiff) {
        omc_transfer_tiff_store_u64(out->data + 8U, little_endian,
                                    (omc_u64)new_ifd_offset);
    } else {
        if ((omc_u64)new_ifd_offset > 0xFFFFFFFFU) {
            *out_status = OMC_TRANSFER_LIMIT;
            return OMC_STATUS_OK;
        }
        omc_transfer_tiff_store_u32(out->data + 4U, little_endian,
                                    (omc_u32)new_ifd_offset);
    }

    memset(entry_buf, 0, sizeof(entry_buf));
    if (big_tiff) {
        omc_transfer_tiff_store_u64(entry_buf, little_endian,
                                    (omc_u64)new_count_u32);
    } else {
        omc_transfer_tiff_store_u16(entry_buf, little_endian,
                                    (omc_u16)new_count_u32);
    }
    status = omc_transfer_append_bytes(out, entry_buf, count_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    sorted_entries = out->data + out->size;
    for (i = 0U; i < (omc_size)count_u64; ++i) {
        const omc_u8* entry;
        omc_u16 entry_tag;

        entry = file_bytes + entries_off + i * entry_size;
        entry_tag = omc_transfer_tiff_read_u16(entry, little_endian);
        if (entry_tag == target_tag) {
            continue;
        }
        status = omc_transfer_append_bytes(out, entry, entry_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    memset(entry_buf, 0, sizeof(entry_buf));
    omc_transfer_tiff_store_u16(entry_buf, little_endian, target_tag);
    omc_transfer_tiff_store_u16(entry_buf + 2U, little_endian, tiff_type);
    if (big_tiff) {
        omc_transfer_tiff_store_u64(entry_buf + 4U, little_endian,
                                    (omc_u64)payload_size);
    } else {
        if (payload_size > 0xFFFFFFFFU) {
            *out_status = OMC_TRANSFER_LIMIT;
            return OMC_STATUS_OK;
        }
        omc_transfer_tiff_store_u32(entry_buf + 4U, little_endian,
                                    (omc_u32)payload_size);
    }
    if (payload_size > inline_size) {
        if (big_tiff) {
            omc_transfer_tiff_store_u64(entry_buf + 12U, little_endian,
                                        (omc_u64)payload_offset);
        } else {
            if ((omc_u64)payload_offset > 0xFFFFFFFFU) {
                *out_status = OMC_TRANSFER_LIMIT;
                return OMC_STATUS_OK;
            }
            omc_transfer_tiff_store_u32(entry_buf + 8U, little_endian,
                                        (omc_u32)payload_offset);
        }
    } else if (payload_size != 0U) {
        memcpy(entry_buf + entry_size - inline_size, payload, payload_size);
    }
    status = omc_transfer_append_bytes(out, entry_buf, entry_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    omc_transfer_tiff_insertion_sort(sorted_entries, new_count_u32,
                                     entry_size, little_endian);

    memset(entry_buf, 0, sizeof(entry_buf));
    if (big_tiff) {
        omc_transfer_tiff_store_u64(entry_buf, little_endian, next_ifd);
    } else {
        if (next_ifd > 0xFFFFFFFFU) {
            *out_status = OMC_TRANSFER_LIMIT;
            return OMC_STATUS_OK;
        }
        omc_transfer_tiff_store_u32(entry_buf, little_endian,
                                    (omc_u32)next_ifd);
    }
    status = omc_transfer_append_bytes(out, entry_buf, next_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    while (out->size < payload_offset) {
        static const omc_u8 k_pad = 0U;

        status = omc_transfer_append_bytes(out, &k_pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if (payload_size > inline_size) {
        status = omc_transfer_append_bytes(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    *out_status = OMC_TRANSFER_OK;
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_apply_icc_overlay(const omc_u8* current_bytes,
                               omc_size current_size,
                               const omc_store* store, omc_scan_fmt format,
                               omc_arena* edited_out,
                               omc_transfer_res* out_res)
{
    omc_arena profile;
    omc_arena icc_out;
    omc_status status;
    omc_transfer_status icc_status;
    omc_arena tmp;
    int has_profile;

    if (current_bytes == (const omc_u8*)0 || store == (const omc_store*)0
        || edited_out == (omc_arena*)0
        || out_res == (omc_transfer_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (format != OMC_SCAN_FMT_JPEG && format != OMC_SCAN_FMT_PNG
        && format != OMC_SCAN_FMT_WEBP && format != OMC_SCAN_FMT_JP2
        && format != OMC_SCAN_FMT_TIFF && format != OMC_SCAN_FMT_DNG) {
        return OMC_STATUS_OK;
    }

    omc_arena_init(&profile);
    omc_arena_init(&icc_out);
    has_profile = 0;
    status = omc_transfer_build_icc_profile(store, &profile, &has_profile);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&icc_out);
        omc_arena_fini(&profile);
        return status;
    }
    if (!has_profile) {
        omc_arena_fini(&icc_out);
        omc_arena_fini(&profile);
        return OMC_STATUS_OK;
    }

    icc_status = OMC_TRANSFER_UNSUPPORTED;
    if (format == OMC_SCAN_FMT_JPEG) {
        status = omc_transfer_rewrite_jpeg_icc(current_bytes, current_size,
                                               profile.data, profile.size,
                                               &icc_out, &icc_status);
    } else if (format == OMC_SCAN_FMT_PNG) {
        status = omc_transfer_rewrite_png_icc(current_bytes, current_size,
                                              profile.data, profile.size,
                                              &icc_out, &icc_status);
    } else if (format == OMC_SCAN_FMT_WEBP) {
        status = omc_transfer_rewrite_webp_icc(current_bytes, current_size,
                                               profile.data, profile.size,
                                               &icc_out, &icc_status);
    } else if (format == OMC_SCAN_FMT_JP2) {
        status = omc_transfer_rewrite_jp2_icc(current_bytes, current_size,
                                              profile.data, profile.size,
                                              &icc_out, &icc_status);
    } else {
        status = omc_transfer_rewrite_tiff_tag_bytes(
            current_bytes, current_size, 34675U, 7U, profile.data,
            profile.size, &icc_out, &icc_status);
    }
    omc_arena_fini(&profile);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&icc_out);
        return status;
    }
    if (icc_status != OMC_TRANSFER_OK) {
        omc_arena_fini(&icc_out);
        out_res->status = icc_status;
        return OMC_STATUS_OK;
    }

    tmp = *edited_out;
    *edited_out = icc_out;
    icc_out = tmp;
    omc_arena_fini(&icc_out);
    out_res->edited_present = 1;
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_apply_iptc_overlay(const omc_u8* current_bytes,
                                omc_size current_size,
                                const omc_store* store, omc_scan_fmt format,
                                omc_arena* edited_out,
                                omc_transfer_res* out_res)
{
    omc_arena iim;
    omc_arena iptc_out;
    omc_arena tmp;
    omc_status status;
    omc_transfer_status iptc_status;
    int has_iptc;
    int iptc_supported;

    if (current_bytes == (const omc_u8*)0 || store == (const omc_store*)0
        || edited_out == (omc_arena*)0
        || out_res == (omc_transfer_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (format != OMC_SCAN_FMT_JPEG && format != OMC_SCAN_FMT_TIFF
        && format != OMC_SCAN_FMT_DNG) {
        return OMC_STATUS_OK;
    }

    omc_arena_init(&iim);
    omc_arena_init(&iptc_out);
    has_iptc = 0;
    iptc_supported = 1;
    status = omc_transfer_build_iptc_iim(store, &iim, &has_iptc,
                                         &iptc_supported);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&iptc_out);
        omc_arena_fini(&iim);
        return status;
    }
    if (!has_iptc) {
        omc_arena_fini(&iptc_out);
        omc_arena_fini(&iim);
        return OMC_STATUS_OK;
    }
    if (!iptc_supported) {
        omc_arena_fini(&iptc_out);
        omc_arena_fini(&iim);
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    iptc_status = OMC_TRANSFER_UNSUPPORTED;
    if (format == OMC_SCAN_FMT_JPEG) {
        omc_arena irb;

        omc_arena_init(&irb);
        status = omc_transfer_build_photoshop_iptc_irb(iim.data, iim.size,
                                                       &irb);
        if (status == OMC_STATUS_OK) {
            status = omc_transfer_rewrite_jpeg_iptc(
                current_bytes, current_size, irb.data, irb.size, &iptc_out,
                &iptc_status);
        }
        omc_arena_fini(&irb);
    } else {
        status = omc_transfer_rewrite_tiff_tag_bytes(
            current_bytes, current_size, 33723U, 7U, iim.data, iim.size,
            &iptc_out, &iptc_status);
    }
    omc_arena_fini(&iim);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&iptc_out);
        return status;
    }
    if (iptc_status != OMC_TRANSFER_OK) {
        omc_arena_fini(&iptc_out);
        out_res->status = iptc_status;
        return OMC_STATUS_OK;
    }

    tmp = *edited_out;
    *edited_out = iptc_out;
    iptc_out = tmp;
    omc_arena_fini(&iptc_out);
    out_res->edited_present = 1;
    return OMC_STATUS_OK;
}

static int
omc_transfer_entry_is_xmp(const omc_entry* entry)
{
    return entry != (const omc_entry*)0
           && entry->key.kind == OMC_KEY_XMP_PROPERTY;
}

static omc_status
omc_transfer_clone_ref(const omc_arena* src, omc_byte_ref ref, omc_arena* dst,
                       omc_byte_ref* out_ref)
{
    omc_const_bytes view;

    if (src == (const omc_arena*)0 || dst == (omc_arena*)0
        || out_ref == (omc_byte_ref*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    out_ref->offset = 0U;
    out_ref->size = 0U;
    if (ref.size == 0U) {
        return OMC_STATUS_OK;
    }

    view = omc_arena_view(src, ref);
    if (view.data == (const omc_u8*)0) {
        return OMC_STATUS_STATE;
    }

    return omc_arena_append(dst, view.data, view.size, out_ref);
}

static omc_status
omc_transfer_clone_key(const omc_key* key, const omc_arena* src,
                       omc_arena* dst, omc_key* out_key)
{
    omc_status status;

    if (key == (const omc_key*)0 || src == (const omc_arena*)0
        || dst == (omc_arena*)0 || out_key == (omc_key*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_key = *key;
    switch (key->kind) {
    case OMC_KEY_EXIF_TAG:
        return omc_transfer_clone_ref(src, key->u.exif_tag.ifd, dst,
                                      &out_key->u.exif_tag.ifd);
    case OMC_KEY_COMMENT:
    case OMC_KEY_IPTC_DATASET:
    case OMC_KEY_ICC_HEADER_FIELD:
    case OMC_KEY_ICC_TAG:
    case OMC_KEY_PHOTOSHOP_IRB:
    case OMC_KEY_GEOTIFF_KEY:
        return OMC_STATUS_OK;
    case OMC_KEY_EXR_ATTR:
        return omc_transfer_clone_ref(src, key->u.exr_attr.name, dst,
                                      &out_key->u.exr_attr.name);
    case OMC_KEY_XMP_PROPERTY:
        status = omc_transfer_clone_ref(src, key->u.xmp_property.schema_ns,
                                        dst,
                                        &out_key->u.xmp_property.schema_ns);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        return omc_transfer_clone_ref(src, key->u.xmp_property.property_path,
                                      dst,
                                      &out_key->u.xmp_property.property_path);
    case OMC_KEY_PHOTOSHOP_IRB_FIELD:
        return omc_transfer_clone_ref(src,
                                      key->u.photoshop_irb_field.field, dst,
                                      &out_key->u.photoshop_irb_field.field);
    case OMC_KEY_PRINTIM_FIELD:
        return omc_transfer_clone_ref(src, key->u.printim_field.field, dst,
                                      &out_key->u.printim_field.field);
    case OMC_KEY_BMFF_FIELD:
        return omc_transfer_clone_ref(src, key->u.bmff_field.field, dst,
                                      &out_key->u.bmff_field.field);
    case OMC_KEY_JUMBF_FIELD:
        return omc_transfer_clone_ref(src, key->u.jumbf_field.field, dst,
                                      &out_key->u.jumbf_field.field);
    case OMC_KEY_JUMBF_CBOR_KEY:
        return omc_transfer_clone_ref(src, key->u.jumbf_cbor_key.key, dst,
                                      &out_key->u.jumbf_cbor_key.key);
    case OMC_KEY_PNG_TEXT:
        status = omc_transfer_clone_ref(src, key->u.png_text.keyword, dst,
                                        &out_key->u.png_text.keyword);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        return omc_transfer_clone_ref(src, key->u.png_text.field, dst,
                                      &out_key->u.png_text.field);
    }

    return OMC_STATUS_STATE;
}

static omc_status
omc_transfer_clone_value(const omc_val* value, const omc_arena* src,
                         omc_arena* dst, omc_val* out_value)
{
    if (value == (const omc_val*)0 || src == (const omc_arena*)0
        || dst == (omc_arena*)0 || out_value == (omc_val*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_value = *value;
    if (value->kind == OMC_VAL_ARRAY || value->kind == OMC_VAL_BYTES
        || value->kind == OMC_VAL_TEXT) {
        return omc_transfer_clone_ref(src, value->u.ref, dst,
                                      &out_value->u.ref);
    }

    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_clone_origin(const omc_origin* origin, const omc_arena* src,
                          omc_arena* dst, omc_origin* out_origin)
{
    if (origin == (const omc_origin*)0 || src == (const omc_arena*)0
        || dst == (omc_arena*)0 || out_origin == (omc_origin*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_origin = *origin;
    return omc_transfer_clone_ref(src, origin->wire_type_name, dst,
                                  &out_origin->wire_type_name);
}

static omc_status
omc_transfer_clone_entry(const omc_entry* entry, const omc_arena* src,
                         omc_arena* dst, omc_entry* out_entry)
{
    omc_status status;

    if (entry == (const omc_entry*)0 || src == (const omc_arena*)0
        || dst == (omc_arena*)0 || out_entry == (omc_entry*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_entry = *entry;
    status = omc_transfer_clone_key(&entry->key, src, dst, &out_entry->key);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_transfer_clone_value(&entry->value, src, dst,
                                      &out_entry->value);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_transfer_clone_origin(&entry->origin, src, dst,
                                     &out_entry->origin);
}

static void
omc_transfer_reset_cloned_xmp_origin(omc_entry* entry)
{
    if (entry == (omc_entry*)0) {
        return;
    }

    entry->origin.block = OMC_INVALID_BLOCK_ID;
    entry->origin.order_in_block = 0U;
    entry->origin.wire_type.family = OMC_WIRE_NONE;
    entry->origin.wire_type.code = 0U;
    entry->origin.wire_count = 0U;
    entry->origin.wire_type_name.offset = 0U;
    entry->origin.wire_type_name.size = 0U;
    entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry->origin.name_context_variant = 0U;
}

static omc_status
omc_transfer_copy_blocks(const omc_store* src, omc_store* dst)
{
    omc_size i;
    omc_status status;

    if (src == (const omc_store*)0 || dst == (omc_store*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    status = omc_store_reserve_blocks(dst, src->block_count);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    for (i = 0U; i < src->block_count; ++i) {
        status = omc_store_add_block(dst, &src->blocks[i],
                                     (omc_block_id*)0);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_append_entries(const omc_store* src, omc_store* dst,
                            int want_xmp, int reset_xmp_origin)
{
    omc_size i;
    omc_status status;

    if (src == (const omc_store*)0 || dst == (omc_store*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    status = omc_store_reserve_entries(dst,
                                       dst->entry_count + src->entry_count);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    for (i = 0U; i < src->entry_count; ++i) {
        omc_entry copied;
        int is_xmp;

        if ((src->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        is_xmp = omc_transfer_entry_is_xmp(&src->entries[i]);
        if ((want_xmp != 0) != (is_xmp != 0)) {
            continue;
        }

        status = omc_transfer_clone_entry(&src->entries[i], &src->arena,
                                          &dst->arena, &copied);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        if (is_xmp && reset_xmp_origin != 0) {
            omc_transfer_reset_cloned_xmp_origin(&copied);
        }
        status = omc_store_add_entry(dst, &copied, (omc_entry_id*)0);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static int
omc_transfer_needs_existing_xmp_merge(const omc_transfer_exec* exec)
{
    if (exec == (const omc_transfer_exec*)0) {
        return 0;
    }

    return (exec->existing_sidecar_xmp_store != (const omc_store*)0
            && exec->existing_sidecar_xmp_mode
                   == OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT)
           || (exec->existing_embedded_xmp_store != (const omc_store*)0
               && exec->existing_embedded_xmp_mode
                      == OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT);
}

static omc_status
omc_transfer_merge_existing_xmp_store(const omc_store* source,
                                      const omc_transfer_exec* exec,
                                      omc_store* out)
{
    const omc_store* first_store;
    const omc_store* second_store;
    omc_transfer_existing_xmp_precedence first_precedence;
    omc_transfer_existing_xmp_precedence second_precedence;
    omc_status status;

    if (source == (const omc_store*)0 || exec == (const omc_transfer_exec*)0
        || out == (omc_store*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_store_init(out);
    status = omc_transfer_copy_blocks(source, out);
    if (status != OMC_STATUS_OK) {
        omc_store_fini(out);
        return status;
    }
    status = omc_transfer_append_entries(source, out, 0, 0);
    if (status != OMC_STATUS_OK) {
        omc_store_fini(out);
        return status;
    }

    if (exec->existing_xmp_carrier_precedence
        == OMC_TRANSFER_EXISTING_XMP_PREFER_EMBEDDED) {
        first_store = exec->existing_embedded_xmp_mode
                              == OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT
                          ? exec->existing_embedded_xmp_store
                          : (const omc_store*)0;
        first_precedence = exec->existing_embedded_xmp_precedence;
        second_store = exec->existing_sidecar_xmp_mode
                               == OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT
                           ? exec->existing_sidecar_xmp_store
                           : (const omc_store*)0;
        second_precedence = exec->existing_sidecar_xmp_precedence;
    } else {
        first_store = exec->existing_sidecar_xmp_mode
                              == OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT
                          ? exec->existing_sidecar_xmp_store
                          : (const omc_store*)0;
        first_precedence = exec->existing_sidecar_xmp_precedence;
        second_store = exec->existing_embedded_xmp_mode
                               == OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT
                           ? exec->existing_embedded_xmp_store
                           : (const omc_store*)0;
        second_precedence = exec->existing_embedded_xmp_precedence;
    }

    if (first_store != (const omc_store*)0
        && first_precedence == OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING) {
        status = omc_transfer_append_entries(first_store, out, 1, 1);
        if (status != OMC_STATUS_OK) {
            omc_store_fini(out);
            return status;
        }
    }
    if (second_store != (const omc_store*)0
        && second_precedence == OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING) {
        status = omc_transfer_append_entries(second_store, out, 1, 1);
        if (status != OMC_STATUS_OK) {
            omc_store_fini(out);
            return status;
        }
    }

    status = omc_transfer_append_entries(source, out, 1, 0);
    if (status != OMC_STATUS_OK) {
        omc_store_fini(out);
        return status;
    }

    if (first_store != (const omc_store*)0
        && first_precedence == OMC_TRANSFER_EXISTING_XMP_PREFER_SOURCE) {
        status = omc_transfer_append_entries(first_store, out, 1, 1);
        if (status != OMC_STATUS_OK) {
            omc_store_fini(out);
            return status;
        }
    }
    if (second_store != (const omc_store*)0
        && second_precedence == OMC_TRANSFER_EXISTING_XMP_PREFER_SOURCE) {
        status = omc_transfer_append_entries(second_store, out, 1, 1);
        if (status != OMC_STATUS_OK) {
            omc_store_fini(out);
            return status;
        }
    }

    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_apply_exif_overlay(const omc_u8* current_bytes,
                                omc_size current_size,
                                const omc_store* store, omc_scan_fmt format,
                                omc_arena* edited_out,
                                omc_transfer_res* out_res)
{
    omc_exif_write_res exif_res;
    omc_arena exif_out;
    omc_arena tmp;
    omc_status status;

    if (current_bytes == (const omc_u8*)0 || store == (const omc_store*)0
        || edited_out == (omc_arena*)0
        || out_res == (omc_transfer_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_arena_init(&exif_out);
    omc_exif_write_res_init(&exif_res);
    status = omc_exif_write_embedded(current_bytes, current_size, store,
                                     &exif_out, format, &exif_res);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&exif_out);
        return status;
    }
    if (exif_res.status != OMC_EXIF_WRITE_OK) {
        out_res->status = omc_transfer_status_from_exif(exif_res.status);
        omc_arena_fini(&exif_out);
        return OMC_STATUS_OK;
    }

    tmp = *edited_out;
    *edited_out = exif_out;
    exif_out = tmp;
    omc_arena_fini(&exif_out);
    out_res->edited_present = 1;
    return OMC_STATUS_OK;
}

static omc_status
omc_transfer_execute_dng_minimal_scaffold(
    const omc_store* source_store, const omc_store* effective_store,
    const omc_transfer_exec* exec, int has_sidecar_route,
    int source_has_exif, int source_has_icc, int source_has_iptc,
    omc_arena* edited_out,
    omc_arena* sidecar_out,
    omc_transfer_res* out_res)
{
    omc_xmp_apply_opts apply_opts;
    omc_xmp_apply_res apply_res;
    omc_arena scaffold;
    omc_status status;

    if (source_store == (const omc_store*)0
        || effective_store == (const omc_store*)0
        || exec == (const omc_transfer_exec*)0
        || edited_out == (omc_arena*)0
        || sidecar_out == (omc_arena*)0
        || out_res == (omc_transfer_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_arena_init(&scaffold);
    status = omc_transfer_build_minimal_dng_scaffold(&scaffold);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&scaffold);
        return status;
    }

    omc_xmp_apply_opts_init(&apply_opts);
    apply_opts.format = OMC_SCAN_FMT_DNG;
    apply_opts.embedded = exec->embedded_write.embed;
    apply_opts.sidecar = exec->sidecar;
    apply_opts.writeback_mode = has_sidecar_route
                                    ? OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR
                                    : OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    apply_opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;

    status = omc_xmp_apply(scaffold.data, scaffold.size, effective_store,
                           edited_out, sidecar_out, &apply_opts, &apply_res);
    omc_arena_fini(&scaffold);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    out_res->embedded = apply_res.embedded;
    out_res->sidecar = apply_res.sidecar;
    out_res->edited_present = apply_res.embedded.status == OMC_XMP_WRITE_OK;
    out_res->sidecar_present =
        apply_res.sidecar_requested && apply_res.sidecar.status == OMC_XMP_DUMP_OK;

    if (apply_res.embedded.status != OMC_XMP_WRITE_OK) {
        out_res->status =
            omc_transfer_status_from_write(apply_res.embedded.status);
        return OMC_STATUS_OK;
    }
    if (apply_res.sidecar_requested
        && apply_res.sidecar.status != OMC_XMP_DUMP_OK) {
        out_res->status =
            omc_transfer_status_from_dump(apply_res.sidecar.status);
        return OMC_STATUS_OK;
    }

    out_res->status = OMC_TRANSFER_OK;
    if (source_has_exif) {
        status = omc_transfer_apply_exif_overlay(
            edited_out->data, edited_out->size, source_store,
            OMC_SCAN_FMT_DNG, edited_out, out_res);
        if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
            return status;
        }
    }
    if (source_has_icc) {
        status = omc_transfer_apply_icc_overlay(
            edited_out->data, edited_out->size, source_store,
            OMC_SCAN_FMT_DNG, edited_out, out_res);
        if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
            return status;
        }
    }
    if (source_has_iptc) {
        status = omc_transfer_apply_iptc_overlay(
            edited_out->data, edited_out->size, source_store,
            OMC_SCAN_FMT_DNG, edited_out, out_res);
    }
    return status;
}

void
omc_transfer_prepare_opts_init(omc_transfer_prepare_opts* opts)
{
    if (opts == (omc_transfer_prepare_opts*)0) {
        return;
    }

    opts->format = OMC_SCAN_FMT_UNKNOWN;
    opts->dng_target_mode = OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD;
    opts->writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    opts->destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;
    opts->existing_sidecar_xmp_store = (const omc_store*)0;
    opts->existing_sidecar_xmp_mode =
        OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT;
    opts->existing_sidecar_xmp_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING;
    opts->existing_embedded_xmp_store = (const omc_store*)0;
    opts->existing_embedded_xmp_mode =
        OMC_TRANSFER_EXISTING_XMP_MERGE_IF_PRESENT;
    opts->existing_embedded_xmp_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_EXISTING;
    opts->existing_xmp_carrier_precedence =
        OMC_TRANSFER_EXISTING_XMP_PREFER_SIDECAR;
    omc_xmp_embed_opts_init(&opts->embedded);
    opts->embedded.packet.include_existing_xmp = 1;
    opts->embedded.packet.include_exif = 0;
    opts->embedded.packet.include_iptc = 0;
    omc_xmp_sidecar_req_init(&opts->sidecar);
    opts->sidecar.format = OMC_XMP_SIDECAR_PORTABLE;
    opts->sidecar.include_existing_xmp = 1;
}

omc_status
omc_transfer_prepare(const omc_u8* file_bytes, omc_size file_size,
                     const omc_store* store,
                     const omc_transfer_prepare_opts* opts,
                     omc_transfer_bundle* out_bundle)
{
    omc_transfer_prepare_opts local_opts;
    omc_scan_fmt format;
    int sidecar_requested;
    int needs_embedded;

    if (store == (const omc_store*)0 || out_bundle == (omc_transfer_bundle*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_transfer_prepare_opts*)0) {
        omc_transfer_prepare_opts_init(&local_opts);
        opts = &local_opts;
    }

    if (file_bytes == (const omc_u8*)0 && file_size != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_bundle_init(out_bundle);
    out_bundle->dng_target_mode = opts->dng_target_mode;
    out_bundle->writeback_mode = opts->writeback_mode;
    out_bundle->destination_embedded_mode =
        opts->destination_embedded_mode;
    out_bundle->existing_sidecar_xmp_store =
        opts->existing_sidecar_xmp_store;
    out_bundle->existing_sidecar_xmp_mode =
        opts->existing_sidecar_xmp_mode;
    out_bundle->existing_sidecar_xmp_precedence =
        opts->existing_sidecar_xmp_precedence;
    out_bundle->existing_embedded_xmp_store =
        opts->existing_embedded_xmp_store;
    out_bundle->existing_embedded_xmp_mode =
        opts->existing_embedded_xmp_mode;
    out_bundle->existing_embedded_xmp_precedence =
        opts->existing_embedded_xmp_precedence;
    out_bundle->existing_xmp_carrier_precedence =
        opts->existing_xmp_carrier_precedence;
    out_bundle->embedded = opts->embedded;
    out_bundle->sidecar = opts->sidecar;

    format = opts->format;
    if (format == OMC_SCAN_FMT_UNKNOWN) {
        format = omc_transfer_detect_format(file_bytes, file_size);
    }
    out_bundle->format = format;
    out_bundle->embedded_supported = omc_transfer_embedded_supported(format);
    out_bundle->existing_xmp_blocks =
        omc_transfer_count_existing_xmp_blocks(file_bytes, file_size);

    if (format == OMC_SCAN_FMT_DNG
        && omc_transfer_dng_target_requires_existing_target(
            opts->dng_target_mode)
        && (file_bytes == (const omc_u8*)0 || file_size == 0U)) {
        out_bundle->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    sidecar_requested =
        opts->writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    out_bundle->sidecar_requested = sidecar_requested;

    needs_embedded = 0;
    if (opts->writeback_mode == OMC_XMP_WRITEBACK_EMBEDDED_ONLY
        || opts->writeback_mode == OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR) {
        needs_embedded = 1;
        out_bundle->embedded_action = OMC_TRANSFER_EMBEDDED_REWRITE;
    } else if (opts->destination_embedded_mode
               == OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING) {
        needs_embedded = 1;
        out_bundle->embedded_action = OMC_TRANSFER_EMBEDDED_STRIP;
    }

    if (needs_embedded) {
        if (!out_bundle->embedded_supported) {
            out_bundle->status = OMC_TRANSFER_UNSUPPORTED;
            return OMC_STATUS_OK;
        }
        out_bundle->route_count += 1U;
    }
    if (sidecar_requested) {
        out_bundle->route_count += 1U;
    }

    if (out_bundle->route_count == 0U) {
        out_bundle->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    if (needs_embedded && format == OMC_SCAN_FMT_UNKNOWN) {
        out_bundle->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    out_bundle->status = OMC_TRANSFER_OK;
    return OMC_STATUS_OK;
}

omc_status
omc_transfer_compile(const omc_transfer_bundle* bundle,
                     omc_transfer_exec* out_exec)
{
    omc_u32 route_index;

    if (bundle == (const omc_transfer_bundle*)0
        || out_exec == (omc_transfer_exec*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_exec_init(out_exec);
    out_exec->status = bundle->status;
    out_exec->format = bundle->format;
    out_exec->dng_target_mode = bundle->dng_target_mode;
    out_exec->writeback_mode = bundle->writeback_mode;
    out_exec->existing_sidecar_xmp_store =
        bundle->existing_sidecar_xmp_store;
    out_exec->existing_sidecar_xmp_mode =
        bundle->existing_sidecar_xmp_mode;
    out_exec->existing_sidecar_xmp_precedence =
        bundle->existing_sidecar_xmp_precedence;
    out_exec->existing_embedded_xmp_store =
        bundle->existing_embedded_xmp_store;
    out_exec->existing_embedded_xmp_mode =
        bundle->existing_embedded_xmp_mode;
    out_exec->existing_embedded_xmp_precedence =
        bundle->existing_embedded_xmp_precedence;
    out_exec->existing_xmp_carrier_precedence =
        bundle->existing_xmp_carrier_precedence;
    if (bundle->status != OMC_TRANSFER_OK) {
        return OMC_STATUS_OK;
    }

    route_index = 0U;
    if (bundle->embedded_action != OMC_TRANSFER_EMBEDDED_NONE) {
        out_exec->routes[route_index].kind = OMC_TRANSFER_ROUTE_EMBEDDED_XMP;
        out_exec->routes[route_index].embedded_action =
            bundle->embedded_action;
        out_exec->embedded_write.format = bundle->format;
        out_exec->embedded_write.embed = bundle->embedded;
        out_exec->embedded_write.write_embedded_xmp =
            bundle->embedded_action == OMC_TRANSFER_EMBEDDED_REWRITE;
        out_exec->embedded_write.strip_existing_xmp = 1;
        route_index += 1U;
    }
    if (bundle->sidecar_requested) {
        out_exec->routes[route_index].kind = OMC_TRANSFER_ROUTE_SIDECAR_XMP;
        out_exec->routes[route_index].embedded_action =
            OMC_TRANSFER_EMBEDDED_NONE;
        out_exec->sidecar = bundle->sidecar;
        route_index += 1U;
    }

    out_exec->route_count = route_index;
    if (route_index == 0U) {
        out_exec->status = OMC_TRANSFER_UNSUPPORTED;
    }
    return OMC_STATUS_OK;
}

omc_status
omc_transfer_execute(const omc_u8* file_bytes, omc_size file_size,
                     const omc_store* store, omc_arena* edited_out,
                     omc_arena* sidecar_out,
                     const omc_transfer_exec* exec,
                     omc_transfer_res* out_res)
{
    omc_u32 i;
    omc_status status;
    int has_embedded_route;
    int has_sidecar_route;
    int source_has_exif;
    int source_has_icc;
    int source_has_iptc;
    omc_transfer_embedded_action embedded_action;
    omc_store merged_store;
    const omc_store* effective_store;
    int merged_store_ready;

    if (store == (const omc_store*)0 || edited_out == (omc_arena*)0
        || sidecar_out == (omc_arena*)0
        || exec == (const omc_transfer_exec*)0
        || out_res == (omc_transfer_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (file_bytes == (const omc_u8*)0 && file_size != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_transfer_res_init(out_res);
    out_res->format = exec->format;
    out_res->dng_target_mode = exec->dng_target_mode;
    out_res->writeback_mode = exec->writeback_mode;
    out_res->route_count = exec->route_count;
    omc_arena_reset(edited_out);
    omc_arena_reset(sidecar_out);

    if (exec->status != OMC_TRANSFER_OK) {
        out_res->status = exec->status;
        return OMC_STATUS_OK;
    }

    if (exec->format == OMC_SCAN_FMT_DNG
        && omc_transfer_dng_target_requires_existing_target(
            exec->dng_target_mode)
        && (file_bytes == (const omc_u8*)0 || file_size == 0U)) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    has_embedded_route = 0;
    has_sidecar_route = 0;
    source_has_exif = omc_exif_write_store_has_supported_tags(store);
    source_has_icc = omc_transfer_store_has_icc(store);
    source_has_iptc = (exec->format == OMC_SCAN_FMT_JPEG
                       || exec->format == OMC_SCAN_FMT_TIFF
                       || exec->format == OMC_SCAN_FMT_DNG)
                      && omc_transfer_store_has_iptc(store);
    embedded_action = OMC_TRANSFER_EMBEDDED_NONE;
    effective_store = store;
    merged_store_ready = 0;
    for (i = 0U; i < exec->route_count; ++i) {
        if (exec->routes[i].kind == OMC_TRANSFER_ROUTE_EMBEDDED_XMP) {
            has_embedded_route = 1;
            embedded_action = exec->routes[i].embedded_action;
        } else if (exec->routes[i].kind == OMC_TRANSFER_ROUTE_SIDECAR_XMP) {
            has_sidecar_route = 1;
        }
    }

    if (omc_transfer_needs_existing_xmp_merge(exec)) {
        status = omc_transfer_merge_existing_xmp_store(store, exec,
                                                       &merged_store);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        effective_store = &merged_store;
        merged_store_ready = 1;
    }

    if (has_embedded_route) {
        omc_xmp_apply_opts apply_opts;
        omc_xmp_apply_res apply_res;

        if (file_bytes == (const omc_u8*)0) {
            if (exec->format == OMC_SCAN_FMT_DNG
                && exec->dng_target_mode
                       == OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD
                && exec->writeback_mode != OMC_XMP_WRITEBACK_SIDECAR_ONLY) {
                status = omc_transfer_execute_dng_minimal_scaffold(
                    store, effective_store, exec, has_sidecar_route,
                    source_has_exif, source_has_icc, source_has_iptc,
                    edited_out, sidecar_out, out_res);
                if (merged_store_ready) {
                    omc_store_fini(&merged_store);
                }
                return status;
            }
            return OMC_STATUS_INVALID_ARGUMENT;
        }

        omc_xmp_apply_opts_init(&apply_opts);
        apply_opts.format = exec->format;
        apply_opts.embedded = exec->embedded_write.embed;
        apply_opts.sidecar = exec->sidecar;

        if (embedded_action == OMC_TRANSFER_EMBEDDED_STRIP) {
            apply_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
            apply_opts.destination_embedded_mode =
                OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        } else if (has_sidecar_route) {
            apply_opts.writeback_mode =
                OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        } else {
            apply_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
        }

        status = omc_xmp_apply(file_bytes, file_size, effective_store,
                               edited_out, sidecar_out, &apply_opts,
                               &apply_res);
        if (status != OMC_STATUS_OK) {
            if (merged_store_ready) {
                omc_store_fini(&merged_store);
            }
            return status;
        }

        out_res->embedded = apply_res.embedded;
        out_res->sidecar = apply_res.sidecar;
        out_res->edited_present =
            apply_res.embedded.status == OMC_XMP_WRITE_OK;
        out_res->sidecar_present =
            apply_res.sidecar_requested
            && apply_res.sidecar.status == OMC_XMP_DUMP_OK;

        if (apply_res.embedded.status != OMC_XMP_WRITE_OK) {
            out_res->status =
                omc_transfer_status_from_write(apply_res.embedded.status);
            if (merged_store_ready) {
                omc_store_fini(&merged_store);
            }
            return OMC_STATUS_OK;
        }
        if (apply_res.sidecar_requested
            && apply_res.sidecar.status != OMC_XMP_DUMP_OK) {
            out_res->status =
                omc_transfer_status_from_dump(apply_res.sidecar.status);
            if (merged_store_ready) {
                omc_store_fini(&merged_store);
            }
            return OMC_STATUS_OK;
        }

        out_res->status = OMC_TRANSFER_OK;
        if (source_has_exif) {
            status = omc_transfer_apply_exif_overlay(
                edited_out->data, edited_out->size, store, exec->format,
                edited_out, out_res);
            if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
                if (merged_store_ready) {
                    omc_store_fini(&merged_store);
                }
                return status;
            }
        }
        if (source_has_icc) {
            status = omc_transfer_apply_icc_overlay(
                edited_out->data, edited_out->size, store, exec->format,
                edited_out, out_res);
            if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
                if (merged_store_ready) {
                    omc_store_fini(&merged_store);
                }
                return status;
            }
        }
        if (source_has_iptc) {
            status = omc_transfer_apply_iptc_overlay(
                edited_out->data, edited_out->size, store, exec->format,
                edited_out, out_res);
            if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
                if (merged_store_ready) {
                    omc_store_fini(&merged_store);
                }
                return status;
            }
        }
        if (merged_store_ready) {
            omc_store_fini(&merged_store);
        }
        return OMC_STATUS_OK;
    }

    if (has_sidecar_route && omc_transfer_embedded_supported(exec->format)) {
        omc_xmp_apply_opts apply_opts;
        omc_xmp_apply_res apply_res;

        if (file_bytes == (const omc_u8*)0) {
            if (exec->format == OMC_SCAN_FMT_DNG
                && exec->dng_target_mode
                       == OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD) {
                status = omc_xmp_dump_sidecar_req(effective_store, sidecar_out,
                                                  &exec->sidecar,
                                                  &out_res->sidecar);
                if (merged_store_ready) {
                    omc_store_fini(&merged_store);
                }
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->sidecar_present =
                    out_res->sidecar.status == OMC_XMP_DUMP_OK;
                out_res->status = out_res->sidecar.status == OMC_XMP_DUMP_OK
                                      ? OMC_TRANSFER_OK
                                      : omc_transfer_status_from_dump(
                                            out_res->sidecar.status);
                return OMC_STATUS_OK;
            }
            return OMC_STATUS_INVALID_ARGUMENT;
        }

        omc_xmp_apply_opts_init(&apply_opts);
        apply_opts.format = exec->format;
        apply_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        apply_opts.destination_embedded_mode =
            OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;
        apply_opts.sidecar = exec->sidecar;

        status = omc_xmp_apply(file_bytes, file_size, effective_store,
                               edited_out, sidecar_out, &apply_opts,
                               &apply_res);
        if (status != OMC_STATUS_OK) {
            if (merged_store_ready) {
                omc_store_fini(&merged_store);
            }
            return status;
        }

        out_res->embedded = apply_res.embedded;
        out_res->sidecar = apply_res.sidecar;
        out_res->edited_present =
            apply_res.embedded.status == OMC_XMP_WRITE_OK;
        out_res->sidecar_present =
            apply_res.sidecar_requested
            && apply_res.sidecar.status == OMC_XMP_DUMP_OK;

        if (apply_res.embedded.status != OMC_XMP_WRITE_OK) {
            out_res->status =
                omc_transfer_status_from_write(apply_res.embedded.status);
            if (merged_store_ready) {
                omc_store_fini(&merged_store);
            }
            return OMC_STATUS_OK;
        }
        out_res->status = omc_transfer_status_from_dump(out_res->sidecar.status);
        if (out_res->status == OMC_TRANSFER_OK && source_has_exif) {
            status = omc_transfer_apply_exif_overlay(
                edited_out->data, edited_out->size, store, exec->format,
                edited_out, out_res);
            if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
                if (merged_store_ready) {
                    omc_store_fini(&merged_store);
                }
                return status;
            }
        }
        if (out_res->status == OMC_TRANSFER_OK && source_has_iptc) {
            status = omc_transfer_apply_iptc_overlay(
                edited_out->data, edited_out->size, store, exec->format,
                edited_out, out_res);
            if (status != OMC_STATUS_OK || out_res->status != OMC_TRANSFER_OK) {
                if (merged_store_ready) {
                    omc_store_fini(&merged_store);
                }
                return status;
            }
        }
        if (merged_store_ready) {
            omc_store_fini(&merged_store);
        }
        return OMC_STATUS_OK;
    }

    if (!has_sidecar_route) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        if (merged_store_ready) {
            omc_store_fini(&merged_store);
        }
        return OMC_STATUS_OK;
    }

    if (source_has_exif) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        if (merged_store_ready) {
            omc_store_fini(&merged_store);
        }
        return OMC_STATUS_OK;
    }

    status = omc_xmp_dump_sidecar_req(effective_store, sidecar_out,
                                      &exec->sidecar, &out_res->sidecar);
    if (status != OMC_STATUS_OK) {
        if (merged_store_ready) {
            omc_store_fini(&merged_store);
        }
        return status;
    }
    out_res->sidecar_present = out_res->sidecar.status == OMC_XMP_DUMP_OK;
    out_res->status = omc_transfer_status_from_dump(out_res->sidecar.status);
    if (merged_store_ready) {
        omc_store_fini(&merged_store);
    }
    return OMC_STATUS_OK;
}
