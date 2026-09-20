#include "omc/omc_translation.h"
#include "omc/omc_exif.h"
#include "omc/omc_exif_tiff_serialize.h"
#include "omc_test_assert.h"

#include <string.h>

static const char k_ns_exif[] = "http://ns.adobe.com/exif/1.0/";

static omc_byte_ref
append_text(omc_store *store, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, text, strlen(text), &ref) ==
           OMC_STATUS_OK);
    return ref;
}

static omc_entry_id
add_xmp(omc_store *store, const char *path, const char *value, int dirty)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(store, k_ns_exif),
                              append_text(store, path));
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.wire_type_name = append_text(store, "gps-source");
    entry.flags = dirty ? OMC_ENTRY_FLAG_DIRTY : 0U;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_native_text(omc_store *store, omc_u16 tag, const char *value)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(store, "gpsifd"), tag);
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_ASCII);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
    return (omc_entry_id)(store->entry_count - 1U);
}

static void
add_native_array(omc_store *store, omc_u16 tag, const omc_urational *values,
                 omc_u32 count)
{
    omc_entry entry;
    omc_byte_ref ref;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(store, "gpsifd"), tag);
    assert(omc_arena_append(&store->arena, values,
                            (omc_size)count * sizeof(*values), &ref) ==
           OMC_STATUS_OK);
    omc_val_make_array(&entry.value, OMC_ELEM_URATIONAL, count, ref,
                       OMC_BYTE_ORDER_NATIVE);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static void
add_native_u8(omc_store *store, omc_u16 tag, omc_u8 value)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(store, "gpsifd"), tag);
    omc_val_make_u8(&entry.value, value);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static const omc_entry *
find_gps(const omc_store *store, omc_u16 tag)
{
    omc_size i;
    omc_const_bytes ifd;
    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind != OMC_KEY_EXIF_TAG ||
            store->entries[i].key.u.exif_tag.tag != tag ||
            (store->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        ifd = omc_arena_view(&store->arena,
                             store->entries[i].key.u.exif_tag.ifd);
        if (ifd.size == 6U && memcmp(ifd.data, "gpsifd", 6U) == 0)
            return &store->entries[i];
    }
    return NULL;
}

static void
expect_text(const omc_store *store, const omc_entry *entry, const char *value)
{
    omc_const_bytes bytes;
    assert(entry != NULL);
    bytes = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(bytes.size == strlen(value));
    assert(memcmp(bytes.data, value, bytes.size) == 0);
}

static void
expect_coordinate(const omc_store *store, omc_u16 tag,
                  omc_urational a, omc_urational b, omc_urational c)
{
    const omc_entry *entry;
    omc_const_bytes bytes;
    omc_urational actual[3];
    entry = find_gps(store, tag);
    assert(entry != NULL && entry->value.kind == OMC_VAL_ARRAY &&
           entry->value.elem_type == OMC_ELEM_URATIONAL &&
           entry->value.count == 3U);
    bytes = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(bytes.size == sizeof(actual));
    memcpy(actual, bytes.data, sizeof(actual));
    assert(actual[0].numer == a.numer && actual[0].denom == a.denom);
    assert(actual[1].numer == b.numer && actual[1].denom == b.denom);
    assert(actual[2].numer == c.numer && actual[2].denom == c.denom);
}

static void
add_position(omc_store *store)
{
    add_xmp(store, "GPSLatitude", "35,48.125N", 1);
    add_xmp(store, "GPSLongitude", "139,34,55.25W", 1);
    add_xmp(store, "GPSAltitude", "12345/100", 1);
    add_xmp(store, "GPSAltitudeRef", "1", 1);
}

static void
test_primary_and_idempotence(void)
{
    omc_store source;
    omc_store output;
    omc_store again;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_exif_tiff_opts tiff_opts;
    omc_exif_tiff_res tiff_measure;
    omc_exif_tiff_res tiff_write;
    omc_exif_opts exif_opts;
    omc_exif_res exif_read;
    omc_mut_bytes tiff_output;
    omc_u8 tiff_bytes[2048];
    omc_store decoded;
    const omc_entry *entry;
    omc_const_bytes wire;
    omc_urational a;
    omc_urational b;
    omc_urational c;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&again);
    omc_store_init(&decoded);
    add_position(&source);
    omc_gps_translation_opts_init(&opts);
    result = omc_translate_xmp_gps(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.source_properties == 4U && result.groups_translated == 3U);
    assert(result.entries_added == 7U && output.entry_count == 11U);
    a.numer = 35U; a.denom = 1U;
    b.numer = 48U; b.denom = 1U;
    c.numer = 15U; c.denom = 2U;
    expect_coordinate(&output, 2U, a, b, c);
    a.numer = 139U; a.denom = 1U;
    b.numer = 34U; b.denom = 1U;
    c.numer = 221U; c.denom = 4U;
    expect_coordinate(&output, 4U, a, b, c);
    entry = find_gps(&output, 1U);
    expect_text(&output, entry, "N");
    entry = find_gps(&output, 3U);
    expect_text(&output, entry, "W");
    assert(find_gps(&output, 5U)->value.u.u64 == 1U);
    assert(find_gps(&output, 6U)->value.u.ur.numer == 2469U &&
           find_gps(&output, 6U)->value.u.ur.denom == 20U);
    entry = find_gps(&output, 2U);
    wire = omc_arena_view(&output.arena, entry->origin.wire_type_name);
    assert(wire.size == 10U && memcmp(wire.data, "gps-source", 10U) == 0);

    /* The translated native values must survive the canonical TIFF carrier. */
    omc_exif_tiff_opts_init(&tiff_opts);
    tiff_output.data = NULL;
    tiff_output.size = 0U;
    tiff_measure = omc_serialize_exif_tiff(&output, tiff_output, &tiff_opts);
    assert(tiff_measure.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED &&
           tiff_measure.needed < sizeof(tiff_bytes));
    tiff_output.data = tiff_bytes;
    tiff_output.size = sizeof(tiff_bytes);
    tiff_write = omc_serialize_exif_tiff(&output, tiff_output, &tiff_opts);
    assert(tiff_write.status == OMC_EXIF_TIFF_OK &&
           tiff_write.written == tiff_measure.needed);
    omc_exif_opts_init(&exif_opts);
    exif_read = omc_exif_dec(tiff_bytes, (omc_size)tiff_write.written,
                             &decoded, OMC_INVALID_BLOCK_ID,
                             (omc_exif_ifd_ref *)0, 0U, &exif_opts);
    assert(exif_read.status == OMC_EXIF_OK ||
           exif_read.status == OMC_EXIF_TRUNCATED);
    a.numer = 35U; a.denom = 1U;
    b.numer = 48U; b.denom = 1U;
    c.numer = 15U; c.denom = 2U;
    expect_coordinate(&decoded, 2U, a, b, c);
    a.numer = 139U; a.denom = 1U;
    b.numer = 34U; b.denom = 1U;
    c.numer = 221U; c.denom = 4U;
    expect_coordinate(&decoded, 4U, a, b, c);
    assert(find_gps(&decoded, 5U)->value.u.u64 == 1U);
    assert(find_gps(&decoded, 6U)->value.u.ur.numer == 2469U &&
           find_gps(&decoded, 6U)->value.u.ur.denom == 20U);
    entry = find_gps(&decoded, 0U);
    assert(entry != NULL && entry->value.kind == OMC_VAL_ARRAY &&
           entry->value.count == 4U);
    omc_store_fini(&decoded);
    result = omc_translate_xmp_gps(&output, &again, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK &&
           result.groups_unchanged == 3U && result.entries_added == 0U);
    omc_store_fini(&again);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_exact_coordinates_and_failures(void)
{
    const char *const values[] = {
        "90,0N", "1,2.50000000000000000000S", "179,59.99999999E",
        "0,0,0.000000001N"
    };
    const omc_urational seconds[] = {
        {0U, 1U}, {30U, 1U}, {299999997U, 5000000U}, {1U, 1000000000U}
    };
    const char *const paths[] = {
        "GPSLatitude", "GPSLatitude", "GPSLongitude", "GPSLatitude"
    };
    omc_size i;
    for (i = 0U; i < 4U; ++i) {
        omc_store source;
        omc_store output;
        omc_gps_translation_opts opts;
        omc_gps_translation_res result;
        omc_urational first;
        omc_urational second;
        omc_urational third;
        omc_store_init(&source);
        omc_store_init(&output);
        add_xmp(&source, paths[i], values[i], 1);
        omc_gps_translation_opts_init(&opts);
        result = omc_translate_xmp_gps(&source, &output, &opts);
        assert(result.status == OMC_GPS_TRANSLATION_OK);
        first.numer = i == 0U ? 90U : i == 1U ? 1U : i == 2U ? 179U : 0U;
        first.denom = 1U;
        second.numer = i == 1U ? 2U : i == 2U ? 59U : 0U;
        second.denom = 1U;
        third = seconds[i];
        expect_coordinate(&output, i == 2U ? 4U : 2U, first, second, third);
        omc_store_fini(&output);
        omc_store_fini(&source);
    }
    {
        static const char *const invalid[] = {
            "35.5", "35,10", "35,10n", "35,10E", "-35,10N", "35, 10N",
            "35,10,1,2N", "35,1/2N", "35,10.N", "91,0N", "90,0.1N"
        };
        omc_gps_translation_res result;
        for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            omc_store source;
            omc_store output;
            omc_gps_translation_opts opts;
            omc_store_init(&source);
            omc_store_init(&output);
            add_xmp(&source, "GPSLatitude", invalid[i], 1);
            omc_gps_translation_opts_init(&opts);
            result = omc_translate_xmp_gps(&source, &output, &opts);
            assert(result.status == OMC_GPS_TRANSLATION_INVALID_SOURCE ||
                   result.status == OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE);
            omc_store_fini(&output);
            omc_store_fini(&source);
        }
    }
}

static void
test_conflicts_limits_and_version(void)
{
    omc_store source;
    omc_store output;
    omc_store before;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_urational version_alt;
    omc_u8 version[4] = {2U, 4U, 0U, 0U};
    omc_byte_ref ref;
    omc_entry entry;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&before);
    add_position(&source);
    add_native_text(&source, 1U, "S");
    add_native_text(&source, 1U, "N");
    add_native_u8(&source, 6U, 9U);
    omc_gps_translation_opts_init(&opts);
    result = omc_translate_xmp_gps(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_NATIVE_CONFLICT);
    omc_store_init(&output);
    opts.conflict = OMC_TRANSLATION_PRESERVE;
    result = omc_translate_xmp_gps(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK && result.groups_preserved == 2U);
    omc_store_fini(&output);
    omc_store_init(&output);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp_gps(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK && result.entries_updated == 2U &&
           result.entries_removed == 1U && result.entries_added == 5U);
    omc_store_fini(&output);

    omc_store_reset(&source);
    add_position(&source);
    omc_store_init(&output);
    opts.conflict = OMC_TRANSLATION_FAIL;
    opts.max_added_entries = 6U;
    result = omc_translate_xmp_gps(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_ENTRY_LIMIT);
    opts.max_added_entries = 7U;
    opts.max_operations = 6U;
    result = omc_translate_xmp_gps(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OPERATION_LIMIT);
    omc_store_fini(&output);

    omc_store_reset(&source);
    add_xmp(&source, "GPSAltitude", "123.5", 1);
    add_xmp(&source, "GPSAltitudeRef", "1", 1);
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(&source, "gpsifd"), 0U);
    assert(omc_arena_append(&source.arena, version, sizeof(version), &ref) ==
           OMC_STATUS_OK);
    omc_val_make_array(&entry.value, OMC_ELEM_U8, 4U, ref, OMC_BYTE_ORDER_NATIVE);
    assert(omc_store_add_entry(&source, &entry, NULL) == OMC_STATUS_OK);
    omc_store_init(&output);
    omc_gps_translation_opts_init(&opts);
    result = omc_translate_xmp_gps(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK &&
           find_gps(&output, 5U)->value.u.u64 == 3U);
    version_alt = find_gps(&output, 6U)->value.u.ur;
    assert(version_alt.numer == 247U && version_alt.denom == 2U);
    omc_store_fini(&output);
    omc_store_fini(&before);
    omc_store_fini(&source);
}

static void
test_removal_and_source_mode(void)
{
    omc_store source;
    omc_store output;
    omc_store removed;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_size i;
    omc_edit edit;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&removed);
    add_position(&source);
    omc_gps_translation_opts_init(&opts);
    assert(omc_translate_xmp_gps(&source, &output, &opts).status ==
           OMC_GPS_TRANSLATION_OK);
    omc_edit_init(&edit);
    for (i = 0U; i < 4U; ++i)
        assert(omc_edit_tombstone(&edit, (omc_entry_id)i) == OMC_STATUS_OK);
    assert(omc_edit_commit(&output, &edit, 1U, &removed) == OMC_STATUS_OK);
    omc_edit_fini(&edit);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp_gps(&removed, &source, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK && result.entries_removed == 7U);
    for (i = 0U; i <= 6U; ++i)
        assert(find_gps(&source, (omc_u16)i) == NULL);
    omc_store_fini(&removed);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

int
main(void)
{
    test_primary_and_idempotence();
    test_exact_coordinates_and_failures();
    test_conflicts_limits_and_version();
    test_removal_and_source_mode();
    return 0;
}
