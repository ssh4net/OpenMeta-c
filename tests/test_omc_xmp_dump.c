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
    assert(contains_text(full, (omc_size)res.written, "<dc:title>XMP</dc:title>"));
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
    assert(contains_text(full, (omc_size)res.written, "<dc:title>XMP</dc:title>"));
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
    assert(contains_text(full, (omc_size)res.written,
                         "<dc:title>IPTC</dc:title>"));
    assert(!contains_text(full, (omc_size)res.written, "<dc:title>XMP</dc:title>"));

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
    test_sidecar_portable_formats_common_exif_and_gps_values();
    test_sidecar_portable_skips_invalid_gps_values();
    test_sidecar_portable_alias_and_gps_text_overrides();
    test_sidecar_portable_formats_remaining_enum_text_values();
    test_sidecar_portable_formats_apex_and_array_values();
    test_sidecar_portable_skips_invalid_apex_values();
    test_sidecar_portable_formats_gps_coords_from_srational_arrays();
    return 0;
}
