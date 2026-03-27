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
    assert(contains_text(full, (omc_size)res.written, "<dc:title>hello</dc:title>"));
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
    assert(contains_text(full, (omc_size)res.written,
                         "<dc:title>Sunset</dc:title>"));
    assert(contains_text(full, (omc_size)res.written,
                         "<dc:description>Golden hour over lake</dc:description>"));
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

int
main(void)
{
    test_sidecar_truncation_and_success();
    test_sidecar_uses_edit_commit_output();
    test_sidecar_limits_and_skips_unsupported_properties();
    test_sidecar_portable_normalizes_exif_dates();
    test_sidecar_portable_decodes_windows_xp_text();
    test_sidecar_portable_exif_and_iptc_projection();
    test_sidecar_arena_auto_grows_and_existing_wins();
    test_sidecar_portable_groups_repeated_iptc_properties();
    test_sidecar_existing_indexed_xmp_emits_grouped_array();
    test_sidecar_canonicalizes_existing_xmp_property_names();
    return 0;
}
