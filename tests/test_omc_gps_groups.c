#include "omc/omc_translation.h"
#include "omc/omc_exif.h"
#include "omc/omc_exif_tiff_serialize.h"
#include "omc_test_assert.h"

#include <string.h>

static const char k_ns[] = "http://ns.adobe.com/exif/1.0/";

static omc_byte_ref
append(omc_store *store, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, text, strlen(text), &ref) == OMC_STATUS_OK);
    return ref;
}

static void
add_xmp(omc_store *store, const char *path, const char *text)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append(store, k_ns), append(store, path));
    omc_val_make_text(&entry.value, append(store, text), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.wire_type_name = append(store, "gps-source");
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static const omc_entry *
find_tag(const omc_store *store, omc_u16 tag)
{
    omc_size i;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag
            || !((omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd).size == 6U)
                  && memcmp(omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd).data,
                            "gpsifd", 6U) == 0))
            continue;
        return entry;
    }
    return NULL;
}

static void
expect_text(const omc_store *store, omc_u16 tag, const char *text)
{
    const omc_entry *entry;
    omc_const_bytes bytes;
    entry = find_tag(store, tag);
    assert(entry != NULL && entry->value.kind == OMC_VAL_TEXT);
    bytes = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(bytes.size == strlen(text));
    assert(memcmp(bytes.data, text, bytes.size) == 0);
}

static void
expect_urational(const omc_store *store, omc_u16 tag, omc_u32 numer,
                 omc_u32 denom)
{
    const omc_entry *entry;
    entry = find_tag(store, tag);
    assert(entry != NULL && entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_URATIONAL);
    assert(entry->value.u.ur.numer == numer);
    assert(entry->value.u.ur.denom == denom);
}

static void
expect_coordinate(const omc_store *store, omc_u16 tag,
                  omc_u32 a_numer, omc_u32 a_denom,
                  omc_u32 b_numer, omc_u32 b_denom,
                  omc_u32 c_numer, omc_u32 c_denom)
{
    const omc_entry *entry;
    omc_const_bytes bytes;
    omc_urational actual[3];
    entry = find_tag(store, tag);
    assert(entry != NULL && entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_URATIONAL);
    assert(entry->value.count == 3U);
    bytes = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(bytes.size == sizeof(actual));
    memcpy(actual, bytes.data, sizeof(actual));
    assert(actual[0].numer == a_numer && actual[0].denom == a_denom);
    assert(actual[1].numer == b_numer && actual[1].denom == b_denom);
    assert(actual[2].numer == c_numer && actual[2].denom == c_denom);
}

static void
expect_version(const omc_store *store)
{
    const omc_entry *entry;
    omc_const_bytes bytes;
    static const omc_u8 expected[4] = { 2U, 3U, 0U, 0U };
    entry = find_tag(store, 0U);
    assert(entry != NULL && entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U8 && entry->value.count == 4U);
    bytes = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(bytes.size == sizeof(expected));
    assert(memcmp(bytes.data, expected, sizeof(expected)) == 0);
}

static void
expect_bytes(const omc_store *store, omc_u16 tag, const omc_u8 *expected,
             omc_size size)
{
    const omc_entry *entry;
    omc_const_bytes bytes;
    entry = find_tag(store, tag);
    assert(entry != NULL && entry->value.kind == OMC_VAL_BYTES);
    bytes = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(bytes.size == size);
    assert(memcmp(bytes.data, expected, size) == 0);
}

static void
roundtrip_tiff(const omc_store *source, omc_store *decoded)
{
    omc_exif_tiff_opts tiff_opts;
    omc_exif_tiff_res measured;
    omc_exif_tiff_res written;
    omc_exif_opts exif_opts;
    omc_exif_res decoded_result;
    omc_mut_bytes output;
    omc_u8 bytes[8192];

    omc_exif_tiff_opts_init(&tiff_opts);
    output.data = NULL;
    output.size = 0U;
    measured = omc_serialize_exif_tiff(source, output, &tiff_opts);
    assert(measured.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED);
    assert(measured.needed < sizeof(bytes));
    output.data = bytes;
    output.size = sizeof(bytes);
    written = omc_serialize_exif_tiff(source, output, &tiff_opts);
    assert(written.status == OMC_EXIF_TIFF_OK);
    assert(written.written == measured.needed);
    omc_exif_opts_init(&exif_opts);
    decoded_result = omc_exif_dec(bytes, (omc_size)written.written, decoded,
                                  OMC_INVALID_BLOCK_ID,
                                  (omc_exif_ifd_ref *)0, 0U, &exif_opts);
    assert(decoded_result.status == OMC_EXIF_OK
           || decoded_result.status == OMC_EXIF_TRUNCATED);
}

static void
test_navigation(void)
{
    omc_store source;
    omc_store output;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_store_init(&source); omc_store_init(&output);
    add_xmp(&source, "GPSTimeStamp", "2026-09-21T12:34:56.25Z");
    add_xmp(&source, "GPSSpeedRef", "km/h");
    add_xmp(&source, "GPSSpeed", "12.5");
    add_xmp(&source, "GPSTrackRef", "True North");
    add_xmp(&source, "GPSTrack", "90");
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_NAV_TRANSLATE_TIMESTAMP
                    | OMC_GPS_NAV_TRANSLATE_SPEED
                    | OMC_GPS_NAV_TRANSLATE_TRACK;
    result = omc_translate_xmp_gps_navigation(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(find_tag(&output, 7U) != NULL && find_tag(&output, 29U) != NULL);
    assert(find_tag(&output, 12U) != NULL && find_tag(&output, 13U) != NULL);
    assert(find_tag(&output, 14U) != NULL && find_tag(&output, 15U) != NULL);
    assert(find_tag(&output, 0U) != NULL);
    omc_store_fini(&output); omc_store_fini(&source);
}

static void
test_destination_quality_text(void)
{
    omc_store source;
    omc_store output;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_store_init(&source); omc_store_init(&output);
    add_xmp(&source, "GPSDestLatitude", "35,41.5N");
    add_xmp(&source, "GPSDestLongitude", "139,41.25E");
    add_xmp(&source, "GPSDestBearingRef", "T");
    add_xmp(&source, "GPSDestBearing", "90");
    add_xmp(&source, "GPSDestDistanceRef", "Nautical miles");
    add_xmp(&source, "GPSDestDistance", "2.5");
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_DEST_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_destination(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(find_tag(&output, 19U) != NULL && find_tag(&output, 20U) != NULL);
    assert(find_tag(&output, 25U) != NULL && find_tag(&output, 26U) != NULL);
    omc_store_fini(&output); omc_store_reset(&source); omc_store_init(&output);
    add_xmp(&source, "GPSStatus", "Measurement Active");
    add_xmp(&source, "GPSMeasureMode", "3");
    add_xmp(&source, "GPSDOP", "1.5");
    add_xmp(&source, "GPSDifferential", "1");
    add_xmp(&source, "GPSHPositioningError", "0.25");
    opts.mappings = OMC_GPS_QUALITY_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_quality(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(find_tag(&output, 9U) != NULL && find_tag(&output, 11U) != NULL
           && find_tag(&output, 30U) != NULL);
    omc_store_fini(&output); omc_store_reset(&source); omc_store_init(&output);
    add_xmp(&source, "GPSSatellites", "8");
    add_xmp(&source, "GPSMapDatum", "WGS-84");
    add_xmp(&source, "GPSProcessingMethod", "GPS");
    add_xmp(&source, "GPSAreaInformation", "Tokyo");
    opts.mappings = OMC_GPS_TEXT_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_text(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(find_tag(&output, 8U) != NULL && find_tag(&output, 18U) != NULL
           && find_tag(&output, 27U) != NULL && find_tag(&output, 28U) != NULL);
    omc_store_fini(&output); omc_store_fini(&source);
}

static void
test_navigation_roundtrip(void)
{
    omc_store source;
    omc_store output;
    omc_store decoded;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&decoded);
    add_xmp(&source, "GPSTimeStamp", "2024-03-01T00:30:12.125+01:00");
    add_xmp(&source, "GPSSpeedRef", "K");
    add_xmp(&source, "GPSSpeed", "12345/100");
    add_xmp(&source, "GPSTrackRef", "T");
    add_xmp(&source, "GPSTrack", "359.99");
    add_xmp(&source, "GPSImgDirectionRef", "M");
    add_xmp(&source, "GPSImgDirection", "45.5");
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_NAV_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_navigation(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.source_properties == 7U && result.groups_translated == 4U);
    assert(result.entries_added == 9U);
    expect_version(&output);
    expect_coordinate(&output, 7U, 23U, 1U, 30U, 1U, 97U, 8U);
    expect_text(&output, 29U, "2024:02:29");
    expect_text(&output, 12U, "K");
    expect_urational(&output, 13U, 2469U, 20U);
    expect_text(&output, 14U, "T");
    expect_urational(&output, 15U, 35999U, 100U);
    expect_text(&output, 16U, "M");
    expect_urational(&output, 17U, 91U, 2U);
    roundtrip_tiff(&output, &decoded);
    expect_version(&decoded);
    expect_coordinate(&decoded, 7U, 23U, 1U, 30U, 1U, 97U, 8U);
    expect_text(&decoded, 29U, "2024:02:29");
    expect_urational(&decoded, 13U, 2469U, 20U);
    expect_urational(&decoded, 15U, 35999U, 100U);
    expect_urational(&decoded, 17U, 91U, 2U);
    omc_store_fini(&decoded);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_destination_roundtrip(void)
{
    omc_store source;
    omc_store output;
    omc_store decoded;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&decoded);
    add_xmp(&source, "GPSDestLatitude", "35,48.125S");
    add_xmp(&source, "GPSDestLongitude", "139,34,55.25E");
    add_xmp(&source, "GPSDestBearingRef", "True North");
    add_xmp(&source, "GPSDestBearing", "359.99");
    add_xmp(&source, "GPSDestDistanceRef", "Nautical miles");
    add_xmp(&source, "GPSDestDistance", "12345/100");
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_DEST_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_destination(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.source_properties == 6U && result.groups_translated == 4U);
    assert(result.entries_added == 9U);
    expect_version(&output);
    expect_text(&output, 19U, "S");
    expect_coordinate(&output, 20U, 35U, 1U, 48U, 1U, 15U, 2U);
    expect_text(&output, 21U, "E");
    expect_coordinate(&output, 22U, 139U, 1U, 34U, 1U, 221U, 4U);
    expect_text(&output, 23U, "T");
    expect_urational(&output, 24U, 35999U, 100U);
    expect_text(&output, 25U, "N");
    expect_urational(&output, 26U, 2469U, 20U);
    roundtrip_tiff(&output, &decoded);
    expect_version(&decoded);
    expect_coordinate(&decoded, 20U, 35U, 1U, 48U, 1U, 15U, 2U);
    expect_coordinate(&decoded, 22U, 139U, 1U, 34U, 1U, 221U, 4U);
    expect_urational(&decoded, 24U, 35999U, 100U);
    expect_urational(&decoded, 26U, 2469U, 20U);
    omc_store_fini(&decoded);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_quality_roundtrip(void)
{
    omc_store source;
    omc_store output;
    omc_store decoded;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&decoded);
    add_xmp(&source, "GPSStatus", "Measurement Void");
    add_xmp(&source, "GPSMeasureMode", "3");
    add_xmp(&source, "GPSDOP", "1.25");
    add_xmp(&source, "GPSDifferential", "Differential Corrected");
    add_xmp(&source, "GPSHPositioningError", "4.5");
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_QUALITY_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_quality(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.source_properties == 5U && result.groups_translated == 5U);
    assert(result.entries_added == 6U);
    expect_version(&output);
    expect_text(&output, 9U, "V");
    expect_text(&output, 10U, "3");
    expect_urational(&output, 11U, 5U, 4U);
    assert(find_tag(&output, 30U)->value.u.u64 == 1U);
    expect_urational(&output, 31U, 9U, 2U);
    roundtrip_tiff(&output, &decoded);
    expect_version(&decoded);
    expect_text(&decoded, 9U, "V");
    expect_text(&decoded, 10U, "3");
    expect_urational(&decoded, 11U, 5U, 4U);
    assert(find_tag(&decoded, 30U)->value.u.u64 == 1U);
    expect_urational(&decoded, 31U, 9U, 2U);
    omc_store_fini(&decoded);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_text_roundtrip(void)
{
    omc_store source;
    omc_store output;
    omc_store decoded;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    static const omc_u8 processing[] =
        { 'A','S','C','I','I',0U,0U,0U,'G','P','S',' ','W','L','A','N' };
    static const omc_u8 area[] =
        { 'A','S','C','I','I',0U,0U,0U,'T','o','k','y','o' };

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&decoded);
    add_xmp(&source, "GPSSatellites", "04 07 12");
    add_xmp(&source, "GPSMapDatum", "WGS-84");
    add_xmp(&source, "GPSProcessingMethod", "GPS WLAN");
    add_xmp(&source, "GPSAreaInformation", "Tokyo");
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_TEXT_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_text(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.source_properties == 4U && result.groups_translated == 4U);
    assert(result.entries_added == 5U);
    expect_version(&output);
    expect_text(&output, 8U, "04 07 12");
    expect_text(&output, 18U, "WGS-84");
    expect_bytes(&output, 27U, processing, sizeof(processing));
    expect_bytes(&output, 28U, area, sizeof(area));
    roundtrip_tiff(&output, &decoded);
    expect_version(&decoded);
    expect_text(&decoded, 8U, "04 07 12");
    expect_text(&decoded, 18U, "WGS-84");
    expect_bytes(&decoded, 27U, processing, sizeof(processing));
    expect_bytes(&decoded, 28U, area, sizeof(area));
    omc_store_fini(&decoded);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_text_unicode_encoding(void)
{
    omc_store source;
    omc_store output;
    omc_store decoded;
    omc_store again;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    const char area_text[] =
        "Tokyo \xE6\x9D\xB1\xE4\xBA\xAC \xF0\x9F\x8C\x90";
    static const omc_u8 expected[] = {
        'U','N','I','C','O','D','E',0U,0xFFU,0xFEU,
        'T',0U,'o',0U,'k',0U,'y',0U,'o',0U,' ',0U,
        0x71U,0x67U,0xACU,0x4EU,' ',0U,0x3CU,0xD8U,0x10U,0xDFU
    };

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&decoded);
    omc_store_init(&again);
    add_xmp(&source, "GPSProcessingMethod", "GPS WLAN");
    add_xmp(&source, "GPSAreaInformation", area_text);
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_TEXT_TRANSLATE_PROCESSING_METHOD
                    | OMC_GPS_TEXT_TRANSLATE_AREA_INFORMATION;
    result = omc_translate_xmp_gps_text(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.groups_translated == 2U && result.entries_added == 3U);
    expect_bytes(&output, 28U, expected, sizeof(expected));
    roundtrip_tiff(&output, &decoded);
    expect_bytes(&decoded, 28U, expected, sizeof(expected));
    result = omc_translate_xmp_gps_text(&output, &again, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.groups_unchanged == 2U && result.entries_added == 0U);
    omc_store_fini(&again);
    omc_store_fini(&decoded);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_navigation_timestamp_edges(void)
{
    static const char *const valid[] = {
        "2024-01-01T00:30:00+01:00",
        "2023-12-31T23:30:00-02:00",
        "2000-03-01T00:00:00+00:30",
        "1900-03-01T00:00:00+00:30",
        "0001-01-01T00:00:00Z",
        "9999-12-31T23:59:00-00:00",
        "2024-01-01T00:00:00-23:59",
        "2024-01-01T00:00:00.0000000005Z"
    };
    static const char *const invalid[] = {
        "2023-02-29T12:34:56Z", "0000-01-01T00:00:00Z",
        "2024-13-01T00:00:00Z", "2024-01-00T00:00:00Z",
        "2024-04-31T00:00:00Z", "2024-01-01T24:00:00Z",
        "2024-01-01T23:60:00Z", "2024-01-01T23:59:60Z",
        "2024-01-01T00:00:00+24:00", "2024-01-01T00:00:00-01:60",
        "0001-01-01T00:00:00+00:01", "9999-12-31T23:59:00-00:01",
        "2024-02-29", "2024-02-29T12:34:56z",
        "2024-02-29T12:34:5Z", "2024-02-29T12:34:56.Z",
        "2024-02-29T12:34:56Zjunk",
        "2024-01-01T00:00:00.0000000001Z"
    };
    static const char *const dates[] = {
        "2023:12:31", "2024:01:01", "2000:02:29", "1900:02:28",
        "0001:01:01", "9999:12:31", "2024:01:01", "2024:01:01"
    };
    static const omc_u32 hours[] = { 23U, 1U, 23U, 23U, 0U, 23U, 23U, 0U };
    static const omc_u32 minutes[] = { 30U, 30U, 30U, 30U, 0U, 59U, 59U, 0U };
    static const omc_u32 seconds_numer[] =
        { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U };
    static const omc_u32 seconds_denom[] =
        { 1U, 1U, 1U, 1U, 1U, 1U, 1U, 2000000000U };
    omc_size i;
    for (i = 0U; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        omc_store source;
        omc_store output;
        omc_gps_translation_opts opts;
        omc_gps_translation_res result;
        omc_store_init(&source);
        omc_store_init(&output);
        add_xmp(&source, "GPSTimeStamp", valid[i]);
        omc_gps_translation_opts_init(&opts);
        opts.mappings = OMC_GPS_NAV_TRANSLATE_TIMESTAMP;
        result = omc_translate_xmp_gps_navigation(&source, &output, &opts);
        assert(result.status == OMC_GPS_TRANSLATION_OK);
        expect_text(&output, 29U, dates[i]);
        expect_coordinate(&output, 7U, hours[i], 1U, minutes[i], 1U,
                          seconds_numer[i], seconds_denom[i]);
        omc_store_fini(&output);
        omc_store_fini(&source);
    }
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        omc_store source;
        omc_store output;
        omc_gps_translation_opts opts;
        omc_gps_translation_res result;
        omc_store_init(&source);
        omc_store_init(&output);
        add_xmp(&source, "GPSTimeStamp", invalid[i]);
        omc_gps_translation_opts_init(&opts);
        opts.mappings = OMC_GPS_NAV_TRANSLATE_TIMESTAMP;
        result = omc_translate_xmp_gps_navigation(&source, &output, &opts);
        assert(result.status == OMC_GPS_TRANSLATION_INVALID_SOURCE
               || result.status == OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE
               || result.status == OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION);
        omc_store_fini(&output);
        omc_store_fini(&source);
    }
}

static void
test_navigation_removal_cleans_version(void)
{
    omc_store source;
    omc_store output;
    omc_store removed;
    omc_store cleaned;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_edit edit;
    omc_size i;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&removed);
    omc_store_init(&cleaned);
    add_xmp(&source, "GPSTimeStamp", "2024-03-01T00:30:12.125+01:00");
    add_xmp(&source, "GPSSpeedRef", "K");
    add_xmp(&source, "GPSSpeed", "12345/100");
    add_xmp(&source, "GPSTrackRef", "T");
    add_xmp(&source, "GPSTrack", "359.99");
    add_xmp(&source, "GPSImgDirectionRef", "M");
    add_xmp(&source, "GPSImgDirection", "45.5");
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_NAV_TRANSLATE_ALL;
    result = omc_translate_xmp_gps_navigation(&source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    omc_edit_init(&edit);
    for (i = 0U; i < output.entry_count; ++i) {
        if (output.entries[i].key.kind == OMC_KEY_XMP_PROPERTY)
            assert(omc_edit_tombstone(&edit, (omc_entry_id)i) == OMC_STATUS_OK);
    }
    assert(omc_edit_commit(&output, &edit, 1U, &removed) == OMC_STATUS_OK);
    omc_edit_fini(&edit);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp_gps_navigation(&removed, &cleaned, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.entries_removed == 9U);
    assert(find_tag(&cleaned, 0U) == NULL);
    assert(find_tag(&cleaned, 7U) == NULL);
    assert(find_tag(&cleaned, 12U) == NULL);
    assert(find_tag(&cleaned, 14U) == NULL);
    assert(find_tag(&cleaned, 16U) == NULL);
    assert(find_tag(&cleaned, 29U) == NULL);
    omc_store_fini(&cleaned);
    omc_store_fini(&removed);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

int
main(void)
{
    test_navigation();
    test_destination_quality_text();
    test_navigation_roundtrip();
    test_destination_roundtrip();
    test_quality_roundtrip();
    test_text_roundtrip();
    test_text_unicode_encoding();
    test_navigation_timestamp_edges();
    test_navigation_removal_cleans_version();
    return 0;
}
