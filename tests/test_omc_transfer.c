#include "omc/omc_read.h"
#include "omc/omc_transfer.h"

#include <assert.h>
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

static int
contains_text(const omc_u8* bytes, omc_size size, const char* text)
{
    omc_size text_size;
    omc_size i;

    text_size = strlen(text);
    if (text_size > size) {
        return 0;
    }
    for (i = 0U; i + text_size <= size; ++i) {
        if (memcmp(bytes + i, text, text_size) == 0) {
            return 1;
        }
    }
    return 0;
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
append_u64be(omc_u8* out, omc_size* io_size, omc_u64 value)
{
    out[*io_size + 0U] = (omc_u8)((value >> 56) & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 48) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 40) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)((value >> 32) & 0xFFU);
    out[*io_size + 4U] = (omc_u8)((value >> 24) & 0xFFU);
    out[*io_size + 5U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 6U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 7U] = (omc_u8)(value & 0xFFU);
    *io_size += 8U;
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

static omc_size
count_xmp_entries(const omc_store* store, const char* schema_ns,
                  const char* property_path)
{
    omc_size i;
    omc_size count;

    count = 0U;
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
            count += 1U;
        }
    }
    return count;
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
build_store_with_custom_flag(omc_store* store)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "urn:vendor:test:1.0/"),
        append_store_bytes(&store->arena, "Flag"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "Alpha"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_creator_contact_info(omc_store* store)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_store_bytes(&store->arena, "CreatorContactInfo/CiEmailWork"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena,
                                         "editor@example.test"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_store_bytes(&store->arena, "CreatorContactInfo/CiUrlWork"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena,
                                         "https://example.test/contact"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_mixed_location_shown_details(omc_store* store)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/xmp:Identifier[1]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "loc-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/xmp:Identifier[2]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "loc-002"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/exif:GPSLatitude"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "41,24.5N"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/exif:GPSLongitude"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "2,9E"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
add_xmp_text_entry(omc_store* store, const char* schema_ns,
                   const char* property_path, const char* value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key,
                              append_store_bytes(&store->arena, schema_ns),
                              append_store_bytes(&store->arena, property_path));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, value),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_pdf_and_rights_namespaces(omc_store* store)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://ns.adobe.com/pdf/1.3/"),
        append_store_bytes(&store->arena, "Keywords"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "tokyo,night"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://ns.adobe.com/xap/1.0/rights/"),
        append_store_bytes(&store->arena, "Marked"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "True"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_rights_canonicalized(omc_store* store)
{
    omc_entry entry;
    omc_status status;
    static const char k_rights[] = "Generated copyright";

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 116U);
    omc_val_make_bytes(&entry.value,
                       append_store_bytes(&store->arena, k_rights));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://ns.adobe.com/xap/1.0/rights/"),
        append_store_bytes(&store->arena, "UsageTerms[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "Licensed use only"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://ns.adobe.com/xap/1.0/rights/"),
        append_store_bytes(&store->arena, "WebStatement"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena,
                                         "https://example.test/license"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_location_child_shapes(omc_store* store)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/LocationName"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "legacy-name"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena,
                           "LocationShown[1]/LocationName[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "Kyoto"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena,
                           "LocationShown[1]/LocationName[@xml:lang=fr-FR]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "Kyoto FR"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/LocationId"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "legacy-id"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/LocationId[1]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "loc-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_store_bytes(&store->arena, "LocationShown[1]/LocationId[2]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "loc-002"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_xmpmm_namespace(omc_store* store)
{
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "DocumentID", "xmp.did:1234");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "InstanceID", "xmp.iid:5678");
}

static void
build_store_with_xmpmm_structured_resources(omc_store* store)
{
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "DerivedFrom", "legacy-derived");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "DerivedFrom/stRef:documentID", "xmp.did:base");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom", "legacy-managed");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom/stRef:documentID", "xmp.did:managed");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients", "legacy-ingredients");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients[1]/stRef:documentID",
                       "xmp.did:ingredient");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Manifest", "legacy-manifest");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Manifest[1]/stMfs:linkForm", "EmbedByReference");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Manifest[1]/stMfs:reference/stRef:filePath",
                       "C:\\some path\\file.ext");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "History", "legacy-history");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "History[1]/stEvt:action", "saved");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions", "legacy-versions");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event", "legacy-event");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:version", "1.0");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event/stEvt:action", "saved");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Pantry", "legacy-pantry");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Pantry[1]/InstanceID", "uuid:pantry-1");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Pantry[1]/dc:format", "image/jpeg");
}

static void
build_store_with_advisory_bag(omc_store* store)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://ns.adobe.com/xap/1.0/"),
        append_store_bytes(&store->arena, "Advisory[1]"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "xmp:MetadataDate"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://ns.adobe.com/xap/1.0/"),
        append_store_bytes(&store->arena, "Advisory[2]"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "photoshop:City"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_language_and_date(omc_store* store)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://purl.org/dc/elements/1.1/"),
        append_store_bytes(&store->arena, "language[1]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "en-US"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://purl.org/dc/elements/1.1/"),
        append_store_bytes(&store->arena, "language[2]"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, "fr-FR"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://purl.org/dc/elements/1.1/"),
        append_store_bytes(&store->arena, "date[1]"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "2026-04-15"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://purl.org/dc/elements/1.1/"),
        append_store_bytes(&store->arena, "date[2]"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "2026-04-16"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
build_store_with_lr_hierarchical_subject(omc_store* store)
{
    add_xmp_text_entry(store, "http://ns.adobe.com/lightroom/1.0/",
                       "hierarchicalSubject[1]", "Places|Japan|Tokyo");
    add_xmp_text_entry(store, "http://ns.adobe.com/lightroom/1.0/",
                       "hierarchicalSubject[2]", "Travel|Spring");
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
make_test_tiff_with_old_xmp_and_make(omc_u8* out)
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
    append_u16le(out, &size, 2U);

    make_off = 8U + 2U + 24U + 4U;
    xmp_off = make_off + (omc_u32)sizeof(make);

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)sizeof(make));
    append_u32le(out, &size, make_off);

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
    omc_u8 tiff[512];
    omc_u8 exif[544];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size size;

    tiff_size = make_test_tiff_with_old_xmp_and_make(tiff);
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
    omc_u8 tiff[512];
    omc_u8 exif_payload[544];
    omc_u8 colr_payload[8];
    omc_u8 colr_box[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size colr_size;
    omc_size colr_box_size;
    omc_size size;

    tiff_size = make_test_tiff_with_old_xmp_and_make(tiff);
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

typedef enum omc_transfer_preserve_kind {
    OMC_TRANSFER_PRESERVE_COMMENT = 0,
    OMC_TRANSFER_PRESERVE_PNG_TEXT = 1,
    OMC_TRANSFER_PRESERVE_EXIF_MAKE = 2,
    OMC_TRANSFER_PRESERVE_DNG_CORE = 3
} omc_transfer_preserve_kind;

typedef enum omc_transfer_embedded_xmp_expect {
    OMC_TRANSFER_EMBEDDED_XMP_OLD = 0,
    OMC_TRANSFER_EMBEDDED_XMP_NEW = 1,
    OMC_TRANSFER_EMBEDDED_XMP_NONE = 2
} omc_transfer_embedded_xmp_expect;

typedef omc_size (*omc_transfer_fixture_builder)(omc_u8* out);

static void
assert_preserved_metadata(const omc_store* store,
                          omc_transfer_preserve_kind preserve_kind)
{
    static const omc_u8 k_dng_version[4] = { 1U, 6U, 0U, 0U };

    if (preserve_kind == OMC_TRANSFER_PRESERVE_COMMENT) {
        assert_text_value(store, find_comment_entry(store), "Preserve me");
    } else if (preserve_kind == OMC_TRANSFER_PRESERVE_PNG_TEXT) {
        assert_text_value(store, find_png_text_entry(store, "Comment", "text"),
                          "Preserve me");
    } else if (preserve_kind == OMC_TRANSFER_PRESERVE_DNG_CORE) {
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
assert_embedded_xmp_state(const omc_store* store,
                          omc_transfer_embedded_xmp_expect expect)
{
    const omc_entry* entry;

    entry = find_xmp_entry(store, "http://ns.adobe.com/xap/1.0/",
                           "CreatorTool");
    if (expect == OMC_TRANSFER_EMBEDDED_XMP_NONE) {
        assert(entry == (const omc_entry*)0);
        return;
    }
    assert(entry != (const omc_entry*)0);
    if (expect == OMC_TRANSFER_EMBEDDED_XMP_NEW) {
        assert_text_value(store, entry, "NewTool");
        assert(count_xmp_entries(store, "http://ns.adobe.com/xap/1.0/",
                                 "CreatorTool")
               == 1U);
    } else {
        assert_text_value(store, entry, "OldTool");
        assert(count_xmp_entries(store, "http://ns.adobe.com/xap/1.0/",
                                 "CreatorTool")
               == 1U);
    }
}

static void
assert_embedded_exif_times(const omc_store* store, const char* expect)
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

static void
exercise_transfer_supported_case(
    omc_transfer_fixture_builder builder, omc_scan_fmt expect_format,
    omc_transfer_preserve_kind preserve_kind,
    omc_xmp_writeback_mode writeback_mode,
    omc_xmp_destination_embedded_mode destination_mode,
    omc_transfer_embedded_xmp_expect embedded_xmp_expect,
    omc_u32 expect_removed_xmp_blocks, omc_u32 expect_inserted_xmp_blocks,
    omc_u32 expect_route_count, omc_transfer_embedded_action expect_action,
    int expect_edited_present)
{
    omc_u8 file_bytes[4096];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = builder(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = writeback_mode;
    opts.destination_embedded_mode = destination_mode;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.format == expect_format);
    assert(bundle.route_count == expect_route_count);
    assert(bundle.embedded_action == expect_action);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_OK);
    assert(exec.route_count == expect_route_count);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present == expect_edited_present);
    assert(res.sidecar_present);
    assert(res.embedded.removed_xmp_blocks == expect_removed_xmp_blocks);
    assert(res.embedded.inserted_xmp_blocks == expect_inserted_xmp_blocks);

    if (expect_edited_present) {
        read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
        assert_embedded_xmp_state(&edited_store, embedded_xmp_expect);
        assert_preserved_metadata(&edited_store, preserve_kind);
    } else {
        assert(edited_out.size == 0U);
    }

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
exercise_transfer_supported_source_exif_case(
    omc_transfer_fixture_builder builder, omc_scan_fmt expect_format,
    omc_transfer_preserve_kind preserve_kind,
    omc_xmp_writeback_mode writeback_mode,
    omc_xmp_destination_embedded_mode destination_mode,
    omc_transfer_embedded_xmp_expect embedded_xmp_expect,
    omc_u32 expect_removed_xmp_blocks, omc_u32 expect_inserted_xmp_blocks,
    omc_u32 expect_route_count, omc_transfer_embedded_action expect_action)
{
    static const char k_dto[] = "2024:01:02 03:04:05";
    omc_u8 file_bytes[4096];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = builder(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool_and_datetime_original(
        &store, "NewTool", k_dto);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = writeback_mode;
    opts.destination_embedded_mode = destination_mode;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.format == expect_format);
    assert(bundle.route_count == expect_route_count);
    assert(bundle.embedded_action == expect_action);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(res.embedded.removed_xmp_blocks == expect_removed_xmp_blocks);
    assert(res.embedded.inserted_xmp_blocks == expect_inserted_xmp_blocks);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert_embedded_xmp_state(&edited_store, embedded_xmp_expect);
    assert_embedded_exif_times(&edited_store, k_dto);
    assert_preserved_metadata(&edited_store, preserve_kind);

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_supported_formats(void)
{
    exercise_transfer_supported_case(
        make_test_png_with_old_xmp_and_text, OMC_SCAN_FMT_PNG,
        OMC_TRANSFER_PRESERVE_PNG_TEXT,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_tiff_with_old_xmp_and_make, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_bigtiff_le_with_make_and_old_xmp, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_dng_with_old_xmp_and_make, OMC_SCAN_FMT_DNG,
        OMC_TRANSFER_PRESERVE_DNG_CORE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_webp_with_old_xmp_and_exif, OMC_SCAN_FMT_WEBP,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_jp2_with_old_xmp_and_exif, OMC_SCAN_FMT_JP2,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_heif_with_old_xmp_and_exif, OMC_SCAN_FMT_HEIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_avif_with_old_xmp_and_exif, OMC_SCAN_FMT_AVIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
    exercise_transfer_supported_case(
        make_test_jxl_with_old_xmp_and_exif, OMC_SCAN_FMT_JXL,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE, 1);
}

static void
test_transfer_execute_embedded_and_sidecar_source_exif_supported_formats(void)
{
    exercise_transfer_supported_source_exif_case(
        make_test_tiff_with_old_xmp_and_make, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_bigtiff_le_with_make_and_old_xmp, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_dng_with_old_xmp_and_make, OMC_SCAN_FMT_DNG,
        OMC_TRANSFER_PRESERVE_DNG_CORE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_jpeg_with_old_xmp_and_comment, OMC_SCAN_FMT_JPEG,
        OMC_TRANSFER_PRESERVE_COMMENT,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_png_with_old_xmp_and_text, OMC_SCAN_FMT_PNG,
        OMC_TRANSFER_PRESERVE_PNG_TEXT,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_webp_with_old_xmp_and_exif, OMC_SCAN_FMT_WEBP,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_jp2_with_old_xmp_and_exif, OMC_SCAN_FMT_JP2,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_heif_with_old_xmp_and_exif, OMC_SCAN_FMT_HEIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_avif_with_old_xmp_and_exif, OMC_SCAN_FMT_AVIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
    exercise_transfer_supported_source_exif_case(
        make_test_jxl_with_old_xmp_and_exif, OMC_SCAN_FMT_JXL,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE,
        OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NEW, 1U, 1U, 2U,
        OMC_TRANSFER_EMBEDDED_REWRITE);
}

static void
test_transfer_execute_sidecar_only_preserve_supported_formats(void)
{
    exercise_transfer_supported_case(
        make_test_jpeg_with_old_xmp_and_comment, OMC_SCAN_FMT_JPEG,
        OMC_TRANSFER_PRESERVE_COMMENT, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_png_with_old_xmp_and_text, OMC_SCAN_FMT_PNG,
        OMC_TRANSFER_PRESERVE_PNG_TEXT, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_tiff_with_old_xmp_and_make, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_bigtiff_le_with_make_and_old_xmp, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_dng_with_old_xmp_and_make, OMC_SCAN_FMT_DNG,
        OMC_TRANSFER_PRESERVE_DNG_CORE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_webp_with_old_xmp_and_exif, OMC_SCAN_FMT_WEBP,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_jp2_with_old_xmp_and_exif, OMC_SCAN_FMT_JP2,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_heif_with_old_xmp_and_exif, OMC_SCAN_FMT_HEIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_avif_with_old_xmp_and_exif, OMC_SCAN_FMT_AVIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
    exercise_transfer_supported_case(
        make_test_jxl_with_old_xmp_and_exif, OMC_SCAN_FMT_JXL,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE, 1);
}

static void
test_transfer_execute_sidecar_only_preserve_source_exif_supported_formats(void)
{
    exercise_transfer_supported_source_exif_case(
        make_test_tiff_with_old_xmp_and_make, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE);
    exercise_transfer_supported_source_exif_case(
        make_test_bigtiff_le_with_make_and_old_xmp, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE);
    exercise_transfer_supported_source_exif_case(
        make_test_dng_with_old_xmp_and_make, OMC_SCAN_FMT_DNG,
        OMC_TRANSFER_PRESERVE_DNG_CORE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE);
    exercise_transfer_supported_source_exif_case(
        make_test_webp_with_old_xmp_and_exif, OMC_SCAN_FMT_WEBP,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE);
    exercise_transfer_supported_source_exif_case(
        make_test_jp2_with_old_xmp_and_exif, OMC_SCAN_FMT_JP2,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE);
    exercise_transfer_supported_source_exif_case(
        make_test_heif_with_old_xmp_and_exif, OMC_SCAN_FMT_HEIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE);
    exercise_transfer_supported_source_exif_case(
        make_test_avif_with_old_xmp_and_exif, OMC_SCAN_FMT_AVIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_OLD, 0U, 0U, 1U,
        OMC_TRANSFER_EMBEDDED_NONE);
}

static void
test_transfer_execute_sidecar_only_strip_supported_formats(void)
{
    exercise_transfer_supported_case(
        make_test_png_with_old_xmp_and_text, OMC_SCAN_FMT_PNG,
        OMC_TRANSFER_PRESERVE_PNG_TEXT, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_tiff_with_old_xmp_and_make, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_bigtiff_le_with_make_and_old_xmp, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_dng_with_old_xmp_and_make, OMC_SCAN_FMT_DNG,
        OMC_TRANSFER_PRESERVE_DNG_CORE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_webp_with_old_xmp_and_exif, OMC_SCAN_FMT_WEBP,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_jp2_with_old_xmp_and_exif, OMC_SCAN_FMT_JP2,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_heif_with_old_xmp_and_exif, OMC_SCAN_FMT_HEIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_avif_with_old_xmp_and_exif, OMC_SCAN_FMT_AVIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
    exercise_transfer_supported_case(
        make_test_jxl_with_old_xmp_and_exif, OMC_SCAN_FMT_JXL,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP, 1);
}

static void
test_transfer_execute_sidecar_only_strip_source_exif_supported_formats(void)
{
    exercise_transfer_supported_source_exif_case(
        make_test_tiff_with_old_xmp_and_make, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP);
    exercise_transfer_supported_source_exif_case(
        make_test_bigtiff_le_with_make_and_old_xmp, OMC_SCAN_FMT_TIFF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP);
    exercise_transfer_supported_source_exif_case(
        make_test_dng_with_old_xmp_and_make, OMC_SCAN_FMT_DNG,
        OMC_TRANSFER_PRESERVE_DNG_CORE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP);
    exercise_transfer_supported_source_exif_case(
        make_test_webp_with_old_xmp_and_exif, OMC_SCAN_FMT_WEBP,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP);
    exercise_transfer_supported_source_exif_case(
        make_test_jp2_with_old_xmp_and_exif, OMC_SCAN_FMT_JP2,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP);
    exercise_transfer_supported_source_exif_case(
        make_test_heif_with_old_xmp_and_exif, OMC_SCAN_FMT_HEIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP);
    exercise_transfer_supported_source_exif_case(
        make_test_avif_with_old_xmp_and_exif, OMC_SCAN_FMT_AVIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE, OMC_XMP_WRITEBACK_SIDECAR_ONLY,
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING,
        OMC_TRANSFER_EMBEDDED_XMP_NONE, 1U, 0U, 2U,
        OMC_TRANSFER_EMBEDDED_STRIP);
}

static void
exercise_transfer_supported_source_exif_embedded_only_case(
    omc_transfer_fixture_builder builder, omc_scan_fmt expect_format,
    omc_transfer_preserve_kind preserve_kind)
{
    static const char k_dto[] = "2024:01:02 03:04:05";
    omc_u8 file_bytes[4096];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = builder(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool_and_datetime_original(
        &store, "NewTool", k_dto);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.format == expect_format);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(!res.sidecar_present);
    assert(sidecar_out.size == 0U);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_embedded_exif_times(&edited_store, k_dto);
    assert_preserved_metadata(&edited_store, preserve_kind);

    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_webp_embedded_only_source_exif(void)
{
    exercise_transfer_supported_source_exif_embedded_only_case(
        make_test_webp_with_old_xmp_and_exif, OMC_SCAN_FMT_WEBP,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE);
}

static void
test_transfer_execute_jp2_embedded_only_source_exif(void)
{
    exercise_transfer_supported_source_exif_embedded_only_case(
        make_test_jp2_with_old_xmp_and_exif, OMC_SCAN_FMT_JP2,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE);
}

static void
test_transfer_execute_heif_embedded_only_source_exif(void)
{
    exercise_transfer_supported_source_exif_embedded_only_case(
        make_test_heif_with_old_xmp_and_exif, OMC_SCAN_FMT_HEIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE);
}

static void
test_transfer_execute_avif_embedded_only_source_exif(void)
{
    exercise_transfer_supported_source_exif_embedded_only_case(
        make_test_avif_with_old_xmp_and_exif, OMC_SCAN_FMT_AVIF,
        OMC_TRANSFER_PRESERVE_EXIF_MAKE);
}

static void
test_transfer_prepare_allows_sidecar_only_preserve_for_gif(void)
{
    static const omc_u8 gif[] = { 'G', 'I', 'F', '8', '9', 'a' };
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_store sidecar_store;
    omc_status status;

    omc_store_init(&store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_GIF;
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;

    status = omc_transfer_prepare(gif, sizeof(gif), &store, &opts, &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.route_count == 1U);
    assert(bundle.embedded_action == OMC_TRANSFER_EMBEDDED_NONE);
    assert(bundle.sidecar_requested);
    assert(!bundle.embedded_supported);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_OK);
    assert(exec.route_count == 1U);
    assert(exec.routes[0].kind == OMC_TRANSFER_ROUTE_SIDECAR_XMP);

    status = omc_transfer_execute((const omc_u8*)0, 0U, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(!res.edited_present);
    assert(res.sidecar_present);
    assert(edited_out.size == 0U);

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_jpeg_sidecar_only_preserve_source_exif(void)
{
    static const char k_dto[] = "2024:01:02 03:04:05";
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool_and_datetime_original(
        &store, "NewTool", k_dto);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert_embedded_xmp_state(&edited_store, OMC_TRANSFER_EMBEDDED_XMP_OLD);
    assert_embedded_exif_times(&edited_store, k_dto);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_jpeg_sidecar_only_strip_source_exif(void)
{
    static const char k_dto[] = "2024:01:02 03:04:05";
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool_and_datetime_original(
        &store, "NewTool", k_dto);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert_embedded_xmp_state(&edited_store, OMC_TRANSFER_EMBEDDED_XMP_NONE);
    assert_embedded_exif_times(&edited_store, k_dto);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_jpeg_embedded_only_source_exif(void)
{
    static const char k_dto[] = "2024:01:02 03:04:05";
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool_and_datetime_original(
        &store, "NewTool", k_dto);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(!res.sidecar_present);
    assert(sidecar_out.size == 0U);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_embedded_exif_times(&edited_store, k_dto);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_prepare_and_execute_jpeg_embedded_and_sidecar(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.format == OMC_SCAN_FMT_JPEG);
    assert(bundle.existing_xmp_blocks == 1U);
    assert(bundle.route_count == 2U);
    assert(bundle.embedded_action == OMC_TRANSFER_EMBEDDED_REWRITE);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_OK);
    assert(exec.route_count == 2U);
    assert(exec.routes[0].kind == OMC_TRANSFER_ROUTE_EMBEDDED_XMP);
    assert(exec.routes[0].embedded_action == OMC_TRANSFER_EMBEDDED_REWRITE);
    assert(exec.routes[1].kind == OMC_TRANSFER_ROUTE_SIDECAR_XMP);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_jpeg_sidecar_only_strip(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.route_count == 2U);
    assert(bundle.embedded_action == OMC_TRANSFER_EMBEDDED_STRIP);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.routes[0].kind == OMC_TRANSFER_ROUTE_EMBEDDED_XMP);
    assert(exec.routes[0].embedded_action == OMC_TRANSFER_EMBEDDED_STRIP);
    assert(exec.routes[1].kind == OMC_TRANSFER_ROUTE_SIDECAR_XMP);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 0U);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert(find_xmp_entry(&edited_store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool")
           == (const omc_entry*)0);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_prepare_reports_unsupported_for_embedded_only_gif(void)
{
    static const omc_u8 gif[] = { 'G', 'I', 'F', '8', '9', 'a' };
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_GIF;
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;

    status = omc_transfer_prepare(gif, sizeof(gif), &store, &opts, &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_UNSUPPORTED);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_UNSUPPORTED);
    assert(exec.route_count == 0U);

    status = omc_transfer_execute(gif, sizeof(gif), &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(edited_out.size == 0U);
    assert(sidecar_out.size == 0U);

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_uses_sidecar_policy_for_embedded_custom_namespace(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_custom_flag(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;
    opts.sidecar.portable_existing_namespace_policy =
        OMC_XMP_NS_PRESERVE_CUSTOM;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "urn:vendor:test:1.0/"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "Flag>Alpha</"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "urn:vendor:test:1.0/"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "Flag>Alpha</"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_structured_creator_contact_info(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_contact_info(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(
        edited_out.data, edited_out.size,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(
        edited_out.data, edited_out.size,
        "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"));
    assert(contains_text(
        sidecar_out.data, sidecar_out.size,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(
        sidecar_out.data, sidecar_out.size,
        "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_mixed_namespace_location_details(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_mixed_location_shown_details(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<Iptc4xmpExt:LocationShown>"));
    assert(contains_text(
        edited_out.data, edited_out.size,
        "<xmp:Identifier xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>loc-001</rdf:li>"));
    assert(contains_text(
        edited_out.data, edited_out.size,
        "<exif:GPSLatitude xmlns:exif=\"http://ns.adobe.com/exif/1.0/\">41,24.5N</exif:GPSLatitude>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<Iptc4xmpExt:LocationShown>"));
    assert(contains_text(
        sidecar_out.data, sidecar_out.size,
        "<xmp:Identifier xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>loc-001</rdf:li>"));
    assert(contains_text(
        sidecar_out.data, sidecar_out.size,
        "<exif:GPSLatitude xmlns:exif=\"http://ns.adobe.com/exif/1.0/\">41,24.5N</exif:GPSLatitude>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_xmpmm_namespace(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_xmpmm_namespace(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\""));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:DocumentID>xmp.did:1234</xmpMM:DocumentID>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:InstanceID>xmp.iid:5678</xmpMM:InstanceID>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\""));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpMM:DocumentID>xmp.did:1234</xmpMM:DocumentID>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpMM:InstanceID>xmp.iid:5678</xmpMM:InstanceID>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_xmpmm_structured_resources(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_xmpmm_structured_resources(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "xmlns:stRef=\"http://ns.adobe.com/xap/1.0/sType/ResourceRef#\""));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:DerivedFrom rdf:parseType=\"Resource\">"));
    assert(contains_text(edited_out.data, edited_out.size, "<xmpMM:Ingredients>"));
    assert(contains_text(edited_out.data, edited_out.size, "<rdf:Bag>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<stMfs:reference"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "rdf:parseType=\"Resource\""));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<stVer:event"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:InstanceID>uuid:pantry-1</xmpMM:InstanceID>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "image/jpeg</dc:format>"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-derived"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-managed"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-ingredients"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-manifest"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-history"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-versions"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-event"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-pantry"));

    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "xmlns:stRef=\"http://ns.adobe.com/xap/1.0/sType/ResourceRef#\""));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpMM:DerivedFrom rdf:parseType=\"Resource\">"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<xmpMM:Ingredients>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<rdf:Bag>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<stMfs:reference"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "rdf:parseType=\"Resource\""));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<stVer:event"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpMM:InstanceID>uuid:pantry-1</xmpMM:InstanceID>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "image/jpeg</dc:format>"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-derived"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-managed"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-ingredients"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-manifest"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-history"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-versions"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-event"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-pantry"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_xmp_advisory_bag(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_advisory_bag(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size, "<xmp:Advisory>"));
    assert(contains_text(edited_out.data, edited_out.size, "<rdf:Bag>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>xmp:MetadataDate</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>photoshop:City</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<xmp:Advisory>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<rdf:Bag>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>xmp:MetadataDate</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>photoshop:City</rdf:li>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_dc_language_and_date(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_language_and_date(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size, "<dc:language>"));
    assert(contains_text(edited_out.data, edited_out.size, "<rdf:Bag>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>en-US</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>fr-FR</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size, "<dc:date>"));
    assert(contains_text(edited_out.data, edited_out.size, "<rdf:Seq>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>2026-04-15</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>2026-04-16</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<dc:language>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<rdf:Bag>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>en-US</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>fr-FR</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<dc:date>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<rdf:Seq>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>2026-04-15</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>2026-04-16</rdf:li>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_lr_hierarchical_subject(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_lr_hierarchical_subject(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<lr:hierarchicalSubject>"));
    assert(contains_text(edited_out.data, edited_out.size, "<rdf:Bag>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Places|Japan|Tokyo</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Travel|Spring</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<lr:hierarchicalSubject>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<rdf:Bag>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Places|Japan|Tokyo</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Travel|Spring</rdf:li>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_preserves_pdf_and_rights_namespaces(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_pdf_and_rights_namespaces(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<pdf:Keywords>tokyo,night</pdf:Keywords>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpRights:Marked>True</xmpRights:Marked>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<pdf:Keywords>tokyo,night</pdf:Keywords>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpRights:Marked>True</xmpRights:Marked>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_canonicalizes_xmprights_usage_terms(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_rights_canonicalized(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 1;
    opts.sidecar.include_existing_xmp = 1;
    opts.sidecar.portable_conflict_policy = OMC_XMP_CONFLICT_GENERATED_WINS;
    opts.sidecar.portable_existing_standard_namespace_policy =
        OMC_XMP_STD_NS_CANONICALIZE_MANAGED;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Generated copyright</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Licensed use only</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpRights:WebStatement>https://example.test/license</xmpRights:WebStatement>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Generated copyright</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Licensed use only</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpRights:WebStatement>https://example.test/license</xmpRights:WebStatement>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_embedded_and_sidecar_canonicalizes_location_child_shapes(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_location_child_shapes(&store);

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"fr-FR\">Kyoto FR</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>loc-001</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>loc-002</rdf:li>"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-name"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-id"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"fr-FR\">Kyoto FR</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>loc-001</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>loc-002</rdf:li>"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-name"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-id"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

int
main(void)
{
    test_transfer_execute_embedded_and_sidecar_supported_formats();
    test_transfer_execute_embedded_and_sidecar_source_exif_supported_formats();
    test_transfer_execute_sidecar_only_preserve_supported_formats();
    test_transfer_execute_sidecar_only_preserve_source_exif_supported_formats();
    test_transfer_execute_sidecar_only_strip_supported_formats();
    test_transfer_execute_sidecar_only_strip_source_exif_supported_formats();
    test_transfer_prepare_allows_sidecar_only_preserve_for_gif();
    test_transfer_prepare_and_execute_jpeg_embedded_and_sidecar();
    test_transfer_execute_webp_embedded_only_source_exif();
    test_transfer_execute_jp2_embedded_only_source_exif();
    test_transfer_execute_heif_embedded_only_source_exif();
    test_transfer_execute_avif_embedded_only_source_exif();
    test_transfer_execute_jpeg_embedded_only_source_exif();
    test_transfer_execute_jpeg_sidecar_only_preserve_source_exif();
    test_transfer_execute_jpeg_sidecar_only_strip_source_exif();
    test_transfer_execute_jpeg_sidecar_only_strip();
    test_transfer_prepare_reports_unsupported_for_embedded_only_gif();
    test_transfer_execute_embedded_and_sidecar_uses_sidecar_policy_for_embedded_custom_namespace();
    test_transfer_execute_embedded_and_sidecar_preserves_structured_creator_contact_info();
    test_transfer_execute_embedded_and_sidecar_preserves_mixed_namespace_location_details();
    test_transfer_execute_embedded_and_sidecar_preserves_xmpmm_namespace();
    test_transfer_execute_embedded_and_sidecar_preserves_xmpmm_structured_resources();
    test_transfer_execute_embedded_and_sidecar_preserves_xmp_advisory_bag();
    test_transfer_execute_embedded_and_sidecar_preserves_dc_language_and_date();
    test_transfer_execute_embedded_and_sidecar_preserves_lr_hierarchical_subject();
    test_transfer_execute_embedded_and_sidecar_preserves_pdf_and_rights_namespaces();
    test_transfer_execute_embedded_and_sidecar_canonicalizes_xmprights_usage_terms();
    test_transfer_execute_embedded_and_sidecar_canonicalizes_location_child_shapes();
    return 0;
}
