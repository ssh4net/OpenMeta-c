#include "omc/omc_transfer.h"
#include "omc_exif_write.h"

#include <string.h>

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
           || format == OMC_SCAN_FMT_DNG;
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
