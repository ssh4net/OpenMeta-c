#include "omc_exif_write.h"

#include "omc/omc_read.h"

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

typedef struct omc_exif_write_text {
    const omc_u8* data;
    omc_size size;
    int present;
} omc_exif_write_text;

typedef struct omc_exif_write_fields {
    omc_exif_write_text make;
    omc_exif_write_text date_time_original;
} omc_exif_write_fields;

typedef struct omc_exif_write_bmff_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
} omc_exif_write_bmff_box;

typedef struct omc_exif_write_bmff_item {
    omc_u32 item_id;
    omc_u32 item_type;
    omc_u16 protection_index;
    int is_exif;
    int has_source;
    omc_size infe_off;
    omc_size infe_size;
    omc_u64 source_off;
    omc_u64 source_len;
    omc_u64 new_off;
    omc_u64 new_len;
} omc_exif_write_bmff_item;

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

static void
omc_exif_write_store_u16be(omc_u8* dst, omc_u16 value)
{
    dst[0] = (omc_u8)((value >> 8) & 0xFFU);
    dst[1] = (omc_u8)(value & 0xFFU);
}

static void
omc_exif_write_store_u16le(omc_u8* dst, omc_u16 value)
{
    dst[0] = (omc_u8)(value & 0xFFU);
    dst[1] = (omc_u8)((value >> 8) & 0xFFU);
}

static void
omc_exif_write_store_u64le(omc_u8* dst, omc_u64 value)
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

static omc_size
omc_exif_write_align_up(omc_size value, omc_size align)
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
omc_exif_write_tiff_store_count(omc_u8* dst, int big_tiff, omc_u64 value)
{
    if (big_tiff) {
        omc_exif_write_store_u64le(dst, value);
    } else {
        omc_exif_write_store_u16le(dst, (omc_u16)value);
    }
}

static omc_u16
omc_exif_write_tiff_entry_tag_le(const omc_u8* entry)
{
    return omc_exif_write_read_u16le(entry);
}

static void
omc_exif_write_tiff_insertion_sort_le(omc_u8* entries, omc_size count,
                                      omc_size entry_size)
{
    omc_size i;

    for (i = 1U; i < count; ++i) {
        omc_size j;
        omc_u8 temp[20];

        memcpy(temp, entries + i * entry_size, entry_size);
        j = i;
        while (j > 0U
               && omc_exif_write_tiff_entry_tag_le(temp)
                      < omc_exif_write_tiff_entry_tag_le(
                          entries + (j - 1U) * entry_size)) {
            memcpy(entries + j * entry_size,
                   entries + (j - 1U) * entry_size, entry_size);
            j -= 1U;
        }
        memcpy(entries + j * entry_size, temp, entry_size);
    }
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

static int
omc_exif_write_ifd_equals(omc_const_bytes ifd_view, const char* expect)
{
    if (expect == (const char*)0) {
        return 0;
    }
    return ifd_view.size == strlen(expect)
           && memcmp(ifd_view.data, expect, ifd_view.size) == 0;
}

static int
omc_exif_write_text_from_value(const omc_store* store, const omc_val* value,
                               omc_exif_write_text* out)
{
    omc_const_bytes view;
    omc_size size;

    if (store == (const omc_store*)0 || value == (const omc_val*)0
        || out == (omc_exif_write_text*)0) {
        return 0;
    }
    if (value->kind != OMC_VAL_TEXT) {
        return 0;
    }
    view = omc_arena_view(&store->arena, value->u.ref);
    size = view.size;
    while (size > 0U && view.data[size - 1U] == 0U) {
        size -= 1U;
    }
    if (size == 0U) {
        return 0;
    }
    out->data = view.data;
    out->size = size;
    out->present = 1;
    return 1;
}

static int
omc_exif_write_find_text_tag(const omc_store* store, const char* ifd_name,
                             omc_u16 tag, omc_exif_write_text* out)
{
    omc_size i;

    if (store == (const omc_store*)0 || ifd_name == (const char*)0
        || out == (omc_exif_write_text*)0) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag) {
            continue;
        }
        ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (!omc_exif_write_ifd_equals(ifd_view, ifd_name)) {
            continue;
        }
        if (omc_exif_write_text_from_value(store, &entry->value, out)) {
            return 1;
        }
    }
    return 0;
}

static int
omc_exif_write_select_fields(const omc_store* source,
                             const omc_store* current,
                             omc_exif_write_fields* out)
{
    if (out == (omc_exif_write_fields*)0) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    if (source != (const omc_store*)0) {
        (void)omc_exif_write_find_text_tag(source, "ifd0", 0x010FU,
                                           &out->make);
        (void)omc_exif_write_find_text_tag(source, "exififd", 0x9003U,
                                           &out->date_time_original);
    }
    if (!out->make.present && current != (const omc_store*)0) {
        (void)omc_exif_write_find_text_tag(current, "ifd0", 0x010FU,
                                           &out->make);
    }
    if (!out->date_time_original.present && current != (const omc_store*)0) {
        (void)omc_exif_write_find_text_tag(current, "exififd", 0x9003U,
                                           &out->date_time_original);
    }
    return out->make.present || out->date_time_original.present;
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

    omc_exif_write_store_u32be(header, (omc_u32)payload_size);
    memcpy(header + 4U, type, 4U);
    memset(trailer, 0, sizeof(trailer));
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

#define OMC_EXIF_WRITE_NOTE_BMFF_BRAND(v) \
    do { \
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

    if (is_avif) {
        return OMC_SCAN_FMT_AVIF;
    }
    if (is_heif) {
        return OMC_SCAN_FMT_HEIF;
    }
    return OMC_SCAN_FMT_UNKNOWN;
}

static int
omc_exif_write_bmff_find_cstring_end(const omc_u8* bytes, omc_u64 start,
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
omc_exif_write_bmff_find_item(omc_exif_write_bmff_item* items,
                              omc_u32 item_count, omc_u32 item_id)
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
omc_exif_write_bmff_value_fits(omc_u64 value, omc_u8 width)
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
omc_exif_write_append_be_n(omc_arena* out, omc_u64 value, omc_u8 width)
{
    omc_u8 buf[8];
    omc_u8 i;

    if (width > 8U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0U; i < width; ++i) {
        buf[width - 1U - i] = (omc_u8)((value >> (i * 8U)) & 0xFFU);
    }
    return omc_exif_write_append(out, buf, width);
}

static omc_exif_write_status
omc_exif_write_bmff_parse_iinf(const omc_u8* file_bytes, omc_size file_size,
                               const omc_exif_write_bmff_box* iinf,
                               omc_exif_write_bmff_item* items,
                               omc_u32 item_cap, omc_u32* out_count)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 version;
    omc_u64 child_off;
    omc_u64 declared_count;
    omc_u32 count;

    if (iinf == (const omc_exif_write_bmff_box*)0
        || items == (omc_exif_write_bmff_item*)0
        || out_count == (omc_u32*)0) {
        return OMC_EXIF_WRITE_MALFORMED;
    }

    payload_off = iinf->offset + iinf->header_size;
    payload_end = iinf->offset + iinf->size;
    if (payload_off + 4U > payload_end || payload_end > (omc_u64)file_size) {
        return OMC_EXIF_WRITE_MALFORMED;
    }

    version = file_bytes[(omc_size)payload_off];
    child_off = payload_off + 4U;
    if (version == 0U) {
        if (child_off + 2U > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_exif_write_read_u16be(
            file_bytes + (omc_size)child_off);
        child_off += 2U;
    } else {
        if (child_off + 4U > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_exif_write_read_u32be(
            file_bytes + (omc_size)child_off);
        child_off += 4U;
    }

    count = 0U;
    while (child_off + 8U <= payload_end) {
        omc_exif_write_bmff_box infe;
        omc_u64 infe_payload_off;
        omc_u64 infe_payload_end;
        omc_u8 infe_version;
        omc_u64 q;
        omc_u64 name_end;
        omc_u32 item_id;
        omc_u16 protection_index;
        omc_u32 item_type;

        if (count >= item_cap) {
            return OMC_EXIF_WRITE_LIMIT;
        }
        if (!omc_exif_write_parse_bmff_box(file_bytes, file_size, child_off,
                                           payload_end, &infe)) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        if (infe.type != omc_exif_write_fourcc('i', 'n', 'f', 'e')) {
            return OMC_EXIF_WRITE_UNSUPPORTED;
        }

        infe_payload_off = infe.offset + infe.header_size;
        infe_payload_end = infe.offset + infe.size;
        if (infe_payload_off + 4U > infe_payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        infe_version = file_bytes[(omc_size)infe_payload_off];
        if (infe_version < 2U) {
            return OMC_EXIF_WRITE_UNSUPPORTED;
        }

        q = infe_payload_off + 4U;
        if (infe_version == 2U) {
            if (q + 2U > infe_payload_end) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
            item_id = (omc_u32)omc_exif_write_read_u16be(
                file_bytes + (omc_size)q);
            q += 2U;
        } else {
            if (q + 4U > infe_payload_end) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
            item_id = omc_exif_write_read_u32be(file_bytes + (omc_size)q);
            q += 4U;
        }
        if (q + 2U + 4U > infe_payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        protection_index = omc_exif_write_read_u16be(file_bytes + (omc_size)q);
        q += 2U;
        item_type = omc_exif_write_read_u32be(file_bytes + (omc_size)q);
        q += 4U;

        if (!omc_exif_write_bmff_find_cstring_end(file_bytes, q, infe_payload_end,
                                                  &name_end)) {
            return OMC_EXIF_WRITE_MALFORMED;
        }

        memset(&items[count], 0, sizeof(items[count]));
        items[count].item_id = item_id;
        items[count].item_type = item_type;
        items[count].protection_index = protection_index;
        items[count].is_exif
            = item_type == omc_exif_write_fourcc('E', 'x', 'i', 'f');
        items[count].infe_off = (omc_size)infe.offset;
        items[count].infe_size = (omc_size)infe.size;
        count += 1U;
        child_off += infe.size;
        if (infe.size == 0U) {
            break;
        }
    }

    if (child_off != payload_end || declared_count != (omc_u64)count) {
        return OMC_EXIF_WRITE_MALFORMED;
    }
    *out_count = count;
    return OMC_EXIF_WRITE_OK;
}

static omc_exif_write_status
omc_exif_write_bmff_parse_iloc(const omc_u8* file_bytes, omc_size file_size,
                               const omc_exif_write_bmff_box* iloc,
                               const omc_exif_write_bmff_box* idat,
                               omc_exif_write_bmff_item* items,
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

    if (iloc == (const omc_exif_write_bmff_box*)0
        || idat == (const omc_exif_write_bmff_box*)0
        || items == (omc_exif_write_bmff_item*)0
        || out_version == (omc_u8*)0 || out_offset_size == (omc_u8*)0
        || out_length_size == (omc_u8*)0
        || out_base_offset_size == (omc_u8*)0
        || out_index_size == (omc_u8*)0) {
        return OMC_EXIF_WRITE_MALFORMED;
    }

    payload_off = iloc->offset + iloc->header_size;
    payload_end = iloc->offset + iloc->size;
    if (payload_off + 6U > payload_end || payload_end > (omc_u64)file_size) {
        return OMC_EXIF_WRITE_MALFORMED;
    }
    if (idat->offset + idat->header_size > idat->offset + idat->size
        || idat->offset + idat->size > (omc_u64)file_size) {
        return OMC_EXIF_WRITE_MALFORMED;
    }

    version = file_bytes[(omc_size)payload_off];
    if (version != 1U && version != 2U) {
        return OMC_EXIF_WRITE_UNSUPPORTED;
    }

    offset_size = (omc_u8)(file_bytes[(omc_size)payload_off + 4U] >> 4);
    length_size = (omc_u8)(file_bytes[(omc_size)payload_off + 4U] & 0x0FU);
    base_offset_size
        = (omc_u8)(file_bytes[(omc_size)payload_off + 5U] >> 4);
    index_size = (omc_u8)(file_bytes[(omc_size)payload_off + 5U] & 0x0FU);
    q = payload_off + 6U;

    if (version < 2U) {
        if (q + 2U > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_exif_write_read_u16be(
            file_bytes + (omc_size)q);
        q += 2U;
    } else {
        if (q + 4U > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        declared_count = (omc_u64)omc_exif_write_read_u32be(
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
        omc_u8 i;

        if (version < 2U) {
            if (q + 2U > payload_end) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
            item_id = (omc_u32)omc_exif_write_read_u16be(
                file_bytes + (omc_size)q);
            q += 2U;
        } else {
            if (q + 4U > payload_end) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
            item_id = omc_exif_write_read_u32be(file_bytes + (omc_size)q);
            q += 4U;
        }

        if (q + 2U > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        construction_method = (omc_u32)(omc_exif_write_read_u16be(
                                 file_bytes + (omc_size)q)
                             & 0x000FU);
        q += 2U;
        if (q + 2U > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        data_ref_index = omc_exif_write_read_u16be(file_bytes + (omc_size)q);
        q += 2U;

        if (base_offset_size > 8U || index_size > 8U || offset_size > 8U
            || length_size > 8U) {
            return OMC_EXIF_WRITE_UNSUPPORTED;
        }
        if (q + (omc_u64)base_offset_size > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        base_offset = 0U;
        if (base_offset_size != 0U) {
            for (i = 0U; i < base_offset_size; ++i) {
                base_offset = (base_offset << 8)
                              | file_bytes[(omc_size)q + i];
            }
        }
        q += base_offset_size;

        if (q + 2U > payload_end) {
            return OMC_EXIF_WRITE_MALFORMED;
        }
        extent_count = omc_exif_write_read_u16be(file_bytes + (omc_size)q);
        q += 2U;
        if (extent_count != 1U || data_ref_index != 0U) {
            return OMC_EXIF_WRITE_UNSUPPORTED;
        }

        extent_index = 0U;
        if (index_size != 0U) {
            if (q + (omc_u64)index_size > payload_end) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
            for (i = 0U; i < index_size; ++i) {
                extent_index = (extent_index << 8)
                               | file_bytes[(omc_size)q + i];
            }
            q += index_size;
        }
        if (extent_index != 0U) {
            return OMC_EXIF_WRITE_UNSUPPORTED;
        }

        extent_offset = 0U;
        if (offset_size != 0U) {
            if (q + (omc_u64)offset_size > payload_end) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
            for (i = 0U; i < offset_size; ++i) {
                extent_offset = (extent_offset << 8)
                                | file_bytes[(omc_size)q + i];
            }
            q += offset_size;
        }

        extent_length = 0U;
        if (length_size != 0U) {
            if (q + (omc_u64)length_size > payload_end) {
                return OMC_EXIF_WRITE_MALFORMED;
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
                return OMC_EXIF_WRITE_MALFORMED;
            }
            source_off = idat_payload_off + base_offset + extent_offset;
            if (source_off > idat_payload_end
                || extent_length > idat_payload_end - source_off) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
        } else if (construction_method == 0U) {
            if (base_offset > ((omc_u64)(~(omc_u64)0) - extent_offset)) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
            source_off = base_offset + extent_offset;
            if (source_off > (omc_u64)file_size
                || extent_length > (omc_u64)file_size - source_off) {
                return OMC_EXIF_WRITE_MALFORMED;
            }
        } else {
            return OMC_EXIF_WRITE_UNSUPPORTED;
        }

        item_idx = omc_exif_write_bmff_find_item(items, item_count, item_id);
        if (item_idx < 0 || items[item_idx].has_source) {
            return OMC_EXIF_WRITE_UNSUPPORTED;
        }
        items[item_idx].has_source = 1;
        items[item_idx].source_off = source_off;
        items[item_idx].source_len = extent_length;
        seen += 1U;
    }

    if (q != payload_end) {
        return OMC_EXIF_WRITE_MALFORMED;
    }

    *out_version = version;
    *out_offset_size = offset_size;
    *out_length_size = length_size;
    *out_base_offset_size = base_offset_size;
    *out_index_size = index_size;
    return OMC_EXIF_WRITE_OK;
}

static omc_status
omc_exif_write_bmff_append_exif_infe(omc_arena* out, omc_u32 item_id)
{
    static const char k_exif_name[] = "Exif";
    omc_u8 payload[32];
    omc_size payload_size;
    omc_u8 version;

    payload_size = 0U;
    version = item_id <= 0xFFFFU ? 2U : 3U;
    payload[payload_size++] = version;
    payload[payload_size++] = 0U;
    payload[payload_size++] = 0U;
    payload[payload_size++] = 0U;
    if (version == 2U) {
        omc_exif_write_store_u16be(payload + payload_size, (omc_u16)item_id);
        payload_size += 2U;
    } else {
        omc_exif_write_store_u32be(payload + payload_size, item_id);
        payload_size += 4U;
    }
    omc_exif_write_store_u16be(payload + payload_size, 0U);
    payload_size += 2U;
    omc_exif_write_store_u32be(payload + payload_size,
                               omc_exif_write_fourcc('E', 'x', 'i', 'f'));
    payload_size += 4U;
    memcpy(payload + payload_size, k_exif_name, sizeof(k_exif_name));
    payload_size += sizeof(k_exif_name);
    return omc_exif_write_append_bmff_box(
        out, omc_exif_write_fourcc('i', 'n', 'f', 'e'), payload, payload_size);
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
omc_exif_write_bmff_build_minimal_meta(const omc_u8* payload,
                                       omc_size payload_size,
                                       omc_arena* meta_payload)
{
    omc_arena iinf_payload;
    omc_arena iloc_payload;
    omc_arena idat_payload;
    omc_status status;
    omc_u8 fullbox[4];
    omc_u8 size_bytes[2];

    omc_arena_init(&iinf_payload);
    omc_arena_init(&iloc_payload);
    omc_arena_init(&idat_payload);

    memset(fullbox, 0, sizeof(fullbox));
    status = omc_exif_write_append(&iinf_payload, fullbox, sizeof(fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iinf_payload, 1U, 2U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_bmff_append_exif_infe(&iinf_payload, 1U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }

    fullbox[0] = 1U;
    status = omc_exif_write_append(&iloc_payload, fullbox, sizeof(fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    size_bytes[0] = 0x44U;
    size_bytes[1] = 0x40U;
    status = omc_exif_write_append(&iloc_payload, size_bytes,
                                   sizeof(size_bytes));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iloc_payload, 1U, 2U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iloc_payload, 1U, 2U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iloc_payload, 0U, 2U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iloc_payload, 0U, 4U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iloc_payload, 1U, 2U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iloc_payload, 0U, 4U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_be_n(&iloc_payload, (omc_u64)payload_size,
                                        4U);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }

    status = omc_exif_write_append(&idat_payload, payload, payload_size);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }

    omc_arena_reset(meta_payload);
    memset(fullbox, 0, sizeof(fullbox));
    status = omc_exif_write_append(meta_payload, fullbox, sizeof(fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_bmff_box(
        meta_payload, omc_exif_write_fourcc('i', 'i', 'n', 'f'),
        iinf_payload.data, iinf_payload.size);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_bmff_box(
        meta_payload, omc_exif_write_fourcc('i', 'l', 'o', 'c'),
        iloc_payload.data, iloc_payload.size);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    status = omc_exif_write_append_bmff_box(
        meta_payload, omc_exif_write_fourcc('i', 'd', 'a', 't'),
        idat_payload.data, idat_payload.size);

cleanup:
    omc_arena_fini(&idat_payload);
    omc_arena_fini(&iloc_payload);
    omc_arena_fini(&iinf_payload);
    return status;
}

static omc_status
omc_exif_write_rewrite_bmff_item_exif(const omc_u8* file_bytes,
                                      omc_size file_size,
                                      const omc_u8* payload,
                                      omc_size payload_size,
                                      omc_arena* out,
                                      omc_exif_write_res* out_res)
{
    omc_exif_write_bmff_box top_boxes[32];
    omc_exif_write_bmff_box meta_children[32];
    omc_exif_write_bmff_box ftyp_box;
    omc_exif_write_bmff_box meta_box;
    omc_exif_write_bmff_box iinf_box;
    omc_exif_write_bmff_box iloc_box;
    omc_exif_write_bmff_box idat_box;
    omc_exif_write_bmff_item items[64];
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
    omc_u32 exif_item_id;
    omc_u64 exif_new_off;
    omc_u64 exif_new_len;
    omc_u8 iloc_version;
    omc_u8 offset_size;
    omc_u8 length_size;
    omc_u8 base_offset_size;
    omc_u8 index_size;
    omc_scan_fmt format;
    omc_exif_write_status parse_status;
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
    omc_arena_init(&meta_payload);
    while (top_off + 8U <= top_limit) {
        if (top_count >= 32U) {
            omc_arena_fini(&meta_payload);
            out_res->status = OMC_EXIF_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        if (!omc_exif_write_parse_bmff_box(file_bytes, file_size, top_off,
                                           top_limit, &top_boxes[top_count])) {
            omc_arena_fini(&meta_payload);
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (top_boxes[top_count].type
            == omc_exif_write_fourcc('f', 't', 'y', 'p')) {
            if (have_ftyp) {
                omc_arena_fini(&meta_payload);
                out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            ftyp_box = top_boxes[top_count];
            have_ftyp = 1;
        } else if (top_boxes[top_count].type
                   == omc_exif_write_fourcc('m', 'e', 't', 'a')) {
            if (have_meta) {
                omc_arena_fini(&meta_payload);
                out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            meta_box = top_boxes[top_count];
            have_meta = 1;
        }
        top_off += top_boxes[top_count].size;
        top_count += 1U;
    }
    if (top_off != top_limit || !have_ftyp) {
        omc_arena_fini(&meta_payload);
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    format = omc_exif_write_bmff_format_from_ftyp(file_bytes, file_size,
                                                  &ftyp_box);
    if (format != OMC_SCAN_FMT_HEIF && format != OMC_SCAN_FMT_AVIF) {
        omc_arena_fini(&meta_payload);
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }
    if (!have_meta) {
        status = omc_exif_write_bmff_build_minimal_meta(payload, payload_size,
                                                        &meta_payload);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&meta_payload);
            return status;
        }
        omc_arena_reset(out);
        status = omc_arena_reserve(out, file_size + payload_size + 512U);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&meta_payload);
            return status;
        }
        for (i = 0U; i < top_count; ++i) {
            status = omc_exif_write_append(
                out, file_bytes + (omc_size)top_boxes[i].offset,
                (omc_size)top_boxes[i].size);
            if (status != OMC_STATUS_OK) {
                omc_arena_fini(&meta_payload);
                return status;
            }
        }
        status = omc_exif_write_append_bmff_box(
            out, omc_exif_write_fourcc('m', 'e', 't', 'a'),
            meta_payload.data, meta_payload.size);
        omc_arena_fini(&meta_payload);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        out_res->status = OMC_EXIF_WRITE_OK;
        out_res->needed = out->size;
        out_res->written = out->size;
        out_res->removed_exif_blocks = 0U;
        out_res->inserted_exif_blocks = 1U;
        return OMC_STATUS_OK;
    }

    child_off = meta_box.offset + meta_box.header_size;
    child_end = meta_box.offset + meta_box.size;
    if (child_off + 4U > child_end || child_end > (omc_u64)file_size) {
        omc_arena_fini(&meta_payload);
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
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
            omc_arena_fini(&meta_payload);
            out_res->status = OMC_EXIF_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        if (!omc_exif_write_parse_bmff_box(file_bytes, file_size, child_off,
                                           child_end,
                                           &meta_children[meta_child_count])) {
            omc_arena_fini(&meta_payload);
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        if (meta_children[meta_child_count].type
            == omc_exif_write_fourcc('i', 'i', 'n', 'f')) {
            if (have_iinf) {
                omc_arena_fini(&meta_payload);
                out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            iinf_box = meta_children[meta_child_count];
            have_iinf = 1;
        } else if (meta_children[meta_child_count].type
                   == omc_exif_write_fourcc('i', 'l', 'o', 'c')) {
            if (have_iloc) {
                omc_arena_fini(&meta_payload);
                out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            iloc_box = meta_children[meta_child_count];
            have_iloc = 1;
        } else if (meta_children[meta_child_count].type
                   == omc_exif_write_fourcc('i', 'd', 'a', 't')) {
            if (have_idat) {
                omc_arena_fini(&meta_payload);
                out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            idat_box = meta_children[meta_child_count];
            have_idat = 1;
        }
        child_off += meta_children[meta_child_count].size;
        meta_child_count += 1U;
    }
    if (child_off != child_end || !have_iinf || !have_iloc || !have_idat) {
        omc_arena_fini(&meta_payload);
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    parse_status = omc_exif_write_bmff_parse_iinf(file_bytes, file_size,
                                                  &iinf_box, items, 64U,
                                                  &item_count);
    if (parse_status != OMC_EXIF_WRITE_OK) {
        omc_arena_fini(&meta_payload);
        out_res->status = parse_status;
        return OMC_STATUS_OK;
    }

    if (iinf_box.offset + iinf_box.header_size + 4U
        > iinf_box.offset + iinf_box.size) {
        omc_arena_fini(&meta_payload);
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    memcpy(iinf_fullbox,
           file_bytes + (omc_size)(iinf_box.offset + iinf_box.header_size),
           sizeof(iinf_fullbox));

    parse_status = omc_exif_write_bmff_parse_iloc(
        file_bytes, file_size, &iloc_box, &idat_box, items, item_count,
        &iloc_version, &offset_size, &length_size, &base_offset_size,
        &index_size);
    if (parse_status != OMC_EXIF_WRITE_OK) {
        omc_arena_fini(&meta_payload);
        out_res->status = parse_status;
        return OMC_STATUS_OK;
    }
    memcpy(iloc_fullbox,
           file_bytes + (omc_size)(iloc_box.offset + iloc_box.header_size),
           sizeof(iloc_fullbox));

    removed_count = 0U;
    max_item_id = 0U;
    exif_item_id = 0U;
    exif_new_off = 0U;
    exif_new_len = (omc_u64)payload_size;
    order_count = 0U;
    for (i = 0U; i < item_count; ++i) {
        if (!items[i].has_source) {
            omc_arena_fini(&meta_payload);
            out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
            return OMC_STATUS_OK;
        }
        if (items[i].item_id > max_item_id) {
            max_item_id = items[i].item_id;
        }
        if (items[i].is_exif) {
            removed_count += 1U;
            if (exif_item_id == 0U) {
                exif_item_id = items[i].item_id;
                order[order_count] = 0xFFFFFFFFU;
                order_count += 1U;
            }
        } else {
            order[order_count] = i;
            order_count += 1U;
        }
    }
    if (exif_item_id == 0U) {
        if (max_item_id == 0xFFFFFFFFU || order_count >= 64U) {
            omc_arena_fini(&meta_payload);
            out_res->status = OMC_EXIF_WRITE_LIMIT;
            return OMC_STATUS_OK;
        }
        exif_item_id = max_item_id + 1U;
        order[order_count] = 0xFFFFFFFFU;
        order_count += 1U;
    }
    if (iloc_version < 2U && exif_item_id > 0xFFFFU) {
        omc_arena_fini(&meta_payload);
        out_res->status = OMC_EXIF_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    omc_arena_init(&iinf_payload);
    omc_arena_init(&iloc_payload);
    omc_arena_init(&idat_payload);
    status = OMC_STATUS_OK;

    for (i = 0U; i < order_count; ++i) {
        if (order[i] == 0xFFFFFFFFU) {
            if (!omc_exif_write_bmff_value_fits((omc_u64)idat_payload.size,
                                                offset_size)
                || !omc_exif_write_bmff_value_fits((omc_u64)payload_size,
                                                   length_size)) {
                out_res->status = OMC_EXIF_WRITE_LIMIT;
                status = OMC_STATUS_OK;
                goto cleanup_bmff;
            }
            exif_new_off = (omc_u64)idat_payload.size;
            status = omc_exif_write_append(&idat_payload, payload, payload_size);
            if (status != OMC_STATUS_OK) {
                goto cleanup_bmff;
            }
        } else {
            omc_exif_write_bmff_item* item;

            item = &items[order[i]];
            if (!omc_exif_write_bmff_value_fits((omc_u64)idat_payload.size,
                                                offset_size)
                || !omc_exif_write_bmff_value_fits(item->source_len,
                                                   length_size)) {
                out_res->status = OMC_EXIF_WRITE_LIMIT;
                status = OMC_STATUS_OK;
                goto cleanup_bmff;
            }
            item->new_off = (omc_u64)idat_payload.size;
            item->new_len = item->source_len;
            status = omc_exif_write_append(
                &idat_payload, file_bytes + (omc_size)item->source_off,
                (omc_size)item->source_len);
            if (status != OMC_STATUS_OK) {
                goto cleanup_bmff;
            }
        }
    }

    status = omc_exif_write_append(&iinf_payload, iinf_fullbox,
                                   sizeof(iinf_fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup_bmff;
    }
    if (iinf_fullbox[0] == 0U) {
        status = omc_exif_write_append_be_n(&iinf_payload, (omc_u64)order_count,
                                            2U);
    } else {
        status = omc_exif_write_append_be_n(&iinf_payload, (omc_u64)order_count,
                                            4U);
    }
    if (status != OMC_STATUS_OK) {
        goto cleanup_bmff;
    }
    for (i = 0U; i < order_count; ++i) {
        if (order[i] == 0xFFFFFFFFU) {
            status = omc_exif_write_bmff_append_exif_infe(&iinf_payload,
                                                          exif_item_id);
        } else {
            status = omc_exif_write_append(&iinf_payload,
                                           file_bytes + items[order[i]].infe_off,
                                           items[order[i]].infe_size);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
    }

    status = omc_exif_write_append(&iloc_payload, iloc_fullbox,
                                   sizeof(iloc_fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup_bmff;
    }
    {
        omc_u8 size_bytes[2];

        size_bytes[0] = (omc_u8)((offset_size << 4) | length_size);
        size_bytes[1] = (omc_u8)((base_offset_size << 4) | index_size);
        status = omc_exif_write_append(&iloc_payload, size_bytes,
                                       sizeof(size_bytes));
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
    }
    if (iloc_version < 2U) {
        status = omc_exif_write_append_be_n(&iloc_payload, (omc_u64)order_count,
                                            2U);
    } else {
        status = omc_exif_write_append_be_n(&iloc_payload, (omc_u64)order_count,
                                            4U);
    }
    if (status != OMC_STATUS_OK) {
        goto cleanup_bmff;
    }

    for (i = 0U; i < order_count; ++i) {
        omc_u32 item_id;
        omc_u64 item_off;
        omc_u64 item_len;

        if (order[i] == 0xFFFFFFFFU) {
            item_id = exif_item_id;
            item_off = exif_new_off;
            item_len = exif_new_len;
        } else {
            item_id = items[order[i]].item_id;
            item_off = items[order[i]].new_off;
            item_len = items[order[i]].new_len;
        }
        if (!omc_exif_write_bmff_value_fits(item_off, offset_size)
            || !omc_exif_write_bmff_value_fits(item_len, length_size)
            || !omc_exif_write_bmff_value_fits(0U, base_offset_size)
            || !omc_exif_write_bmff_value_fits(0U, index_size)) {
            out_res->status = OMC_EXIF_WRITE_LIMIT;
            status = OMC_STATUS_OK;
            goto cleanup_bmff;
        }

        if (iloc_version < 2U) {
            status = omc_exif_write_append_be_n(&iloc_payload, (omc_u64)item_id,
                                                2U);
        } else {
            status = omc_exif_write_append_be_n(&iloc_payload, (omc_u64)item_id,
                                                4U);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
        status = omc_exif_write_append_be_n(&iloc_payload, 1U, 2U);
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
        status = omc_exif_write_append_be_n(&iloc_payload, 0U, 2U);
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
        status = omc_exif_write_append_be_n(&iloc_payload, 0U,
                                            base_offset_size);
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
        status = omc_exif_write_append_be_n(&iloc_payload, 1U, 2U);
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
        if (index_size != 0U) {
            status = omc_exif_write_append_be_n(&iloc_payload, 0U, index_size);
            if (status != OMC_STATUS_OK) {
                goto cleanup_bmff;
            }
        }
        status = omc_exif_write_append_be_n(&iloc_payload, item_off,
                                            offset_size);
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
        status = omc_exif_write_append_be_n(&iloc_payload, item_len,
                                            length_size);
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
    }

    status = omc_exif_write_append(&meta_payload, meta_fullbox,
                                   sizeof(meta_fullbox));
    if (status != OMC_STATUS_OK) {
        goto cleanup_bmff;
    }
    emitted_iinf = 0;
    emitted_iloc = 0;
    emitted_idat = 0;
    for (i = 0U; i < meta_child_count; ++i) {
        if (meta_children[i].type == omc_exif_write_fourcc('i', 'i', 'n', 'f')) {
            status = omc_exif_write_append_bmff_box(
                &meta_payload, omc_exif_write_fourcc('i', 'i', 'n', 'f'),
                iinf_payload.data, iinf_payload.size);
            emitted_iinf = 1;
        } else if (meta_children[i].type
                   == omc_exif_write_fourcc('i', 'l', 'o', 'c')) {
            status = omc_exif_write_append_bmff_box(
                &meta_payload, omc_exif_write_fourcc('i', 'l', 'o', 'c'),
                iloc_payload.data, iloc_payload.size);
            emitted_iloc = 1;
        } else if (meta_children[i].type
                   == omc_exif_write_fourcc('i', 'd', 'a', 't')) {
            status = omc_exif_write_append_bmff_box(
                &meta_payload, omc_exif_write_fourcc('i', 'd', 'a', 't'),
                idat_payload.data, idat_payload.size);
            emitted_idat = 1;
        } else {
            status = omc_exif_write_append(
                &meta_payload, file_bytes + (omc_size)meta_children[i].offset,
                (omc_size)meta_children[i].size);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
    }
    if (!emitted_iinf || !emitted_iloc || !emitted_idat) {
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        status = OMC_STATUS_OK;
        goto cleanup_bmff;
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, file_size + payload_size + 512U);
    if (status != OMC_STATUS_OK) {
        goto cleanup_bmff;
    }
    for (i = 0U; i < top_count; ++i) {
        if (top_boxes[i].type == omc_exif_write_fourcc('m', 'e', 't', 'a')) {
            status = omc_exif_write_append_bmff_box(
                out, omc_exif_write_fourcc('m', 'e', 't', 'a'),
                meta_payload.data, meta_payload.size);
        } else {
            status = omc_exif_write_append(
                out, file_bytes + (omc_size)top_boxes[i].offset,
                (omc_size)top_boxes[i].size);
        }
        if (status != OMC_STATUS_OK) {
            goto cleanup_bmff;
        }
    }

    out_res->status = OMC_EXIF_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
    out_res->removed_exif_blocks = removed_count;
    out_res->inserted_exif_blocks = 1U;

cleanup_bmff:
    omc_arena_fini(&meta_payload);
    omc_arena_fini(&idat_payload);
    omc_arena_fini(&iloc_payload);
    omc_arena_fini(&iinf_payload);
    if (status == OMC_STATUS_OK && out_res->status != OMC_EXIF_WRITE_OK) {
        omc_arena_reset(out);
        out_res->written = 0U;
        out_res->needed = 0U;
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
        }
    }
    return count;
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
omc_exif_write_emit_tiff_payload(const omc_exif_write_fields* fields,
                                 omc_arena* out)
{
    omc_u32 ifd0_count;
    omc_u32 ifd0_table_bytes;
    omc_u32 data_off;
    omc_u32 make_off;
    omc_u32 exif_ifd_off;
    omc_u32 dto_off;
    omc_u32 total_size;
    omc_status status;
    omc_u8 header[8];
    omc_u8 count_bytes[2];
    omc_u8 next_ifd[4];
    omc_u8 entry[12];
    omc_u8 zero;
    omc_u32 dto_count;
    omc_u32 make_count;

    if (fields == (const omc_exif_write_fields*)0 || out == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    ifd0_count = 0U;
    if (fields->make.present) {
        ifd0_count += 1U;
    }
    if (fields->date_time_original.present) {
        ifd0_count += 1U;
    }
    if (ifd0_count == 0U) {
        omc_arena_reset(out);
        return OMC_STATUS_OK;
    }

    ifd0_table_bytes = 2U + ifd0_count * 12U + 4U;
    data_off = 8U + ifd0_table_bytes;
    make_off = 0U;
    exif_ifd_off = 0U;
    dto_off = 0U;

    if (fields->make.present) {
        make_count = (omc_u32)fields->make.size + 1U;
        if (data_off > 0xFFFFFFFFU - make_count) {
            return OMC_STATUS_OVERFLOW;
        }
        make_off = data_off;
        data_off += make_count;
    }

    if (fields->date_time_original.present) {
        dto_count = (omc_u32)fields->date_time_original.size + 1U;
        if (data_off > 0xFFFFFFFFU - (2U + 12U + 4U)) {
            return OMC_STATUS_OVERFLOW;
        }
        exif_ifd_off = data_off;
        data_off += 2U + 12U + 4U;
        if (data_off > 0xFFFFFFFFU - dto_count) {
            return OMC_STATUS_OVERFLOW;
        }
        dto_off = data_off;
        data_off += dto_count;
    }

    total_size = data_off;
    omc_arena_reset(out);
    status = omc_arena_reserve(out, total_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    header[0] = 'I';
    header[1] = 'I';
    omc_exif_write_store_u16le(header + 2U, 42U);
    omc_exif_write_store_u32le(header + 4U, 8U);
    status = omc_exif_write_append(out, header, sizeof(header));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    omc_exif_write_store_u16le(count_bytes, (omc_u16)ifd0_count);
    status = omc_exif_write_append(out, count_bytes, sizeof(count_bytes));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    if (fields->make.present) {
        memset(entry, 0, sizeof(entry));
        omc_exif_write_store_u16le(entry + 0U, 0x010FU);
        omc_exif_write_store_u16le(entry + 2U, 2U);
        omc_exif_write_store_u32le(entry + 4U,
                                   (omc_u32)fields->make.size + 1U);
        omc_exif_write_store_u32le(entry + 8U, make_off);
        status = omc_exif_write_append(out, entry, sizeof(entry));
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if (fields->date_time_original.present) {
        memset(entry, 0, sizeof(entry));
        omc_exif_write_store_u16le(entry + 0U, 0x8769U);
        omc_exif_write_store_u16le(entry + 2U, 4U);
        omc_exif_write_store_u32le(entry + 4U, 1U);
        omc_exif_write_store_u32le(entry + 8U, exif_ifd_off);
        status = omc_exif_write_append(out, entry, sizeof(entry));
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    memset(next_ifd, 0, sizeof(next_ifd));
    status = omc_exif_write_append(out, next_ifd, sizeof(next_ifd));
    if (status != OMC_STATUS_OK) {
        return status;
    }

    zero = 0U;
    if (fields->make.present) {
        status = omc_exif_write_append(out, fields->make.data, fields->make.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_exif_write_append(out, &zero, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (fields->date_time_original.present) {
        omc_u8 exif_count[2];

        omc_exif_write_store_u16le(exif_count, 1U);
        status = omc_exif_write_append(out, exif_count, sizeof(exif_count));
        if (status != OMC_STATUS_OK) {
            return status;
        }

        memset(entry, 0, sizeof(entry));
        omc_exif_write_store_u16le(entry + 0U, 0x9003U);
        omc_exif_write_store_u16le(entry + 2U, 2U);
        omc_exif_write_store_u32le(
            entry + 4U, (omc_u32)fields->date_time_original.size + 1U);
        omc_exif_write_store_u32le(entry + 8U, dto_off);
        status = omc_exif_write_append(out, entry, sizeof(entry));
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_exif_write_append(out, next_ifd, sizeof(next_ifd));
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_exif_write_append(out, fields->date_time_original.data,
                                       fields->date_time_original.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        status = omc_exif_write_append(out, &zero, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    return OMC_STATUS_OK;
}

static omc_status
omc_exif_write_rewrite_tiff(const omc_u8* file_bytes, omc_size file_size,
                            const omc_exif_write_fields* fields, omc_arena* out,
                            omc_exif_write_res* out_res)
{
    int big_tiff;
    omc_u16 magic;
    omc_u64 ifd0_off;
    omc_u64 count_u64;
    omc_size count_size;
    omc_size entry_size;
    omc_size next_size;
    omc_size inline_size;
    omc_size align;
    omc_size entries_off;
    omc_size next_ifd_off;
    omc_size i;
    omc_u32 preserved_count;
    omc_u32 new_count_u32;
    omc_size new_ifd_offset;
    omc_size entry_table_size;
    omc_size payload_cursor;
    omc_size make_data_off;
    omc_size exif_ifd_off;
    omc_size dto_data_off;
    omc_size exif_ifd_size;
    omc_u32 make_count;
    omc_u32 dto_count;
    omc_u8 entry_buf[20];
    omc_u8* sorted_entries;
    omc_status status;
    omc_u8 zero;

    if (file_bytes == (const omc_u8*)0 || fields == (const omc_exif_write_fields*)0
        || out == (omc_arena*)0 || out_res == (omc_exif_write_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (file_size < 8U) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    if (!(file_bytes[0] == 'I' && file_bytes[1] == 'I')) {
        if (file_bytes[0] == 'M' && file_bytes[1] == 'M') {
            out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        } else {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
        }
        return OMC_STATUS_OK;
    }

    magic = omc_exif_write_read_u16le(file_bytes + 2U);
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
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        off_size = omc_exif_write_read_u16le(file_bytes + 4U);
        reserved = omc_exif_write_read_u16le(file_bytes + 6U);
        if (off_size != 8U || reserved != 0U) {
            out_res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        ifd0_off = omc_exif_write_read_u64le(file_bytes + 8U);
    } else if (magic == 42U) {
        big_tiff = 0;
        count_size = 2U;
        entry_size = 12U;
        next_size = 4U;
        inline_size = 4U;
        align = 2U;
        ifd0_off = (omc_u64)omc_exif_write_read_u32le(file_bytes + 4U);
    } else {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (ifd0_off >= (omc_u64)file_size || ifd0_off + count_size > (omc_u64)file_size) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    if (big_tiff) {
        count_u64 = omc_exif_write_read_u64le(file_bytes + (omc_size)ifd0_off);
        entries_off = (omc_size)ifd0_off + 8U;
    } else {
        count_u64 = (omc_u64)omc_exif_write_read_u16le(file_bytes + (omc_size)ifd0_off);
        entries_off = (omc_size)ifd0_off + 2U;
    }
    if (count_u64 > 0xFFFFU
        || count_u64 * entry_size > (omc_u64)file_size
        || (omc_u64)entries_off + count_u64 * entry_size + next_size
               > (omc_u64)file_size) {
        out_res->status = OMC_EXIF_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    preserved_count = 0U;
    for (i = 0U; i < (omc_size)count_u64; ++i) {
        const omc_u8* entry;
        omc_u16 tag;

        entry = file_bytes + entries_off + i * entry_size;
        tag = omc_exif_write_read_u16le(entry);
        if (tag == 0x010FU || tag == 0x8769U) {
            continue;
        }
        preserved_count += 1U;
    }

    new_count_u32 = preserved_count;
    if (fields->make.present) {
        new_count_u32 += 1U;
    }
    if (fields->date_time_original.present) {
        new_count_u32 += 1U;
    }
    if (new_count_u32 > 0xFFFFU) {
        out_res->status = OMC_EXIF_WRITE_LIMIT;
        return OMC_STATUS_OK;
    }

    new_ifd_offset = omc_exif_write_align_up(file_size, align);
    entry_table_size = (omc_size)new_count_u32 * entry_size;
    payload_cursor = new_ifd_offset + count_size + entry_table_size + next_size;
    make_data_off = 0U;
    exif_ifd_off = 0U;
    dto_data_off = 0U;
    exif_ifd_size = count_size + entry_size + next_size;
    make_count = fields->make.present ? (omc_u32)fields->make.size + 1U : 0U;
    dto_count = fields->date_time_original.present
                    ? (omc_u32)fields->date_time_original.size + 1U
                    : 0U;

    if (fields->make.present && make_count > inline_size) {
        make_data_off = payload_cursor;
        payload_cursor += make_count;
    }
    if (fields->date_time_original.present) {
        payload_cursor = omc_exif_write_align_up(payload_cursor, align);
        exif_ifd_off = payload_cursor;
        payload_cursor += exif_ifd_size;
        if (dto_count > inline_size) {
            dto_data_off = payload_cursor;
            payload_cursor += dto_count;
        }
    }

    omc_arena_reset(out);
    status = omc_arena_reserve(out, payload_cursor);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    status = omc_exif_write_append(out, file_bytes, file_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    while (out->size < new_ifd_offset) {
        zero = 0U;
        status = omc_exif_write_append(out, &zero, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (big_tiff) {
        omc_exif_write_store_u64le(out->data + 8U, (omc_u64)new_ifd_offset);
    } else {
        omc_exif_write_store_u32le(out->data + 4U, (omc_u32)new_ifd_offset);
    }

    memset(entry_buf, 0, sizeof(entry_buf));
    omc_exif_write_tiff_store_count(entry_buf, big_tiff, (omc_u64)new_count_u32);
    status = omc_exif_write_append(out, entry_buf, count_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    sorted_entries = out->data + out->size;
    for (i = 0U; i < (omc_size)count_u64; ++i) {
        const omc_u8* entry;
        omc_u16 tag;

        entry = file_bytes + entries_off + i * entry_size;
        tag = omc_exif_write_read_u16le(entry);
        if (tag == 0x010FU || tag == 0x8769U) {
            continue;
        }
        status = omc_exif_write_append(out, entry, entry_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (fields->make.present) {
        memset(entry_buf, 0, sizeof(entry_buf));
        omc_exif_write_store_u16le(entry_buf + 0U, 0x010FU);
        omc_exif_write_store_u16le(entry_buf + 2U, 2U);
        if (big_tiff) {
            omc_exif_write_store_u64le(entry_buf + 4U, (omc_u64)make_count);
            if (make_count > inline_size) {
                omc_exif_write_store_u64le(entry_buf + 12U,
                                           (omc_u64)make_data_off);
            } else {
                memcpy(entry_buf + 12U, fields->make.data, fields->make.size);
                entry_buf[12U + fields->make.size] = 0U;
            }
        } else {
            omc_exif_write_store_u32le(entry_buf + 4U, make_count);
            if (make_count > inline_size) {
                omc_exif_write_store_u32le(entry_buf + 8U, (omc_u32)make_data_off);
            } else {
                memcpy(entry_buf + 8U, fields->make.data, fields->make.size);
                entry_buf[8U + fields->make.size] = 0U;
            }
        }
        status = omc_exif_write_append(out, entry_buf, entry_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    if (fields->date_time_original.present) {
        memset(entry_buf, 0, sizeof(entry_buf));
        omc_exif_write_store_u16le(entry_buf + 0U, 0x8769U);
        omc_exif_write_store_u16le(entry_buf + 2U, big_tiff ? 13U : 4U);
        if (big_tiff) {
            omc_exif_write_store_u64le(entry_buf + 4U, 1U);
            omc_exif_write_store_u64le(entry_buf + 12U, (omc_u64)exif_ifd_off);
        } else {
            omc_exif_write_store_u32le(entry_buf + 4U, 1U);
            omc_exif_write_store_u32le(entry_buf + 8U, (omc_u32)exif_ifd_off);
        }
        status = omc_exif_write_append(out, entry_buf, entry_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    omc_exif_write_tiff_insertion_sort_le(sorted_entries, (omc_size)new_count_u32,
                                          entry_size);

    next_ifd_off = entries_off + (omc_size)count_u64 * entry_size;
    status = omc_exif_write_append(out, file_bytes + next_ifd_off, next_size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    if (fields->make.present && make_count > inline_size) {
        while (out->size < make_data_off) {
            zero = 0U;
            status = omc_exif_write_append(out, &zero, 1U);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        status = omc_exif_write_append(out, fields->make.data, fields->make.size);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        zero = 0U;
        status = omc_exif_write_append(out, &zero, 1U);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (fields->date_time_original.present) {
        while (out->size < exif_ifd_off) {
            zero = 0U;
            status = omc_exif_write_append(out, &zero, 1U);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
        memset(entry_buf, 0, sizeof(entry_buf));
        omc_exif_write_tiff_store_count(entry_buf, big_tiff, 1U);
        status = omc_exif_write_append(out, entry_buf, count_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        memset(entry_buf, 0, sizeof(entry_buf));
        omc_exif_write_store_u16le(entry_buf + 0U, 0x9003U);
        omc_exif_write_store_u16le(entry_buf + 2U, 2U);
        if (big_tiff) {
            omc_exif_write_store_u64le(entry_buf + 4U, (omc_u64)dto_count);
            if (dto_count > inline_size) {
                omc_exif_write_store_u64le(entry_buf + 12U,
                                           (omc_u64)dto_data_off);
            } else {
                memcpy(entry_buf + 12U, fields->date_time_original.data,
                       fields->date_time_original.size);
                entry_buf[12U + fields->date_time_original.size] = 0U;
            }
        } else {
            omc_exif_write_store_u32le(entry_buf + 4U, dto_count);
            if (dto_count > inline_size) {
                omc_exif_write_store_u32le(entry_buf + 8U, (omc_u32)dto_data_off);
            } else {
                memcpy(entry_buf + 8U, fields->date_time_original.data,
                       fields->date_time_original.size);
                entry_buf[8U + fields->date_time_original.size] = 0U;
            }
        }
        status = omc_exif_write_append(out, entry_buf, entry_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        memset(entry_buf, 0, sizeof(entry_buf));
        status = omc_exif_write_append(out, entry_buf, next_size);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        if (dto_count > inline_size) {
            while (out->size < dto_data_off) {
                zero = 0U;
                status = omc_exif_write_append(out, &zero, 1U);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
            }
            status = omc_exif_write_append(out, fields->date_time_original.data,
                                           fields->date_time_original.size);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            zero = 0U;
            status = omc_exif_write_append(out, &zero, 1U);
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
    }

    out_res->status = OMC_EXIF_WRITE_OK;
    out_res->needed = out->size;
    out_res->written = out->size;
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
    omc_exif_write_text value;

    if (store == (const omc_store*)0) {
        return 0;
    }
    if (omc_exif_write_find_text_tag(store, "ifd0", 0x010FU, &value)) {
        return 1;
    }
    if (omc_exif_write_find_text_tag(store, "exififd", 0x9003U, &value)) {
        return 1;
    }
    return 0;
}

omc_status
omc_exif_write_embedded(const omc_u8* file_bytes, omc_size file_size,
                        const omc_store* source_store, omc_arena* out,
                        omc_scan_fmt format, omc_exif_write_res* out_res)
{
    omc_store current_store;
    omc_arena tiff_payload;
    omc_arena container_payload;
    omc_exif_write_fields fields;
    omc_status status;

    if (file_bytes == (const omc_u8*)0 || source_store == (const omc_store*)0
        || out == (omc_arena*)0 || out_res == (omc_exif_write_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_exif_write_res_init(out_res);
    out_res->format = format;
    omc_arena_reset(out);

    if (!omc_exif_write_store_has_supported_tags(source_store)) {
        return omc_exif_write_copy_original(file_bytes, file_size, out, out_res);
    }

    if (format == OMC_SCAN_FMT_UNKNOWN) {
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }
    if (format != OMC_SCAN_FMT_JPEG && format != OMC_SCAN_FMT_PNG
        && format != OMC_SCAN_FMT_TIFF && format != OMC_SCAN_FMT_WEBP
        && format != OMC_SCAN_FMT_JP2
        && format != OMC_SCAN_FMT_JXL && format != OMC_SCAN_FMT_HEIF
        && format != OMC_SCAN_FMT_AVIF) {
        out_res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }

    status = omc_exif_write_read_current_store(file_bytes, file_size,
                                               &current_store);
    if (status != OMC_STATUS_OK) {
        out_res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }

    omc_arena_init(&tiff_payload);
    omc_arena_init(&container_payload);
    status = OMC_STATUS_OK;
    if (!omc_exif_write_select_fields(source_store, &current_store, &fields)) {
        status = omc_exif_write_copy_original(file_bytes, file_size, out, out_res);
        goto cleanup;
    }

    status = omc_exif_write_emit_tiff_payload(&fields, &tiff_payload);
    if (status != OMC_STATUS_OK) {
        goto cleanup;
    }
    if (tiff_payload.size == 0U) {
        status = omc_exif_write_copy_original(file_bytes, file_size, out, out_res);
        goto cleanup;
    }

    if (format == OMC_SCAN_FMT_TIFF) {
        status = omc_exif_write_rewrite_tiff(file_bytes, file_size, &fields, out,
                                             out_res);
        goto cleanup;
    }

    if (format == OMC_SCAN_FMT_JPEG || format == OMC_SCAN_FMT_WEBP) {
        status = omc_exif_write_build_jpeg_payload(&tiff_payload,
                                                   &container_payload);
        if (status != OMC_STATUS_OK) {
            goto cleanup;
        }
    } else if (format == OMC_SCAN_FMT_PNG) {
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
    } else if (format == OMC_SCAN_FMT_HEIF || format == OMC_SCAN_FMT_AVIF) {
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
    } else if (format == OMC_SCAN_FMT_HEIF || format == OMC_SCAN_FMT_AVIF) {
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
    omc_arena_fini(&container_payload);
    omc_arena_fini(&tiff_payload);
    omc_store_fini(&current_store);
    return status;
}
