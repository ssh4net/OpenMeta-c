#include "omc/omc_edit.h"
#include "omc/omc_xmp_dump.h"

#include <assert.h>
#include <string.h>

static omc_byte_ref
append_bytes(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, strlen(text), &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static omc_byte_ref
append_raw(omc_arena* arena, const omc_u8* bytes, omc_size size)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, bytes, size, &ref);
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
add_xmp_text_entry(omc_store* store, const char* schema_ns,
                   const char* property_path, const char* value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_bytes(&store->arena, schema_ns),
                              append_bytes(&store->arena, property_path));
    omc_val_make_text(&entry.value, append_bytes(&store->arena, value),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static void
test_sidecar_truncation_and_success(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref name_ref;
    omc_byte_ref value_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 small[16];
    omc_u8 full[512];

    omc_store_init(&store);
    schema_ref = append_bytes(&store.arena, "http://ns.adobe.com/xap/1.0/");
    name_ref = append_bytes(&store.arena, "CreatorTool");
    value_ref = append_bytes(&store.arena, "OpenMeta-c");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, name_ref);
    omc_val_make_text(&entry.value, value_ref, OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);

    status = omc_xmp_dump_sidecar(&store, small, sizeof(small), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_TRUNCATED);
    assert(res.needed > sizeof(small));
    assert(res.entries == 1U);

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(res.entries == 1U);
    assert(contains_text(full, (omc_size)res.written, "<x:xmpmeta"));
    assert(contains_text(full, (omc_size)res.written,
                         "<xmp:CreatorTool>OpenMeta-c</xmp:CreatorTool>"));

    omc_store_fini(&store);
}

static void
test_sidecar_uses_edit_commit_output(void)
{
    omc_store base;
    omc_store merged;
    omc_edit edit;
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref name_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[512];

    omc_store_init(&base);
    omc_store_init(&merged);
    omc_edit_init(&edit);

    schema_ref = append_bytes(&edit.arena, "urn:test:writeback/");
    name_ref = append_bytes(&edit.arena, "Thing");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, name_ref);
    omc_val_make_u32(&entry.value, 42U);
    status = omc_edit_add_entry(&edit, &entry);
    assert(status == OMC_STATUS_OK);

    status = omc_edit_commit(&base, &edit, 1U, &merged);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.existing_namespace_policy = OMC_XMP_NS_PRESERVE_CUSTOM;
    status = omc_xmp_dump_sidecar(&merged, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "xmlns:ns1=\"urn:test:writeback/\""));
    assert(contains_text(full, (omc_size)res.written,
                         "<ns1:Thing>42</ns1:Thing>"));

    omc_edit_fini(&edit);
    omc_store_fini(&merged);
    omc_store_fini(&base);
}

static void
test_sidecar_limits_and_skips_unsupported_properties(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref first_name_ref;
    omc_byte_ref second_name_ref;
    omc_byte_ref invalid_name_ref;
    omc_byte_ref text_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[512];

    omc_store_init(&store);
    schema_ref = append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/");
    first_name_ref = append_bytes(&store.arena, "title");
    second_name_ref = append_bytes(&store.arena, "creator");
    invalid_name_ref = append_bytes(&store.arena, "bad/name");
    text_ref = append_bytes(&store.arena, "hello");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, invalid_name_ref);
    omc_val_make_text(&entry.value, text_ref, OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, first_name_ref);
    omc_val_make_text(&entry.value, text_ref, OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, second_name_ref);
    omc_val_make_u16(&entry.value, 5U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.limits.max_entries = 1U;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_LIMIT);
    assert(res.entries == 1U);
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<rdf:li xml:lang=\"x-default\">hello</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written, "bad[1]"));
    assert(!contains_text(full, (omc_size)res.written, "<dc:creator>5</dc:creator>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_normalizes_exif_dates(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[1024];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x0132U);
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "2010-11-14 16:25:16 UTC"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9003U);
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "2010:11:14 16:25:16"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0xC71BU);
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "2010:11:14 16:25:16+0900"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:DateTime>2010-11-14T16:25:16Z</tiff:DateTime>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<exif:DateTimeOriginal>2010-11-14T16:25:16</exif:DateTimeOriginal>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<tiff:PreviewDateTime>2010-11-14T16:25:16+09:00</tiff:PreviewDateTime>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_decodes_windows_xp_text(void)
{
    static const omc_u8 xp_keywords[] = {
        'a', 0U, 'l', 0U, 'p', 0U, 'h', 0U, 'a', 0U, ';', 0U,
        'b', 0U, 'e', 0U, 't', 0U, 'a', 0U, 0U, 0U
    };
    static const omc_u8 xp_subject[] = {
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[1024];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x9C9EU);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_U8;
    entry.value.count = (omc_u32)sizeof(xp_keywords);
    entry.value.u.ref = append_raw(&store.arena, xp_keywords,
                                   (omc_size)sizeof(xp_keywords));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x9C9FU);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_U8;
    entry.value.count = (omc_u32)sizeof(xp_subject);
    entry.value.u.ref = append_raw(&store.arena, xp_subject,
                                   (omc_size)sizeof(xp_subject));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:XPKeywords>alpha;beta</tiff:XPKeywords>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:XPSubject></tiff:XPSubject>"));
    assert(!contains_text(full, (omc_size)res.written, "<rdf:Seq>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_exif_and_iptc_projection(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref ifd0_ref;
    omc_byte_ref exififd_ref;
    omc_byte_ref city_ref;
    omc_byte_ref country_code_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[1024];

    omc_store_init(&store);
    ifd0_ref = append_bytes(&store.arena, "ifd0");
    exififd_ref = append_bytes(&store.arena, "exififd");
    city_ref = append_bytes(&store.arena, "Paris");
    country_code_ref = append_bytes(&store.arena, "FR");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd0_ref, 0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, exififd_ref, 0x829AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 1U;
    entry.value.u.ur.denom = 125U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 90U);
    omc_val_make_bytes(&entry.value, city_ref);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 101U);
    omc_val_make_bytes(&entry.value, country_code_ref);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 1;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:Make>Canon</tiff:Make>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ExposureTime>1/125</exif:ExposureTime>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<photoshop:City>Paris</photoshop:City>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpCore:CountryCode>FR</Iptc4xmpCore:CountryCode>"));

    omc_store_fini(&store);
}

static void
test_sidecar_arena_auto_grows_and_existing_wins(void)
{
    omc_store store;
    omc_arena out;
    omc_entry entry;
    omc_byte_ref ifd0_ref;
    omc_byte_ref ns_ref;
    omc_byte_ref name_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_const_bytes view;

    omc_store_init(&store);
    omc_arena_init(&out);

    ifd0_ref = append_bytes(&store.arena, "ifd0");
    ns_ref = append_bytes(&store.arena, "http://ns.adobe.com/tiff/1.0/");
    name_ref = append_bytes(&store.arena, "Make");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd0_ref, 0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, ns_ref, name_ref);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Nikon"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 1;
    opts.include_existing_xmp = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar_arena(&store, &out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(out.size == (omc_size)res.written);

    view = omc_arena_view(&out, (omc_byte_ref){ 0U, (omc_u32)out.size });
    assert(view.data != NULL);
    assert(contains_text(view.data, view.size, "<tiff:Make>Nikon</tiff:Make>"));
    assert(!contains_text(view.data, view.size, "<tiff:Make>Canon</tiff:Make>"));

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_sidecar_portable_groups_repeated_iptc_properties(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 5U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "Sunset"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 120U);
    omc_val_make_bytes(&entry.value,
                       append_bytes(&store.arena, "Golden hour over lake"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 80U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "Alice"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 80U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "Bob"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 25U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "nature"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 25U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "sunset"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 20U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "Travel"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 20U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "Museum"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 0;
    opts.include_iptc = 1;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Sunset</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written, "<dc:description>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Golden hour over lake</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written, "<dc:creator>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Seq>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:li>Alice</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:li>Bob</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written, "<dc:subject>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>nature</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>sunset</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<photoshop:SupplementalCategories>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>Travel</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>Museum</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_conflict_policies(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref ifd0_ref;
    omc_byte_ref ns_tiff_ref;
    omc_byte_ref ns_dc_ref;
    omc_byte_ref name_make_ref;
    omc_byte_ref name_title_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    ifd0_ref = append_bytes(&store.arena, "ifd0");
    ns_tiff_ref = append_bytes(&store.arena, "http://ns.adobe.com/tiff/1.0/");
    ns_dc_ref = append_bytes(&store.arena,
                             "http://purl.org/dc/elements/1.1/");
    name_make_ref = append_bytes(&store.arena, "Make");
    name_title_ref = append_bytes(&store.arena, "title");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd0_ref, 0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, ns_tiff_ref, name_make_ref);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Nikon"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 5U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "IPTC"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, ns_dc_ref, name_title_ref);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "XMP"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 1;
    opts.include_iptc = 1;

    opts.conflict_policy = OMC_XMP_CONFLICT_EXISTING_WINS;
    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:Make>Nikon</tiff:Make>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<tiff:Make>Canon</tiff:Make>"));
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">XMP</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<dc:title>IPTC</dc:title>"));

    opts.conflict_policy = OMC_XMP_CONFLICT_CURRENT_BEHAVIOR;
    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:Make>Canon</tiff:Make>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<tiff:Make>Nikon</tiff:Make>"));
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">XMP</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<dc:title>IPTC</dc:title>"));

    opts.conflict_policy = OMC_XMP_CONFLICT_GENERATED_WINS;
    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:Make>Canon</tiff:Make>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<tiff:Make>Nikon</tiff:Make>"));
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">IPTC</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<rdf:li xml:lang=\"x-default\">XMP</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_generated_iptc_overrides_existing_indexed_xmp(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref ns_dc_ref;
    omc_byte_ref name_subject_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    ns_dc_ref = append_bytes(&store.arena,
                             "http://purl.org/dc/elements/1.1/");
    name_subject_ref = append_bytes(&store.arena, "subject[1]");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 25U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena,
                                                  "iptc-keyword"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, ns_dc_ref, name_subject_ref);
    omc_val_make_text(&entry.value, append_bytes(&store.arena,
                                                 "xmp-keyword"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 1;
    opts.conflict_policy = OMC_XMP_CONFLICT_GENERATED_WINS;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written, "<dc:subject>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>iptc-keyword</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<rdf:li>xmp-keyword</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_indexed_xmp_emits_grouped_array(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[1024];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key,
                              append_bytes(&store.arena,
                                           "http://purl.org/dc/elements/1.1/"),
                              append_bytes(&store.arena, "subject[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "xmp-a"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key,
                              append_bytes(&store.arena,
                                           "http://purl.org/dc/elements/1.1/"),
                              append_bytes(&store.arena, "subject[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "xmp-b"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(res.entries == 1U);
    assert(contains_text(full, (omc_size)res.written, "<dc:subject>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:li>xmp-a</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:li>xmp-b</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_canonicalizes_existing_xmp_property_names(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/tiff/1.0/"),
        append_bytes(&store.arena, "ImageLength"));
    omc_val_make_u32(&entry.value, 3456U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/exif/1.0/"),
        append_bytes(&store.arena, "ExposureBiasValue"));
    omc_val_make_u16(&entry.value, 0U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/exif/1.0/"),
        append_bytes(&store.arena, "ISOSpeedRatings"));
    omc_val_make_u16(&entry.value, 400U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/exif/1.0/"),
        append_bytes(&store.arena, "PixelXDimension"));
    omc_val_make_u16(&entry.value, 6000U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/exif/1.0/"),
        append_bytes(&store.arena, "PixelYDimension"));
    omc_val_make_u16(&entry.value, 4000U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/exif/1.0/"),
        append_bytes(&store.arena, "FocalLengthIn35mmFilm"));
    omc_val_make_u16(&entry.value, 50U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/exif/1.0/"),
        append_bytes(&store.arena, "MakerNote"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "AAAA"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/tiff/1.0/"),
        append_bytes(&store.arena, "DNGPrivateData"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "BBBB"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(res.entries == 6U);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:ImageHeight>3456</tiff:ImageHeight>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ExposureCompensation>0</exif:ExposureCompensation>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ISO>400</exif:ISO>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ExifImageWidth>6000</exif:ExifImageWidth>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ExifImageHeight>4000</exif:ExifImageHeight>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<exif:FocalLengthIn35mmFormat>50</exif:FocalLengthIn35mmFormat>"));
    assert(!contains_text(full, (omc_size)res.written, "<tiff:ImageLength>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<exif:ExposureBiasValue>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<exif:ISOSpeedRatings>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<exif:PixelXDimension>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<exif:PixelYDimension>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<exif:FocalLengthIn35mmFilm>"));
    assert(!contains_text(full, (omc_size)res.written, "<exif:MakerNote>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<tiff:DNGPrivateData>"));

    omc_store_fini(&store);
}

static void
test_sidecar_known_portable_only_skips_custom_existing_xmp(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[1024];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key, append_bytes(&store.arena, "urn:test:writeback/"),
        append_bytes(&store.arena, "Thing"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "42"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(!contains_text(full, (omc_size)res.written, "urn:test:writeback/"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<ns1:Thing>42</ns1:Thing>"));

    opts.existing_namespace_policy = OMC_XMP_NS_PRESERVE_CUSTOM;
    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "xmlns:ns1=\"urn:test:writeback/\""));
    assert(contains_text(full, (omc_size)res.written,
                         "<ns1:Thing>42</ns1:Thing>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_lang_alt_emits_grouped_alt(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/"),
        append_bytes(&store.arena, "title[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Legacy Title"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/"),
        append_bytes(&store.arena, "title[@xml:lang=fr]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Titre"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(res.entries == 1U);
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Alt>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Legacy Title</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"fr\">Titre</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_promotes_flat_standard_scalars_to_canonical_shapes(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/"),
        append_bytes(&store.arena, "title"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Legacy Title"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/"),
        append_bytes(&store.arena, "subject"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-subject"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/xap/1.0/rights/"),
        append_bytes(&store.arena, "Owner"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "OpenMeta Labs"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/lightroom/1.0/"),
        append_bytes(&store.arena, "hierarchicalSubject"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Places|Museum"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Legacy Title</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<dc:title>Legacy Title</dc:title>"));
    assert(contains_text(full, (omc_size)res.written, "<dc:subject>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>legacy-subject</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written, "<xmpRights:Owner>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>OpenMeta Labs</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<lr:hierarchicalSubject>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>Places|Museum</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_lr_hierarchical_subject_emits_bag(void)
{
    omc_store store;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[1024];

    omc_store_init(&store);
    add_xmp_text_entry(&store, "http://ns.adobe.com/lightroom/1.0/",
                       "hierarchicalSubject[1]", "Places|Japan|Tokyo");
    add_xmp_text_entry(&store, "http://ns.adobe.com/lightroom/1.0/",
                       "hierarchicalSubject[2]", "Travel|Spring");

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written,
                         "<lr:hierarchicalSubject>"));
    assert(contains_text(out, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Places|Japan|Tokyo</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Travel|Spring</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_preserves_remaining_standard_grouped_scalars(void)
{
    omc_store store;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[4096];

    omc_store_init(&store);
    add_xmp_text_entry(&store, "http://purl.org/dc/elements/1.1/",
                       "language", "en-US");
    add_xmp_text_entry(&store, "http://purl.org/dc/elements/1.1/",
                       "contributor", "Alice");
    add_xmp_text_entry(&store, "http://purl.org/dc/elements/1.1/",
                       "publisher", "OpenMeta Press");
    add_xmp_text_entry(&store, "http://purl.org/dc/elements/1.1/",
                       "relation", "urn:related:test");
    add_xmp_text_entry(&store, "http://purl.org/dc/elements/1.1/",
                       "type", "Image");
    add_xmp_text_entry(&store, "http://purl.org/dc/elements/1.1/",
                       "date", "2026-04-15");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/",
                       "Identifier", "urn:om:test:id");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/",
                       "Advisory", "photoshop:City");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/rights/",
                       "Owner", "OpenMeta Labs");
    add_xmp_text_entry(&store, "http://ns.adobe.com/lightroom/1.0/",
                       "hierarchicalSubject", "Places|Museum");
    add_xmp_text_entry(&store, "http://ns.useplus.org/ldf/xmp/1.0/",
                       "ImageAlterationConstraints", "No compositing");

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);

    assert(contains_text(out, (omc_size)res.written, "<dc:language>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>en-US</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<dc:contributor>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Alice</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<dc:publisher>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>OpenMeta Press</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<dc:relation>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>urn:related:test</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<dc:type>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Image</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<dc:date>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>2026-04-15</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<xmp:Identifier>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>urn:om:test:id</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<xmp:Advisory>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>photoshop:City</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<xmpRights:Owner>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>OpenMeta Labs</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<lr:hierarchicalSubject>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Places|Museum</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<plus:ImageAlterationConstraints>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>No compositing</rdf:li>"));

    assert(!contains_text(out, (omc_size)res.written,
                          "<dc:language>en-US</dc:language>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<dc:contributor>Alice</dc:contributor>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<dc:publisher>OpenMeta Press</dc:publisher>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<dc:relation>urn:related:test</dc:relation>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<dc:type>Image</dc:type>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<dc:date>2026-04-15</dc:date>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmp:Identifier>urn:om:test:id</xmp:Identifier>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmp:Advisory>photoshop:City</xmp:Advisory>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpRights:Owner>OpenMeta Labs</xmpRights:Owner>"));
    assert(!contains_text(
        out, (omc_size)res.written,
        "<lr:hierarchicalSubject>Places|Museum</lr:hierarchicalSubject>"));
    assert(!contains_text(
        out, (omc_size)res.written,
        "<plus:ImageAlterationConstraints>No compositing</plus:ImageAlterationConstraints>"));

    omc_store_fini(&store);
}

static void
test_sidecar_emits_pdf_and_rights_namespaces(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key, append_bytes(&store.arena, "http://ns.adobe.com/pdf/1.3/"),
        append_bytes(&store.arena, "Keywords"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "tokyo,night"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/xap/1.0/rights/"),
        append_bytes(&store.arena, "Marked"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "True"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\""));
    assert(contains_text(full, (omc_size)res.written,
                         "xmlns:xmpRights=\"http://ns.adobe.com/xap/1.0/rights/\""));
    assert(contains_text(full, (omc_size)res.written,
                         "<pdf:Keywords>tokyo,night</pdf:Keywords>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<xmpRights:Marked>True</xmpRights:Marked>"));

    omc_store_fini(&store);
}

static void
test_sidecar_canonicalize_managed_prefers_generated_portable_value(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 5U);
    omc_val_make_bytes(&entry.value, append_bytes(&store.arena, "IPTC Title"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/"),
        append_bytes(&store.arena, "title"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Legacy Title"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 1;
    opts.conflict_policy = OMC_XMP_CONFLICT_EXISTING_WINS;
    opts.existing_standard_namespace_policy
        = OMC_XMP_STD_NS_CANONICALIZE_MANAGED;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Alt>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">IPTC Title</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written, "Legacy Title"));

    omc_store_fini(&store);
}

static void
test_sidecar_canonicalize_managed_replaces_xdefault_and_preserves_other_locales(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 5U);
    omc_val_make_bytes(&entry.value,
                       append_bytes(&store.arena, "Generated Title"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/"),
        append_bytes(&store.arena, "title[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Default title"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/"),
        append_bytes(&store.arena, "title[@xml:lang=fr-FR]"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "Titre localise"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 1;
    opts.conflict_policy = OMC_XMP_CONFLICT_EXISTING_WINS;
    opts.existing_standard_namespace_policy
        = OMC_XMP_STD_NS_CANONICALIZE_MANAGED;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written, "<dc:title>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Alt>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"fr-FR\">Titre localise</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<rdf:li xml:lang=\"x-default\">Default title</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_preserves_xmprights_standard_namespace(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 116U);
    omc_val_make_bytes(&entry.value,
                       append_bytes(&store.arena, "Generated copyright"));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://ns.adobe.com/xap/1.0/rights/"),
        append_bytes(&store.arena, "UsageTerms[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "Licensed use only"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://ns.adobe.com/xap/1.0/rights/"),
        append_bytes(&store.arena, "WebStatement"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena,
                                   "https://example.test/license"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 1;
    opts.conflict_policy = OMC_XMP_CONFLICT_GENERATED_WINS;
    opts.existing_standard_namespace_policy
        = OMC_XMP_STD_NS_CANONICALIZE_MANAGED;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "xmlns:xmpRights=\"http://ns.adobe.com/xap/1.0/rights/\""));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Generated copyright</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Licensed use only</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<xmpRights:WebStatement>https://example.test/license</xmpRights:WebStatement>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_core_emits_resource(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena, "CreatorContactInfo/CiEmailWork"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "editor@example.test"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena, "CreatorContactInfo/CiUrlWork"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "https://example.test/contact"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(
        full, (omc_size)res.written,
        "xmlns:Iptc4xmpCore=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\""));
    assert(contains_text(
        full, (omc_size)res.written,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<Iptc4xmpCore:CiUrlWork>https://example.test/contact</Iptc4xmpCore:CiUrlWork>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_indexed_resources_emit_seq(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/City"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Paris"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/CountryName"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "France"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[2]/City"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Kyoto"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(
        full, (omc_size)res.written,
        "xmlns:Iptc4xmpExt=\"http://iptc.org/std/Iptc4xmpExt/2008-02-29/\""));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:LocationShown>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Seq>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li rdf:parseType=\"Resource\">"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_canonicalizes_flat_bases(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena, "CreatorContactInfo"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-flat-contact"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena, "CreatorContactInfo/CiEmailWork"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "editor@example.test"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown/City"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "LegacyParis"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/City"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Paris"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key, append_bytes(&store.arena,
                                 "http://ns.useplus.org/ldf/xmp/1.0/"),
        append_bytes(&store.arena, "Licensee"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-licensee"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key, append_bytes(&store.arena,
                                 "http://ns.useplus.org/ldf/xmp/1.0/"),
        append_bytes(&store.arena, "Licensee[1]/LicenseeName"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Example Archive"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(
        full, (omc_size)res.written,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:LocationShown>"));
    assert(contains_text(full, (omc_size)res.written, "<plus:Licensee>"));
    assert(!contains_text(full, (omc_size)res.written, "legacy-flat-contact"));
    assert(!contains_text(full, (omc_size)res.written, "LegacyParis"));
    assert(!contains_text(full, (omc_size)res.written, "legacy-licensee"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_nested_and_lang_alt(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena,
                     "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Tokyo"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena,
                     "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=ja-JP]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "東京"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(
        full, (omc_size)res.written,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpCore:ProvinceName>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Alt>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"ja-JP\">東京</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_child_indexed_seq(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena, "CreatorContactInfo/CiAdrExtadr"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-line"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena, "CreatorContactInfo/CiAdrExtadr[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Line 1"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"),
        append_bytes(&store.arena, "CreatorContactInfo/CiAdrExtadr[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Line 2"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(
        full, (omc_size)res.written,
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpCore:CiAdrExtadr>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Seq>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>Line 1</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>Line 2</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written, "legacy-line"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_mixed_namespace_children(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/xmp:Identifier[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "loc-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/xmp:Identifier[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "loc-002"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/exif:GPSLatitude"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "41,24.5N"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/exif:GPSLongitude"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "2,9E"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:LocationShown>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<xmp:Identifier xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>loc-001</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>loc-002</rdf:li>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<exif:GPSLatitude xmlns:exif=\"http://ns.adobe.com/exif/1.0/\">41,24.5N</exif:GPSLatitude>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<exif:GPSLongitude xmlns:exif=\"http://ns.adobe.com/exif/1.0/\">2,9E</exif:GPSLongitude>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_location_child_shapes(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/LocationName"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-name"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena,
                     "LocationShown[1]/LocationName[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Kyoto"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena,
                     "LocationShown[1]/LocationName[@xml:lang=fr-FR]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Kyoto FR"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/LocationId"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-id"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/LocationId[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "loc-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena,
                     "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "LocationShown[1]/LocationId[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "loc-002"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 1;
    opts.include_exif = 0;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:LocationShown>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:LocationName>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Alt>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li xml:lang=\"fr-FR\">Kyoto FR</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<Iptc4xmpExt:LocationId>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>loc-001</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<rdf:li>loc-002</rdf:li>"));
    assert(!contains_text(full, (omc_size)res.written, "legacy-name"));
    assert(!contains_text(full, (omc_size)res.written, "legacy-id"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_creator_shapes(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-creator"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Name"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-name"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Name[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Alice Example"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Name[@xml:lang=ja-JP]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "\xE3\x82\xA2\xE3\x83\xAA\xE3\x82\xB9"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Role"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-role"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Role[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "photographer"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Role[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "editor"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 0;
    opts.include_iptc = 0;
    opts.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written, "<Iptc4xmpExt:Creator>"));
    assert(contains_text(out, (omc_size)res.written, "<Iptc4xmpExt:Name>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Alice Example</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"ja-JP\">\xE3\x82\xA2\xE3\x83\xAA\xE3\x82\xB9</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<Iptc4xmpExt:Role>"));
    assert(contains_text(out, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>photographer</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>editor</rdf:li>"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-creator"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-name"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-role"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_artwork_shapes(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-artwork"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOTitle"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-title"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOTitle[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Sunset Study"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOCreator"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-creator"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOCreator[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Alice Example"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOCreator[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Bob Example"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOStylePeriod"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-style"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOStylePeriod[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Impressionism"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOStylePeriod[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Modernism"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 0;
    opts.include_iptc = 0;
    opts.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:ArtworkOrObject>"));
    assert(contains_text(out, (omc_size)res.written, "<Iptc4xmpExt:AOTitle>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Sunset Study</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:AOCreator>"));
    assert(contains_text(out, (omc_size)res.written, "<rdf:Seq>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Alice Example</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Bob Example</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:AOStylePeriod>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Impressionism</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Modernism</rdf:li>"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-artwork"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-title"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-creator"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-style"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_person_and_cvterm_shapes(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonInImageWDetails"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-person"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonInImageWDetails[1]/PersonName"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-person-name"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonInImageWDetails[1]/PersonName[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Jane Doe"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonInImageWDetails[1]/PersonId"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-person-id"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonInImageWDetails[1]/PersonId[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "person-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonInImageWDetails[1]/PersonId[2]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "person-002"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "AboutCvTerm[1]/CvTermName"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Culture"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ProductInImage"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-product"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ProductInImage[1]/ProductName"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-product-name"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ProductInImage[1]/ProductName[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Camera Body"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ProductInImage[1]/ProductDescription"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "legacy-product-desc"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ProductInImage[1]/ProductDescription[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Mirrorless"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 0;
    opts.include_iptc = 0;
    opts.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:PersonInImageWDetails>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:PersonName>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Jane Doe</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<Iptc4xmpExt:PersonId>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>person-001</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>person-002</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:AboutCvTerm>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:CvTermName>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Culture</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:ProductInImage>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:ProductName>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Camera Body</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Mirrorless</rdf:li>"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-person"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-person-name"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-person-id"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-product"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-product-name"));
    assert(!contains_text(out, (omc_size)res.written, "legacy-product-desc"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_flat_child_scalars_promote(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Name"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Alice Flat"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Creator[1]/Role"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "photographer"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ArtworkOrObject[1]/AOCreator"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Alice Example"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonInImageWDetails[1]/PersonId"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "person-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "AboutCvTerm[1]/CvTermName"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Culture"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 0;
    opts.include_iptc = 0;
    opts.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Alice Flat</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written, "<Iptc4xmpExt:Role>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>photographer</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:AOCreator>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>Alice Example</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:PersonId>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>person-001</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<Iptc4xmpExt:CvTermName>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Culture</rdf:li>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Role>photographer</Iptc4xmpExt:Role>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_remaining_indexed_base_shapes(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[8192];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Contributor"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-contributor-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Contributor[1]/Name[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Desk Editor"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Contributor[1]/Role[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "editor"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PlanningRef"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-planning-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PlanningRef[1]/Name[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Editorial Plan"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PlanningRef[1]/Role[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "assignment"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonHeard"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-person-heard-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PersonHeard[1]/Name[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Witness"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ShownEvent"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-shown-event-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ShownEvent[1]/Name[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Press Conference"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "SupplyChainSource"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-supply-chain-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "SupplyChainSource[1]/Name[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Agency Feed"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "VideoShotType"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-video-shot-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "VideoShotType[1]/Name[@xml:lang=x-default]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Interview"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "DopesheetLink"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-dopesheet-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "DopesheetLink[1]/LinkQualifier[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "keyframe"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Snapshot"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-snapshot-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Snapshot[1]/LinkQualifier[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "frame-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "TranscriptLink"));
    omc_val_make_text(&entry.value,
                      append_bytes(&store.arena, "legacy-transcript-base"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "TranscriptLink[1]/LinkQualifier[1]"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "quote"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 0;
    opts.include_iptc = 0;
    opts.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Desk Editor</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>editor</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Editorial Plan</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>assignment</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Witness</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Press Conference</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Agency Feed</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Interview</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>keyframe</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>frame-001</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>quote</rdf:li>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-contributor-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-planning-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-person-heard-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-shown-event-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-supply-chain-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-video-shot-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-dopesheet-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-snapshot-base"));
    assert(!contains_text(out, (omc_size)res.written,
                          "legacy-transcript-base"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_structured_remaining_flat_child_promotions(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 out[8192];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Contributor[1]/Name"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Desk Editor"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Contributor[1]/Role"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "editor"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PlanningRef[1]/Name"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Editorial Plan"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "PlanningRef[1]/Role"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "assignment"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "ShownEvent[1]/Name"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Press Conference"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "SupplyChainSource[1]/Name"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Agency Feed"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "VideoShotType[1]/Name"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Interview"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "Snapshot[1]/LinkQualifier"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "frame-001"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"),
        append_bytes(&store.arena, "TranscriptLink[1]/LinkQualifier"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "quote"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 0;
    opts.include_iptc = 0;
    opts.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Desk Editor</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>editor</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Editorial Plan</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>assignment</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Press Conference</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Agency Feed</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li xml:lang=\"x-default\">Interview</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>frame-001</rdf:li>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<rdf:li>quote</rdf:li>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Name>Desk Editor</Iptc4xmpExt:Name>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Role>editor</Iptc4xmpExt:Role>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Name>Editorial Plan</Iptc4xmpExt:Name>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Role>assignment</Iptc4xmpExt:Role>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Name>Press Conference</Iptc4xmpExt:Name>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Name>Agency Feed</Iptc4xmpExt:Name>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:Name>Interview</Iptc4xmpExt:Name>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:LinkQualifier>frame-001</Iptc4xmpExt:LinkQualifier>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<Iptc4xmpExt:LinkQualifier>quote</Iptc4xmpExt:LinkQualifier>"));

    omc_store_fini(&store);
}

static void
test_sidecar_existing_xmpmm_structured_resources(void)
{
    omc_store store;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 out[8192];

    omc_store_init(&store);

    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "DerivedFrom", "legacy-derived");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "DerivedFrom/stRef:documentID", "xmp.did:base");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "DerivedFrom/stRef:instanceID", "xmp.iid:base");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom", "legacy-managed");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom/stRef:documentID", "xmp.did:managed");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "ManagedFrom/stRef:instanceID", "xmp.iid:managed");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf", "legacy-rendition");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf/stRef:documentID", "xmp.did:rendition");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf/stRef:filePath", "/tmp/rendition.jpg");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "RenditionOf/stRef:renditionClass", "proof:pdf");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients", "legacy-ingredients");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients[1]/stRef:documentID",
                       "xmp.did:ingredient");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Ingredients[1]/stRef:instanceID",
                       "xmp.iid:ingredient");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Manifest", "legacy-manifest");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Manifest[1]/stMfs:linkForm", "EmbedByReference");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Manifest[1]/stMfs:reference/stRef:filePath",
                       "C:\\some path\\file.ext");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "History", "legacy-history");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "History[1]/stEvt:action", "saved");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "History[1]/stEvt:when", "2026-04-15T09:00:00Z");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions", "legacy-versions");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event", "legacy-event");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:version", "1.0");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:comments", "Initial import");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:modifier", "OpenMeta");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:modifyDate",
                       "2026-04-16T10:15:00Z");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event/stEvt:action", "saved");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event/stEvt:changed",
                       "/metadata");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Versions[1]/stVer:event/stEvt:when",
                       "2026-04-16T10:15:00Z");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Pantry", "legacy-pantry");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Pantry[1]/InstanceID", "uuid:pantry-1");
    add_xmp_text_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "Pantry[1]/dc:format", "image/jpeg");

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_exif = 0;
    opts.include_iptc = 0;
    opts.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written,
                         "xmlns:stRef=\"http://ns.adobe.com/xap/1.0/sType/ResourceRef#\""));
    assert(contains_text(out, (omc_size)res.written,
                         "xmlns:stEvt=\"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#\""));
    assert(contains_text(out, (omc_size)res.written,
                         "xmlns:stVer=\"http://ns.adobe.com/xap/1.0/sType/Version#\""));
    assert(contains_text(out, (omc_size)res.written,
                         "xmlns:stMfs=\"http://ns.adobe.com/xap/1.0/sType/ManifestItem#\""));
    assert(contains_text(out, (omc_size)res.written,
                         "<xmpMM:DerivedFrom rdf:parseType=\"Resource\">"));
    assert(contains_text(out, (omc_size)res.written,
                         "xmp.did:base</stRef:documentID>"));
    assert(contains_text(out, (omc_size)res.written,
                         "xmp.iid:base</stRef:instanceID>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<xmpMM:ManagedFrom rdf:parseType=\"Resource\">"));
    assert(contains_text(out, (omc_size)res.written,
                         "xmp.iid:managed</stRef:instanceID>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<xmpMM:RenditionOf rdf:parseType=\"Resource\">"));
    assert(contains_text(out, (omc_size)res.written,
                         "xmp.did:rendition</stRef:documentID>"));
    assert(contains_text(out, (omc_size)res.written,
                         "/tmp/rendition.jpg</stRef:filePath>"));
    assert(contains_text(out, (omc_size)res.written,
                         "proof:pdf</stRef:renditionClass>"));
    assert(contains_text(out, (omc_size)res.written, "<xmpMM:Ingredients>"));
    assert(contains_text(out, (omc_size)res.written, "<rdf:Bag>"));
    assert(contains_text(out, (omc_size)res.written,
                         "xmp.iid:ingredient</stRef:instanceID>"));
    assert(contains_text(out, (omc_size)res.written, "<xmpMM:Manifest>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<stMfs:reference"));
    assert(contains_text(out, (omc_size)res.written,
                         "rdf:parseType=\"Resource\""));
    assert(contains_text(out, (omc_size)res.written, "<xmpMM:History>"));
    assert(contains_text(out, (omc_size)res.written,
                         "saved</stEvt:action>"));
    assert(contains_text(out, (omc_size)res.written,
                         "2026-04-15T09:00:00Z</stEvt:when>"));
    assert(contains_text(out, (omc_size)res.written, "<xmpMM:Versions>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<stVer:event"));
    assert(contains_text(out, (omc_size)res.written,
                         "Initial import</stVer:comments>"));
    assert(contains_text(out, (omc_size)res.written,
                         "OpenMeta</stVer:modifier>"));
    assert(contains_text(out, (omc_size)res.written,
                         "2026-04-16T10:15:00Z</stVer:modifyDate>"));
    assert(contains_text(out, (omc_size)res.written,
                         "/metadata</stEvt:changed>"));
    assert(contains_text(out, (omc_size)res.written,
                         "2026-04-16T10:15:00Z</stEvt:when>"));
    assert(contains_text(out, (omc_size)res.written, "<xmpMM:Pantry>"));
    assert(contains_text(out, (omc_size)res.written,
                         "<xmpMM:InstanceID>uuid:pantry-1</xmpMM:InstanceID>"));
    assert(contains_text(out, (omc_size)res.written,
                         "image/jpeg</dc:format>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpMM:DerivedFrom>legacy-derived</xmpMM:DerivedFrom>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpMM:ManagedFrom>legacy-managed</xmpMM:ManagedFrom>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpMM:Ingredients>legacy-ingredients</xmpMM:Ingredients>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpMM:Manifest>legacy-manifest</xmpMM:Manifest>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpMM:History>legacy-history</xmpMM:History>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpMM:Versions>legacy-versions</xmpMM:Versions>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<stVer:event>legacy-event</stVer:event>"));
    assert(!contains_text(out, (omc_size)res.written,
                          "<xmpMM:Pantry>legacy-pantry</xmpMM:Pantry>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_formats_common_exif_and_gps_values(void)
{
    static const omc_u8 gps_ver[] = { 2U, 3U, 0U, 0U };
    static const omc_urational lens_spec[] = {
        { 24U, 1U }, { 70U, 1U }, { 0U, 1U }, { 0U, 1U }
    };
    static const omc_urational gps_lat[] = {
        { 41U, 1U }, { 24U, 1U }, { 30U, 1U }
    };
    static const omc_urational gps_lon[] = {
        { 2U, 1U }, { 9U, 1U }, { 0U, 1U }
    };
    static const omc_urational gps_time[] = {
        { 12U, 1U }, { 11U, 1U }, { 13U, 1U }
    };
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x0112U);
    omc_val_make_u16(&entry.value, 6U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x0128U);
    omc_val_make_u16(&entry.value, 2U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9207U);
    omc_val_make_u16(&entry.value, 5U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x8822U);
    omc_val_make_u16(&entry.value, 2U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x920AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 66U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9201U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_SRATIONAL;
    entry.value.count = 1U;
    entry.value.u.sr.numer = 6;
    entry.value.u.sr.denom = 1;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA432U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 4U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)lens_spec,
                                   (omc_size)sizeof(lens_spec));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0000U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_U8;
    entry.value.count = (omc_u32)sizeof(gps_ver);
    entry.value.u.ref = append_raw(&store.arena, gps_ver,
                                   (omc_size)sizeof(gps_ver));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0001U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "N"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0002U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)gps_lat,
                                   (omc_size)sizeof(gps_lat));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0003U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "E"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0004U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)gps_lon,
                                   (omc_size)sizeof(gps_lon));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x001DU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "2024:04:19"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0007U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)gps_time,
                                   (omc_size)sizeof(gps_time));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:Orientation>6</tiff:Orientation>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:ResolutionUnit>2</tiff:ResolutionUnit>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:MeteringMode>5</exif:MeteringMode>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ExposureProgram>2</exif:ExposureProgram>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:FocalLength>66.0 mm</exif:FocalLength>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ShutterSpeedValue>1/64</exif:ShutterSpeedValue>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSVersionID>2</exif:GPSVersionID>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSLatitude>41,24.5N</exif:GPSLatitude>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSLongitude>2,9E</exif:GPSLongitude>"));
    assert(contains_text(
        full, (omc_size)res.written,
        "<exif:GPSTimeStamp>2024-04-19T12:11:13Z</exif:GPSTimeStamp>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:li>24</rdf:li>"));
    assert(contains_text(full, (omc_size)res.written, "<rdf:li>70</rdf:li>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_skips_invalid_gps_values(void)
{
    static const omc_urational invalid_triplet[] = {
        { 0U, 0U }, { 0U, 0U }, { 0U, 0U }
    };
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0001U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "N"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0002U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena,
                                   (const omc_u8*)invalid_triplet,
                                   (omc_size)sizeof(invalid_triplet));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0007U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena,
                                   (const omc_u8*)invalid_triplet,
                                   (omc_size)sizeof(invalid_triplet));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0006U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 0U;
    entry.value.u.ur.denom = 0U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0011U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 42889U;
    entry.value.u.ur.denom = 241U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSLatitudeRef>N</exif:GPSLatitudeRef>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSImgDirection>177.9626556</exif:GPSImgDirection>"));
    assert(!contains_text(full, (omc_size)res.written, "<exif:GPSLatitude>"));
    assert(!contains_text(full, (omc_size)res.written, "<exif:GPSTimeStamp>"));
    assert(!contains_text(full, (omc_size)res.written, "<exif:GPSAltitude>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_alias_and_gps_text_overrides(void)
{
    static const omc_urational gps_time[] = {
        { 12U, 1U }, { 11U, 1U }, { 13U, 1U }
    };
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x001DU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "2024:04:19"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0007U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)gps_time,
                                   (omc_size)sizeof(gps_time));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0009U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "A"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x000BU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 3U;
    entry.value.u.ur.denom = 2U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x000CU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "K"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x000DU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 88U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x000EU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "M"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x000FU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 25U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0017U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "T"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0018U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 90U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0019U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "N"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x001AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 4U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x001EU);
    omc_val_make_u16(&entry.value, 1U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x001FU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 5U;
    entry.value.u.ur.denom = 2U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA001U);
    omc_val_make_u16(&entry.value, 1U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA210U);
    omc_val_make_u16(&entry.value, 3U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA300U);
    omc_val_make_u16(&entry.value, 3U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;
    opts.exiftool_gpsdatetime_alias = 1;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSDateTime>2024-04-19T12:11:13Z</exif:GPSDateTime>"));
    assert(!contains_text(full, (omc_size)res.written, "<exif:GPSTimeStamp>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSStatus>Measurement Active</exif:GPSStatus>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSDOP>1.5</exif:GPSDOP>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSSpeedRef>km/h</exif:GPSSpeedRef>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSSpeed>88</exif:GPSSpeed>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSTrackRef>Magnetic North</exif:GPSTrackRef>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSTrack>25</exif:GPSTrack>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSDestBearingRef>True North</exif:GPSDestBearingRef>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSDestBearing>90</exif:GPSDestBearing>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSDestDistanceRef>Knots</exif:GPSDestDistanceRef>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSDestDistance>4</exif:GPSDestDistance>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSDifferential>Differential Corrected</exif:GPSDifferential>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSHPositioningError>2.5</exif:GPSHPositioningError>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ColorSpace>sRGB</exif:ColorSpace>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:FocalPlaneResolutionUnit>cm</exif:FocalPlaneResolutionUnit>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:FileSource>Digital Camera</exif:FileSource>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_formats_remaining_enum_text_values(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[4096];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x0103U);
    omc_val_make_u16(&entry.value, 4U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x011CU);
    omc_val_make_u16(&entry.value, 2U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x0106U);
    omc_val_make_u16(&entry.value, 6U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x0213U);
    omc_val_make_u16(&entry.value, 2U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9208U);
    omc_val_make_u16(&entry.value, 21U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA407U);
    omc_val_make_u16(&entry.value, 4U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA408U);
    omc_val_make_u16(&entry.value, 2U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA409U);
    omc_val_make_u16(&entry.value, 1U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA40AU);
    omc_val_make_u16(&entry.value, 1U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA40CU);
    omc_val_make_u16(&entry.value, 3U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA001U);
    omc_val_make_u16(&entry.value, 2U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0xA210U);
    omc_val_make_u16(&entry.value, 5U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:Compression>T6/Group 4 Fax</tiff:Compression>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:PlanarConfiguration>Planar</tiff:PlanarConfiguration>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:PhotometricInterpretation>YCbCr</tiff:PhotometricInterpretation>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<tiff:YCbCrPositioning>Co-sited</tiff:YCbCrPositioning>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:LightSource>D65</exif:LightSource>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GainControl>High gain down</exif:GainControl>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:Contrast>High</exif:Contrast>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:Saturation>Low</exif:Saturation>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:Sharpness>Soft</exif:Sharpness>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:SubjectDistanceRange>Distant</exif:SubjectDistanceRange>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ColorSpace>Adobe RGB</exif:ColorSpace>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:FocalPlaneResolutionUnit>um</exif:FocalPlaneResolutionUnit>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_formats_apex_and_array_values(void)
{
    static const omc_urational fnumber_vals[] = {
        { 139U, 50U }, { 0U, 0U }
    };
    static const omc_urational aperture_vals[] = {
        { 0U, 0U }, { 4U, 1U }
    };
    static const omc_srational shutter_vals[] = {
        { 0, 0 }, { 6, 1 }
    };
    static const omc_srational exp_comp_vals[] = {
        { 0, 0 }, { 1, 2 }
    };
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x829DU);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 2U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)fnumber_vals,
                                   (omc_size)sizeof(fnumber_vals));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9202U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 2U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)aperture_vals,
                                   (omc_size)sizeof(aperture_vals));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9201U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_SRATIONAL;
    entry.value.count = 2U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)shutter_vals,
                                   (omc_size)sizeof(shutter_vals));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9204U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_SRATIONAL;
    entry.value.count = 2U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)exp_comp_vals,
                                   (omc_size)sizeof(exp_comp_vals));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:FNumber>2.8</exif:FNumber>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ApertureValue>4.0</exif:ApertureValue>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ShutterSpeedValue>1/64</exif:ShutterSpeedValue>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:ExposureCompensation>0.5</exif:ExposureCompensation>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_skips_invalid_apex_values(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x829DU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 0U;
    entry.value.u.ur.denom = 0U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9202U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 30U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x9204U);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_SRATIONAL;
    entry.value.count = 1U;
    entry.value.u.sr.numer = 0;
    entry.value.u.sr.denom = 0;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x920AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 66U;
    entry.value.u.ur.denom = 1U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(!contains_text(full, (omc_size)res.written, "<exif:FNumber>"));
    assert(!contains_text(full, (omc_size)res.written, "<exif:ApertureValue>"));
    assert(!contains_text(full, (omc_size)res.written,
                          "<exif:ExposureCompensation>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:FocalLength>66.0 mm</exif:FocalLength>"));

    omc_store_fini(&store);
}

static void
test_sidecar_portable_formats_gps_coords_from_srational_arrays(void)
{
    static const omc_srational gps_lat_vals[] = {
        { 45, 1 }, { 0, 1 }, { 185806272, 10000000 }
    };
    static const omc_srational gps_lon_vals[] = {
        { 93, 1 }, { 27, 1 }, { 411877440, 10000000 }
    };
    omc_store store;
    omc_entry entry;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0001U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "N"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0002U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_SRATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)gps_lat_vals,
                                   (omc_size)sizeof(gps_lat_vals));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0003U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "W"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "gpsifd"),
                          0x0004U);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_SRATIONAL;
    entry.value.count = 3U;
    entry.value.u.ref = append_raw(&store.arena, (const omc_u8*)gps_lon_vals,
                                   (omc_size)sizeof(gps_lon_vals));
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 1;
    opts.include_iptc = 0;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSLatitude>45,0.30967712N</exif:GPSLatitude>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<exif:GPSLongitude>93,27.6864624W</exif:GPSLongitude>"));

    omc_store_fini(&store);
}

static void
test_lossless_truncation_and_success(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_lossless_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 small[64];
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    entry.origin.block = 0U;
    entry.origin.order_in_block = 0U;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 2U;
    entry.origin.wire_count = 5U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_lossless_opts_init(&opts);

    status = omc_xmp_dump_lossless(&store, small, sizeof(small), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_TRUNCATED);
    assert(res.needed > sizeof(small));

    status = omc_xmp_dump_lossless(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(res.entries == 1U);
    assert(contains_text(full, (omc_size)res.written, "<x:xmpmeta"));
    assert(contains_text(full, (omc_size)res.written,
                         "urn:openmeta:dump:1.0"));
    assert(contains_text(full, (omc_size)res.written, "exif:ifd0:0x010F"));
    assert(contains_text(full, (omc_size)res.written, "Q2Fub24="));

    omc_store_fini(&store);
}

static void
test_lossless_emits_exr_type_name(void)
{
    static const omc_u8 raw[] = { 0xAAU, 0xBBU, 0xCCU };
    omc_store store;
    omc_entry entry;
    omc_xmp_lossless_opts opts;
    omc_xmp_dump_res res;
    omc_status status;
    omc_u8 full[2048];

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    entry.key.kind = OMC_KEY_EXR_ATTR;
    entry.key.u.exr_attr.part_index = 0U;
    entry.key.u.exr_attr.name = append_bytes(&store.arena, "customA");
    omc_val_make_bytes(&entry.value,
                       append_raw(&store.arena, raw, (omc_size)sizeof(raw)));
    entry.origin.block = 0U;
    entry.origin.order_in_block = 0U;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 31U;
    entry.origin.wire_count = 3U;
    entry.origin.wire_type_name = append_bytes(&store.arena, "myVendorFoo");
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_lossless_opts_init(&opts);
    status = omc_xmp_dump_lossless(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written, "exr:part:0:customA"));
    assert(contains_text(full, (omc_size)res.written,
                         "<omd:exrTypeName>myVendorFoo</omd:exrTypeName>"));

    omc_store_fini(&store);
}

static void
test_sidecar_cfg_lossless_auto_grows_output(void)
{
    omc_store store;
    omc_entry entry;
    omc_arena out;
    omc_xmp_sidecar_cfg cfg;
    omc_xmp_dump_res res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&out);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_cfg_init(&cfg);
    cfg.format = OMC_XMP_SIDECAR_LOSSLESS;
    cfg.initial_output_bytes = 16U;

    status = omc_xmp_dump_sidecar_cfg(&store, &out, &cfg, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(out.size == (omc_size)res.written);
    assert(contains_text(out.data, out.size, "exif:ifd0:0x010F"));

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_sidecar_cfg_portable_uses_format_switch(void)
{
    omc_store store;
    omc_entry entry;
    omc_arena out;
    omc_xmp_sidecar_cfg cfg;
    omc_xmp_dump_res res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&out);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "exififd"),
                          0x829AU);
    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = 1U;
    entry.value.u.ur.numer = 1U;
    entry.value.u.ur.denom = 1250U;
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_cfg_init(&cfg);
    cfg.format = OMC_XMP_SIDECAR_PORTABLE;
    cfg.initial_output_bytes = 32U;
    cfg.portable.include_exif = 1;

    status = omc_xmp_dump_sidecar_cfg(&store, &out, &cfg, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out.data, out.size, "<tiff:Make>Canon</tiff:Make>"));
    assert(contains_text(out.data, out.size,
                         "<exif:ExposureTime>1/1250</exif:ExposureTime>"));

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_sidecar_req_portable_is_deterministic(void)
{
    omc_store store;
    omc_entry entry;
    omc_arena out_a;
    omc_arena out_b;
    omc_xmp_sidecar_req req;
    omc_xmp_dump_res ra;
    omc_xmp_dump_res rb;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&out_a);
    omc_arena_init(&out_b);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x0110U);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "EOS R6"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_req_init(&req);
    req.format = OMC_XMP_SIDECAR_PORTABLE;
    req.initial_output_bytes = 32U;

    status = omc_xmp_dump_sidecar_req(&store, &out_a, &req, &ra);
    assert(status == OMC_STATUS_OK);
    status = omc_xmp_dump_sidecar_req(&store, &out_b, &req, &rb);
    assert(status == OMC_STATUS_OK);
    assert(ra.status == OMC_XMP_DUMP_OK);
    assert(rb.status == OMC_XMP_DUMP_OK);
    assert(ra.entries == rb.entries);
    assert(ra.written == rb.written);
    assert(out_a.size == out_b.size);
    assert(memcmp(out_a.data, out_b.data, out_a.size) == 0);

    omc_arena_fini(&out_b);
    omc_arena_fini(&out_a);
    omc_store_fini(&store);
}

static void
test_sidecar_req_respects_output_limit(void)
{
    omc_store store;
    omc_entry entry;
    omc_arena out;
    omc_xmp_sidecar_req req;
    omc_xmp_dump_res res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&out);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_req_init(&req);
    req.format = OMC_XMP_SIDECAR_LOSSLESS;
    req.initial_output_bytes = 32U;
    req.limits.max_output_bytes = 32U;

    status = omc_xmp_dump_sidecar_req(&store, &out, &req, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_LIMIT);

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_sidecar_cfg_clamps_initial_output_to_limit(void)
{
    omc_store store;
    omc_entry entry;
    omc_arena out;
    omc_xmp_sidecar_cfg cfg;
    omc_xmp_dump_res res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&out);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_bytes(&store.arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "Canon"),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_cfg_init(&cfg);
    cfg.format = OMC_XMP_SIDECAR_LOSSLESS;
    cfg.initial_output_bytes = 4096U;
    cfg.lossless.limits.max_output_bytes = 32U;

    status = omc_xmp_dump_sidecar_cfg(&store, &out, &cfg, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_LIMIT);
    assert(out.capacity == 32U);

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_sidecar_req_portable_escapes_unsafe_text(void)
{
    omc_store store;
    omc_entry entry;
    omc_arena out;
    omc_xmp_sidecar_req req;
    omc_xmp_dump_res res;
    omc_status status;
    char unsafe[8];

    omc_store_init(&store);
    omc_arena_init(&out);

    unsafe[0] = 'A';
    unsafe[1] = 0x01;
    unsafe[2] = '<';
    unsafe[3] = '>';
    unsafe[4] = '&';
    unsafe[5] = '"';
    unsafe[6] = '\'';
    unsafe[7] = '\0';

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key,
                              append_bytes(&store.arena,
                                           "http://ns.adobe.com/xap/1.0/"),
                              append_bytes(&store.arena, "Label"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, unsafe),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_req_init(&req);
    req.format = OMC_XMP_SIDECAR_PORTABLE;
    req.initial_output_bytes = 64U;
    req.include_exif = 0;
    req.include_existing_xmp = 1;

    status = omc_xmp_dump_sidecar_req(&store, &out, &req, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out.data, out.size,
                         "<xmp:Label>A\\x01&lt;&gt;&amp;&quot;&apos;</xmp:Label>"));

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

int
main(void)
{
    test_lossless_truncation_and_success();
    test_lossless_emits_exr_type_name();
    test_sidecar_cfg_lossless_auto_grows_output();
    test_sidecar_cfg_portable_uses_format_switch();
    test_sidecar_req_portable_is_deterministic();
    test_sidecar_req_respects_output_limit();
    test_sidecar_cfg_clamps_initial_output_to_limit();
    test_sidecar_req_portable_escapes_unsafe_text();
    test_sidecar_truncation_and_success();
    test_sidecar_uses_edit_commit_output();
    test_sidecar_limits_and_skips_unsupported_properties();
    test_sidecar_portable_normalizes_exif_dates();
    test_sidecar_portable_decodes_windows_xp_text();
    test_sidecar_portable_exif_and_iptc_projection();
    test_sidecar_arena_auto_grows_and_existing_wins();
    test_sidecar_portable_groups_repeated_iptc_properties();
    test_sidecar_portable_conflict_policies();
    test_sidecar_portable_generated_iptc_overrides_existing_indexed_xmp();
    test_sidecar_existing_indexed_xmp_emits_grouped_array();
    test_sidecar_canonicalizes_existing_xmp_property_names();
    test_sidecar_known_portable_only_skips_custom_existing_xmp();
    test_sidecar_existing_lang_alt_emits_grouped_alt();
    test_sidecar_promotes_flat_standard_scalars_to_canonical_shapes();
    test_sidecar_existing_lr_hierarchical_subject_emits_bag();
    test_sidecar_preserves_remaining_standard_grouped_scalars();
    test_sidecar_emits_pdf_and_rights_namespaces();
    test_sidecar_canonicalize_managed_prefers_generated_portable_value();
    test_sidecar_canonicalize_managed_replaces_xdefault_and_preserves_other_locales();
    test_sidecar_preserves_xmprights_standard_namespace();
    test_sidecar_existing_structured_core_emits_resource();
    test_sidecar_existing_structured_indexed_resources_emit_seq();
    test_sidecar_existing_structured_canonicalizes_flat_bases();
    test_sidecar_existing_structured_nested_and_lang_alt();
    test_sidecar_existing_structured_child_indexed_seq();
    test_sidecar_existing_structured_mixed_namespace_children();
    test_sidecar_existing_structured_location_child_shapes();
    test_sidecar_existing_structured_creator_shapes();
    test_sidecar_existing_structured_artwork_shapes();
    test_sidecar_existing_structured_person_and_cvterm_shapes();
    test_sidecar_existing_structured_flat_child_scalars_promote();
    test_sidecar_existing_structured_remaining_indexed_base_shapes();
    test_sidecar_existing_structured_remaining_flat_child_promotions();
    test_sidecar_existing_xmpmm_structured_resources();
    test_sidecar_portable_formats_common_exif_and_gps_values();
    test_sidecar_portable_skips_invalid_gps_values();
    test_sidecar_portable_alias_and_gps_text_overrides();
    test_sidecar_portable_formats_remaining_enum_text_values();
    test_sidecar_portable_formats_apex_and_array_values();
    test_sidecar_portable_skips_invalid_apex_values();
    test_sidecar_portable_formats_gps_coords_from_srational_arrays();
    return 0;
}
