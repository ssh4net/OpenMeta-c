#include "omc_bmff_rewrite.h"
#include "../base/omc_crc32.h"
#include "omc_exif_write.h"

#include "omc/omc_read.h"
#include "omc/omc_edit.h"
#include "omc/omc_exif_tiff_serialize.h"
#include "../core/omc_value_internal.h"
#include <stdlib.h>

#include <string.h>

static const omc_u8 k_omc_exif_write_png_sig[8] = {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
};
static const omc_u8 k_omc_exif_write_jp2_sig[12] = {
    0x00U, 0x00U, 0x00U, 0x0CU, 0x6AU, 0x50U, 0x20U, 0x20U,
    0x0DU, 0x0AU, 0x87U, 0x0AU
};
static const omc_u8 k_omc_exif_write_jxl_sig[12] = {
    0x00U, 0x00U, 0x00U, 0x0CU, 0x4AU, 0x58U, 0x4CU, 0x20U,
    0x0DU, 0x0AU, 0x87U, 0x0AU
};

static const omc_u8 k_omc_exif_write_jpeg_prefix[] = {
    'E', 'x', 'i', 'f', 0x00U, 0x00U
};

static const omc_u8 k_omc_exif_write_webp_vp8x_exif_bit = 0x08U;

typedef struct omc_exif_write_bmff_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
} omc_exif_write_bmff_box;

static omc_status
omc_exif_write_append(omc_arena* out, const void* src, omc_size size)
{
    omc_byte_ref ref;

    return omc_arena_append(out, src, size, &ref);
}

static omc_u16
omc_exif_write_read_u16le(const omc_u8* src)
{
    return (omc_u16)(((omc_u16)src[1] << 8) | (omc_u16)src[0]);
}

static omc_u16
omc_exif_write_read_u16be(const omc_u8* src)
{
    return (omc_u16)(((omc_u16)src[0] << 8) | (omc_u16)src[1]);
}

static omc_u32
omc_exif_write_read_u32be(const omc_u8* src)
{
    return ((omc_u32)src[0] << 24) | ((omc_u32)src[1] << 16)
           | ((omc_u32)src[2] << 8) | (omc_u32)src[3];
}

static omc_u32
omc_exif_write_read_u32le(const omc_u8* src)
{
    return ((omc_u32)src[3] << 24) | ((omc_u32)src[2] << 16)
           | ((omc_u32)src[1] << 8) | (omc_u32)src[0];
}

static omc_u64
omc_exif_write_read_u64le(const omc_u8* src)
{
    return ((omc_u64)src[7] << 56) | ((omc_u64)src[6] << 48)
           | ((omc_u64)src[5] << 40) | ((omc_u64)src[4] << 32)
           | ((omc_u64)src[3] << 24) | ((omc_u64)src[2] << 16)
           | ((omc_u64)src[1] << 8) | (omc_u64)src[0];
}

static omc_u64
omc_exif_write_read_u64be(const omc_u8* src)
{
    return ((omc_u64)src[0] << 56) | ((omc_u64)src[1] << 48)
           | ((omc_u64)src[2] << 40) | ((omc_u64)src[3] << 32)
           | ((omc_u64)src[4] << 24) | ((omc_u64)src[5] << 16)
           | ((omc_u64)src[6] << 8) | (omc_u64)src[7];
}

static int
omc_exif_write_tiff_has_dng_version(const omc_u8* bytes, omc_size size)
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

    if ((little_endian ? omc_exif_write_read_u16le(bytes + 2U)
                       : omc_exif_write_read_u16be(bytes + 2U))
        == 42U) {
        big_tiff = 0;
        ifd_off = (omc_u64)(
            little_endian ? omc_exif_write_read_u32le(bytes + 4U)
                          : omc_exif_write_read_u32be(bytes + 4U));
    } else if ((little_endian ? omc_exif_write_read_u16le(bytes + 2U)
                              : omc_exif_write_read_u16be(bytes + 2U))
               == 43U) {
        if (size < 16U) {
            return 0;
        }
        if ((little_endian ? omc_exif_write_read_u16le(bytes + 4U)
                           : omc_exif_write_read_u16be(bytes + 4U))
                != 8U
            || (little_endian ? omc_exif_write_read_u16le(bytes + 6U)
                              : omc_exif_write_read_u16be(bytes + 6U))
                   != 0U) {
            return 0;
        }
        big_tiff = 1;
        ifd_off = little_endian ? omc_exif_write_read_u64le(bytes + 8U)
                                : omc_exif_write_read_u64be(bytes + 8U);
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
        count = (omc_u64)(
            little_endian
                ? omc_exif_write_read_u16le(bytes + (omc_size)ifd_off)
                : omc_exif_write_read_u16be(bytes + (omc_size)ifd_off));
        count_off = ifd_off + 2U;
        entry_size = 12U;
    } else {
        if (ifd_off + 8U > (omc_u64)size) {
            return 0;
        }
        count = little_endian
                    ? omc_exif_write_read_u64le(bytes + (omc_size)ifd_off)
                    : omc_exif_write_read_u64be(bytes + (omc_size)ifd_off);
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
                  ? omc_exif_write_read_u16le(bytes + (omc_size)entry_off)
                  : omc_exif_write_read_u16be(bytes + (omc_size)entry_off);
        if (tag == 0xC612U) {
            return 1;
        }
    }
    return 0;
}

static void
omc_exif_write_store_u16be(omc_u8* dst, omc_u16 value)
{
    dst[0] = (omc_u8)((value >> 8) & 0xFFU);
    dst[1] = (omc_u8)(value & 0xFFU);
}

static void
omc_exif_write_store_u32be(omc_u8* dst, omc_u32 value)
{
    dst[0] = (omc_u8)((value >> 24) & 0xFFU);
    dst[1] = (omc_u8)((value >> 16) & 0xFFU);
    dst[2] = (omc_u8)((value >> 8) & 0xFFU);
    dst[3] = (omc_u8)(value & 0xFFU);
}

static void
omc_exif_write_store_u32le(omc_u8* dst, omc_u32 value)
{
    dst[0] = (omc_u8)(value & 0xFFU);
    dst[1] = (omc_u8)((value >> 8) & 0xFFU);
    dst[2] = (omc_u8)((value >> 16) & 0xFFU);
    dst[3] = (omc_u8)((value >> 24) & 0xFFU);
}

static omc_u32
omc_exif_write_fourcc(char a, char b, char c, char d)
{
    return ((omc_u32)(omc_u8)a << 24) | ((omc_u32)(omc_u8)b << 16)
           | ((omc_u32)(omc_u8)c << 8) | (omc_u32)(omc_u8)d;
}

static int
omc_exif_write_is_jpeg_standalone_marker(omc_u8 marker)
{
    if (marker == 0x01U || (marker >= 0xD0U && marker <= 0xD9U)) {
        return 1;
    }
    return 0;
}

static int
omc_exif_write_is_jpeg_exif_app1(const omc_u8* bytes, omc_size size)
{
    if (bytes == (const omc_u8*)0 || size < 6U) {
        return 0;
    }
    return memcmp(bytes, k_omc_exif_write_jpeg_prefix,
                  sizeof(k_omc_exif_write_jpeg_prefix))
           == 0;
}

static int
omc_exif_write_is_png_exif_chunk(const omc_u8* type)
{
    if (type == (const omc_u8*)0) {
        return 0;
    }
    return memcmp(type, "eXIf", 4U) == 0;
}

static omc_status
omc_exif_write_copy_original(const omc_u8* file_bytes, omc_size file_size,
                             omc_arena* out, omc_exif_write_res* out_res)
{
    omc_status status;

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, file_bytes, file_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    out_res->status = OMC_EXIF_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    return OMC_STATUS_OK;
}

static omc_status
omc_exif_write_append_jpeg_exif_segment(omc_arena* out, const omc_u8* payload,
                                        omc_size payload_size)
{
    omc_u8 header[4];
    omc_status status;

    if (payload_size > 65533U) {
        return OMC_STATUS_OVERFLOW;
    }
    header[0] = 0xFFU;
    header[1] = 0xE1U;
    omc_exif_write_store_u16be(header + 2U, (omc_u16)(payload_size + 2U));
    status = omc_exif_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_exif_write_append(out, payload, payload_size);
}

static omc_status
omc_exif_write_append_png_chunk(omc_arena* out, const char* type,
                                const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_u8 trailer[4];
    omc_status status;
    omc_u32 crc;

    omc_exif_write_store_u32be(header, (omc_u32)payload_size);
    memcpy(header + 4U, type, 4U);
    crc = omc_crc32_update(0xFFFFFFFFU, header + 4U, 4U);
    crc = omc_crc32_update(crc, payload, payload_size);
    omc_exif_write_store_u32be(trailer, ~crc);
    status = omc_exif_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_exif_write_append(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return omc_exif_write_append(out, trailer, sizeof(trailer));
}

static omc_status
omc_exif_write_append_webp_chunk(omc_arena* out, const char* type,
                                 const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_u8 pad;
    omc_status status;

    memcpy(header, type, 4U);
    omc_exif_write_store_u32le(header + 4U, (omc_u32)payload_size);
    status = omc_exif_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_exif_write_append(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if ((payload_size & 1U) != 0U) {
        pad = 0U;
        status = omc_exif_write_append(out, &pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_exif_write_append_bmff_box(omc_arena* out, omc_u32 type,
                               const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_status status;

    if (payload_size > (omc_size)(0xFFFFFFFFU - 8U)) {
        return OMC_STATUS_OVERFLOW;
    }
    omc_exif_write_store_u32be(header, (omc_u32)(payload_size + 8U));
    omc_exif_write_store_u32be(header + 4U, type);
    status = omc_exif_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_exif_write_append(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static int
omc_exif_write_parse_bmff_box(const omc_u8* bytes, omc_size size,
                              omc_u64 offset, omc_u64 limit,
                              omc_exif_write_bmff_box* out_box)
{
    omc_u32 size32;
    omc_u64 box_size;
    omc_u64 header_size;

    if (out_box == (omc_exif_write_bmff_box*)0) {
        return 0;
    }
    memset(out_box, 0, sizeof(*out_box));
    if (offset > limit || limit > (omc_u64)size || offset + 8U > limit) {
        return 0;
    }

    size32 = omc_exif_write_read_u32be(bytes + (omc_size)offset);
    out_box->type = omc_exif_write_read_u32be(bytes + (omc_size)offset + 4U);
    header_size = 8U;
    if (size32 == 1U) {
        if (offset + 16U > limit) {
            return 0;
        }
        box_size = omc_exif_write_read_u64be(bytes + (omc_size)offset + 8U);
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
omc_exif_write_bmff_format_from_ftyp(const omc_u8* bytes, omc_size size,
                                     const omc_exif_write_bmff_box* ftyp)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u32 brand;
    int is_heif;
    int is_avif;
    int is_cr3;
    omc_u64 off;

    if (ftyp == (const omc_exif_write_bmff_box*)0 || ftyp->size < 16U) {
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

#define OMC_EXIF_WRITE_NOTE_BMFF_BRAND(v) \
    do { \
        if ((v) == omc_exif_write_fourcc('c', 'r', 'x', ' ') \
            || (v) == omc_exif_write_fourcc('C', 'R', '3', ' ')) { \
            is_cr3 = 1; \
        } \
        if ((v) == omc_exif_write_fourcc('a', 'v', 'i', 'f') \
            || (v) == omc_exif_write_fourcc('a', 'v', 'i', 's')) { \
            is_avif = 1; \
        } \
        if ((v) == omc_exif_write_fourcc('m', 'i', 'f', '1') \
            || (v) == omc_exif_write_fourcc('m', 's', 'f', '1') \
            || (v) == omc_exif_write_fourcc('h', 'e', 'i', 'c') \
            || (v) == omc_exif_write_fourcc('h', 'e', 'i', 'x') \
            || (v) == omc_exif_write_fourcc('h', 'e', 'v', 'c') \
            || (v) == omc_exif_write_fourcc('h', 'e', 'v', 'x')) { \
            is_heif = 1; \
        } \
    } while (0)

    brand = omc_exif_write_read_u32be(bytes + (omc_size)payload_off);
    OMC_EXIF_WRITE_NOTE_BMFF_BRAND(brand);
    off = payload_off + 8U;
    while (off + 4U <= payload_end) {
        brand = omc_exif_write_read_u32be(bytes + (omc_size)off);
        OMC_EXIF_WRITE_NOTE_BMFF_BRAND(brand);
        off += 4U;
    }

#undef OMC_EXIF_WRITE_NOTE_BMFF_BRAND

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
omc_exif_write_detect_format(const omc_u8* file_bytes, omc_size file_size)
{
    omc_u64 off;
    omc_u64 limit;

    if (file_bytes == (const omc_u8*)0 || file_size == 0U) {
        return OMC_SCAN_FMT_UNKNOWN;
    }
    if (file_size >= 2U && file_bytes[0] == 0xFFU && file_bytes[1] == 0xD8U) {
        return OMC_SCAN_FMT_JPEG;
    }
    if (file_size >= sizeof(k_omc_exif_write_png_sig)
        && memcmp(file_bytes, k_omc_exif_write_png_sig,
                  sizeof(k_omc_exif_write_png_sig))
               == 0) {
        return OMC_SCAN_FMT_PNG;
    }
    if (file_size >= 8U
        && ((file_bytes[0] == 'I' && file_bytes[1] == 'I')
            || (file_bytes[0] == 'M' && file_bytes[1] == 'M'))) {
        if (omc_exif_write_tiff_has_dng_version(file_bytes, file_size)) {
            return OMC_SCAN_FMT_DNG;
        }
        return OMC_SCAN_FMT_TIFF;
    }
    if (file_size >= 12U && memcmp(file_bytes, "RIFF", 4U) == 0
        && memcmp(file_bytes + 8U, "WEBP", 4U) == 0) {
        return OMC_SCAN_FMT_WEBP;
    }
    if (file_size >= sizeof(k_omc_exif_write_jp2_sig)
        && memcmp(file_bytes, k_omc_exif_write_jp2_sig,
                  sizeof(k_omc_exif_write_jp2_sig))
               == 0) {
        return OMC_SCAN_FMT_JP2;
    }
    if (file_size >= sizeof(k_omc_exif_write_jxl_sig)
        && memcmp(file_bytes, k_omc_exif_write_jxl_sig,
                  sizeof(k_omc_exif_write_jxl_sig))
               == 0) {
        return OMC_SCAN_FMT_JXL;
    }

    off = 0U;
    limit = (omc_u64)file_size;
    while (off + 8U <= limit) {
        omc_exif_write_bmff_box box;

        if (!omc_exif_write_parse_bmff_box(file_bytes, file_size, off, limit,
                                           &box)) {
            break;
        }
        if (box.type == omc_exif_write_fourcc('f', 't', 'y', 'p')) {
            return omc_exif_write_bmff_format_from_ftyp(file_bytes, file_size,
                                                        &box);
        }
        if (box.size == 0U) {
            break;
        }
        off += box.size;
    }

    return OMC_SCAN_FMT_UNKNOWN;
}

static omc_status
omc_exif_write_build_item_bmff_payload(const omc_arena* tiff_payload,
                                       omc_arena* out)
{
    omc_u8 header[10];
    omc_status status;

    omc_exif_write_store_u32be(header, 6U);
    memcpy(header + 4U, k_omc_exif_write_jpeg_prefix,
           sizeof(k_omc_exif_write_jpeg_prefix));

    omc_arena_reset(out);
    status = omc_arena_reserve(out, sizeof(header) + tiff_payload->size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_exif_write_append(out, tiff_payload->data, tiff_payload->size);
}

static omc_status
omc_exif_write_rewrite_bmff_item_exif(const omc_u8* file_bytes,
                                      omc_size file_size,
                                      const omc_u8* payload,
                                      omc_size payload_size,
                                      omc_arena* out,
                                      omc_exif_write_res* out_res)
{
    omc_bmff_write_item item;
    omc_bmff_write_res res;
    omc_const_bytes profile;
    omc_status status;
    item.family = OMC_BMFF_FAMILY_EXIF;
    item.payload.data = payload; item.payload.size = payload_size;
    profile.data = NULL; profile.size = 0U;
    status = omc_bmff_rewrite(file_bytes, file_size, &item, 1U, 0U, 0,
                             profile, 0, out, &res);
    switch (res.status) {
    case OMC_TRANSFER_OK: out_res->status = OMC_EXIF_WRITE_OK; break;
    case OMC_TRANSFER_LIMIT: out_res->status = OMC_EXIF_WRITE_LIMIT; break;
    case OMC_TRANSFER_MALFORMED: out_res->status = OMC_EXIF_WRITE_MALFORMED; break;
    default: out_res->status = OMC_EXIF_WRITE_UNSUPPORTED; break;
    }
    out_res->removed_exif_blocks = res.removed[OMC_BMFF_FAMILY_EXIF];
    out_res->inserted_exif_blocks = res.inserted[OMC_BMFF_FAMILY_EXIF];
    if (status == OMC_STATUS_OK && res.status == OMC_TRANSFER_OK) {
        out_res->written = out->size; out_res->needed = out->size;
    }
    return status;
}

static omc_u32
omc_exif_write_count_existing_exif_blocks(const omc_u8* file_bytes,
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
        if (blocks[i].kind == OMC_BLK_EXIF) {
            count += 1U;
        } else if (blocks[i].kind == OMC_BLK_COMP_METADATA
                   && blocks[i].format == OMC_SCAN_FMT_JXL
                   && blocks[i].aux_u32
                          == OMC_FOURCC('E', 'x', 'i', 'f')) {
            count += 1U;
        }
    }
    return count;
}

static int
omc_exif_write_is_jxl_brob_exif_box(const omc_u8* file_bytes,
                                    omc_size file_size,
                                    const omc_exif_write_bmff_box* box)
{
    omc_u64 payload_off;
    omc_u64 payload_size;

    if (file_bytes == (const omc_u8*)0
        || box == (const omc_exif_write_bmff_box*)0) {
        return 0;
    }
    if (box->type != omc_exif_write_fourcc('b', 'r', 'o', 'b')) {
        return 0;
    }

    payload_off = box->offset + box->header_size;
    payload_size = box->size - box->header_size;
    if (payload_size < 4U || payload_off > (omc_u64)file_size - 4U) {
        return 0;
    }
    return omc_exif_write_read_u32be(file_bytes + (omc_size)payload_off)
           == omc_exif_write_fourcc('E', 'x', 'i', 'f');
}

static omc_status
omc_exif_write_read_current_store(const omc_u8* file_bytes, omc_size file_size,
                                  omc_store* out_store)
{
    omc_blk_ref blocks[64];
    omc_exif_ifd_ref ifds[16];
    omc_u8 payload[8192];
    omc_u32 scratch[64];
    omc_read_res res;

    if (out_store == (omc_store*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    omc_store_init(out_store);
    res = omc_read_simple(file_bytes, file_size, out_store, blocks, 64U, ifds,
                          16U, payload, sizeof(payload), scratch, 64U,
                          (const omc_read_opts*)0);
    if (res.scan.status == OMC_SCAN_MALFORMED
        || res.pay.status == OMC_PAY_MALFORMED
        || res.exif.status == OMC_EXIF_MALFORMED) {
        omc_store_fini(out_store);
        omc_store_init(out_store);
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_exif_write_build_jpeg_payload(const omc_arena* tiff_payload,
                                  omc_arena* out)
{
    omc_status status;

    omc_arena_reset(out);
    status = omc_arena_reserve(out, sizeof(k_omc_exif_write_jpeg_prefix)
                                        + tiff_payload->size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, k_omc_exif_write_jpeg_prefix,
                                   sizeof(k_omc_exif_write_jpeg_prefix));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_exif_write_append(out, tiff_payload->data, tiff_payload->size);
}

static omc_status
omc_exif_write_build_direct_bmff_payload(const omc_arena* tiff_payload,
                                         omc_arena* out)
{
    omc_u8 prefix[4];
    omc_status status;

    memset(prefix, 0, sizeof(prefix));
    omc_arena_reset(out);
    status = omc_arena_reserve(out, sizeof(prefix) + tiff_payload->size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, prefix, sizeof(prefix));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_exif_write_append(out, tiff_payload->data, tiff_payload->size);
}

static omc_status
omc_exif_write_canonical(const omc_store* store, omc_scan_fmt format,
                          omc_arena* out, omc_exif_write_res* result)
{
    omc_exif_tiff_opts opts;
    omc_exif_tiff_res serialized;
    omc_mut_bytes bytes;
    omc_status status;
    omc_exif_tiff_opts_init(&opts);
    result->status = OMC_EXIF_WRITE_OK;
    /* Carrier input has already passed the transfer policy. Decoded stores can
     * contain noncanonical legacy wire hints and vendor-derived entries. */
    opts.validate = 0;
    opts.include_subifds = format == OMC_SCAN_FMT_DNG;
    opts.inject_minimal_dng_version = format == OMC_SCAN_FMT_DNG;
    bytes.data = NULL;
    bytes.size = 0U;
    serialized = omc_serialize_exif_tiff(store, bytes, &opts);
    if (serialized.status == OMC_EXIF_TIFF_NO_EXIF_DATA) return OMC_STATUS_OK;
    if (serialized.status == OMC_EXIF_TIFF_NO_MEMORY) return OMC_STATUS_NO_MEMORY;
    if (serialized.status != OMC_EXIF_TIFF_OUTPUT_TRUNCATED) {
        result->status = serialized.status == OMC_EXIF_TIFF_LIMIT ? OMC_EXIF_WRITE_LIMIT : OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    status = omc_arena_reserve(out, (omc_size)serialized.needed);
    if (status != OMC_STATUS_OK) return status;
    bytes.data = out->data;
    bytes.size = (omc_size)serialized.needed;
    serialized = omc_serialize_exif_tiff(store, bytes, &opts);
    if (serialized.status != OMC_EXIF_TIFF_OK) return OMC_STATUS_STATE;
    out->size = (omc_size)serialized.written;
    return OMC_STATUS_OK;
}

static omc_u32
omc_exif_write_key_hash(const omc_store* store, const omc_entry* entry)
{
    omc_const_bytes ifd;
    omc_u32 hash;
    omc_size i;
    ifd = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
    hash = 2166136261U ^ entry->key.u.exif_tag.tag;
    for (i = 0U; i < ifd.size; ++i) hash = (hash ^ ifd.data[i]) * 16777619U;
    return hash;
}

static omc_status
omc_exif_write_merge(const omc_store* source, const omc_store* current,
                      omc_store* merged)
{
    omc_size capacity;
    omc_size slot;
    omc_size i;
    omc_entry_id* slots;
    omc_entry_id id;
    omc_entry copy;
    omc_const_bytes a;
    omc_const_bytes b;
    const omc_entry* entry;
    const omc_entry* previous;
    omc_status status;
    if (!omc_store_shape_valid(source) || source->entry_count > 200000U
        || (current != NULL && (!omc_store_shape_valid(current) || current->entry_count > 200000U)))
        return OMC_STATUS_OVERFLOW;
    status = omc_edit_commit(source, NULL, 0U, merged);
    if (status != OMC_STATUS_OK || current == NULL) return status;
    capacity = 8U;
    while (capacity < source->entry_count * 2U) capacity *= 2U;
    slots = (omc_entry_id*)malloc(capacity * sizeof(*slots));
    if (slots == NULL) return OMC_STATUS_NO_MEMORY;
    for (i = 0U; i < capacity; ++i) slots[i] = OMC_INVALID_ENTRY_ID;
    for (i = 0U; i < source->entry_count; ++i) {
        entry = &source->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) continue;
        slot = omc_exif_write_key_hash(source, entry) & (capacity - 1U);
        while (slots[slot] != OMC_INVALID_ENTRY_ID) slot = (slot + 1U) & (capacity - 1U);
        slots[slot] = (omc_entry_id)i;
    }
    for (i = 0U; i < current->entry_count; ++i) {
        entry = &current->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG || (entry->flags & OMC_ENTRY_FLAG_DELETED)) continue;
        a = omc_arena_view(&current->arena, entry->key.u.exif_tag.ifd);
        if (a.data == NULL && a.size != 0U) { status = OMC_STATUS_STATE; break; }
        slot = omc_exif_write_key_hash(current, entry) & (capacity - 1U);
        while (slots[slot] != OMC_INVALID_ENTRY_ID) {
            previous = &source->entries[slots[slot]];
            b = omc_arena_view(&source->arena, previous->key.u.exif_tag.ifd);
            if (previous->key.u.exif_tag.tag == entry->key.u.exif_tag.tag
                && a.size == b.size && (a.size == 0U || memcmp(a.data, b.data, a.size) == 0)) break;
            slot = (slot + 1U) & (capacity - 1U);
        }
        if (slots[slot] != OMC_INVALID_ENTRY_ID) continue;
        status = omc_clone_entry(entry, &current->arena, &merged->arena, &copy);
        if (status != OMC_STATUS_OK) break;
        copy.origin.block = OMC_INVALID_BLOCK_ID;
        status = omc_store_add_entry(merged, &copy, &id);
        if (status != OMC_STATUS_OK) break;
    }
    free(slots);
    return status;
}

omc_status
omc_exif_write_build_transfer_payload(const omc_store* source_store,
                                      omc_arena* out, omc_scan_fmt format,
                                      omc_exif_write_res* out_res)
{
    omc_arena tiff;
    omc_status status;
    if (source_store == NULL || out == NULL || out_res == NULL)
        return OMC_STATUS_INVALID_ARGUMENT;
    omc_exif_write_res_init(out_res);
    omc_arena_reset(out);
    out_res->format = format;
    if (format != OMC_SCAN_FMT_JPEG && format != OMC_SCAN_FMT_PNG
        && format != OMC_SCAN_FMT_TIFF && format != OMC_SCAN_FMT_DNG
        && format != OMC_SCAN_FMT_WEBP && format != OMC_SCAN_FMT_JP2
        && format != OMC_SCAN_FMT_JXL && format != OMC_SCAN_FMT_HEIF
        && format != OMC_SCAN_FMT_AVIF && format != OMC_SCAN_FMT_CR3) {
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }
    omc_arena_init(&tiff);
    status = omc_exif_write_canonical(source_store, format, &tiff, out_res);
    if (status == OMC_STATUS_OK && out_res->status == OMC_EXIF_WRITE_OK && tiff.size != 0U) {
        if (format == OMC_SCAN_FMT_PNG || format == OMC_SCAN_FMT_WEBP)
            status = omc_exif_write_append(out, tiff.data, tiff.size);
        else if (format == OMC_SCAN_FMT_JPEG || format == OMC_SCAN_FMT_TIFF || format == OMC_SCAN_FMT_DNG)
            status = omc_exif_write_build_jpeg_payload(&tiff, out);
        else status = omc_exif_write_build_item_bmff_payload(&tiff, out);
    }
    if (status == OMC_STATUS_OK && out_res->status == OMC_EXIF_WRITE_OK) {
        out_res->written = out->size;
        out_res->needed = out->size;
    }
    omc_arena_fini(&tiff);
    return status;
}

static omc_status
omc_exif_write_rewrite_jpeg(const omc_u8* file_bytes, omc_size file_size,
                            const omc_u8* payload, omc_size payload_size,
                            omc_arena* out, omc_exif_write_res* out_res)
{
    omc_size offset;
    omc_status status;
    int inserted;

    if (file_size < 4U || file_bytes[0] != 0xFFU || file_bytes[1] != 0xD8U) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (payload_size > 65533U) {
        out_res->status = OMC_EXIF_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + payload_size + 4U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, file_bytes, 2U);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    inserted = 0;
    offset = 2U;
    while (offset < file_size) {
        omc_size marker_start;
        omc_u8 marker;

        if (file_bytes[offset] != 0xFFU) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        marker_start = offset;
        offset += 1U;
        while (offset < file_size && file_bytes[offset] == 0xFFU) {
            offset += 1U;
        }
        if (offset >= file_size) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }

        marker = file_bytes[offset];
        offset += 1U;

        if (marker == 0xD9U) {
            if (!inserted) {
                status = omc_exif_write_append_jpeg_exif_segment(out, payload,
                                                                 payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_exif_blocks = 1U;
                inserted = 1;
            }
            status = omc_exif_write_append(out, file_bytes + marker_start,
                                           file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->status = OMC_EXIF_WRITE_OK;
            out_res->needed = out->size;
            out_res->written = out->size;
            return OMC_STATUS_OK;
        }

        if (omc_exif_write_is_jpeg_standalone_marker(marker)) {
            if (!inserted) {
                status = omc_exif_write_append_jpeg_exif_segment(out, payload,
                                                                 payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_exif_blocks = 1U;
                inserted = 1;
            }
            status = omc_exif_write_append(out, file_bytes + marker_start,
                                           offset - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            continue;
        }

        if (offset + 2U > file_size) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (marker == 0xDAU) {
            if (!inserted) {
                status = omc_exif_write_append_jpeg_exif_segment(out, payload,
                                                                 payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_exif_blocks = 1U;
                inserted = 1;
            }
            status = omc_exif_write_append(out, file_bytes + marker_start,
                                           file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->status = OMC_EXIF_WRITE_OK;
            out_res->needed = out->size;
            out_res->written = out->size;
            return OMC_STATUS_OK;
        }

        {
            omc_u16 seg_len;
            omc_size segment_end;
            const omc_u8* seg_data;
            omc_size seg_data_size;
            int is_exif;
            int is_leading;

            seg_len = omc_exif_write_read_u16be(file_bytes + offset);
            if (seg_len < 2U || (omc_size)seg_len > file_size - offset) {
                out_res->status = OMC_EXIF_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }

            segment_end = offset + (omc_size)seg_len;
            seg_data = file_bytes + offset + 2U;
            seg_data_size = (omc_size)seg_len - 2U;
            is_exif = marker == 0xE1U
                      && omc_exif_write_is_jpeg_exif_app1(seg_data,
                                                          seg_data_size);
            is_leading = marker == 0xE0U || is_exif;

            if (is_exif) {
                out_res->removed_exif_blocks += 1U;
                offset = segment_end;
                continue;
            }
            if (!inserted && !is_leading) {
                status = omc_exif_write_append_jpeg_exif_segment(out, payload,
                                                                 payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_exif_blocks = 1U;
                inserted = 1;
            }
            status = omc_exif_write_append(out, file_bytes + marker_start,
                                           segment_end - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            offset = segment_end;
        }
    }

    out_res->status = OMC_EXIF_WRITE_MALFORMED;
    return OMC_STATUS_OK;
}

static omc_status
omc_exif_write_rewrite_png(const omc_u8* file_bytes, omc_size file_size,
                           const omc_u8* payload, omc_size payload_size,
                           omc_arena* out, omc_exif_write_res* out_res)
{
    omc_size offset;
    omc_status status;
    int saw_ihdr;
    int inserted;

    if (file_size < sizeof(k_omc_exif_write_png_sig)
        || memcmp(file_bytes, k_omc_exif_write_png_sig,
                  sizeof(k_omc_exif_write_png_sig))
               != 0) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + payload_size + 12U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, file_bytes,
                                   sizeof(k_omc_exif_write_png_sig));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    saw_ihdr = 0;
    inserted = 0;
    offset = sizeof(k_omc_exif_write_png_sig);
    while (offset + 12U <= file_size) {
        omc_u32 chunk_len;
        omc_size chunk_size;
        const omc_u8* chunk_type;

        chunk_len = omc_exif_write_read_u32be(file_bytes + offset);
        if ((omc_u64)chunk_len + 12U > (omc_u64)(file_size - offset)) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        chunk_size = (omc_size)chunk_len + 12U;
        chunk_type = file_bytes + offset + 4U;

        if (!saw_ihdr) {
            if (memcmp(chunk_type, "IHDR", 4U) != 0) {
                out_res->status = OMC_EXIF_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            saw_ihdr = 1;
            status = omc_exif_write_append(out, file_bytes + offset, chunk_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            status = omc_exif_write_append_png_chunk(out, "eXIf", payload,
                                                     payload_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->inserted_exif_blocks = 1U;
            inserted = 1;
        } else if (omc_exif_write_is_png_exif_chunk(chunk_type)) {
            out_res->removed_exif_blocks += 1U;
        } else if (memcmp(chunk_type, "IEND", 4U) == 0) {
            if (!inserted) {
                status = omc_exif_write_append_png_chunk(out, "eXIf", payload,
                                                         payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_exif_blocks = 1U;
                inserted = 1;
            }
            status = omc_exif_write_append(out, file_bytes + offset,
                                           file_size - offset);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->status = OMC_EXIF_WRITE_OK;
            out_res->needed = out->size;
            out_res->written = out->size;
            return OMC_STATUS_OK;
        } else {
            status = omc_exif_write_append(out, file_bytes + offset, chunk_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        offset += chunk_size;
    }

    out_res->status = OMC_EXIF_WRITE_MALFORMED;
    return OMC_STATUS_OK;
}

static omc_status
omc_exif_write_rewrite_webp(const omc_u8* file_bytes, omc_size file_size,
                            const omc_u8* payload, omc_size payload_size,
                            omc_arena* out, omc_exif_write_res* out_res)
{
    omc_size offset;
    omc_status status;
    int inserted;
    int saw_vp8x;

    if (file_size < 12U || memcmp(file_bytes, "RIFF", 4U) != 0
        || memcmp(file_bytes + 8U, "WEBP", 4U) != 0) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + payload_size + 9U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, file_bytes, 12U);
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
        chunk_size = omc_exif_write_read_u32le(file_bytes + offset + 4U);
        if ((omc_u64)chunk_size + 8U > (omc_u64)(file_size - offset)) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        padded_size = 8U + (omc_size)chunk_size + (chunk_size & 1U);
        if (offset + padded_size > file_size) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }

        if (memcmp(chunk_type, "EXIF", 4U) == 0) {
            out_res->removed_exif_blocks += 1U;
        } else if (memcmp(chunk_type, "VP8X", 4U) == 0) {
            omc_u8 vp8x[18];

            if (chunk_size != 10U) {
                out_res->status = OMC_EXIF_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            memset(vp8x, 0, sizeof(vp8x));
            memcpy(vp8x, file_bytes + offset, padded_size);
            vp8x[8] = (omc_u8)(vp8x[8] | k_omc_exif_write_webp_vp8x_exif_bit);
            status = omc_exif_write_append(out, vp8x, padded_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            saw_vp8x = 1;
            if (!inserted) {
                status = omc_exif_write_append_webp_chunk(out, "EXIF", payload,
                                                          payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_exif_blocks = 1U;
                inserted = 1;
            }
        } else {
            if (!inserted && !saw_vp8x) {
                status = omc_exif_write_append_webp_chunk(out, "EXIF", payload,
                                                          payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_exif_blocks = 1U;
                inserted = 1;
            }
            status = omc_exif_write_append(out, file_bytes + offset, padded_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        offset += padded_size;
    }

    if (offset != file_size) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (!inserted) {
        status = omc_exif_write_append_webp_chunk(out, "EXIF", payload,
                                                  payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        out_res->inserted_exif_blocks = 1U;
    }
    if (out->size < 8U || out->size - 8U > 0xFFFFFFFFU) {
        out_res->status = OMC_EXIF_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }
    omc_exif_write_store_u32le(out->data + 4U, (omc_u32)(out->size - 8U));
    out_res->status = OMC_EXIF_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    return OMC_STATUS_OK;
}

static omc_status
omc_exif_write_rewrite_bmff_direct(const omc_u8* file_bytes,
                                   omc_size file_size, const omc_u8* payload,
                                   omc_size payload_size, omc_scan_fmt format,
                                   omc_arena* out,
                                   omc_exif_write_res* out_res)
{
    omc_u64 offset;
    omc_u64 limit;
    omc_status status;
    omc_u32 direct_removed;
    omc_u32 existing_exif_total;
    omc_size start_offset;

    start_offset = 0U;
    if (format == OMC_SCAN_FMT_JP2) {
        if (file_size < sizeof(k_omc_exif_write_jp2_sig)
            || memcmp(file_bytes, k_omc_exif_write_jp2_sig,
                      sizeof(k_omc_exif_write_jp2_sig))
                   != 0) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
    } else if (format == OMC_SCAN_FMT_JXL) {
        if (file_size < sizeof(k_omc_exif_write_jxl_sig)
            || memcmp(file_bytes, k_omc_exif_write_jxl_sig,
                      sizeof(k_omc_exif_write_jxl_sig))
                   != 0) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        start_offset = sizeof(k_omc_exif_write_jxl_sig);
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + payload_size + 16U);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (start_offset != 0U) {
        status = omc_exif_write_append(out, file_bytes, start_offset);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    offset = (omc_u64)start_offset;
    limit = (omc_u64)file_size;
    direct_removed = 0U;
    while (offset + 8U <= limit) {
        omc_exif_write_bmff_box box;

        if (!omc_exif_write_parse_bmff_box(file_bytes, file_size, offset, limit,
                                           &box)) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (box.type == omc_exif_write_fourcc('E', 'x', 'i', 'f')) {
            direct_removed += 1U;
            out_res->removed_exif_blocks += 1U;
        } else if (format == OMC_SCAN_FMT_JXL
                   && omc_exif_write_is_jxl_brob_exif_box(file_bytes,
                                                          file_size, &box)) {
            direct_removed += 1U;
            out_res->removed_exif_blocks += 1U;
        } else {
            status = omc_exif_write_append(out,
                                           file_bytes + (omc_size)box.offset,
                                           (omc_size)box.size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        offset += box.size;
    }
    if (offset != limit) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    existing_exif_total = omc_exif_write_count_existing_exif_blocks(file_bytes,
                                                                    file_size);
    if (existing_exif_total > direct_removed) {
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    status = omc_exif_write_append_bmff_box(
        out, omc_exif_write_fourcc('E', 'x', 'i', 'f'), payload,
        payload_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    out_res->inserted_exif_blocks = 1U;
    out_res->status = OMC_EXIF_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    return OMC_STATUS_OK;
}

void
omc_exif_write_res_init(omc_exif_write_res* res)
{
    if (res == (omc_exif_write_res*)0) {
        return;
    }
    memset(res, 0, sizeof(*res));
    res->status = OMC_EXIF_WRITE_UNSUPPORTED;
    res->format = OMC_SCAN_FMT_UNKNOWN;
}

int
omc_exif_write_store_has_supported_tags(const omc_store* store)
{
    omc_exif_tiff_opts opts;
    omc_exif_tiff_res res;
    omc_mut_bytes bytes;
    omc_exif_tiff_opts_init(&opts);
    opts.validate = 0;
    bytes.data = NULL;
    bytes.size = 0U;
    res = omc_serialize_exif_tiff(store, bytes, &opts);
    return res.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED;
}

static omc_status
omc_exif_write_embedded_impl(const omc_u8* file_bytes, omc_size file_size,
                             const omc_store* source_store, omc_arena* out,
                             omc_scan_fmt format,
                             omc_exif_write_res* out_res,
                             int preserve_current_fields)
{
    omc_store current_store;
    const omc_store* current_store_ptr;
    omc_arena tiff_payload;
    omc_arena container_payload;
    omc_store merged;
    omc_status status;
    int current_store_ready;

    if (file_bytes == (const omc_u8*)0 || source_store == (const omc_store*)0
        || out == (omc_arena*)0 || out_res == (omc_exif_write_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_exif_write_res_init(out_res);
    omc_arena_reset(out);

    if (!omc_exif_write_store_has_supported_tags(source_store)) {
        return omc_exif_write_copy_original(file_bytes, file_size, out, out_res);
    }

    if (format == OMC_SCAN_FMT_UNKNOWN) {
        format = omc_exif_write_detect_format(file_bytes, file_size);
    }
    out_res->format = format;
    if (format != OMC_SCAN_FMT_JPEG && format != OMC_SCAN_FMT_PNG
        && format != OMC_SCAN_FMT_TIFF && format != OMC_SCAN_FMT_DNG
        && format != OMC_SCAN_FMT_WEBP
        && format != OMC_SCAN_FMT_JP2
        && format != OMC_SCAN_FMT_JXL && format != OMC_SCAN_FMT_HEIF
        && format != OMC_SCAN_FMT_AVIF && format != OMC_SCAN_FMT_CR3) {
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    current_store_ptr = (const omc_store*)0;
    current_store_ready = 0;
    if (preserve_current_fields) {
        status = omc_exif_write_read_current_store(file_bytes, file_size,
                                                   &current_store);
        if (status != OMC_STATUS_OK) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        current_store_ptr = &current_store;
        current_store_ready = 1;
    }

    omc_store_init(&merged);
    omc_arena_init(&tiff_payload);
    omc_arena_init(&container_payload);
    status = OMC_STATUS_OK;
    if (format == OMC_SCAN_FMT_TIFF || format == OMC_SCAN_FMT_DNG) {
        status = omc_exif_write_canonical(source_store, format, &tiff_payload, out_res);
        if (status == OMC_STATUS_OK && out_res->status == OMC_EXIF_WRITE_OK)
            status = omc_exif_overlay_tiff(file_bytes, file_size, source_store,
                                          &tiff_payload, out, out_res);
        goto cleanup;
    }
    status = omc_exif_write_merge(source_store, current_store_ptr, &merged);
    if (status != OMC_STATUS_OK) goto cleanup;
    status = omc_exif_write_canonical(&merged, format, &tiff_payload, out_res);
    if (status != OMC_STATUS_OK || out_res->status != OMC_EXIF_WRITE_OK) goto cleanup;
    if (tiff_payload.size == 0U) {
        status = omc_exif_write_copy_original(file_bytes, file_size, out, out_res);
        goto cleanup;
    }

    if (format == OMC_SCAN_FMT_JPEG) {
        status = omc_exif_write_build_jpeg_payload(&tiff_payload,
                                                   &container_payload);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    } else if (format == OMC_SCAN_FMT_PNG || format == OMC_SCAN_FMT_WEBP) {
        omc_arena_reset(&container_payload);
        status = omc_arena_reserve(&container_payload, tiff_payload.size);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
        status = omc_exif_write_append(&container_payload, tiff_payload.data,
                                       tiff_payload.size);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    } else if (format == OMC_SCAN_FMT_HEIF || format == OMC_SCAN_FMT_AVIF
               || format == OMC_SCAN_FMT_CR3) {
        status = omc_exif_write_build_item_bmff_payload(&tiff_payload,
                                                        &container_payload);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    } else {
        status = omc_exif_write_build_direct_bmff_payload(&tiff_payload,
                                                          &container_payload);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    }

    if (format == OMC_SCAN_FMT_JPEG) {
        status = omc_exif_write_rewrite_jpeg(file_bytes, file_size,
                                             container_payload.data,
                                             container_payload.size, out,
                                             out_res);
    } else if (format == OMC_SCAN_FMT_PNG) {
        status = omc_exif_write_rewrite_png(file_bytes, file_size,
                                            container_payload.data,
                                            container_payload.size, out,
                                            out_res);
    } else if (format == OMC_SCAN_FMT_WEBP) {
        status = omc_exif_write_rewrite_webp(file_bytes, file_size,
                                             container_payload.data,
                                             container_payload.size, out,
                                             out_res);
    } else if (format == OMC_SCAN_FMT_HEIF || format == OMC_SCAN_FMT_AVIF
               || format == OMC_SCAN_FMT_CR3) {
        status = omc_exif_write_rewrite_bmff_item_exif(
            file_bytes, file_size, container_payload.data,
            container_payload.size, out, out_res);
    } else {
        status = omc_exif_write_rewrite_bmff_direct(file_bytes, file_size,
                                                    container_payload.data,
                                                    container_payload.size,
                                                    format, out, out_res);
    }

cleanup:
    omc_store_fini(&merged);
    if (status != OMC_STATUS_OK || out_res->status != OMC_EXIF_WRITE_OK) {
        omc_arena_reset(out);
        out_res->written = 0U;
    }
    omc_arena_fini(&container_payload);
    omc_arena_fini(&tiff_payload);
    if (current_store_ready) {
        omc_store_fini(&current_store);
    }
    return status;
}

omc_status
omc_exif_write_embedded(const omc_u8* file_bytes, omc_size file_size,
                        const omc_store* source_store, omc_arena* out,
                        omc_scan_fmt format, omc_exif_write_res* out_res)
{
    return omc_exif_write_embedded_impl(file_bytes, file_size, source_store,
                                        out, format, out_res, 1);
}

omc_status
omc_exif_write_embedded_source_only(const omc_u8* file_bytes,
                                    omc_size file_size,
                                    const omc_store* source_store,
                                    omc_arena* out, omc_scan_fmt format,
                                    omc_exif_write_res* out_res)
{
    return omc_exif_write_embedded_impl(file_bytes, file_size, source_store,
                                        out, format, out_res, 0);
}
