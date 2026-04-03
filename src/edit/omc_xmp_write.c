#include "omc/omc_xmp_write.h"

#include <string.h>

static const omc_u8 k_omc_xmp_write_png_sig[8] = {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
};
static const omc_u8 k_omc_xmp_write_jp2_sig[12] = {
    0x00U, 0x00U, 0x00U, 0x0CU, 0x6AU, 0x50U, 0x20U, 0x20U,
    0x0DU, 0x0AU, 0x87U, 0x0AU
};
static const omc_u8 k_omc_xmp_write_jxl_sig[12] = {
    0x00U, 0x00U, 0x00U, 0x0CU, 0x4AU, 0x58U, 0x4CU, 0x20U,
    0x0DU, 0x0AU, 0x87U, 0x0AU
};

static const omc_u8 k_omc_xmp_write_jpeg_xmp_prefix[]
    = "http://ns.adobe.com/xap/1.0/\0";

static const omc_u8 k_omc_xmp_write_png_xmp_keyword[]
    = "XML:com.adobe.xmp";

static const omc_u8 k_omc_xmp_write_webp_vp8x_xmp_bit = 0x04U;

typedef struct omc_xmp_write_bmff_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
} omc_xmp_write_bmff_box;

typedef struct omc_xmp_write_bmff_item {
    omc_u32 item_id;
    omc_u32 item_type;
    omc_u16 protection_index;
    int is_xmp;
    int has_source;
    omc_size infe_off;
    omc_size infe_size;
    omc_u64 source_off;
    omc_u64 source_len;
    omc_u64 new_off;
    omc_u64 new_len;
} omc_xmp_write_bmff_item;

static void
omc_xmp_write_res_init(omc_xmp_write_res* res)
{
    if (res == (omc_xmp_write_res*)0) {
        return;
    }

    memset(res, 0, sizeof(*res));
    res->status = OMC_XMP_WRITE_UNSUPPORTED;
    res->format = OMC_SCAN_FMT_UNKNOWN;
    res->payload.status = OMC_XMP_DUMP_OK;
}

static omc_status
omc_xmp_write_append(omc_arena* out, const void* src, omc_size size)
{
    omc_byte_ref ref;

    return omc_arena_append(out, src, size, &ref);
}

static omc_u16
omc_xmp_write_read_u16be(const omc_u8* src)
{
    return (omc_u16)(((omc_u16)src[0] << 8) | (omc_u16)src[1]);
}

static omc_u16
omc_xmp_write_read_u16le(const omc_u8* src)
{
    return (omc_u16)(((omc_u16)src[1] << 8) | (omc_u16)src[0]);
}

static omc_u32
omc_xmp_write_read_u32be(const omc_u8* src)
{
    return ((omc_u32)src[0] << 24) | ((omc_u32)src[1] << 16)
           | ((omc_u32)src[2] << 8) | (omc_u32)src[3];
}

static omc_u32
omc_xmp_write_read_u32le(const omc_u8* src)
{
    return ((omc_u32)src[3] << 24) | ((omc_u32)src[2] << 16)
           | ((omc_u32)src[1] << 8) | (omc_u32)src[0];
}

static omc_u64
omc_xmp_write_read_u64be(const omc_u8* src)
{
    return ((omc_u64)src[0] << 56) | ((omc_u64)src[1] << 48)
           | ((omc_u64)src[2] << 40) | ((omc_u64)src[3] << 32)
           | ((omc_u64)src[4] << 24) | ((omc_u64)src[5] << 16)
           | ((omc_u64)src[6] << 8) | (omc_u64)src[7];
}

static omc_u64
omc_xmp_write_read_u64le(const omc_u8* src)
{
    return ((omc_u64)src[7] << 56) | ((omc_u64)src[6] << 48)
           | ((omc_u64)src[5] << 40) | ((omc_u64)src[4] << 32)
           | ((omc_u64)src[3] << 24) | ((omc_u64)src[2] << 16)
           | ((omc_u64)src[1] << 8) | (omc_u64)src[0];
}

static void
omc_xmp_write_store_u16be(omc_u8* dst, omc_u16 value)
{
    dst[0] = (omc_u8)((value >> 8) & 0xFFU);
    dst[1] = (omc_u8)(value & 0xFFU);
}

static void
omc_xmp_write_store_u16le(omc_u8* dst, omc_u16 value)
{
    dst[0] = (omc_u8)(value & 0xFFU);
    dst[1] = (omc_u8)((value >> 8) & 0xFFU);
}

static void
omc_xmp_write_store_u32be(omc_u8* dst, omc_u32 value)
{
    dst[0] = (omc_u8)((value >> 24) & 0xFFU);
    dst[1] = (omc_u8)((value >> 16) & 0xFFU);
    dst[2] = (omc_u8)((value >> 8) & 0xFFU);
    dst[3] = (omc_u8)(value & 0xFFU);
}

static void
omc_xmp_write_store_u32le(omc_u8* dst, omc_u32 value)
{
    dst[0] = (omc_u8)(value & 0xFFU);
    dst[1] = (omc_u8)((value >> 8) & 0xFFU);
    dst[2] = (omc_u8)((value >> 16) & 0xFFU);
    dst[3] = (omc_u8)((value >> 24) & 0xFFU);
}

static void
omc_xmp_write_store_u64be(omc_u8* dst, omc_u64 value)
{
    dst[0] = (omc_u8)((value >> 56) & 0xFFU);
    dst[1] = (omc_u8)((value >> 48) & 0xFFU);
    dst[2] = (omc_u8)((value >> 40) & 0xFFU);
    dst[3] = (omc_u8)((value >> 32) & 0xFFU);
    dst[4] = (omc_u8)((value >> 24) & 0xFFU);
    dst[5] = (omc_u8)((value >> 16) & 0xFFU);
    dst[6] = (omc_u8)((value >> 8) & 0xFFU);
    dst[7] = (omc_u8)(value & 0xFFU);
}

static void
omc_xmp_write_store_u64le(omc_u8* dst, omc_u64 value)
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

static omc_u32
omc_xmp_write_fourcc(char a, char b, char c, char d)
{
    return ((omc_u32)(omc_u8)a << 24) | ((omc_u32)(omc_u8)b << 16)
           | ((omc_u32)(omc_u8)c << 8) | (omc_u32)(omc_u8)d;
}

static int
omc_xmp_write_bmff_parse_box(const omc_u8* bytes, omc_size size,
                             omc_u64 offset, omc_u64 limit,
                             omc_xmp_write_bmff_box* out_box)
{
    omc_u32 size32;
    omc_u64 box_size;
    omc_u64 header_size;

    if (out_box == (omc_xmp_write_bmff_box*)0) {
        return 0;
    }
    memset(out_box, 0, sizeof(*out_box));
    if (offset > limit || limit > (omc_u64)size || offset + 8U > limit) {
        return 0;
    }

    size32 = omc_xmp_write_read_u32be(bytes + (omc_size)offset);
    out_box->type = omc_xmp_write_read_u32be(bytes + (omc_size)offset + 4U);
    header_size = 8U;
    if (size32 == 1U) {
        if (offset + 16U > limit) {
            return 0;
        }
        box_size = omc_xmp_write_read_u64be(bytes + (omc_size)offset + 8U);
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
omc_xmp_write_bmff_format_from_ftyp(const omc_u8* bytes, omc_size size,
                                    const omc_xmp_write_bmff_box* ftyp)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u32 brand;
    int is_heif;
    int is_avif;
    int is_cr3;
    omc_u64 off;

    if (ftyp == (const omc_xmp_write_bmff_box*)0 || ftyp->size < 16U) {
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

#define OMC_XMP_WRITE_NOTE_BMFF_BRAND(v) \
    do { \
        if ((v) == omc_xmp_write_fourcc('c', 'r', 'x', ' ') \
            || (v) == omc_xmp_write_fourcc('C', 'R', '3', ' ')) { \
            is_cr3 = 1; \
        } \
        if ((v) == omc_xmp_write_fourcc('a', 'v', 'i', 'f') \
            || (v) == omc_xmp_write_fourcc('a', 'v', 'i', 's')) { \
            is_avif = 1; \
        } \
        if ((v) == omc_xmp_write_fourcc('m', 'i', 'f', '1') \
            || (v) == omc_xmp_write_fourcc('m', 's', 'f', '1') \
            || (v) == omc_xmp_write_fourcc('h', 'e', 'i', 'c') \
            || (v) == omc_xmp_write_fourcc('h', 'e', 'i', 'x') \
            || (v) == omc_xmp_write_fourcc('h', 'e', 'v', 'c') \
            || (v) == omc_xmp_write_fourcc('h', 'e', 'v', 'x')) { \
            is_heif = 1; \
        } \
    } while (0)

    brand = omc_xmp_write_read_u32be(bytes + (omc_size)payload_off);
    OMC_XMP_WRITE_NOTE_BMFF_BRAND(brand);
    off = payload_off + 8U;
    while (off + 4U <= payload_end) {
        brand = omc_xmp_write_read_u32be(bytes + (omc_size)off);
        OMC_XMP_WRITE_NOTE_BMFF_BRAND(brand);
        off += 4U;
    }

#undef OMC_XMP_WRITE_NOTE_BMFF_BRAND

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

static int
omc_xmp_write_bmff_mime_is_xmp(const omc_u8* bytes, omc_size len)
{
    static const char k_rdf[] = "application/rdf+xml";
    static const char k_xmp[] = "application/xmp+xml";
    omc_size i;

    if (len == sizeof(k_rdf) - 1U) {
        for (i = 0U; i < len; ++i) {
            omc_u8 a;
            omc_u8 b;

            a = bytes[i];
            b = (omc_u8)k_rdf[i];
            if (a >= 'A' && a <= 'Z') {
                a = (omc_u8)(a - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
        }
        if (i == len) {
            return 1;
        }
    }
    if (len == sizeof(k_xmp) - 1U) {
        for (i = 0U; i < len; ++i) {
            omc_u8 a;
            omc_u8 b;

            a = bytes[i];
            b = (omc_u8)k_xmp[i];
            if (a >= 'A' && a <= 'Z') {
                a = (omc_u8)(a - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
        }
        if (i == len) {
            return 1;
        }
    }
    return 0;
}

static int
omc_xmp_write_bmff_find_cstring_end(const omc_u8* bytes, omc_u64 start,
                                    omc_u64 end, omc_u64* out_end)
{
    omc_u64 off;

    if (out_end == (omc_u64*)0 || start > end) {
        return 0;
    }
    off = start;
    while (off < end) {
        if (bytes[(omc_size)off] == 0U) {
            *out_end = off;
            return 1;
        }
        off += 1U;
    }
    return 0;
}

static int
omc_xmp_write_bmff_find_item(omc_xmp_write_bmff_item* items, omc_u32 item_count,
                             omc_u32 item_id)
{
    omc_u32 i;

    for (i = 0U; i < item_count; ++i) {
        if (items[i].item_id == item_id) {
            return (int)i;
        }
    }
    return -1;
}

static int
omc_xmp_write_bmff_value_fits(omc_u64 value, omc_u8 width)
{
    if (width == 0U) {
        return value == 0U;
    }
    if (width >= 8U) {
        return 1;
    }
    return (value >> (width * 8U)) == 0U;
}

static omc_status
omc_xmp_write_append_be_n(omc_arena* out, omc_u64 value, omc_u8 width)
{
    omc_u8 buf[8];
    omc_u8 i;

    if (width > 8U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0U; i < width; ++i) {
        buf[width - 1U - i] = (omc_u8)((value >> (i * 8U)) & 0xFFU);
    }
    return omc_xmp_write_append(out, buf, width);
}

static omc_u16
omc_xmp_write_tiff_read_u16(const omc_u8* src, int little_endian)
{
    if (little_endian) {
        return omc_xmp_write_read_u16le(src);
    }
    return omc_xmp_write_read_u16be(src);
}

static omc_u32
omc_xmp_write_tiff_read_u32(const omc_u8* src, int little_endian)
{
    if (little_endian) {
        return omc_xmp_write_read_u32le(src);
    }
    return omc_xmp_write_read_u32be(src);
}

static omc_u64
omc_xmp_write_tiff_read_u64(const omc_u8* src, int little_endian)
{
    if (little_endian) {
        return omc_xmp_write_read_u64le(src);
    }
    return omc_xmp_write_read_u64be(src);
}

static void
omc_xmp_write_tiff_store_u16(omc_u8* dst, int little_endian, omc_u16 value)
{
    if (little_endian) {
        omc_xmp_write_store_u16le(dst, value);
    } else {
        omc_xmp_write_store_u16be(dst, value);
    }
}

static void
omc_xmp_write_tiff_store_u32(omc_u8* dst, int little_endian, omc_u32 value)
{
    if (little_endian) {
        omc_xmp_write_store_u32le(dst, value);
    } else {
        omc_xmp_write_store_u32be(dst, value);
    }
}

static void
omc_xmp_write_tiff_store_u64(omc_u8* dst, int little_endian, omc_u64 value)
{
    if (little_endian) {
        omc_xmp_write_store_u64le(dst, value);
    } else {
        omc_xmp_write_store_u64be(dst, value);
    }
}

static omc_scan_fmt
omc_xmp_write_detect_format(const omc_u8* file_bytes, omc_size file_size)
{
    omc_u64 off;
    omc_u64 limit;

    if (file_size >= 2U && file_bytes[0] == 0xFFU && file_bytes[1] == 0xD8U) {
        return OMC_SCAN_FMT_JPEG;
    }
    if (file_size >= sizeof(k_omc_xmp_write_png_sig)
        && memcmp(file_bytes, k_omc_xmp_write_png_sig,
                  sizeof(k_omc_xmp_write_png_sig))
               == 0) {
        return OMC_SCAN_FMT_PNG;
    }
    if (file_size >= 8U
        && ((file_bytes[0] == 'I' && file_bytes[1] == 'I')
            || (file_bytes[0] == 'M' && file_bytes[1] == 'M'))) {
        return OMC_SCAN_FMT_TIFF;
    }
    if (file_size >= 12U && memcmp(file_bytes, "RIFF", 4U) == 0
        && memcmp(file_bytes + 8U, "WEBP", 4U) == 0) {
        return OMC_SCAN_FMT_WEBP;
    }
    if (file_size >= sizeof(k_omc_xmp_write_jp2_sig)
        && memcmp(file_bytes, k_omc_xmp_write_jp2_sig,
                  sizeof(k_omc_xmp_write_jp2_sig))
               == 0) {
        return OMC_SCAN_FMT_JP2;
    }
    if (file_size >= sizeof(k_omc_xmp_write_jxl_sig)
        && memcmp(file_bytes, k_omc_xmp_write_jxl_sig,
                  sizeof(k_omc_xmp_write_jxl_sig))
               == 0) {
        return OMC_SCAN_FMT_JXL;
    }

    off = 0U;
    limit = (omc_u64)file_size;
    while (off + 8U <= limit) {
        omc_xmp_write_bmff_box box;

        if (!omc_xmp_write_bmff_parse_box(file_bytes, file_size, off, limit,
                                          &box)) {
            break;
        }
        if (box.type == omc_xmp_write_fourcc('f', 't', 'y', 'p')) {
            return omc_xmp_write_bmff_format_from_ftyp(file_bytes, file_size,
                                                       &box);
        }
        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }
    return OMC_SCAN_FMT_UNKNOWN;
}

static int
omc_xmp_write_is_jpeg_standalone_marker(omc_u8 marker)
{
    if (marker == 0x01U || marker == 0xD8U || marker == 0xD9U) {
        return 1;
    }
    return marker >= 0xD0U && marker <= 0xD7U;
}

static int
omc_xmp_write_is_jpeg_exif_app1(const omc_u8* data, omc_size size)
{
    static const omc_u8 k_exif_sig[6]
        = { 'E', 'x', 'i', 'f', 0x00U, 0x00U };

    return size >= sizeof(k_exif_sig)
           && memcmp(data, k_exif_sig, sizeof(k_exif_sig)) == 0;
}

static int
omc_xmp_write_is_jpeg_xmp_app1(const omc_u8* data, omc_size size)
{
    return size >= sizeof(k_omc_xmp_write_jpeg_xmp_prefix) - 1U
           && memcmp(data, k_omc_xmp_write_jpeg_xmp_prefix,
                     sizeof(k_omc_xmp_write_jpeg_xmp_prefix) - 1U)
                  == 0;
}

static omc_status
omc_xmp_write_append_jpeg_xmp_segment(omc_arena* out, const omc_u8* payload,
                                      omc_size payload_size)
{
    omc_u8 header[4];
    omc_status status;

    if (payload_size > 65533U) {
        return OMC_STATUS_OVERFLOW;
    }

    header[0] = 0xFFU;
    header[1] = 0xE1U;
    omc_xmp_write_store_u16be(header + 2U, (omc_u16)(payload_size + 2U));
    status = omc_xmp_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return omc_xmp_write_append(out, payload, payload_size);
}

static omc_u32
omc_xmp_write_crc32_update(omc_u32 crc, const omc_u8* bytes, omc_size size)
{
    omc_size i;

    for (i = 0U; i < size; ++i) {
        omc_u32 x;
        int bit;

        crc ^= (omc_u32)bytes[i];
        x = crc;
        for (bit = 0; bit < 8; ++bit) {
            if ((x & 1U) != 0U) {
                x = (x >> 1) ^ 0xEDB88320U;
            } else {
                x >>= 1;
            }
        }
        crc = x;
    }
    return crc;
}

static omc_status
omc_xmp_write_append_png_xmp_chunk(omc_arena* out, const omc_u8* payload,
                                   omc_size payload_size)
{
    omc_u8 header[8];
    omc_u8 crc_bytes[4];
    omc_u32 crc;
    omc_status status;

    if (payload_size > (omc_size)(~(omc_u32)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_xmp_write_store_u32be(header, (omc_u32)payload_size);
    memcpy(header + 4U, "iTXt", 4U);
    status = omc_xmp_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_xmp_write_append(out, payload, payload_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    crc = 0xFFFFFFFFU;
    crc = omc_xmp_write_crc32_update(crc, header + 4U, 4U);
    crc = omc_xmp_write_crc32_update(crc, payload, payload_size);
    crc = ~crc;

    omc_xmp_write_store_u32be(crc_bytes, crc);
    return omc_xmp_write_append(out, crc_bytes, sizeof(crc_bytes));
}

static omc_status
omc_xmp_write_append_webp_chunk(omc_arena* out, const char* type,
                                const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_status status;

    if (payload_size > (omc_size)(~(omc_u32)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    memcpy(header, type, 4U);
    omc_xmp_write_store_u32le(header + 4U, (omc_u32)payload_size);
    status = omc_xmp_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_xmp_write_append(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if ((payload_size & 1U) != 0U) {
        static const omc_u8 k_pad = 0U;
        status = omc_xmp_write_append(out, &k_pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static omc_status
omc_xmp_write_append_jp2_box(omc_arena* out, omc_u32 type,
                             const omc_u8* payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_status status;

    if (payload_size > (omc_size)(0xFFFFFFFFU - 8U)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_xmp_write_store_u32be(header, (omc_u32)(payload_size + 8U));
    omc_xmp_write_store_u32be(header + 4U, type);
    status = omc_xmp_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (payload_size != 0U) {
        status = omc_xmp_write_append(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    return OMC_STATUS_OK;
}

static int
omc_xmp_write_is_png_xmp_itxt(const omc_u8* data, omc_u32 size)
{
    omc_u32 i;

    if (size < sizeof(k_omc_xmp_write_png_xmp_keyword)) {
        return 0;
    }

    i = 0U;
    while (i < size && data[i] != 0U) {
        i += 1U;
    }
    if (i != sizeof(k_omc_xmp_write_png_xmp_keyword) - 1U || i >= size) {
        return 0;
    }

    return memcmp(data, k_omc_xmp_write_png_xmp_keyword, i) == 0;
}

static omc_size
omc_xmp_write_align_up(omc_size value, omc_size align)
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

static int
omc_xmp_write_tiff_entry_less(const omc_u8* a, const omc_u8* b,
                              int little_endian)
{
    omc_u16 tag_a;
    omc_u16 tag_b;

    tag_a = omc_xmp_write_tiff_read_u16(a, little_endian);
    tag_b = omc_xmp_write_tiff_read_u16(b, little_endian);
    return tag_a < tag_b;
}

static void
omc_xmp_write_tiff_insertion_sort(omc_u8* entries, omc_size count,
                                  omc_size entry_size, int little_endian)
{
    omc_size i;

    for (i = 1U; i < count; ++i) {
        omc_size j;
        omc_u8 temp[20];

        memcpy(temp, entries + i * entry_size, entry_size);
        j = i;
        while (j > 0U
               && omc_xmp_write_tiff_entry_less(temp,
                                               entries + (j - 1U) * entry_size,
                                               little_endian)) {
            memcpy(entries + j * entry_size,
                   entries + (j - 1U) * entry_size, entry_size);
            j -= 1U;
        }
        memcpy(entries + j * entry_size, temp, entry_size);
    }
}

static omc_status
omc_xmp_write_rewrite_tiff(const omc_u8* file_bytes, omc_size file_size,
                           const omc_u8* payload, omc_size payload_size,
                           int strip_existing_xmp, int insert_xmp,
                           omc_arena* out, omc_xmp_write_res* out_res)
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

    if (file_size < 8U) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (file_bytes[0] == 'I' && file_bytes[1] == 'I') {
        little_endian = 1;
    } else if (file_bytes[0] == 'M' && file_bytes[1] == 'M') {
        little_endian = 0;
    } else {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    magic = omc_xmp_write_tiff_read_u16(file_bytes + 2U, little_endian);
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
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        off_size = omc_xmp_write_tiff_read_u16(file_bytes + 4U,
                                               little_endian);
        reserved = omc_xmp_write_tiff_read_u16(file_bytes + 6U,
                                               little_endian);
        if (off_size != 8U || reserved != 0U) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        ifd0_off = omc_xmp_write_tiff_read_u64(file_bytes + 8U,
                                               little_endian);
    } else if (magic == 42U) {
        big_tiff = 0;
        count_size = 2U;
        entry_size = 12U;
        next_size = 4U;
        inline_size = 4U;
        align = 2U;
        ifd0_off = omc_xmp_write_tiff_read_u32(file_bytes + 4U,
                                               little_endian);
    } else {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (ifd0_off >= (omc_u64)file_size || ifd0_off + count_size > file_size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    entries_off = (omc_size)ifd0_off + 2U;
    if (big_tiff) {
        count_u64 = omc_xmp_write_tiff_read_u64(file_bytes + ifd0_off,
                                                little_endian);
        entries_off = (omc_size)ifd0_off + 8U;
    } else {
        count_u64 = omc_xmp_write_tiff_read_u16(file_bytes + ifd0_off,
                                                little_endian);
    }
    if (count_u64 > 0xFFFFU) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }
    if (count_u64 > 0U
        && (omc_u64)entries_off + count_u64 * entry_size > (omc_u64)file_size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    next_ifd_off = entries_off + (omc_size)count_u64 * entry_size;
    if ((omc_u64)next_ifd_off + next_size > (omc_u64)file_size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (big_tiff) {
        next_ifd = omc_xmp_write_tiff_read_u64(file_bytes + next_ifd_off,
                                               little_endian);
    } else {
        next_ifd = omc_xmp_write_tiff_read_u32(file_bytes + next_ifd_off,
                                               little_endian);
    }

    removed = 0U;
    for (i = 0U; i < (omc_size)count_u64; ++i) {
        const omc_u8* entry;
        omc_u16 tag;

        entry = file_bytes + entries_off + i * entry_size;
        tag = omc_xmp_write_tiff_read_u16(entry, little_endian);
        if (tag == 700U && strip_existing_xmp) {
            removed += 1U;
        }
    }

    new_count_u32 = (omc_u32)count_u64 - removed + (insert_xmp ? 1U : 0U);
    if (new_count_u32 > 0xFFFFU) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    new_ifd_offset = omc_xmp_write_align_up(file_size, align);
    entry_table_size = (omc_size)new_count_u32 * entry_size;
    new_ifd_size = count_size + entry_table_size + next_size;
    payload_offset = 0U;
    if (insert_xmp && payload_size > inline_size) {
        payload_offset = omc_xmp_write_align_up(new_ifd_offset + new_ifd_size,
                                               align);
    } else {
        payload_offset = new_ifd_offset + count_size + entry_table_size;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, payload_offset + payload_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_xmp_write_append(out, file_bytes, file_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    while (out->size < new_ifd_offset) {
        static const omc_u8 k_pad = 0U;
        status = omc_xmp_write_append(out, &k_pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (big_tiff) {
        omc_xmp_write_tiff_store_u64(out->data + 8U, little_endian,
                                     (omc_u64)new_ifd_offset);
    } else {
        if (new_ifd_offset > 0xFFFFFFFFU) {
            out_res->status = OMC_XMP_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        omc_xmp_write_tiff_store_u32(out->data + 4U, little_endian,
                                     (omc_u32)new_ifd_offset);
    }

    if (big_tiff) {
        omc_xmp_write_tiff_store_u64(entry_buf, little_endian,
                                     (omc_u64)new_count_u32);
    } else {
        omc_xmp_write_tiff_store_u16(entry_buf, little_endian,
                                     (omc_u16)new_count_u32);
    }
    status = omc_xmp_write_append(out, entry_buf, count_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    sorted_entries = out->data + out->size;
    for (i = 0U; i < (omc_size)count_u64; ++i) {
        const omc_u8* entry;
        omc_u16 tag;

        entry = file_bytes + entries_off + i * entry_size;
        tag = omc_xmp_write_tiff_read_u16(entry, little_endian);
        if (tag == 700U && strip_existing_xmp) {
            continue;
        }
        status = omc_xmp_write_append(out, entry, entry_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (insert_xmp) {
        memset(entry_buf, 0, sizeof(entry_buf));
        omc_xmp_write_tiff_store_u16(entry_buf, little_endian, 700U);
        omc_xmp_write_tiff_store_u16(entry_buf + 2U, little_endian, 7U);
        if (big_tiff) {
            omc_xmp_write_tiff_store_u64(entry_buf + 4U, little_endian,
                                         (omc_u64)payload_size);
        } else if (payload_size != 0U) {
            omc_xmp_write_tiff_store_u32(entry_buf + 4U, little_endian,
                                         (omc_u32)payload_size);
        }
        if (payload_size > inline_size) {
            if (big_tiff) {
                omc_xmp_write_tiff_store_u64(entry_buf + 12U, little_endian,
                                             (omc_u64)payload_offset);
            } else {
                if (payload_offset > 0xFFFFFFFFU) {
                    out_res->status = OMC_XMP_WRITE_LIMIT;
                    return OMC_STATUS_OK;
                }
                omc_xmp_write_tiff_store_u32(entry_buf + 8U, little_endian,
                                             (omc_u32)payload_offset);
            }
        } else if (payload_size != 0U) {
            memcpy(entry_buf + entry_size - inline_size, payload, payload_size);
        }
        status = omc_xmp_write_append(out, entry_buf, entry_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    omc_xmp_write_tiff_insertion_sort(sorted_entries, new_count_u32,
                                      entry_size, little_endian);

    if (big_tiff) {
        omc_xmp_write_tiff_store_u64(entry_buf, little_endian, next_ifd);
    } else {
        if (next_ifd > 0xFFFFFFFFU) {
            out_res->status = OMC_XMP_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        omc_xmp_write_tiff_store_u32(entry_buf, little_endian,
                                     (omc_u32)next_ifd);
    }
    status = omc_xmp_write_append(out, entry_buf, next_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    while (out->size < payload_offset) {
        static const omc_u8 k_pad = 0U;
        status = omc_xmp_write_append(out, &k_pad, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if (insert_xmp && payload_size > inline_size) {
        status = omc_xmp_write_append(out, payload, payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    out_res->status = OMC_XMP_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    out_res->removed_xmp_blocks = removed;
    out_res->inserted_xmp_blocks = insert_xmp ? 1U : 0U;
    return OMC_STATUS_OK;
}

static omc_status
omc_xmp_write_rewrite_webp(const omc_u8* file_bytes, omc_size file_size,
                           const omc_u8* payload, omc_size payload_size,
                           int strip_existing_xmp, int insert_xmp,
                           omc_arena* out, omc_xmp_write_res* out_res)
{
    omc_size offset;
    omc_status status;
    omc_u32 riff_size;
    int inserted;
    int saw_vp8x;

    if (file_size < 12U || memcmp(file_bytes, "RIFF", 4U) != 0
        || memcmp(file_bytes + 8U, "WEBP", 4U) != 0) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (insert_xmp && payload_size > 0xFFFFFFFFU) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    riff_size = omc_xmp_write_read_u32le(file_bytes + 4U);
    if ((omc_u64)riff_size + 8U != (omc_u64)file_size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + (insert_xmp ? (8U + payload_size + 1U) : 0U));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_xmp_write_append(out, file_bytes, 12U);
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
        chunk_size = omc_xmp_write_read_u32le(file_bytes + offset + 4U);
        if ((omc_u64)chunk_size + 8U > (omc_u64)(file_size - offset)) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        padded_size = 8U + (omc_size)chunk_size + (chunk_size & 1U);
        if (offset + padded_size > file_size) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }

        if (memcmp(chunk_type, "XMP ", 4U) == 0 && strip_existing_xmp) {
            out_res->removed_xmp_blocks += 1U;
        } else if (memcmp(chunk_type, "VP8X", 4U) == 0) {
            omc_u8 vp8x[18];

            if (chunk_size != 10U) {
                out_res->status = OMC_XMP_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            memset(vp8x, 0, sizeof(vp8x));
            memcpy(vp8x, file_bytes + offset, padded_size);
            if (insert_xmp) {
                vp8x[8] = (omc_u8)(vp8x[8] | k_omc_xmp_write_webp_vp8x_xmp_bit);
            } else if (strip_existing_xmp) {
                vp8x[8] = (omc_u8)(vp8x[8] & (omc_u8)~k_omc_xmp_write_webp_vp8x_xmp_bit);
            }
            status = omc_xmp_write_append(out, vp8x, padded_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            saw_vp8x = 1;
            if (insert_xmp && !inserted) {
                status = omc_xmp_write_append_webp_chunk(out, "XMP ", payload,
                                                         payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_xmp_blocks = 1U;
                inserted = 1;
            }
        } else {
            if (insert_xmp && !inserted && !saw_vp8x) {
                status = omc_xmp_write_append_webp_chunk(out, "XMP ", payload,
                                                         payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_xmp_blocks = 1U;
                inserted = 1;
            }
            status = omc_xmp_write_append(out, file_bytes + offset, padded_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }

        offset += padded_size;
    }

    if (offset != file_size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (insert_xmp && !inserted) {
        status = omc_xmp_write_append_webp_chunk(out, "XMP ", payload,
                                                 payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        out_res->inserted_xmp_blocks = 1U;
    }
    if (out->size < 8U || out->size - 8U > 0xFFFFFFFFU) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }
    omc_xmp_write_store_u32le(out->data + 4U, (omc_u32)(out->size - 8U));
    out_res->status = OMC_XMP_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    return OMC_STATUS_OK;
}

static omc_status
omc_xmp_write_rewrite_jp2(const omc_u8* file_bytes, omc_size file_size,
                          const omc_u8* payload, omc_size payload_size,
                          int strip_existing_xmp, int insert_xmp,
                          omc_arena* out, omc_xmp_write_res* out_res)
{
    omc_size offset;
    omc_status status;
    int saw_signature;

    if (file_size < sizeof(k_omc_xmp_write_jp2_sig)
        || memcmp(file_bytes, k_omc_xmp_write_jp2_sig,
                  sizeof(k_omc_xmp_write_jp2_sig))
               != 0) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (insert_xmp && payload_size > (omc_size)(0xFFFFFFFFU - 8U)) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + (insert_xmp ? payload_size + 8U : 0U));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    saw_signature = 0;
    offset = 0U;
    while (offset + 8U <= file_size) {
        omc_u32 box_size_u32;
        omc_u32 box_type;
        omc_size box_size;

        box_size_u32 = omc_xmp_write_read_u32be(file_bytes + offset);
        box_type = omc_xmp_write_read_u32be(file_bytes + offset + 4U);
        if (box_size_u32 == 0U || box_size_u32 == 1U) {
            out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
            return OMC_STATUS_OK;
        }
        if (box_size_u32 < 8U) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        box_size = (omc_size)box_size_u32;
        if (offset + box_size > file_size) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }

        if (box_type == omc_xmp_write_fourcc('j', 'P', ' ', ' ')) {
            saw_signature = 1;
        }
        if (box_type == omc_xmp_write_fourcc('x', 'm', 'l', ' ')
            && strip_existing_xmp) {
            out_res->removed_xmp_blocks += 1U;
        } else {
            status = omc_xmp_write_append(out, file_bytes + offset, box_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        offset += box_size;
    }

    if (!saw_signature || offset != file_size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (insert_xmp) {
        status = omc_xmp_write_append_jp2_box(
            out, omc_xmp_write_fourcc('x', 'm', 'l', ' '), payload,
            payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        out_res->inserted_xmp_blocks = 1U;
    }
    out_res->status = OMC_XMP_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    return OMC_STATUS_OK;
}

static omc_status
omc_xmp_write_rewrite_jxl(const omc_u8* file_bytes, omc_size file_size,
                          const omc_u8* payload, omc_size payload_size,
                          int strip_existing_xmp, int insert_xmp,
                          omc_arena* out, omc_xmp_write_res* out_res)
{
    omc_u64 offset;
    omc_u64 limit;
    omc_status status;

    if (file_size < sizeof(k_omc_xmp_write_jxl_sig)
        || memcmp(file_bytes, k_omc_xmp_write_jxl_sig,
                  sizeof(k_omc_xmp_write_jxl_sig))
               != 0) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (insert_xmp && payload_size > (omc_size)(0xFFFFFFFFU - 8U)) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }
    if (insert_xmp
        && file_size > ((omc_size)(~(omc_size)0) - payload_size - 16U)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + (insert_xmp ? payload_size + 16U : 0U));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_xmp_write_append(out, file_bytes,
                                  sizeof(k_omc_xmp_write_jxl_sig));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    offset = (omc_u64)sizeof(k_omc_xmp_write_jxl_sig);
    limit = (omc_u64)file_size;
    while (offset + 8U <= limit) {
        omc_xmp_write_bmff_box box;

        if (!omc_xmp_write_bmff_parse_box(file_bytes, file_size, offset, limit,
                                          &box)) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (box.type == omc_xmp_write_fourcc('x', 'm', 'l', ' ')
            && strip_existing_xmp) {
            out_res->removed_xmp_blocks += 1U;
        } else if (box.type == omc_xmp_write_fourcc('b', 'r', 'o', 'b')) {
            omc_u64 payload_off;
            omc_u64 payload_len;

            payload_off = box.offset + box.header_size;
            payload_len = box.size - box.header_size;
            if (strip_existing_xmp && payload_len >= 4U
                && omc_xmp_write_read_u32be(
                       file_bytes + (omc_size)payload_off)
                       == omc_xmp_write_fourcc('x', 'm', 'l', ' ')) {
                out_res->removed_xmp_blocks += 1U;
            } else {
                status = omc_xmp_write_append(
                    out, file_bytes + (omc_size)box.offset,
                    (omc_size)box.size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
            }
        } else {
            status = omc_xmp_write_append(out,
                                          file_bytes + (omc_size)box.offset,
                                          (omc_size)box.size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        offset += box.size;
    }
    if (offset != limit) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (insert_xmp) {
        status = omc_xmp_write_append_jp2_box(
            out, omc_xmp_write_fourcc('x', 'm', 'l', ' '), payload,
            payload_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        out_res->inserted_xmp_blocks = 1U;
    }
    out_res->status = OMC_XMP_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    return OMC_STATUS_OK;
}

static omc_xmp_write_status
omc_xmp_write_bmff_parse_iinf(const omc_u8* file_bytes, omc_size file_size,
                              const omc_xmp_write_bmff_box* iinf,
                              omc_xmp_write_bmff_item* items,
                              omc_u32 item_cap, omc_u32* out_count)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 version;
    omc_u64 child_off;
    omc_u64 declared_count;
    omc_u32 count;

    if (iinf == (const omc_xmp_write_bmff_box*)0
        || items == (omc_xmp_write_bmff_item*)0
        || out_count == (omc_u32*)0) {
        return OMC_XMP_WRITE_MALFORMED;
    }

    payload_off = iinf->offset + iinf->header_size;
    payload_end = iinf->offset + iinf->size;
    if (payload_off + 4U > payload_end || payload_end > (omc_u64)file_size) {
        return OMC_XMP_WRITE_MALFORMED;
    }

    version = file_bytes[(omc_size)payload_off];
    child_off = payload_off + 4U;
    if (version == 0U) {
        if (child_off + 2U > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_xmp_write_read_u16be(
            file_bytes + (omc_size)child_off);
        child_off += 2U;
    } else {
        if (child_off + 4U > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_xmp_write_read_u32be(
            file_bytes + (omc_size)child_off);
        child_off += 4U;
    }

    count = 0U;
    while (child_off + 8U <= payload_end) {
        omc_xmp_write_bmff_box infe;
        omc_u64 infe_payload_off;
        omc_u64 infe_payload_end;
        omc_u8 infe_version;
        omc_u64 q;
        omc_u64 name_end;
        omc_u32 item_id;
        omc_u16 protection_index;
        omc_u32 item_type;
        int is_xmp;

        if (count >= item_cap) {
            return OMC_XMP_WRITE_LIMIT;
        }
        if (!omc_xmp_write_bmff_parse_box(file_bytes, file_size, child_off,
                                          payload_end, &infe)) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        if (infe.type != omc_xmp_write_fourcc('i', 'n', 'f', 'e')) {
            return OMC_XMP_WRITE_UNSUPPORTED;
        }

        infe_payload_off = infe.offset + infe.header_size;
        infe_payload_end = infe.offset + infe.size;
        if (infe_payload_off + 4U > infe_payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        infe_version = file_bytes[(omc_size)infe_payload_off];
        if (infe_version < 2U) {
            return OMC_XMP_WRITE_UNSUPPORTED;
        }

        q = infe_payload_off + 4U;
        if (infe_version == 2U) {
            if (q + 2U > infe_payload_end) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            item_id = (omc_u32)omc_xmp_write_read_u16be(
                file_bytes + (omc_size)q);
            q += 2U;
        } else {
            if (q + 4U > infe_payload_end) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            item_id = omc_xmp_write_read_u32be(file_bytes + (omc_size)q);
            q += 4U;
        }
        if (q + 2U + 4U > infe_payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        protection_index = omc_xmp_write_read_u16be(file_bytes + (omc_size)q);
        q += 2U;
        item_type = omc_xmp_write_read_u32be(file_bytes + (omc_size)q);
        q += 4U;

        if (!omc_xmp_write_bmff_find_cstring_end(file_bytes, q, infe_payload_end,
                                                 &name_end)) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        q = name_end + 1U;

        is_xmp = 0;
        if (item_type == omc_xmp_write_fourcc('x', 'm', 'l', ' ')) {
            is_xmp = 1;
        } else if (item_type == omc_xmp_write_fourcc('m', 'i', 'm', 'e')) {
            omc_u64 ct_end;

            if (!omc_xmp_write_bmff_find_cstring_end(
                    file_bytes, q, infe_payload_end, &ct_end)) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            if (omc_xmp_write_bmff_mime_is_xmp(
                    file_bytes + (omc_size)q, (omc_size)(ct_end - q))) {
                is_xmp = 1;
            }
        }

        memset(&items[count], 0, sizeof(items[count]));
        items[count].item_id = item_id;
        items[count].item_type = item_type;
        items[count].protection_index = protection_index;
        items[count].is_xmp = is_xmp;
        items[count].infe_off = (omc_size)infe.offset;
        items[count].infe_size = (omc_size)infe.size;
        count += 1U;
        child_off += infe.size;
        if (infe.size == 0U) {
            break;
        }
    }

    if (child_off != payload_end || declared_count != (omc_u64)count) {
        return OMC_XMP_WRITE_MALFORMED;
    }
    *out_count = count;
    return OMC_XMP_WRITE_OK;
}

static omc_xmp_write_status
omc_xmp_write_bmff_parse_iloc(const omc_u8* file_bytes, omc_size file_size,
                              const omc_xmp_write_bmff_box* iloc,
                              const omc_xmp_write_bmff_box* idat,
                              omc_xmp_write_bmff_item* items,
                              omc_u32 item_count, omc_u8* out_version,
                              omc_u8* out_offset_size,
                              omc_u8* out_length_size,
                              omc_u8* out_base_offset_size,
                              omc_u8* out_index_size)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 version;
    omc_u8 offset_size;
    omc_u8 length_size;
    omc_u8 base_offset_size;
    omc_u8 index_size;
    omc_u64 q;
    omc_u64 declared_count;
    omc_u64 idat_payload_off;
    omc_u64 idat_payload_end;
    omc_u64 seen;

    if (iloc == (const omc_xmp_write_bmff_box*)0
        || idat == (const omc_xmp_write_bmff_box*)0
        || items == (omc_xmp_write_bmff_item*)0 || out_version == (omc_u8*)0
        || out_offset_size == (omc_u8*)0 || out_length_size == (omc_u8*)0
        || out_base_offset_size == (omc_u8*)0
        || out_index_size == (omc_u8*)0) {
        return OMC_XMP_WRITE_MALFORMED;
    }

    payload_off = iloc->offset + iloc->header_size;
    payload_end = iloc->offset + iloc->size;
    if (payload_off + 6U > payload_end || payload_end > (omc_u64)file_size) {
        return OMC_XMP_WRITE_MALFORMED;
    }
    if (idat->offset + idat->header_size > idat->offset + idat->size
        || idat->offset + idat->size > (omc_u64)file_size) {
        return OMC_XMP_WRITE_MALFORMED;
    }

    version = file_bytes[(omc_size)payload_off];
    if (version != 1U && version != 2U) {
        return OMC_XMP_WRITE_UNSUPPORTED;
    }

    offset_size = (omc_u8)(file_bytes[(omc_size)payload_off + 4U] >> 4);
    length_size = (omc_u8)(file_bytes[(omc_size)payload_off + 4U] & 0x0FU);
    base_offset_size
        = (omc_u8)(file_bytes[(omc_size)payload_off + 5U] >> 4);
    index_size = (omc_u8)(file_bytes[(omc_size)payload_off + 5U] & 0x0FU);
    q = payload_off + 6U;

    if (version < 2U) {
        if (q + 2U > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_xmp_write_read_u16be(
            file_bytes + (omc_size)q);
        q += 2U;
    } else {
        if (q + 4U > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_xmp_write_read_u32be(
            file_bytes + (omc_size)q);
        q += 4U;
    }

    idat_payload_off = idat->offset + idat->header_size;
    idat_payload_end = idat->offset + idat->size;
    seen = 0U;
    while (seen < declared_count) {
        omc_u32 item_id;
        omc_u32 construction_method;
        omc_u16 data_ref_index;
        omc_u64 base_offset;
        omc_u16 extent_count;
        omc_u64 extent_index;
        omc_u64 extent_offset;
        omc_u64 extent_length;
        omc_u64 source_off;
        int item_idx;

        if (version < 2U) {
            if (q + 2U > payload_end) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            item_id = (omc_u32)omc_xmp_write_read_u16be(
                file_bytes + (omc_size)q);
            q += 2U;
        } else {
            if (q + 4U > payload_end) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            item_id = omc_xmp_write_read_u32be(file_bytes + (omc_size)q);
            q += 4U;
        }

        if (q + 2U > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        construction_method = (omc_u32)(omc_xmp_write_read_u16be(
                                 file_bytes + (omc_size)q)
                             & 0x000FU);
        q += 2U;
        if (q + 2U > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        data_ref_index = omc_xmp_write_read_u16be(file_bytes + (omc_size)q);
        q += 2U;

        if (base_offset_size > 8U || index_size > 8U || offset_size > 8U
            || length_size > 8U) {
            return OMC_XMP_WRITE_UNSUPPORTED;
        }
        if (q + (omc_u64)base_offset_size > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        base_offset = 0U;
        if (base_offset_size != 0U) {
            omc_u8 i;

            for (i = 0U; i < base_offset_size; ++i) {
                base_offset = (base_offset << 8)
                              | file_bytes[(omc_size)q + i];
            }
        }
        q += base_offset_size;

        if (q + 2U > payload_end) {
            return OMC_XMP_WRITE_MALFORMED;
        }
        extent_count = omc_xmp_write_read_u16be(file_bytes + (omc_size)q);
        q += 2U;
        if (extent_count != 1U || data_ref_index != 0U) {
            return OMC_XMP_WRITE_UNSUPPORTED;
        }

        extent_index = 0U;
        if (index_size != 0U) {
            omc_u8 i;

            if (q + (omc_u64)index_size > payload_end) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            for (i = 0U; i < index_size; ++i) {
                extent_index = (extent_index << 8)
                               | file_bytes[(omc_size)q + i];
            }
            q += index_size;
        }
        if (extent_index != 0U) {
            return OMC_XMP_WRITE_UNSUPPORTED;
        }

        extent_offset = 0U;
        if (offset_size != 0U) {
            omc_u8 i;

            if (q + (omc_u64)offset_size > payload_end) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            for (i = 0U; i < offset_size; ++i) {
                extent_offset = (extent_offset << 8)
                                | file_bytes[(omc_size)q + i];
            }
            q += offset_size;
        }

        extent_length = 0U;
        if (length_size != 0U) {
            omc_u8 i;

            if (q + (omc_u64)length_size > payload_end) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            for (i = 0U; i < length_size; ++i) {
                extent_length = (extent_length << 8)
                                | file_bytes[(omc_size)q + i];
            }
            q += length_size;
        }

        if (construction_method == 1U) {
            if (idat_payload_off > ((omc_u64)(~(omc_u64)0) - base_offset)
                || idat_payload_off + base_offset
                       > ((omc_u64)(~(omc_u64)0) - extent_offset)) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            source_off = idat_payload_off + base_offset + extent_offset;
            if (source_off > idat_payload_end
                || extent_length > idat_payload_end - source_off) {
                return OMC_XMP_WRITE_MALFORMED;
            }
        } else if (construction_method == 0U) {
            if (base_offset > ((omc_u64)(~(omc_u64)0) - extent_offset)) {
                return OMC_XMP_WRITE_MALFORMED;
            }
            source_off = base_offset + extent_offset;
            if (source_off > (omc_u64)file_size
                || extent_length > (omc_u64)file_size - source_off) {
                return OMC_XMP_WRITE_MALFORMED;
            }
        } else {
            return OMC_XMP_WRITE_UNSUPPORTED;
        }

        item_idx = omc_xmp_write_bmff_find_item(items, item_count, item_id);
        if (item_idx < 0 || items[item_idx].has_source) {
            return OMC_XMP_WRITE_UNSUPPORTED;
        }
        items[item_idx].has_source = 1;
        items[item_idx].source_off = source_off;
        items[item_idx].source_len = extent_length;
        seen += 1U;
    }

    if (q != payload_end) {
        return OMC_XMP_WRITE_MALFORMED;
    }

    *out_version = version;
    *out_offset_size = offset_size;
    *out_length_size = length_size;
    *out_base_offset_size = base_offset_size;
    *out_index_size = index_size;
    return OMC_XMP_WRITE_OK;
}

static omc_status
omc_xmp_write_bmff_append_xmp_infe(omc_arena* out, omc_u32 item_id)
{
    static const char k_xmp_name[] = "XMP";
    static const char k_xmp_ct[] = "application/rdf+xml";
    omc_u8 payload[80];
    omc_size payload_size;
    omc_u8 version;

    payload_size = 0U;
    version = item_id <= 0xFFFFU ? 2U : 3U;
    payload[payload_size++] = version;
    payload[payload_size++] = 0U;
    payload[payload_size++] = 0U;
    payload[payload_size++] = 0U;
    if (version == 2U) {
        omc_xmp_write_store_u16be(payload + payload_size, (omc_u16)item_id);
        payload_size += 2U;
    } else {
        omc_xmp_write_store_u32be(payload + payload_size, item_id);
        payload_size += 4U;
    }
    omc_xmp_write_store_u16be(payload + payload_size, 0U);
    payload_size += 2U;
    omc_xmp_write_store_u32be(payload + payload_size,
                              omc_xmp_write_fourcc('m', 'i', 'm', 'e'));
    payload_size += 4U;
    memcpy(payload + payload_size, k_xmp_name, sizeof(k_xmp_name));
    payload_size += sizeof(k_xmp_name);
    memcpy(payload + payload_size, k_xmp_ct, sizeof(k_xmp_ct));
    payload_size += sizeof(k_xmp_ct);
    payload[payload_size++] = 0U;
    return omc_xmp_write_append_jp2_box(
        out, omc_xmp_write_fourcc('i', 'n', 'f', 'e'), payload, payload_size);
}

static omc_status
omc_xmp_write_rewrite_bmff(const omc_u8* file_bytes, omc_size file_size,
                           const omc_u8* payload, omc_size payload_size,
                           int strip_existing_xmp, int insert_xmp,
                           omc_arena* out, omc_xmp_write_res* out_res)
{
    omc_xmp_write_bmff_box top_boxes[32];
    omc_xmp_write_bmff_box meta_children[32];
    omc_xmp_write_bmff_box ftyp_box;
    omc_xmp_write_bmff_box meta_box;
    omc_xmp_write_bmff_box iinf_box;
    omc_xmp_write_bmff_box iloc_box;
    omc_xmp_write_bmff_box idat_box;
    omc_xmp_write_bmff_item items[64];
    omc_u32 order[64];
    omc_u8 meta_fullbox[4];
    omc_u8 iinf_fullbox[4];
    omc_u8 iloc_fullbox[4];
    omc_u64 top_off;
    omc_u64 top_limit;
    omc_u64 child_off;
    omc_u64 child_end;
    omc_u32 top_count;
    omc_u32 meta_child_count;
    omc_u32 item_count;
    omc_u32 order_count;
    omc_u32 removed_count;
    omc_u32 max_item_id;
    omc_u32 xmp_item_id;
    omc_u64 xmp_new_off;
    omc_u64 xmp_new_len;
    omc_u8 iloc_version;
    omc_u8 offset_size;
    omc_u8 length_size;
    omc_u8 base_offset_size;
    omc_u8 index_size;
    omc_scan_fmt format;
    omc_xmp_write_status parse_status;
    omc_arena iinf_payload;
    omc_arena iloc_payload;
    omc_arena idat_payload;
    omc_arena meta_payload;
    omc_status status;
    int have_ftyp;
    int have_meta;
    int have_iinf;
    int have_iloc;
    int have_idat;
    int emitted_iinf;
    int emitted_iloc;
    int emitted_idat;
    omc_u32 i;

    if (insert_xmp && payload_size > (omc_size)(0xFFFFFFFFU - 8U)) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    memset(&ftyp_box, 0, sizeof(ftyp_box));
    memset(&meta_box, 0, sizeof(meta_box));
    memset(&iinf_box, 0, sizeof(iinf_box));
    memset(&iloc_box, 0, sizeof(iloc_box));
    memset(&idat_box, 0, sizeof(idat_box));
    memset(items, 0, sizeof(items));
    top_off = 0U;
    top_limit = (omc_u64)file_size;
    top_count = 0U;
    have_ftyp = 0;
    have_meta = 0;
    while (top_off + 8U <= top_limit) {
        if (top_count >= 32U) {
            out_res->status = OMC_XMP_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        if (!omc_xmp_write_bmff_parse_box(file_bytes, file_size, top_off,
                                          top_limit, &top_boxes[top_count])) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (top_boxes[top_count].type
            == omc_xmp_write_fourcc('f', 't', 'y', 'p')) {
            if (have_ftyp) {
                out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            ftyp_box = top_boxes[top_count];
            have_ftyp = 1;
        } else if (top_boxes[top_count].type
                   == omc_xmp_write_fourcc('m', 'e', 't', 'a')) {
            if (have_meta) {
                out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            meta_box = top_boxes[top_count];
            have_meta = 1;
        }
        top_off += top_boxes[top_count].size;
        top_count += 1U;
    }
    if (top_off != top_limit || !have_ftyp || !have_meta) {
        out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    format = omc_xmp_write_bmff_format_from_ftyp(file_bytes, file_size,
                                                 &ftyp_box);
    if (format != OMC_SCAN_FMT_HEIF && format != OMC_SCAN_FMT_AVIF) {
        out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    child_off = meta_box.offset + meta_box.header_size;
    child_end = meta_box.offset + meta_box.size;
    if (child_off + 4U > child_end || child_end > (omc_u64)file_size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    memcpy(meta_fullbox, file_bytes + (omc_size)child_off, sizeof(meta_fullbox));
    child_off += 4U;
    meta_child_count = 0U;
    have_iinf = 0;
    have_iloc = 0;
    have_idat = 0;
    while (child_off + 8U <= child_end) {
        if (meta_child_count >= 32U) {
            out_res->status = OMC_XMP_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        if (!omc_xmp_write_bmff_parse_box(file_bytes, file_size, child_off,
                                          child_end,
                                          &meta_children[meta_child_count])) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (meta_children[meta_child_count].type
            == omc_xmp_write_fourcc('i', 'i', 'n', 'f')) {
            if (have_iinf) {
                out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            iinf_box = meta_children[meta_child_count];
            have_iinf = 1;
        } else if (meta_children[meta_child_count].type
                   == omc_xmp_write_fourcc('i', 'l', 'o', 'c')) {
            if (have_iloc) {
                out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            iloc_box = meta_children[meta_child_count];
            have_iloc = 1;
        } else if (meta_children[meta_child_count].type
                   == omc_xmp_write_fourcc('i', 'd', 'a', 't')) {
            if (have_idat) {
                out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            idat_box = meta_children[meta_child_count];
            have_idat = 1;
        }
        child_off += meta_children[meta_child_count].size;
        meta_child_count += 1U;
    }
    if (child_off != child_end || !have_iinf || !have_iloc || !have_idat) {
        out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    parse_status = omc_xmp_write_bmff_parse_iinf(file_bytes, file_size,
                                                 &iinf_box, items, 64U,
                                                 &item_count);
    if (parse_status != OMC_XMP_WRITE_OK) {
        out_res->status = parse_status;
        return OMC_STATUS_OK;
    }

    if (iinf_box.offset + iinf_box.header_size + 4U > iinf_box.offset + iinf_box.size) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    memcpy(iinf_fullbox,
           file_bytes + (omc_size)(iinf_box.offset + iinf_box.header_size),
           sizeof(iinf_fullbox));

    parse_status = omc_xmp_write_bmff_parse_iloc(
        file_bytes, file_size, &iloc_box, &idat_box, items, item_count,
        &iloc_version, &offset_size, &length_size, &base_offset_size,
        &index_size);
    if (parse_status != OMC_XMP_WRITE_OK) {
        out_res->status = parse_status;
        return OMC_STATUS_OK;
    }
    memcpy(iloc_fullbox,
           file_bytes + (omc_size)(iloc_box.offset + iloc_box.header_size),
           sizeof(iloc_fullbox));

    removed_count = 0U;
    max_item_id = 0U;
    xmp_item_id = 0U;
    xmp_new_off = 0U;
    xmp_new_len = (omc_u64)payload_size;
    order_count = 0U;
    for (i = 0U; i < item_count; ++i) {
        if (!items[i].has_source) {
            out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
            return OMC_STATUS_OK;
        }
        if (items[i].item_id > max_item_id) {
            max_item_id = items[i].item_id;
        }
        if (items[i].is_xmp && strip_existing_xmp) {
            removed_count += 1U;
            if (insert_xmp && xmp_item_id == 0U) {
                xmp_item_id = items[i].item_id;
                order[order_count] = 0xFFFFFFFFU;
                order_count += 1U;
            }
        } else {
            order[order_count] = i;
            order_count += 1U;
        }
    }
    if (insert_xmp && xmp_item_id == 0U) {
        if (max_item_id == 0xFFFFFFFFU || order_count >= 64U) {
            out_res->status = OMC_XMP_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        xmp_item_id = max_item_id + 1U;
        order[order_count] = 0xFFFFFFFFU;
        order_count += 1U;
    }
    if (insert_xmp && iloc_version < 2U && xmp_item_id > 0xFFFFU) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }
    if (iinf_fullbox[0] == 0U && order_count > 0xFFFFU) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    omc_arena_init(&iinf_payload);
    omc_arena_init(&iloc_payload);
    omc_arena_init(&idat_payload);
    omc_arena_init(&meta_payload);
    status = OMC_STATUS_OK;

    for (i = 0U; i < order_count; ++i) {
        if (order[i] == 0xFFFFFFFFU) {
            if (!omc_xmp_write_bmff_value_fits((omc_u64)idat_payload.size,
                                               offset_size)
                || !omc_xmp_write_bmff_value_fits((omc_u64)payload_size,
                                                  length_size)) {
                out_res->status = OMC_XMP_WRITE_LIMIT;
                status = OMC_STATUS_OK;
                goto cleanup;
            }
            xmp_new_off = (omc_u64)idat_payload.size;
            status = omc_xmp_write_append(&idat_payload, payload, payload_size);
            if (status != OMC_STATUS_OK) {
                goto cleanup;
            }
        } else {
            omc_xmp_write_bmff_item* item;

            item = &items[order[i]];
            if (!omc_xmp_write_bmff_value_fits((omc_u64)idat_payload.size,
                                               offset_size)
                || !omc_xmp_write_bmff_value_fits(item->source_len,
                                                  length_size)) {
                out_res->status = OMC_XMP_WRITE_LIMIT;
                status = OMC_STATUS_OK;
                goto cleanup;
            }
            item->new_off = (omc_u64)idat_payload.size;
            item->new_len = item->source_len;
            status = omc_xmp_write_append(
                                          &idat_payload,
                                          file_bytes + (omc_size)item->source_off,
                                          (omc_size)item->source_len);
            if (status != OMC_STATUS_OK) {
                goto cleanup;
            }
        }
    }

    status = omc_xmp_write_append(&iinf_payload, iinf_fullbox,
                                  sizeof(iinf_fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    if (iinf_fullbox[0] == 0U) {
        status = omc_xmp_write_append_be_n(&iinf_payload, (omc_u64)order_count,
                                           2U);
    } else {
        status = omc_xmp_write_append_be_n(&iinf_payload, (omc_u64)order_count,
                                           4U);
    }
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    for (i = 0U; i < order_count; ++i) {
        if (order[i] == 0xFFFFFFFFU) {
            status = omc_xmp_write_bmff_append_xmp_infe(&iinf_payload,
                                                        xmp_item_id);
        } else {
            status = omc_xmp_write_append(&iinf_payload,
                                          file_bytes + items[order[i]].infe_off,
                                          items[order[i]].infe_size);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    }

    status = omc_xmp_write_append(&iloc_payload, iloc_fullbox,
                                  sizeof(iloc_fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    {
        omc_u8 size_bytes[2];

        size_bytes[0] = (omc_u8)((offset_size << 4) | length_size);
        size_bytes[1] = (omc_u8)((base_offset_size << 4) | index_size);
        status = omc_xmp_write_append(&iloc_payload, size_bytes,
                                      sizeof(size_bytes));
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    }
    if (iloc_version < 2U) {
        status = omc_xmp_write_append_be_n(&iloc_payload, (omc_u64)order_count,
                                           2U);
    } else {
        status = omc_xmp_write_append_be_n(&iloc_payload, (omc_u64)order_count,
                                           4U);
    }
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }

    for (i = 0U; i < order_count; ++i) {
        omc_u32 item_id;
        omc_u64 item_off;
        omc_u64 item_len;

        if (order[i] == 0xFFFFFFFFU) {
            item_id = xmp_item_id;
            item_off = xmp_new_off;
            item_len = xmp_new_len;
        } else {
            item_id = items[order[i]].item_id;
            item_off = items[order[i]].new_off;
            item_len = items[order[i]].new_len;
        }
        if (!omc_xmp_write_bmff_value_fits(item_off, offset_size)
            || !omc_xmp_write_bmff_value_fits(item_len, length_size)
            || !omc_xmp_write_bmff_value_fits(0U, base_offset_size)
            || !omc_xmp_write_bmff_value_fits(0U, index_size)) {
            out_res->status = OMC_XMP_WRITE_LIMIT;
            status = OMC_STATUS_OK;
            goto cleanup;
        }

        if (iloc_version < 2U) {
            status = omc_xmp_write_append_be_n(&iloc_payload, (omc_u64)item_id,
                                               2U);
        } else {
            status = omc_xmp_write_append_be_n(&iloc_payload, (omc_u64)item_id,
                                               4U);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
        status = omc_xmp_write_append_be_n(&iloc_payload, 1U, 2U);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
        status = omc_xmp_write_append_be_n(&iloc_payload, 0U, 2U);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
        status = omc_xmp_write_append_be_n(&iloc_payload, 0U, base_offset_size);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
        status = omc_xmp_write_append_be_n(&iloc_payload, 1U, 2U);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
        if (index_size != 0U) {
            status = omc_xmp_write_append_be_n(&iloc_payload, 0U, index_size);
            if (status != OMC_STATUS_OK) {
                goto cleanup;
            }
        }
        status = omc_xmp_write_append_be_n(&iloc_payload, item_off, offset_size);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
        status = omc_xmp_write_append_be_n(&iloc_payload, item_len, length_size);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    }

    status = omc_xmp_write_append(&meta_payload, meta_fullbox,
                                  sizeof(meta_fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    emitted_iinf = 0;
    emitted_iloc = 0;
    emitted_idat = 0;
    for (i = 0U; i < meta_child_count; ++i) {
        if (meta_children[i].type == omc_xmp_write_fourcc('i', 'i', 'n', 'f')) {
            status = omc_xmp_write_append_jp2_box(
                &meta_payload, omc_xmp_write_fourcc('i', 'i', 'n', 'f'),
                iinf_payload.data, iinf_payload.size);
            emitted_iinf = 1;
        } else if (meta_children[i].type
                   == omc_xmp_write_fourcc('i', 'l', 'o', 'c')) {
            status = omc_xmp_write_append_jp2_box(
                &meta_payload, omc_xmp_write_fourcc('i', 'l', 'o', 'c'),
                iloc_payload.data, iloc_payload.size);
            emitted_iloc = 1;
        } else if (meta_children[i].type
                   == omc_xmp_write_fourcc('i', 'd', 'a', 't')) {
            status = omc_xmp_write_append_jp2_box(
                &meta_payload, omc_xmp_write_fourcc('i', 'd', 'a', 't'),
                idat_payload.data, idat_payload.size);
            emitted_idat = 1;
        } else {
            status = omc_xmp_write_append(
                &meta_payload, file_bytes + (omc_size)meta_children[i].offset,
                (omc_size)meta_children[i].size);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    }
    if (!emitted_iinf || !emitted_iloc || !emitted_idat) {
        out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
        status = OMC_STATUS_OK;
        goto cleanup;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + (insert_xmp ? payload_size + 512U : 512U));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    for (i = 0U; i < top_count; ++i) {
        if (top_boxes[i].type == omc_xmp_write_fourcc('m', 'e', 't', 'a')) {
            status = omc_xmp_write_append_jp2_box(
                out, omc_xmp_write_fourcc('m', 'e', 't', 'a'),
                meta_payload.data, meta_payload.size);
        } else {
            status = omc_xmp_write_append(out,
                                          file_bytes + (omc_size)top_boxes[i].offset,
                                          (omc_size)top_boxes[i].size);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    }

    out_res->status = OMC_XMP_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    out_res->removed_xmp_blocks = removed_count;
    out_res->inserted_xmp_blocks = insert_xmp ? 1U : 0U;

cleanup:
    omc_arena_fini(&meta_payload);
    omc_arena_fini(&idat_payload);
    omc_arena_fini(&iloc_payload);
    omc_arena_fini(&iinf_payload);
    if (status == OMC_STATUS_OK && out_res->status != OMC_XMP_WRITE_OK) {
        omc_arena_reset(out);
        out_res->written = 0U;
        out_res->needed = 0U;
    }
    return status;
}

static omc_status
omc_xmp_write_rewrite_jpeg(const omc_u8* file_bytes, omc_size file_size,
                           const omc_u8* payload, omc_size payload_size,
                           int strip_existing_xmp, int insert_xmp,
                           omc_arena* out, omc_xmp_write_res* out_res)
{
    omc_size offset;
    omc_status status;
    int inserted;

    if (file_size < 4U || file_bytes[0] != 0xFFU || file_bytes[1] != 0xD8U) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (insert_xmp && payload_size > 65533U) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }
    if (insert_xmp
        && file_size > ((omc_size)(~(omc_size)0) - payload_size - 4U)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + (insert_xmp ? payload_size + 4U : 0U));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_xmp_write_append(out, file_bytes, 2U);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    inserted = 0;
    offset = 2U;
    while (offset < file_size) {
        omc_size marker_start;
        omc_u8 marker;

        if (file_bytes[offset] != 0xFFU) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }

        marker_start = offset;
        offset += 1U;
        while (offset < file_size && file_bytes[offset] == 0xFFU) {
            offset += 1U;
        }
        if (offset >= file_size) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }

        marker = file_bytes[offset];
        offset += 1U;

        if (marker == 0xD9U) {
            if (insert_xmp && !inserted) {
                status = omc_xmp_write_append_jpeg_xmp_segment(
                    out, payload, payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_xmp_blocks = 1U;
                inserted = 1;
            }
            status = omc_xmp_write_append(out, file_bytes + marker_start,
                                          file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->status = OMC_XMP_WRITE_OK;
            out_res->needed = out->size;
            out_res->written = out->size;
            return OMC_STATUS_OK;
        }

        if (omc_xmp_write_is_jpeg_standalone_marker(marker)) {
            if (insert_xmp && !inserted) {
                status = omc_xmp_write_append_jpeg_xmp_segment(
                    out, payload, payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_xmp_blocks = 1U;
                inserted = 1;
            }
            status = omc_xmp_write_append(out, file_bytes + marker_start,
                                          offset - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            continue;
        }

        if (offset + 2U > file_size) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }

        if (marker == 0xDAU) {
            omc_u16 seg_len;

            seg_len = omc_xmp_write_read_u16be(file_bytes + offset);
            if (seg_len < 2U || offset + (omc_size)seg_len > file_size) {
                out_res->status = OMC_XMP_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            if (insert_xmp && !inserted) {
                status = omc_xmp_write_append_jpeg_xmp_segment(
                    out, payload, payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_xmp_blocks = 1U;
                inserted = 1;
            }
            status = omc_xmp_write_append(out, file_bytes + marker_start,
                                          file_size - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->status = OMC_XMP_WRITE_OK;
            out_res->needed = out->size;
            out_res->written = out->size;
            return OMC_STATUS_OK;
        }

        {
            omc_u16 seg_len;
            omc_size segment_end;
            const omc_u8* seg_data;
            omc_size seg_data_size;
            int is_xmp;
            int is_leading;

            seg_len = omc_xmp_write_read_u16be(file_bytes + offset);
            if (seg_len < 2U) {
                out_res->status = OMC_XMP_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            if ((omc_size)seg_len > file_size - offset) {
                out_res->status = OMC_XMP_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }

            segment_end = offset + (omc_size)seg_len;
            seg_data = file_bytes + offset + 2U;
            seg_data_size = (omc_size)seg_len - 2U;
            is_xmp = marker == 0xE1U
                     && omc_xmp_write_is_jpeg_xmp_app1(seg_data, seg_data_size);
            is_leading = marker == 0xE0U
                         || (marker == 0xE1U
                             && omc_xmp_write_is_jpeg_exif_app1(seg_data,
                                                                seg_data_size));

            if (is_xmp && strip_existing_xmp) {
                out_res->removed_xmp_blocks += 1U;
                offset = segment_end;
                continue;
            }
            if (insert_xmp && !inserted && !is_leading) {
                status = omc_xmp_write_append_jpeg_xmp_segment(
                    out, payload, payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_xmp_blocks = 1U;
                inserted = 1;
            }
            status = omc_xmp_write_append(out, file_bytes + marker_start,
                                          segment_end - marker_start);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            offset = segment_end;
        }
    }

    out_res->status = OMC_XMP_WRITE_MALFORMED;
    return OMC_STATUS_OK;
}

static omc_status
omc_xmp_write_rewrite_png(const omc_u8* file_bytes, omc_size file_size,
                          const omc_u8* payload, omc_size payload_size,
                          int strip_existing_xmp, int insert_xmp,
                          omc_arena* out, omc_xmp_write_res* out_res)
{
    omc_size offset;
    omc_status status;
    int saw_ihdr;

    if (file_size < sizeof(k_omc_xmp_write_png_sig)
        || memcmp(file_bytes, k_omc_xmp_write_png_sig,
                  sizeof(k_omc_xmp_write_png_sig))
               != 0) {
        out_res->status = OMC_XMP_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (insert_xmp && payload_size > (omc_size)(~(omc_u32)0)) {
        out_res->status = OMC_XMP_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }
    if (insert_xmp
        && file_size > ((omc_size)(~(omc_size)0) - payload_size - 12U)) {
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + (insert_xmp ? payload_size + 12U : 0U));
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_xmp_write_append(out, file_bytes,
                                  sizeof(k_omc_xmp_write_png_sig));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    saw_ihdr = 0;
    offset = sizeof(k_omc_xmp_write_png_sig);
    while (offset + 12U <= file_size) {
        omc_u32 chunk_len;
        omc_size chunk_size;
        const omc_u8* chunk_type;

        chunk_len = omc_xmp_write_read_u32be(file_bytes + offset);
        if ((omc_u64)chunk_len + 12U > (omc_u64)(file_size - offset)) {
            out_res->status = OMC_XMP_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        chunk_size = (omc_size)chunk_len + 12U;
        chunk_type = file_bytes + offset + 4U;

        if (!saw_ihdr) {
            if (memcmp(chunk_type, "IHDR", 4U) != 0) {
                out_res->status = OMC_XMP_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            saw_ihdr = 1;
            status = omc_xmp_write_append(out, file_bytes + offset, chunk_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            if (insert_xmp) {
                status = omc_xmp_write_append_png_xmp_chunk(out, payload,
                                                            payload_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out_res->inserted_xmp_blocks = 1U;
            }
        } else if (memcmp(chunk_type, "iTXt", 4U) == 0
                   && omc_xmp_write_is_png_xmp_itxt(file_bytes + offset + 8U,
                                                    chunk_len)) {
            if (strip_existing_xmp) {
                out_res->removed_xmp_blocks += 1U;
            } else {
                status = omc_xmp_write_append(out, file_bytes + offset,
                                              chunk_size);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
            }
        } else if (memcmp(chunk_type, "IEND", 4U) == 0) {
            status = omc_xmp_write_append(out, file_bytes + offset,
                                          file_size - offset);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->status = OMC_XMP_WRITE_OK;
            out_res->needed = out->size;
            out_res->written = out->size;
            return OMC_STATUS_OK;
        } else {
            status = omc_xmp_write_append(out, file_bytes + offset, chunk_size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }

        offset += chunk_size;
    }

    out_res->status = OMC_XMP_WRITE_MALFORMED;
    return OMC_STATUS_OK;
}

static omc_status
omc_xmp_write_copy_original(const omc_u8* file_bytes, omc_size file_size,
                            omc_arena* out, omc_xmp_write_res* out_res)
{
    omc_status status;

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_xmp_write_append(out, file_bytes, file_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    out_res->status = OMC_XMP_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    return OMC_STATUS_OK;
}

void
omc_xmp_write_opts_init(omc_xmp_write_opts* opts)
{
    if (opts == (omc_xmp_write_opts*)0) {
        return;
    }

    opts->format = OMC_SCAN_FMT_UNKNOWN;
    opts->write_embedded_xmp = 1;
    opts->strip_existing_xmp = 1;
    omc_xmp_embed_opts_init(&opts->embed);
}

omc_status
omc_xmp_write_embedded(const omc_u8* file_bytes, omc_size file_size,
                       const omc_store* store, omc_arena* out,
                       const omc_xmp_write_opts* opts,
                       omc_xmp_write_res* out_res)
{
    omc_xmp_write_opts local_opts;
    omc_scan_fmt format;
    omc_xmp_embed_opts embed_opts;
    omc_arena payload;
    int strip_existing_xmp;
    int insert_xmp;
    omc_status status;

    if (file_bytes == (const omc_u8*)0 || store == (const omc_store*)0
        || out == (omc_arena*)0 || out_res == (omc_xmp_write_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_write_opts*)0) {
        omc_xmp_write_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_write_res_init(out_res);
    omc_arena_reset(out);

    format = opts->format;
    if (format == OMC_SCAN_FMT_UNKNOWN) {
        format = omc_xmp_write_detect_format(file_bytes, file_size);
    }
    out_res->format = format;
    if (format != OMC_SCAN_FMT_JPEG && format != OMC_SCAN_FMT_PNG
        && format != OMC_SCAN_FMT_TIFF && format != OMC_SCAN_FMT_WEBP
        && format != OMC_SCAN_FMT_JP2 && format != OMC_SCAN_FMT_JXL
        && format != OMC_SCAN_FMT_HEIF
        && format != OMC_SCAN_FMT_AVIF) {
        out_res->status = OMC_XMP_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    strip_existing_xmp = opts->strip_existing_xmp ? 1 : 0;
    insert_xmp = opts->write_embedded_xmp ? 1 : 0;
    if (insert_xmp) {
        strip_existing_xmp = 1;
    }
    if (!insert_xmp && !strip_existing_xmp) {
        return omc_xmp_write_copy_original(file_bytes, file_size, out, out_res);
    }

    omc_arena_init(&payload);
    if (insert_xmp) {
        embed_opts = opts->embed;
        embed_opts.format = format;
        status = omc_xmp_embed_payload_arena(store, &payload, &embed_opts,
                                             &out_res->payload);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&payload);
            return status;
        }
        if (out_res->payload.status != OMC_XMP_DUMP_OK) {
            out_res->status = OMC_XMP_WRITE_LIMIT;
            omc_arena_fini(&payload);
            omc_arena_reset(out);
            return OMC_STATUS_OK;
        }
    }

    if (format == OMC_SCAN_FMT_JPEG) {
        status = omc_xmp_write_rewrite_jpeg(file_bytes, file_size, payload.data,
                                            payload.size, strip_existing_xmp,
                                            insert_xmp, out, out_res);
    } else if (format == OMC_SCAN_FMT_PNG) {
        status = omc_xmp_write_rewrite_png(file_bytes, file_size, payload.data,
                                           payload.size, strip_existing_xmp,
                                           insert_xmp, out, out_res);
    } else if (format == OMC_SCAN_FMT_TIFF) {
        status = omc_xmp_write_rewrite_tiff(file_bytes, file_size, payload.data,
                                            payload.size, strip_existing_xmp,
                                            insert_xmp, out, out_res);
    } else if (format == OMC_SCAN_FMT_WEBP) {
        status = omc_xmp_write_rewrite_webp(file_bytes, file_size, payload.data,
                                            payload.size, strip_existing_xmp,
                                            insert_xmp, out, out_res);
    } else if (format == OMC_SCAN_FMT_JP2) {
        status = omc_xmp_write_rewrite_jp2(file_bytes, file_size, payload.data,
                                           payload.size, strip_existing_xmp,
                                           insert_xmp, out, out_res);
    } else if (format == OMC_SCAN_FMT_JXL) {
        status = omc_xmp_write_rewrite_jxl(file_bytes, file_size, payload.data,
                                           payload.size, strip_existing_xmp,
                                           insert_xmp, out, out_res);
    } else {
        status = omc_xmp_write_rewrite_bmff(file_bytes, file_size, payload.data,
                                            payload.size, strip_existing_xmp,
                                            insert_xmp, out, out_res);
    }

    omc_arena_fini(&payload);
    if (status == OMC_STATUS_OK && out_res->status != OMC_XMP_WRITE_OK) {
        omc_arena_reset(out);
        out_res->written = 0U;
        out_res->needed = 0U;
    }
    return status;
}
