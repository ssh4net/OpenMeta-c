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
