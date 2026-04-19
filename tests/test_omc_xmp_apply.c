#include "omc/omc_read.h"
#include "omc/omc_xmp_apply.h"

#include <assert.h>
#include <string.h>

static omc_byte_ref
append_store_bytes(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, strlen(text), &ref);
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
append_u32le(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    out[*io_size + 0U] = (omc_u8)(value & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)((value >> 24) & 0xFFU);
    *io_size += 4U;
}

static void
append_raw(omc_u8* out, omc_size* io_size, const void* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
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
build_store_with_iptc_title_and_existing_xmp_title(omc_store* store)
{
    omc_entry entry;
    omc_status status;
    static const char k_title[] = "Generated Title";

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 5U);
    omc_val_make_bytes(
        &entry.value,
        append_store_bytes(&store->arena, k_title));
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://purl.org/dc/elements/1.1/"),
        append_store_bytes(&store->arena, "title[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "Default title"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://purl.org/dc/elements/1.1/"),
        append_store_bytes(&store->arena, "title[@xml:lang=fr-FR]"));
    omc_val_make_text(&entry.value,
                      append_store_bytes(&store->arena, "Titre localise"),
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
                       "DerivedFrom/stRef:instanceID", "xmp.iid:base");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom", "legacy-managed");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom/stRef:documentID", "xmp.did:managed");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom/stRef:instanceID", "xmp.iid:managed");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients", "legacy-ingredients");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients[1]/stRef:documentID",
                       "xmp.did:ingredient");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients[1]/stRef:instanceID",
                       "xmp.iid:ingredient");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf", "legacy-rendition");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf/stRef:documentID", "xmp.did:rendition");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf/stRef:filePath", "/tmp/rendition.jpg");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf/stRef:renditionClass", "proof:pdf");
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
                       "History[1]/stEvt:when", "2026-04-15T09:00:00Z");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions", "legacy-versions");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event", "legacy-event");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:version", "1.0");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:comments", "Initial import");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:modifier", "OpenMeta");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:modifyDate",
                       "2026-04-16T10:15:00Z");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event/stEvt:action", "saved");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event/stEvt:changed",
                       "/metadata");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event/stEvt:when",
                       "2026-04-16T10:15:00Z");
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
build_store_with_remaining_standard_grouped_scalars(omc_store* store)
{
    add_xmp_text_entry(store, "http://purl.org/dc/elements/1.1/",
                       "language", "en-US");
    add_xmp_text_entry(store, "http://purl.org/dc/elements/1.1/",
                       "contributor", "Alice");
    add_xmp_text_entry(store, "http://purl.org/dc/elements/1.1/",
                       "publisher", "OpenMeta Press");
    add_xmp_text_entry(store, "http://purl.org/dc/elements/1.1/",
                       "relation", "urn:related:test");
    add_xmp_text_entry(store, "http://purl.org/dc/elements/1.1/",
                       "type", "Image");
    add_xmp_text_entry(store, "http://purl.org/dc/elements/1.1/",
                       "date", "2026-04-15");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/",
                       "Identifier", "urn:om:test:id");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/",
                       "Advisory", "photoshop:City");
    add_xmp_text_entry(store, "http://ns.adobe.com/xap/1.0/rights/",
                       "Owner", "OpenMeta Labs");
    add_xmp_text_entry(store, "http://ns.adobe.com/lightroom/1.0/",
                       "hierarchicalSubject", "Places|Museum");
    add_xmp_text_entry(store, "http://ns.useplus.org/ldf/xmp/1.0/",
                       "ImageAlterationConstraints", "No compositing");
}

static void
build_store_with_creator_contact_info_deep_children(omc_store* store)
{
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
                       "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=x-default]",
                       "Tokyo Prefecture");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
                       "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=ja-JP]",
                       "東京都");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
                       "CreatorContactInfo/CiAdrExtadr[1]",
                       "Building A");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
                       "CreatorContactInfo/CiAdrExtadr[2]",
                       "Room 42");
}

static void
build_store_with_structured_iptc_entities(omc_store* store)
{
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject",
                       "legacy-artwork");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOTitle",
                       "legacy-title");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOTitle[@xml:lang=x-default]",
                       "Sunset Study");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOCreator",
                       "legacy-creator");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOCreator[1]",
                       "Alice Example");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOCreator[2]",
                       "Bob Example");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOStylePeriod",
                       "legacy-style");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOStylePeriod[1]",
                       "Impressionism");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ArtworkOrObject[1]/AOStylePeriod[2]",
                       "Modernism");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonInImageWDetails",
                       "legacy-person");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonInImageWDetails[1]/PersonName",
                       "legacy-person-name");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonInImageWDetails[1]/PersonName[@xml:lang=x-default]",
                       "Jane Doe");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonInImageWDetails[1]/PersonId",
                       "legacy-person-id");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonInImageWDetails[1]/PersonId[1]",
                       "person-001");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonInImageWDetails[1]/PersonId[2]",
                       "person-002");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "AboutCvTerm[1]/CvTermName",
                       "Culture");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ProductInImage",
                       "legacy-product");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ProductInImage[1]/ProductName",
                       "legacy-product-name");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ProductInImage[1]/ProductName[@xml:lang=x-default]",
                       "Camera Body");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ProductInImage[1]/ProductDescription",
                       "legacy-product-desc");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ProductInImage[1]/ProductDescription[@xml:lang=x-default]",
                       "Mirrorless");
}

static void
build_store_with_remaining_iptc_structured_entities(omc_store* store)
{
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "Contributor",
                       "legacy-contributor-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "Contributor[1]/Name[@xml:lang=x-default]",
                       "Desk Editor");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "Contributor[1]/Role[1]",
                       "editor");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PlanningRef",
                       "legacy-planning-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PlanningRef[1]/Name[@xml:lang=x-default]",
                       "Editorial Plan");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PlanningRef[1]/Role[1]",
                       "assignment");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonHeard",
                       "legacy-person-heard-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "PersonHeard[1]/Name[@xml:lang=x-default]",
                       "Witness");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ShownEvent",
                       "legacy-shown-event-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "ShownEvent[1]/Name[@xml:lang=x-default]",
                       "Press Conference");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "SupplyChainSource",
                       "legacy-supply-chain-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "SupplyChainSource[1]/Name[@xml:lang=x-default]",
                       "Agency Feed");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "VideoShotType",
                       "legacy-video-shot-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "VideoShotType[1]/Name[@xml:lang=x-default]",
                       "Interview");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "DopesheetLink",
                       "legacy-dopesheet-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "DopesheetLink[1]/LinkQualifier[1]",
                       "keyframe");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "Snapshot",
                       "legacy-snapshot-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "Snapshot[1]/LinkQualifier[1]",
                       "frame-001");

    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "TranscriptLink",
                       "legacy-transcript-base");
    add_xmp_text_entry(store,
                       "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "TranscriptLink[1]/LinkQualifier[1]",
                       "quote");
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

static void
test_xmp_apply_jpeg_sidecar_only_preserves_existing_embedded_xmp(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "OldTool");
    assert(count_xmp_entries(&edited_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_jpeg_sidecar_only_strips_existing_embedded_xmp(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 0U);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(find_xmp_entry(&edited_store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool")
           == (const omc_entry*)0);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_jpeg_embedded_and_sidecar_writes_both(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.embedded.packet.include_existing_xmp = 1;
    opts.embedded.packet.include_exif = 0;
    opts.embedded.packet.include_iptc = 0;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 1U);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&edited_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_tiff_sidecar_only_strips_existing_embedded_xmp(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_tiff_with_old_xmp_and_make(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 0U);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(find_xmp_entry(&edited_store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool")
           == (const omc_entry*)0);
    assert_text_value(&edited_store, find_exif_entry(&edited_store, "ifd0",
                                                     0x010FU),
                      "Canon");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_uses_sidecar_policy_for_embedded_custom_namespace(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_custom_flag(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;
    opts.sidecar.portable_existing_namespace_policy =
        OMC_XMP_NS_PRESERVE_CUSTOM;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_uses_sidecar_policy_for_embedded_canonicalize_managed(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_iptc_title_and_existing_xmp_title(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 1;
    opts.sidecar.include_existing_xmp = 1;
    opts.sidecar.portable_conflict_policy = OMC_XMP_CONFLICT_EXISTING_WINS;
    opts.sidecar.portable_existing_standard_namespace_policy =
        OMC_XMP_STD_NS_CANONICALIZE_MANAGED;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"fr-FR\">Titre localise</rdf:li>"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "<rdf:li xml:lang=\"x-default\">Default title</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"fr-FR\">Titre localise</rdf:li>"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "<rdf:li xml:lang=\"x-default\">Default title</rdf:li>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_structured_creator_contact_info(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_contact_info(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_mixed_namespace_location_details(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_mixed_location_shown_details(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_xmpmm_namespace(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_xmpmm_namespace(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_xmpmm_structured_resources(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_xmpmm_structured_resources(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
    assert(contains_text(edited_out.data, edited_out.size,
                         "xmlns:stRef=\"http://ns.adobe.com/xap/1.0/sType/ResourceRef#\""));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:DerivedFrom rdf:parseType=\"Resource\">"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "xmp.iid:base</stRef:instanceID>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:ManagedFrom rdf:parseType=\"Resource\">"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "xmp.iid:managed</stRef:instanceID>"));
    assert(contains_text(edited_out.data, edited_out.size, "<xmpMM:Ingredients>"));
    assert(contains_text(edited_out.data, edited_out.size, "<rdf:Bag>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "xmp.iid:ingredient</stRef:instanceID>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:RenditionOf rdf:parseType=\"Resource\">"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "xmp.did:rendition</stRef:documentID>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "/tmp/rendition.jpg</stRef:filePath>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "proof:pdf</stRef:renditionClass>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<stMfs:reference"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "rdf:parseType=\"Resource\""));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpMM:History>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "2026-04-15T09:00:00Z</stEvt:when>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<stVer:event"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "Initial import</stVer:comments>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "OpenMeta</stVer:modifier>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "2026-04-16T10:15:00Z</stVer:modifyDate>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "/metadata</stEvt:changed>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "2026-04-16T10:15:00Z</stEvt:when>"));
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
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "xmp.iid:base</stRef:instanceID>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpMM:ManagedFrom rdf:parseType=\"Resource\">"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "xmp.iid:managed</stRef:instanceID>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<xmpMM:Ingredients>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<rdf:Bag>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "xmp.iid:ingredient</stRef:instanceID>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpMM:RenditionOf rdf:parseType=\"Resource\">"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "xmp.did:rendition</stRef:documentID>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "/tmp/rendition.jpg</stRef:filePath>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "proof:pdf</stRef:renditionClass>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<stMfs:reference"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "rdf:parseType=\"Resource\""));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpMM:History>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "2026-04-15T09:00:00Z</stEvt:when>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<stVer:event"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "Initial import</stVer:comments>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "OpenMeta</stVer:modifier>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "2026-04-16T10:15:00Z</stVer:modifyDate>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "/metadata</stEvt:changed>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "2026-04-16T10:15:00Z</stEvt:when>"));
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_xmp_advisory_bag(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_advisory_bag(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_dc_language_and_date(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_language_and_date(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_lr_hierarchical_subject(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_lr_hierarchical_subject(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_remaining_standard_grouped_scalars(
    void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_remaining_standard_grouped_scalars(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
    assert(contains_text(edited_out.data, edited_out.size, "<dc:language>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>en-US</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<dc:contributor>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Alice</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<dc:publisher>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>OpenMeta Press</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size, "<dc:relation>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>urn:related:test</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size, "<dc:type>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Image</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size, "<dc:date>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>2026-04-15</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmp:Identifier>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>urn:om:test:id</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size, "<xmp:Advisory>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>photoshop:City</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<xmpRights:Owner>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>OpenMeta Labs</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<lr:hierarchicalSubject>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Places|Museum</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<plus:ImageAlterationConstraints>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>No compositing</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<dc:language>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>en-US</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<dc:contributor>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Alice</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<dc:publisher>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>OpenMeta Press</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<dc:relation>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>urn:related:test</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<dc:type>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Image</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size, "<dc:date>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>2026-04-15</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmp:Identifier>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>urn:om:test:id</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmp:Advisory>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>photoshop:City</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<xmpRights:Owner>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>OpenMeta Labs</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<lr:hierarchicalSubject>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Places|Museum</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<plus:ImageAlterationConstraints>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>No compositing</rdf:li>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_pdf_and_rights_namespaces(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_pdf_and_rights_namespaces(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_canonicalizes_xmprights_usage_terms(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_rights_canonicalized(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 1;
    opts.sidecar.include_existing_xmp = 1;
    opts.sidecar.portable_conflict_policy = OMC_XMP_CONFLICT_GENERATED_WINS;
    opts.sidecar.portable_existing_standard_namespace_policy =
        OMC_XMP_STD_NS_CANONICALIZE_MANAGED;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_canonicalizes_location_child_shapes(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_location_child_shapes(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
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
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_creator_contact_info_deep_children(
    void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_contact_info_deep_children(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
    assert(contains_text(
        edited_out.data, edited_out.size,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(
        edited_out.data, edited_out.size,
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Tokyo Prefecture</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"ja-JP\">東京都</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<Iptc4xmpCore:CiAdrExtadr>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Building A</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Room 42</rdf:li>"));
    assert(contains_text(
        sidecar_out.data, sidecar_out.size,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(
        sidecar_out.data, sidecar_out.size,
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Tokyo Prefecture</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"ja-JP\">東京都</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<Iptc4xmpCore:CiAdrExtadr>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Building A</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Room 42</rdf:li>"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_structured_iptc_entities(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_structured_iptc_entities(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<Iptc4xmpExt:ArtworkOrObject>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Sunset Study</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Alice Example</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>Impressionism</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<Iptc4xmpExt:PersonInImageWDetails>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Jane Doe</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>person-001</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<Iptc4xmpExt:AboutCvTerm>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Culture</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<Iptc4xmpExt:ProductInImage>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Camera Body</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Mirrorless</rdf:li>"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-artwork"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-person"));
    assert(!contains_text(edited_out.data, edited_out.size, "legacy-product"));

    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<Iptc4xmpExt:ArtworkOrObject>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Sunset Study</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Alice Example</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>Impressionism</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<Iptc4xmpExt:PersonInImageWDetails>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Jane Doe</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>person-001</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<Iptc4xmpExt:AboutCvTerm>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Culture</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<Iptc4xmpExt:ProductInImage>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Camera Body</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Mirrorless</rdf:li>"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-artwork"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-person"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size, "legacy-product"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_embedded_and_sidecar_preserves_remaining_iptc_structured_entities(
    void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_remaining_iptc_structured_entities(&source_store);

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.sidecar.include_exif = 0;
    opts.sidecar.include_iptc = 0;
    opts.sidecar.include_existing_xmp = 1;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Desk Editor</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>editor</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Editorial Plan</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>assignment</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Witness</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Press Conference</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Agency Feed</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li xml:lang=\"x-default\">Interview</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>keyframe</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>frame-001</rdf:li>"));
    assert(contains_text(edited_out.data, edited_out.size,
                         "<rdf:li>quote</rdf:li>"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-contributor-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-planning-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-person-heard-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-shown-event-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-supply-chain-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-video-shot-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-dopesheet-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-snapshot-base"));
    assert(!contains_text(edited_out.data, edited_out.size,
                          "legacy-transcript-base"));

    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Desk Editor</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>editor</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Editorial Plan</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>assignment</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Witness</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Press Conference</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Agency Feed</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li xml:lang=\"x-default\">Interview</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>keyframe</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>frame-001</rdf:li>"));
    assert(contains_text(sidecar_out.data, sidecar_out.size,
                         "<rdf:li>quote</rdf:li>"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-contributor-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-planning-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-person-heard-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-shown-event-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-supply-chain-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-video-shot-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-dopesheet-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-snapshot-base"));
    assert(!contains_text(sidecar_out.data, sidecar_out.size,
                          "legacy-transcript-base"));

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

int
main(void)
{
    test_xmp_apply_jpeg_sidecar_only_preserves_existing_embedded_xmp();
    test_xmp_apply_jpeg_sidecar_only_strips_existing_embedded_xmp();
    test_xmp_apply_jpeg_embedded_and_sidecar_writes_both();
    test_xmp_apply_tiff_sidecar_only_strips_existing_embedded_xmp();
    test_xmp_apply_embedded_and_sidecar_uses_sidecar_policy_for_embedded_custom_namespace();
    test_xmp_apply_embedded_and_sidecar_uses_sidecar_policy_for_embedded_canonicalize_managed();
    test_xmp_apply_embedded_and_sidecar_preserves_structured_creator_contact_info();
    test_xmp_apply_embedded_and_sidecar_preserves_mixed_namespace_location_details();
    test_xmp_apply_embedded_and_sidecar_preserves_xmpmm_namespace();
    test_xmp_apply_embedded_and_sidecar_preserves_xmpmm_structured_resources();
    test_xmp_apply_embedded_and_sidecar_preserves_xmp_advisory_bag();
    test_xmp_apply_embedded_and_sidecar_preserves_dc_language_and_date();
    test_xmp_apply_embedded_and_sidecar_preserves_lr_hierarchical_subject();
    test_xmp_apply_embedded_and_sidecar_preserves_remaining_standard_grouped_scalars();
    test_xmp_apply_embedded_and_sidecar_preserves_pdf_and_rights_namespaces();
    test_xmp_apply_embedded_and_sidecar_canonicalizes_xmprights_usage_terms();
    test_xmp_apply_embedded_and_sidecar_canonicalizes_location_child_shapes();
    test_xmp_apply_embedded_and_sidecar_preserves_creator_contact_info_deep_children();
    test_xmp_apply_embedded_and_sidecar_preserves_structured_iptc_entities();
    test_xmp_apply_embedded_and_sidecar_preserves_remaining_iptc_structured_entities();
    return 0;
}
