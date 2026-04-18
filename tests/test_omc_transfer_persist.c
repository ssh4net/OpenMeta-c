#include "omc/omc_read.h"
#include "omc/omc_transfer.h"
#include "omc/omc_transfer_persist.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const omc_u8 k_png_sig[8] = {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
};

static omc_byte_ref
append_store_bytes(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, strlen(text), &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static omc_byte_ref
append_store_raw(omc_arena* arena, const void* src, omc_size size)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, src, size, &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_u16be(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    out[*io_size + 0U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 1U] = (omc_u8)(value & 0xFFU);
    *io_size += 2U;
}

static void
append_u16le(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    out[*io_size + 0U] = (omc_u8)(value & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    *io_size += 2U;
}

static void
append_u32be(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    out[*io_size + 0U] = (omc_u8)((value >> 24) & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)(value & 0xFFU);
    *io_size += 4U;
}

static void
append_u32le(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    out[*io_size + 0U] = (omc_u8)(value & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)((value >> 24) & 0xFFU);
    *io_size += 4U;
}

static void
append_u64le(omc_u8* out, omc_size* io_size, omc_u64 value)
{
    out[*io_size + 0U] = (omc_u8)(value & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)((value >> 24) & 0xFFU);
    out[*io_size + 4U] = (omc_u8)((value >> 32) & 0xFFU);
    out[*io_size + 5U] = (omc_u8)((value >> 40) & 0xFFU);
    out[*io_size + 6U] = (omc_u8)((value >> 48) & 0xFFU);
    out[*io_size + 7U] = (omc_u8)((value >> 56) & 0xFFU);
    *io_size += 8U;
}

static void
append_raw(omc_u8* out, omc_size* io_size, const void* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
write_u32le_at(omc_u8* out, omc_size off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)(value & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 24) & 0xFFU);
}

static void
append_text(omc_u8* out, omc_size* io_size, const char* text)
{
    append_raw(out, io_size, text, strlen(text));
}

static void
append_jpeg_segment(omc_u8* out, omc_size* io_size, omc_u8 marker,
                    const omc_u8* payload, omc_size payload_size)
{
    append_u8(out, io_size, 0xFFU);
    append_u8(out, io_size, marker);
    append_u16be(out, io_size, (omc_u16)(payload_size + 2U));
    append_raw(out, io_size, payload, payload_size);
}

static void
append_png_chunk(omc_u8* out, omc_size* io_size, const char* type,
                 const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)payload_size);
    append_raw(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_raw(out, io_size, payload, payload_size);
    }
    append_u32be(out, io_size, 0U);
}

static void
append_webp_chunk(omc_u8* out, omc_size* io_size, const char* type,
                  const omc_u8* payload, omc_size payload_size)
{
    append_text(out, io_size, type);
    append_u32le(out, io_size, (omc_u32)payload_size);
    if (payload_size != 0U) {
        append_raw(out, io_size, payload, payload_size);
    }
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static omc_u32
fourcc(char a, char b, char c, char d)
{
    return ((omc_u32)(omc_u8)a << 24) | ((omc_u32)(omc_u8)b << 16)
           | ((omc_u32)(omc_u8)c << 8) | (omc_u32)(omc_u8)d;
}

static void
append_bmff_box(omc_u8* out, omc_size* io_size, omc_u32 type,
                const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)(payload_size + 8U));
    append_u32be(out, io_size, type);
    if (payload_size != 0U) {
        append_raw(out, io_size, payload, payload_size);
    }
}

static void
append_fullbox_header(omc_u8* out, omc_size* io_size, omc_u8 version)
{
    append_u8(out, io_size, version);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
}

static const omc_entry*
find_xmp_entry(const omc_store* store, const char* schema_ns,
               const char* property_path)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ns_view;
        omc_const_bytes path_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        ns_view = omc_arena_view(&store->arena,
                                 entry->key.u.xmp_property.schema_ns);
        path_view = omc_arena_view(&store->arena,
                                   entry->key.u.xmp_property.property_path);
        if (ns_view.size == strlen(schema_ns)
            && path_view.size == strlen(property_path)
            && memcmp(ns_view.data, schema_ns, ns_view.size) == 0
            && memcmp(path_view.data, property_path, path_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_png_text_entry(const omc_store* store, const char* keyword,
                    const char* field)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes keyword_view;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_PNG_TEXT) {
            continue;
        }
        keyword_view = omc_arena_view(&store->arena,
                                      entry->key.u.png_text.keyword);
        field_view = omc_arena_view(&store->arena,
                                    entry->key.u.png_text.field);
        if (keyword_view.size == strlen(keyword)
            && field_view.size == strlen(field)
            && memcmp(keyword_view.data, keyword, keyword_view.size) == 0
            && memcmp(field_view.data, field, field_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_exif_entry(const omc_store* store, const char* ifd_name, omc_u16 tag)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        if (entry->key.u.exif_tag.tag != tag) {
            continue;
        }
        ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_view.size == strlen(ifd_name)
            && memcmp(ifd_view.data, ifd_name, ifd_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_comment_entry(const omc_store* store)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind == OMC_KEY_COMMENT) {
            return &store->entries[i];
        }
    }
    return (const omc_entry*)0;
}

static void
assert_text_value(const omc_store* store, const omc_entry* entry,
                  const char* expect)
{
    omc_const_bytes value;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(value.size == strlen(expect));
    assert(memcmp(value.data, expect, value.size) == 0);
}

static void
assert_u8_array_value(const omc_store* store, const omc_entry* entry,
                      const omc_u8* expect, omc_u32 count)
{
    omc_const_bytes value;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.count == count);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(value.size == (omc_size)count);
    assert(memcmp(value.data, expect, value.size) == 0);
}

static void
assert_u8_blob_value(const omc_store* store, const omc_entry* entry,
                     const omc_u8* expect, omc_u32 count)
{
    omc_const_bytes value;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.count == count);
    assert(entry->value.kind == OMC_VAL_ARRAY
           || entry->value.kind == OMC_VAL_BYTES);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(value.size == (omc_size)count);
    assert(memcmp(value.data, expect, value.size) == 0);
}

static void
assert_u8_value(const omc_entry* entry, omc_u8 expect)
{
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert((omc_u8)entry->value.u.u64 == expect);
}

static void
assert_u16_value(const omc_entry* entry, omc_u16 expect)
{
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert((omc_u16)entry->value.u.u64 == expect);
}

static void
assert_urational_scalar_value(const omc_entry* entry, omc_u32 numer,
                              omc_u32 denom)
{
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_URATIONAL);
    assert(entry->value.u.ur.numer == numer);
    assert(entry->value.u.ur.denom == denom);
}

static void
assert_urational_array_value(const omc_store* store, const omc_entry* entry,
                             const omc_urational* expect, omc_u32 count)
{
    omc_const_bytes value;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_URATIONAL);
    assert(entry->value.count == count);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(value.size == (omc_size)count * sizeof(expect[0]));
    assert(memcmp(value.data, expect, value.size) == 0);
}

static void
build_store_with_creator_tool(omc_store* store, const char* tool)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://ns.adobe.com/xap/1.0/"),
        append_store_bytes(&store->arena, "CreatorTool"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, tool),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_creator_tool_and_datetime_original(omc_store* store,
                                                    const char* tool,
                                                    const char* dto)
{
    static const char k_model[] = "EOS R5";
    static const char k_interop_index[] = "R98";
    static const omc_urational k_x_resolution = { 300U, 1U };
    static const omc_urational k_y_resolution = { 300U, 1U };
    static const omc_u8 k_gps_version[4] = { 2U, 3U, 0U, 0U };
    static const char k_gps_lat_ref[] = "N";
    static const omc_urational k_gps_lat[3] = {
        { 41U, 1U }, { 24U, 1U }, { 5000U, 100U }
    };
    static const char k_gps_lon_ref[] = "W";
    static const omc_urational k_gps_lon[3] = {
        { 93U, 1U }, { 27U, 1U }, { 6864624U, 1000000U }
    };
    static const omc_u8 k_gps_alt_ref = 0U;
    static const omc_urational k_gps_altitude = { 350U, 10U };
    static const omc_urational k_gps_timestamp[3] = {
        { 12U, 1U }, { 11U, 1U }, { 13U, 1U }
    };
    static const char k_gps_satellites[] = "7";
    static const char k_gps_status[] = "A";
    static const char k_gps_img_direction_ref[] = "T";
    static const omc_urational k_gps_img_direction = {
        1779626556U, 10000000U
    };
    static const char k_gps_map_datum[] = "WGS-84";
    static const char k_gps_dest_lat_ref[] = "N";
    static const omc_urational k_gps_dest_lat[3] = {
        { 35U, 1U }, { 48U, 1U }, { 8U, 10U }
    };
    static const char k_gps_dest_lon_ref[] = "E";
    static const omc_urational k_gps_dest_lon[3] = {
        { 139U, 1U }, { 34U, 1U }, { 55U, 10U }
    };
    static const char k_gps_dest_bearing_ref[] = "T";
    static const omc_urational k_gps_dest_bearing = { 90U, 1U };
    static const char k_gps_dest_distance_ref[] = "N";
    static const omc_urational k_gps_dest_distance = { 4U, 1U };
    static const char k_gps_measure_mode[] = "3";
    static const omc_urational k_gps_dop = { 16U, 10U };
    static const char k_gps_speed_ref[] = "K";
    static const omc_urational k_gps_speed = { 50U, 1U };
    static const char k_gps_track_ref[] = "T";
    static const omc_urational k_gps_track = { 315U, 1U };
    static const omc_u8 k_gps_processing_method[] = {
        'A', 'S', 'C', 'I', 'I', 0U, 0U, 0U, 'G', 'P', 'S'
    };
    static const omc_u8 k_gps_area_information[] = {
        'A', 'S', 'C', 'I', 'I', 0U, 0U, 0U, 'T', 'o', 'k', 'y', 'o'
    };
    static const omc_u16 k_gps_differential = 1U;
    static const char k_gps_date_stamp[] = "2024:04:19";
    static const omc_urational k_lens_spec[4] = {
        { 24U, 10U }, { 70U, 10U }, { 28U, 10U }, { 40U, 10U }
    };
    omc_entry entry;
    omc_status status;

    build_store_with_creator_tool(store, tool);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "ifd0"),
                          0x0110U);
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, k_model),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "ifd0"),
                          0x011AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_x_resolution;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "ifd0"),
                          0x011BU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_y_resolution;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "ifd0"),
                          0x0128U);
    omc_val_make_u16(&entry.value, 2U);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "ifd0"),
                          0x0132U);
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, dto),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "exififd"),
                          0x9003U);
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, dto),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "exififd"),
                          0x9004U);
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, dto),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "exififd"),
                          0x8827U);
    omc_val_make_u16(&entry.value, 400U);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "exififd"),
                          0x829AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 1U;
    entry.value.u.ur.denom = 125U;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "exififd"),
                          0x829DU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 28U;
    entry.value.u.ur.denom = 10U;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "exififd"),
                          0x920AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 66U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "exififd"),
                          0xA432U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 4U;
    entry.value.u.ref = append_store_raw(&store->arena, k_lens_spec,
                                         (omc_size)sizeof(k_lens_spec));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "interopifd"),
                          0x0001U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_interop_index),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0005U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_U8;
    entry.value.count = 1U;
    entry.value.u.u64 = (omc_u64)k_gps_alt_ref;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0006U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_gps_altitude;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0007U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_store_raw(&store->arena, k_gps_timestamp,
                                         (omc_size)sizeof(k_gps_timestamp));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0008U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_satellites),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0009U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_status),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x000AU);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_measure_mode),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x000BU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_gps_dop;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x000CU);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_speed_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x000DU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_gps_speed;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x000EU);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_track_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x000FU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_gps_track;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0010U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena,
                                         k_gps_img_direction_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0011U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_gps_img_direction;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0012U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_map_datum),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0013U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_dest_lat_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0014U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_store_raw(&store->arena, k_gps_dest_lat,
                                         (omc_size)sizeof(k_gps_dest_lat));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0015U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_dest_lon_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0016U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_store_raw(&store->arena, k_gps_dest_lon,
                                         (omc_size)sizeof(k_gps_dest_lon));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0017U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena,
                                         k_gps_dest_bearing_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0018U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_gps_dest_bearing;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0019U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena,
                                         k_gps_dest_distance_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x001AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur = k_gps_dest_distance;
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x001BU);
    omc_val_make_bytes(
        &entry.value,
        append_store_raw(&store->arena, k_gps_processing_method,
                         (omc_size)sizeof(k_gps_processing_method)));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x001CU);
    omc_val_make_bytes(
        &entry.value,
        append_store_raw(&store->arena, k_gps_area_information,
                         (omc_size)sizeof(k_gps_area_information)));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x001DU);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_date_stamp),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x001EU);
    omc_val_make_u16(&entry.value, k_gps_differential);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0000U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_U8;
    entry.value.count = 4U;
    entry.value.u.ref = append_store_raw(&store->arena, k_gps_version,
                                         (omc_size)sizeof(k_gps_version));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0001U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_lat_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0002U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_store_raw(&store->arena, k_gps_lat,
                                         (omc_size)sizeof(k_gps_lat));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0003U);
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, k_gps_lon_ref),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_bytes(&store->arena, "gpsifd"),
                          0x0004U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_store_raw(&store->arena, k_gps_lon,
                                         (omc_size)sizeof(k_gps_lon));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static omc_size
make_test_jpeg_with_old_xmp_and_comment(omc_u8* out)
{
    static const char jfif[] = { 'J', 'F', 'I', 'F', 0x00U, 0x01U, 0x02U,
                                 0x00U, 0x00U, 0x01U, 0x00U, 0x01U, 0x00U,
                                 0x00U };
    static const char comment[] = "Preserve me";
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 xmp_payload[512];
    omc_size xmp_size;
    omc_size size;

    xmp_size = 0U;
    append_text(xmp_payload, &xmp_size, "http://ns.adobe.com/xap/1.0/");
    append_u8(xmp_payload, &xmp_size, 0U);
    append_text(xmp_payload, &xmp_size, xmp);

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);
    append_jpeg_segment(out, &size, 0xE0U, (const omc_u8*)jfif,
                        sizeof(jfif));
    append_jpeg_segment(out, &size, 0xE1U, xmp_payload, xmp_size);
    append_jpeg_segment(out, &size, 0xFEU, (const omc_u8*)comment,
                        sizeof(comment) - 1U);
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD9U);
    return size;
}

static omc_size
make_test_png_with_old_xmp_and_text(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 ihdr[13];
    omc_u8 xmp_payload[512];
    omc_u8 text_payload[64];
    omc_size xmp_size;
    omc_size text_size;
    omc_size size;

    memset(ihdr, 0, sizeof(ihdr));
    ihdr[3] = 1U;
    ihdr[7] = 1U;
    ihdr[8] = 8U;
    ihdr[9] = 2U;

    xmp_size = 0U;
    append_text(xmp_payload, &xmp_size, "XML:com.adobe.xmp");
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_text(xmp_payload, &xmp_size, xmp);

    text_size = 0U;
    append_text(text_payload, &text_size, "Comment");
    append_u8(text_payload, &text_size, 0U);
    append_text(text_payload, &text_size, "Preserve me");

    size = 0U;
    append_raw(out, &size, k_png_sig, sizeof(k_png_sig));
    append_png_chunk(out, &size, "IHDR", ihdr, sizeof(ihdr));
    append_png_chunk(out, &size, "iTXt", xmp_payload, xmp_size);
    append_png_chunk(out, &size, "tEXt", text_payload, text_size);
    append_png_chunk(out, &size, "IEND", (const omc_u8*)0, 0U);
    return size;
}

static omc_size
make_test_bigtiff_le_with_make_and_old_xmp(omc_u8* out)
{
    static const char make[] = { 'C', 'a', 'n', 'o', 'n', 0U };
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_size size;
    omc_u64 xmp_off;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 43U);
    append_u16le(out, &size, 8U);
    append_u16le(out, &size, 0U);
    append_u64le(out, &size, 16U);
    append_u64le(out, &size, 2U);

    xmp_off = 16U + 8U + 40U + 8U;

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u64le(out, &size, (omc_u64)sizeof(make));
    append_raw(out, &size, make, sizeof(make));
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);

    append_u16le(out, &size, 700U);
    append_u16le(out, &size, 7U);
    append_u64le(out, &size, (omc_u64)(sizeof(xmp) - 1U));
    append_u64le(out, &size, xmp_off);

    append_u64le(out, &size, 0U);
    append_text(out, &size, xmp);
    return size;
}

static omc_size
make_test_tiff_le_with_make_only(omc_u8* out)
{
    static const char make[] = "Canon";
    omc_size size;
    omc_u32 make_off;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);

    make_off = 8U + 2U + 12U + 4U;

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)sizeof(make));
    append_u32le(out, &size, make_off);

    append_u32le(out, &size, 0U);
    append_text(out, &size, make);
    append_u8(out, &size, 0U);
    return size;
}

static omc_size
make_test_dng_with_old_xmp_and_make(omc_u8* out)
{
    static const char make[] = "Canon";
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_size size;
    omc_u32 make_off;
    omc_u32 xmp_off;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 3U);

    make_off = 8U + 2U + 36U + 4U;
    xmp_off = make_off + (omc_u32)sizeof(make);

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)sizeof(make));
    append_u32le(out, &size, make_off);

    append_u16le(out, &size, 0xC612U);
    append_u16le(out, &size, 1U);
    append_u32le(out, &size, 4U);
    append_u8(out, &size, 1U);
    append_u8(out, &size, 6U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);

    append_u16le(out, &size, 700U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, (omc_u32)(sizeof(xmp) - 1U));
    append_u32le(out, &size, xmp_off);

    append_u32le(out, &size, 0U);
    append_text(out, &size, make);
    append_u8(out, &size, 0U);
    append_text(out, &size, xmp);
    return size;
}

static omc_size
make_test_webp_with_old_xmp_and_exif(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[256];
    omc_u8 exif[320];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);
    exif_size = 0U;
    append_text(exif, &exif_size, "Exif");
    append_u8(exif, &exif_size, 0U);
    append_u8(exif, &exif_size, 0U);
    append_raw(exif, &exif_size, tiff, tiff_size);

    size = 0U;
    append_text(out, &size, "RIFF");
    append_u32le(out, &size, 0U);
    append_text(out, &size, "WEBP");
    append_webp_chunk(out, &size, "EXIF", exif, exif_size);
    append_webp_chunk(out, &size, "XMP ", (const omc_u8*)xmp,
                      sizeof(xmp) - 1U);
    write_u32le_at(out, 4U, (omc_u32)(size - 8U));
    return size;
}

static omc_size
make_test_jp2_with_old_xmp_and_exif(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[256];
    omc_u8 exif_payload[320];
    omc_u8 colr_payload[8];
    omc_u8 colr_box[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size colr_size;
    omc_size colr_box_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);
    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    colr_size = 0U;
    append_u8(colr_payload, &colr_size, 1U);
    append_u8(colr_payload, &colr_size, 0U);
    append_u8(colr_payload, &colr_size, 0U);
    append_u8(colr_payload, &colr_size, 0U);
    colr_box_size = 0U;
    append_bmff_box(colr_box, &colr_box_size, fourcc('c', 'o', 'l', 'r'),
                    colr_payload, colr_size);

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('j', 'P', ' ', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('j', 'p', '2', 'h'),
                    colr_box, colr_box_size);
    append_bmff_box(out, &size, fourcc('x', 'm', 'l', ' '),
                    (const omc_u8*)xmp, sizeof(xmp) - 1U);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    return size;
}

static omc_size
make_test_bmff_with_old_xmp_and_exif(omc_u8* out, omc_u32 major_brand)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[128];
    omc_u8 exif_payload[256];
    omc_u8 idat_payload[512];
    omc_u8 infe_exif[64];
    omc_u8 infe_xmp[96];
    omc_u8 iinf_payload[256];
    omc_u8 iloc_payload[160];
    omc_u8 meta_payload[1024];
    omc_u8 moov_box[16];
    omc_u8 ftyp_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size idat_size;
    omc_size exif_off;
    omc_size xmp_off;
    omc_size infe_exif_size;
    omc_size infe_xmp_size;
    omc_size iinf_size;
    omc_size iloc_size;
    omc_size meta_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 6U);
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    idat_size = 0U;
    exif_off = idat_size;
    append_raw(idat_payload, &idat_size, exif_payload, exif_size);
    xmp_off = idat_size;
    append_text(idat_payload, &idat_size, xmp);

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif, &infe_exif_size, 2U);
    append_u16be(infe_exif, &infe_exif_size, 1U);
    append_u16be(infe_exif, &infe_exif_size, 0U);
    append_u32be(infe_exif, &infe_exif_size, fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif, &infe_exif_size, "Exif");
    append_u8(infe_exif, &infe_exif_size, 0U);

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 0U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/rdf+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 0U);
    append_u16be(iinf_payload, &iinf_size, 2U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_exif, infe_exif_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);

    iloc_size = 0U;
    append_fullbox_header(iloc_payload, &iloc_size, 1U);
    append_u8(iloc_payload, &iloc_size, 0x44U);
    append_u8(iloc_payload, &iloc_size, 0x40U);
    append_u16be(iloc_payload, &iloc_size, 2U);

    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_size);

    append_u16be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)xmp_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)(sizeof(xmp) - 1U));

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'l', 'o', 'c'),
                    iloc_payload, iloc_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'd', 'a', 't'),
                    idat_payload, idat_size);

    moov_size = 0U;
    append_bmff_box(moov_box, &moov_size, fourcc('m', 'o', 'o', 'v'),
                    (const omc_u8*)0, 0U);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_raw(out, &size, moov_box, moov_size);
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static omc_size
make_test_heif_with_old_xmp_and_exif(omc_u8* out)
{
    return make_test_bmff_with_old_xmp_and_exif(out,
                                                fourcc('h', 'e', 'i', 'c'));
}

static omc_size
make_test_avif_with_old_xmp_and_exif(omc_u8* out)
{
    return make_test_bmff_with_old_xmp_and_exif(out,
                                                fourcc('a', 'v', 'i', 'f'));
}

static omc_size
make_test_bmff_minimal(omc_u8* out, omc_u32 major_brand)
{
    omc_u8 ftyp_payload[16];
    static const omc_u8 mdat_payload[] = { 0x11U, 0x22U, 0x33U, 0x44U };
    omc_size ftyp_size;
    omc_size size;

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));
    append_u32be(ftyp_payload, &ftyp_size, major_brand);

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'd', 'a', 't'),
                    mdat_payload, sizeof(mdat_payload));
    return size;
}

static omc_size
make_test_heif_minimal(omc_u8* out)
{
    return make_test_bmff_minimal(out, fourcc('h', 'e', 'i', 'c'));
}

static omc_size
make_test_avif_minimal(omc_u8* out)
{
    return make_test_bmff_minimal(out, fourcc('a', 'v', 'i', 'f'));
}

static omc_size
make_test_jxl_with_old_xmp_and_exif(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[128];
    omc_u8 exif_payload[256];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('J', 'X', 'L', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    append_bmff_box(out, &size, fourcc('x', 'm', 'l', ' '),
                    (const omc_u8*)xmp, sizeof(xmp) - 1U);
    return size;
}

static void
read_store_from_bytes(const omc_u8* bytes, omc_size size, omc_store* out)
{
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[2048];
    omc_u32 payload_scratch[16];
    omc_read_res res;

    res = omc_read_simple(bytes, size, out, blocks, 16U, ifds, 8U, payload,
                          sizeof(payload), payload_scratch, 16U,
                          (const omc_read_opts*)0);
    assert(res.entries_added >= 1U);
}

typedef enum omc_transfer_persist_preserve_kind {
    OMC_TRANSFER_PERSIST_PRESERVE_NONE = 0,
    OMC_TRANSFER_PERSIST_PRESERVE_COMMENT = 1,
    OMC_TRANSFER_PERSIST_PRESERVE_PNG_TEXT = 2,
    OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE = 3,
    OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE = 4
} omc_transfer_persist_preserve_kind;

typedef enum omc_transfer_persist_xmp_expect {
    OMC_TRANSFER_PERSIST_XMP_OLD = 0,
    OMC_TRANSFER_PERSIST_XMP_NEW = 1,
    OMC_TRANSFER_PERSIST_XMP_NONE = 2
} omc_transfer_persist_xmp_expect;

typedef omc_size (*omc_transfer_persist_fixture_builder)(omc_u8* out);

static void
assert_persist_preserved_metadata(const omc_store* store,
                                  omc_transfer_persist_preserve_kind kind)
{
    static const omc_u8 k_dng_version[4] = { 1U, 6U, 0U, 0U };

    if (kind == OMC_TRANSFER_PERSIST_PRESERVE_NONE) {
        return;
    }
    if (kind == OMC_TRANSFER_PERSIST_PRESERVE_COMMENT) {
        assert_text_value(store, find_comment_entry(store), "Preserve me");
    } else if (kind == OMC_TRANSFER_PERSIST_PRESERVE_PNG_TEXT) {
        assert_text_value(store, find_png_text_entry(store, "Comment", "text"),
                          "Preserve me");
    } else if (kind == OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE) {
        assert_text_value(store, find_exif_entry(store, "ifd0", 0x010FU),
                          "Canon");
        assert_u8_array_value(store, find_exif_entry(store, "ifd0", 0xC612U),
                              k_dng_version, 4U);
    } else {
        assert_text_value(store, find_exif_entry(store, "ifd0", 0x010FU),
                          "Canon");
    }
}

static void
assert_persist_xmp_state(const omc_store* store,
                         omc_transfer_persist_xmp_expect expect)
{
    const omc_entry* entry;

    entry = find_xmp_entry(store, "http://ns.adobe.com/xap/1.0/",
                           "CreatorTool");
    if (expect == OMC_TRANSFER_PERSIST_XMP_NONE) {
        assert(entry == (const omc_entry*)0);
        return;
    }
    assert(entry != (const omc_entry*)0);
    if (expect == OMC_TRANSFER_PERSIST_XMP_NEW) {
        assert_text_value(store, entry, "NewTool");
    } else {
        assert_text_value(store, entry, "OldTool");
    }
}

static void
assert_persist_datetime_original(const omc_store* store, const char* expect)
{
    static const omc_urational k_x_resolution = { 300U, 1U };
    static const omc_urational k_y_resolution = { 300U, 1U };
    static const omc_u8 k_gps_version[4] = { 2U, 3U, 0U, 0U };
    static const omc_urational k_gps_lat[3] = {
        { 41U, 1U }, { 24U, 1U }, { 5000U, 100U }
    };
    static const omc_urational k_gps_lon[3] = {
        { 93U, 1U }, { 27U, 1U }, { 6864624U, 1000000U }
    };
    static const omc_urational k_gps_altitude = { 350U, 10U };
    static const omc_urational k_gps_timestamp[3] = {
        { 12U, 1U }, { 11U, 1U }, { 13U, 1U }
    };
    static const omc_urational k_gps_img_direction = {
        1779626556U, 10000000U
    };
    static const omc_urational k_gps_dest_lat[3] = {
        { 35U, 1U }, { 48U, 1U }, { 8U, 10U }
    };
    static const omc_urational k_gps_dest_lon[3] = {
        { 139U, 1U }, { 34U, 1U }, { 55U, 10U }
    };
    static const omc_urational k_gps_dest_bearing = { 90U, 1U };
    static const omc_urational k_gps_dest_distance = { 4U, 1U };
    static const omc_u8 k_gps_processing_method[] = {
        'A', 'S', 'C', 'I', 'I', 0U, 0U, 0U, 'G', 'P', 'S'
    };
    static const omc_u8 k_gps_area_information[] = {
        'A', 'S', 'C', 'I', 'I', 0U, 0U, 0U, 'T', 'o', 'k', 'y', 'o'
    };
    static const omc_urational k_lens_spec[4] = {
        { 24U, 10U }, { 70U, 10U }, { 28U, 10U }, { 40U, 10U }
    };

    assert_text_value(store, find_exif_entry(store, "ifd0", 0x0110U),
                      "EOS R5");
    assert_urational_scalar_value(find_exif_entry(store, "ifd0", 0x011AU),
                                  k_x_resolution.numer,
                                  k_x_resolution.denom);
    assert_urational_scalar_value(find_exif_entry(store, "ifd0", 0x011BU),
                                  k_y_resolution.numer,
                                  k_y_resolution.denom);
    assert_u16_value(find_exif_entry(store, "ifd0", 0x0128U), 2U);
    assert_text_value(store, find_exif_entry(store, "ifd0", 0x0132U), expect);
    assert_text_value(store, find_exif_entry(store, "exififd", 0x9003U),
                      expect);
    assert_text_value(store, find_exif_entry(store, "exififd", 0x9004U),
                      expect);
    assert_u16_value(find_exif_entry(store, "exififd", 0x8827U), 400U);
    assert_urational_scalar_value(find_exif_entry(store, "exififd", 0x829AU),
                                  1U, 125U);
    assert_urational_scalar_value(find_exif_entry(store, "exififd", 0x829DU),
                                  28U, 10U);
    assert_urational_scalar_value(find_exif_entry(store, "exififd", 0x920AU),
                                  66U, 1U);
    assert_urational_array_value(store,
                                 find_exif_entry(store, "exififd", 0xA432U),
                                 k_lens_spec, 4U);
    assert_text_value(store, find_exif_entry(store, "interopifd", 0x0001U),
                      "R98");
    assert_u8_array_value(store, find_exif_entry(store, "gpsifd", 0x0000U),
                          k_gps_version, 4U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0001U), "N");
    assert_urational_array_value(store, find_exif_entry(store, "gpsifd", 0x0002U),
                                 k_gps_lat, 3U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0003U), "W");
    assert_urational_array_value(store, find_exif_entry(store, "gpsifd", 0x0004U),
                                 k_gps_lon, 3U);
    assert_u8_value(find_exif_entry(store, "gpsifd", 0x0005U), 0U);
    assert_urational_scalar_value(find_exif_entry(store, "gpsifd", 0x0006U),
                                  k_gps_altitude.numer,
                                  k_gps_altitude.denom);
    assert_urational_array_value(store, find_exif_entry(store, "gpsifd", 0x0007U),
                                 k_gps_timestamp, 3U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0008U), "7");
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0009U), "A");
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x000AU), "3");
    assert_urational_scalar_value(find_exif_entry(store, "gpsifd", 0x000BU),
                                  16U, 10U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x000CU), "K");
    assert_urational_scalar_value(find_exif_entry(store, "gpsifd", 0x000DU),
                                  50U, 1U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x000EU), "T");
    assert_urational_scalar_value(find_exif_entry(store, "gpsifd", 0x000FU),
                                  315U, 1U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0010U), "T");
    assert_urational_scalar_value(find_exif_entry(store, "gpsifd", 0x0011U),
                                  k_gps_img_direction.numer,
                                  k_gps_img_direction.denom);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0012U),
                      "WGS-84");
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0013U), "N");
    assert_urational_array_value(
        store, find_exif_entry(store, "gpsifd", 0x0014U), k_gps_dest_lat, 3U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0015U), "E");
    assert_urational_array_value(
        store, find_exif_entry(store, "gpsifd", 0x0016U), k_gps_dest_lon, 3U);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0017U), "T");
    assert_urational_scalar_value(find_exif_entry(store, "gpsifd", 0x0018U),
                                  k_gps_dest_bearing.numer,
                                  k_gps_dest_bearing.denom);
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x0019U), "N");
    assert_urational_scalar_value(find_exif_entry(store, "gpsifd", 0x001AU),
                                  k_gps_dest_distance.numer,
                                  k_gps_dest_distance.denom);
    assert_u8_blob_value(store, find_exif_entry(store, "gpsifd", 0x001BU),
                         k_gps_processing_method,
                         (omc_u32)sizeof(k_gps_processing_method));
    assert_u8_blob_value(store, find_exif_entry(store, "gpsifd", 0x001CU),
                         k_gps_area_information,
                         (omc_u32)sizeof(k_gps_area_information));
    assert_text_value(store, find_exif_entry(store, "gpsifd", 0x001DU),
                      "2024:04:19");
    assert_u16_value(find_exif_entry(store, "gpsifd", 0x001EU), 1U);
}

static int
read_file_bytes(const char* path, omc_arena* out)
{
    FILE* fp;
    long end_pos;
    omc_size size;

    fp = fopen(path, "rb");
    if (fp == (FILE*)0) {
        return 0;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    end_pos = ftell(fp);
    if (end_pos < 0L) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    size = (omc_size)end_pos;
    if (omc_arena_reserve(out, size) != OMC_STATUS_OK) {
        fclose(fp);
        return 0;
    }
    out->size = size;
    if (size != 0U
        && fread(out->data, 1U, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static void
build_temp_path(char* out, const char* ext)
{
    char temp_buf[L_tmpnam];

    assert(tmpnam(temp_buf) != (char*)0);
    strcpy(out, temp_buf);
    strcat(out, ext);
}

static void
derive_sidecar_path(const char* path, char* out)
{
    const char* dot;
    const char* slash;
    omc_size stem_len;

    slash = strrchr(path, '/');
    dot = strrchr(path, '.');
    if (dot != (const char*)0
        && (slash == (const char*)0 || dot > slash + 0)) {
        stem_len = (omc_size)(dot - path);
    } else {
        stem_len = (omc_size)strlen(path);
    }

    memcpy(out, path, stem_len);
    memcpy(out + stem_len, ".xmp", 5U);
}

static void
execute_transfer(const omc_u8* file_bytes, omc_size file_size,
                 const omc_store* store,
                 const omc_transfer_prepare_opts* opts,
                 omc_arena* edited_out, omc_arena* sidecar_out,
                 omc_transfer_exec* exec, omc_transfer_res* res)
{
    omc_transfer_bundle bundle;
    omc_status status;

    status = omc_transfer_prepare(file_bytes, file_size, store, opts, &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, exec);
    assert(status == OMC_STATUS_OK);
    assert(exec->status == OMC_TRANSFER_OK);

    status = omc_transfer_execute(file_bytes, file_size, store, edited_out,
                                  sidecar_out, exec, res);
    assert(status == OMC_STATUS_OK);
    assert(res->status == OMC_TRANSFER_OK);
}

static void
exercise_transfer_persist_case(
    omc_transfer_persist_fixture_builder builder, const char* output_ext,
    omc_xmp_writeback_mode writeback_mode,
    omc_xmp_destination_embedded_mode destination_mode,
    omc_transfer_persist_preserve_kind preserve_kind,
    omc_transfer_persist_xmp_expect output_xmp_expect,
    int strip_destination_sidecar)
{
    omc_u8 file_bytes[4096];
    omc_size file_size;
    omc_store source_store;
    omc_store output_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 16];
    char sidecar_path[L_tmpnam + 16];
    FILE* fp;
    omc_status status;

    file_size = builder(file_bytes);
    build_temp_path(output_path, output_ext);
    derive_sidecar_path(output_path, sidecar_path);

    if (strip_destination_sidecar) {
        fp = fopen(sidecar_path, "wb");
        assert(fp != (FILE*)0);
        fputs("stale sidecar", fp);
        fclose(fp);
    }

    omc_store_init(&source_store);
    omc_store_init(&output_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = writeback_mode;
    prepare_opts.destination_embedded_mode = destination_mode;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    if (strip_destination_sidecar) {
        persist_opts.destination_sidecar_mode =
            OMC_TRANSFER_DEST_SIDECAR_STRIP_EXISTING;
    }

    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    assert(persist_res.output_status == OMC_TRANSFER_OK);
    if (writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY) {
        assert(persist_res.xmp_sidecar_status == OMC_TRANSFER_OK);
    }
    if (strip_destination_sidecar) {
        assert(persist_res.xmp_sidecar_cleanup_requested);
        assert(persist_res.xmp_sidecar_cleanup_removed);
    }

    assert(read_file_bytes(output_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &output_store);
    assert_persist_xmp_state(&output_store, output_xmp_expect);
    assert_persist_preserved_metadata(&output_store, preserve_kind);

    if (writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY) {
        omc_arena_reset(&file_read);
        assert(read_file_bytes(sidecar_path, &file_read));
        read_store_from_bytes(file_read.data, file_read.size, &sidecar_store);
        assert_text_value(&sidecar_store,
                          find_xmp_entry(&sidecar_store,
                                         "http://ns.adobe.com/xap/1.0/",
                                         "CreatorTool"),
                          "NewTool");
    } else {
        omc_arena_reset(&file_read);
        assert(!read_file_bytes(sidecar_path, &file_read));
    }

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&output_store);
    omc_store_fini(&source_store);
}

static void
exercise_transfer_persist_source_exif_case(
    omc_transfer_persist_fixture_builder builder, const char* output_ext,
    omc_xmp_writeback_mode writeback_mode,
    omc_xmp_destination_embedded_mode destination_mode,
    omc_transfer_persist_preserve_kind preserve_kind,
    omc_transfer_persist_xmp_expect output_xmp_expect,
    int strip_destination_sidecar)
{
    static const char k_dto[] = "2024:01:02 03:04:05";
    omc_u8 file_bytes[4096];
    omc_size file_size;
    omc_store source_store;
    omc_store output_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 16];
    char sidecar_path[L_tmpnam + 16];
    FILE* fp;
    omc_status status;

    file_size = builder(file_bytes);
    build_temp_path(output_path, output_ext);
    derive_sidecar_path(output_path, sidecar_path);

    if (strip_destination_sidecar) {
        fp = fopen(sidecar_path, "wb");
        assert(fp != (FILE*)0);
        fputs("stale sidecar", fp);
        fclose(fp);
    }

    omc_store_init(&source_store);
    omc_store_init(&output_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool_and_datetime_original(
        &source_store, "NewTool", k_dto);

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = writeback_mode;
    prepare_opts.destination_embedded_mode = destination_mode;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    if (strip_destination_sidecar) {
        persist_opts.destination_sidecar_mode =
            OMC_TRANSFER_DEST_SIDECAR_STRIP_EXISTING;
    }

    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    assert(persist_res.output_status == OMC_TRANSFER_OK);
    if (writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY) {
        assert(persist_res.xmp_sidecar_status == OMC_TRANSFER_OK);
    }
    if (strip_destination_sidecar) {
        assert(persist_res.xmp_sidecar_cleanup_requested);
        assert(persist_res.xmp_sidecar_cleanup_removed);
    }

    assert(read_file_bytes(output_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &output_store);
    assert_persist_xmp_state(&output_store, output_xmp_expect);
    assert_persist_datetime_original(&output_store, k_dto);
    assert_persist_preserved_metadata(&output_store, preserve_kind);

    if (writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY) {
        omc_arena_reset(&file_read);
        assert(read_file_bytes(sidecar_path, &file_read));
        read_store_from_bytes(file_read.data, file_read.size, &sidecar_store);
        assert_text_value(&sidecar_store,
                          find_xmp_entry(&sidecar_store,
                                         "http://ns.adobe.com/xap/1.0/",
                                         "CreatorTool"),
                          "NewTool");
    } else {
        omc_arena_reset(&file_read);
        assert(!read_file_bytes(sidecar_path, &file_read));
    }

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&output_store);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_writes_png_output_and_sidecar(void)
{
    exercise_transfer_persist_case(
        make_test_png_with_old_xmp_and_text, ".png",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_PNG_TEXT,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
}

static void
test_transfer_persist_writes_bigtiff_output_and_sidecar_with_preserve(void)
{
    exercise_transfer_persist_case(
        make_test_bigtiff_le_with_make_and_old_xmp, ".tif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
}

static void
test_transfer_persist_writes_heif_output_and_sidecar_with_strip(void)
{
    exercise_transfer_persist_case(
        make_test_heif_with_old_xmp_and_exif, ".heic",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
}

static void
test_transfer_persist_writes_embedded_and_sidecar_supported_formats(void)
{
    exercise_transfer_persist_case(
        make_test_dng_with_old_xmp_and_make, ".dng",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_case(
        make_test_webp_with_old_xmp_and_exif, ".webp",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_case(
        make_test_jp2_with_old_xmp_and_exif, ".jp2",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_case(
        make_test_heif_with_old_xmp_and_exif, ".heic",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_case(
        make_test_avif_with_old_xmp_and_exif, ".avif",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_case(
        make_test_jxl_with_old_xmp_and_exif, ".jxl",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
}

static void
test_transfer_persist_writes_sidecar_only_preserve_supported_formats(void)
{
    exercise_transfer_persist_case(
        make_test_dng_with_old_xmp_and_make, ".dng",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_case(
        make_test_webp_with_old_xmp_and_exif, ".webp",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_case(
        make_test_jp2_with_old_xmp_and_exif, ".jp2",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_case(
        make_test_heif_with_old_xmp_and_exif, ".heic",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_case(
        make_test_avif_with_old_xmp_and_exif, ".avif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_case(
        make_test_jxl_with_old_xmp_and_exif, ".jxl",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
}

static void
test_transfer_persist_writes_sidecar_only_strip_supported_formats(void)
{
    exercise_transfer_persist_case(
        make_test_dng_with_old_xmp_and_make, ".dng",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_case(
        make_test_webp_with_old_xmp_and_exif, ".webp",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_case(
        make_test_jp2_with_old_xmp_and_exif, ".jp2",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_case(
        make_test_avif_with_old_xmp_and_exif, ".avif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_case(
        make_test_jxl_with_old_xmp_and_exif, ".jxl",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
}

static void
test_transfer_persist_embedded_and_sidecar_source_exif_supported_formats(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_tiff_le_with_make_only, ".tif",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_bigtiff_le_with_make_and_old_xmp, ".tif",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_dng_with_old_xmp_and_make, ".dng",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_jpeg_with_old_xmp_and_comment, ".jpg",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_COMMENT,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_png_with_old_xmp_and_text, ".png",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_PNG_TEXT,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_webp_with_old_xmp_and_exif, ".webp",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_jp2_with_old_xmp_and_exif, ".jp2",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_heif_with_old_xmp_and_exif, ".heic",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_avif_with_old_xmp_and_exif, ".avif",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_jxl_with_old_xmp_and_exif, ".jxl",
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
}

static void
test_transfer_persist_sidecar_only_preserve_source_exif_supported_formats(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_tiff_le_with_make_only, ".tif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_bigtiff_le_with_make_and_old_xmp, ".tif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_dng_with_old_xmp_and_make, ".dng",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_webp_with_old_xmp_and_exif, ".webp",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_jp2_with_old_xmp_and_exif, ".jp2",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_heif_with_old_xmp_and_exif, ".heic",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_avif_with_old_xmp_and_exif, ".avif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
}

static void
test_transfer_persist_jpeg_sidecar_only_preserve_source_exif(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_jpeg_with_old_xmp_and_comment, ".jpg",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_COMMENT,
        OMC_TRANSFER_PERSIST_XMP_OLD, 0);
}

static void
test_transfer_persist_jpeg_sidecar_only_strip_source_exif(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_jpeg_with_old_xmp_and_comment, ".jpg",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_COMMENT,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
}

static void
test_transfer_persist_jpeg_embedded_only_source_exif(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_jpeg_with_old_xmp_and_comment, ".jpg",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_COMMENT,
        OMC_TRANSFER_PERSIST_XMP_NEW, 1);
}

static void
test_transfer_persist_sidecar_only_strip_source_exif_supported_formats(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_tiff_le_with_make_only, ".tif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_bigtiff_le_with_make_and_old_xmp, ".tif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_dng_with_old_xmp_and_make, ".dng",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_DNG_CORE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_webp_with_old_xmp_and_exif, ".webp",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_jp2_with_old_xmp_and_exif, ".jp2",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_heif_with_old_xmp_and_exif, ".heic",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
    exercise_transfer_persist_source_exif_case(
        make_test_avif_with_old_xmp_and_exif, ".avif",
        OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NONE, 0);
}

static void
test_transfer_persist_webp_embedded_only_source_exif(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_webp_with_old_xmp_and_exif, ".webp",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 1);
}

static void
test_transfer_persist_jp2_embedded_only_source_exif(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_jp2_with_old_xmp_and_exif, ".jp2",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 1);
}

static void
test_transfer_persist_heif_embedded_only_source_exif(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_heif_with_old_xmp_and_exif, ".heic",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 1);
}

static void
test_transfer_persist_avif_embedded_only_source_exif(void)
{
    exercise_transfer_persist_source_exif_case(
        make_test_avif_with_old_xmp_and_exif, ".avif",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 1);
}

static void
test_transfer_persist_writes_heif_minimal_embedded_only_output(void)
{
    exercise_transfer_persist_case(
        make_test_heif_minimal, ".heic",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_NONE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
}

static void
test_transfer_persist_writes_avif_minimal_embedded_only_output(void)
{
    exercise_transfer_persist_case(
        make_test_avif_minimal, ".avif",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_NONE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 0);
}

static void
test_transfer_persist_can_remove_stale_destination_sidecar_for_jxl(void)
{
    exercise_transfer_persist_case(
        make_test_jxl_with_old_xmp_and_exif, ".jxl",
        OMC_XMP_WRITEBACK_EMBEDDED_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_PERSIST_PRESERVE_EXIF_MAKE,
        OMC_TRANSFER_PERSIST_XMP_NEW, 1);
}

static void
test_transfer_persist_writes_output_and_sidecar(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    derive_sidecar_path(output_path, sidecar_path);

    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    assert(persist_res.output_status == OMC_TRANSFER_OK);
    assert(persist_res.xmp_sidecar_status == OMC_TRANSFER_OK);

    assert(read_file_bytes(output_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    omc_arena_reset(&file_read);
    assert(read_file_bytes(sidecar_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_rejects_existing_sidecar_without_overwrite(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    char output_path[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    FILE* fp;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    derive_sidecar_path(output_path, sidecar_path);

    fp = fopen(sidecar_path, "wb");
    assert(fp != (FILE*)0);
    fputs("stale sidecar", fp);
    fclose(fp);

    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(persist_res.xmp_sidecar_status == OMC_TRANSFER_UNSUPPORTED);

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_uses_explicit_sidecar_base_path(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 8];
    char sidecar_base[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    omc_const_bytes sidecar_path_view;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    build_temp_path(sidecar_base, ".tif");
    derive_sidecar_path(sidecar_base, sidecar_path);

    omc_store_init(&source_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    persist_opts.xmp_sidecar_base_path = sidecar_base;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    sidecar_path_view = omc_arena_view(&meta_out, persist_res.xmp_sidecar_path);
    assert(sidecar_path_view.size == strlen(sidecar_path));
    assert(memcmp(sidecar_path_view.data, sidecar_path,
                  sidecar_path_view.size) == 0);

    assert(read_file_bytes(sidecar_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_can_remove_stale_destination_sidecar(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    FILE* fp;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    derive_sidecar_path(output_path, sidecar_path);

    fp = fopen(sidecar_path, "wb");
    assert(fp != (FILE*)0);
    fputs("stale sidecar", fp);
    fclose(fp);

    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    persist_opts.destination_sidecar_mode =
        OMC_TRANSFER_DEST_SIDECAR_STRIP_EXISTING;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    assert(persist_res.xmp_sidecar_cleanup_requested);
    assert(persist_res.xmp_sidecar_cleanup_removed);

    assert(!read_file_bytes(sidecar_path, &file_read));
    assert(read_file_bytes(output_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_dng_minimal_scaffold_sidecar_only_without_output_path(
    void)
{
    omc_store source_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char sidecar_base[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    omc_const_bytes sidecar_path_view;
    omc_status status;

    build_temp_path(sidecar_base, ".dng");
    derive_sidecar_path(sidecar_base, sidecar_path);

    omc_store_init(&source_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.format = OMC_SCAN_FMT_DNG;
    prepare_opts.dng_target_mode = OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD;
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    execute_transfer((const omc_u8*)0, 0U, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);
    assert(transfer_res.dng_target_mode
           == OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD);
    assert(!transfer_res.edited_present);
    assert(transfer_res.sidecar_present);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.write_output = 0;
    persist_opts.xmp_sidecar_base_path = sidecar_base;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    assert(persist_res.output_status == OMC_TRANSFER_OK);
    assert(persist_res.xmp_sidecar_status == OMC_TRANSFER_OK);
    assert(persist_res.output_path.size == 0U);
    sidecar_path_view = omc_arena_view(&meta_out, persist_res.xmp_sidecar_path);
    assert(sidecar_path_view.size == strlen(sidecar_path));
    assert(memcmp(sidecar_path_view.data, sidecar_path,
                  sidecar_path_view.size) == 0);

    assert(read_file_bytes(sidecar_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_dng_template_sidecar_only_requires_output_path(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char sidecar_base[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    omc_status status;

    file_size = make_test_dng_with_old_xmp_and_make(file_bytes);
    build_temp_path(sidecar_base, ".dng");
    derive_sidecar_path(sidecar_base, sidecar_path);

    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.format = OMC_SCAN_FMT_DNG;
    prepare_opts.dng_target_mode = OMC_DNG_TARGET_TEMPLATE;
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);
    assert(transfer_res.dng_target_mode == OMC_DNG_TARGET_TEMPLATE);
    assert(transfer_res.edited_present);
    assert(transfer_res.sidecar_present);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.write_output = 0;
    persist_opts.xmp_sidecar_base_path = sidecar_base;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(persist_res.output_status == OMC_TRANSFER_UNSUPPORTED);
    assert(!read_file_bytes(sidecar_path, &file_read));

    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

int
main(void)
{
    test_transfer_persist_writes_png_output_and_sidecar();
    test_transfer_persist_writes_bigtiff_output_and_sidecar_with_preserve();
    test_transfer_persist_writes_heif_output_and_sidecar_with_strip();
    test_transfer_persist_writes_embedded_and_sidecar_supported_formats();
    test_transfer_persist_writes_sidecar_only_preserve_supported_formats();
    test_transfer_persist_writes_sidecar_only_strip_supported_formats();
    test_transfer_persist_embedded_and_sidecar_source_exif_supported_formats();
    test_transfer_persist_sidecar_only_preserve_source_exif_supported_formats();
    test_transfer_persist_webp_embedded_only_source_exif();
    test_transfer_persist_jp2_embedded_only_source_exif();
    test_transfer_persist_heif_embedded_only_source_exif();
    test_transfer_persist_avif_embedded_only_source_exif();
    test_transfer_persist_jpeg_sidecar_only_preserve_source_exif();
    test_transfer_persist_jpeg_sidecar_only_strip_source_exif();
    test_transfer_persist_jpeg_embedded_only_source_exif();
    test_transfer_persist_sidecar_only_strip_source_exif_supported_formats();
    test_transfer_persist_writes_heif_minimal_embedded_only_output();
    test_transfer_persist_writes_avif_minimal_embedded_only_output();
    test_transfer_persist_can_remove_stale_destination_sidecar_for_jxl();
    test_transfer_persist_writes_output_and_sidecar();
    test_transfer_persist_rejects_existing_sidecar_without_overwrite();
    test_transfer_persist_uses_explicit_sidecar_base_path();
    test_transfer_persist_can_remove_stale_destination_sidecar();
    test_transfer_persist_dng_minimal_scaffold_sidecar_only_without_output_path();
    test_transfer_persist_dng_template_sidecar_only_requires_output_path();
    return 0;
}
