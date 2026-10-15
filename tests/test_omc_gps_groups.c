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

static omc_byte_ref
append_bytes(omc_store *store, const omc_u8 *bytes, omc_size size)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, bytes, size, &ref) == OMC_STATUS_OK);
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

static void
add_xmp_flags(omc_store *store, const char *path, const char *text,
              omc_entry_flags flags)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append(store, k_ns),
                              append(store, path));
    omc_val_make_text(&entry.value, append(store, text), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.wire_type_name = append(store, "gps-source");
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static omc_entry_id
add_native_text(omc_store *store, omc_u16 tag, const char *text,
                omc_entry_flags flags, omc_u32 wire_count)
{
    omc_entry entry;
    omc_entry_id id;
    omc_byte_ref ifd;
    memset(&entry, 0, sizeof(entry));
    ifd = append(store, "gpsifd");
    omc_key_make_exif_tag(&entry.key, ifd, tag);
    omc_val_make_text(&entry.value, append(store, text), OMC_TEXT_ASCII);
    entry.origin.block = 7U;
    entry.origin.order_in_block = (omc_u32)(100U + tag);
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 2U;
    entry.origin.wire_count = wire_count;
    entry.origin.wire_type_name = append(store, "gps-native-text");
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_native_rational(omc_store *store, omc_u16 tag, omc_u32 numer,
                    omc_u32 denom, omc_entry_flags flags)
{
    omc_entry entry;
    omc_entry_id id;
    omc_byte_ref ifd;
    omc_urational rational;
    memset(&entry, 0, sizeof(entry));
    ifd = append(store, "gpsifd");
    omc_key_make_exif_tag(&entry.key, ifd, tag);
    rational.numer = numer;
    rational.denom = denom;
    omc_val_make_urational(&entry.value, rational);
    entry.origin.block = 7U;
    entry.origin.order_in_block = (omc_u32)(100U + tag);
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 5U;
    entry.origin.wire_count = 1U;
    entry.origin.wire_type_name = append(store, "gps-native-rational");
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_native_coordinate(omc_store *store, omc_u16 tag, omc_entry_flags flags)
{
    static const omc_u8 big_endian[] = {
        0U, 0U, 0U, 70U, 0U, 0U, 0U, 2U,
        0U, 0U, 0U, 96U, 0U, 0U, 0U, 2U,
        0U, 0U, 0U, 150U, 0U, 0U, 0U, 20U
    };
    omc_entry entry;
    omc_entry_id id;
    omc_byte_ref ifd;
    memset(&entry, 0, sizeof(entry));
    ifd = append(store, "gpsifd");
    omc_key_make_exif_tag(&entry.key, ifd, tag);
    omc_val_make_array(&entry.value, OMC_ELEM_URATIONAL, 3U,
                       append_bytes(store, big_endian, sizeof(big_endian)),
                       OMC_BYTE_ORDER_BIG);
    entry.origin.block = 7U;
    entry.origin.order_in_block = (omc_u32)(100U + tag);
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 5U;
    entry.origin.wire_count = 3U;
    entry.origin.wire_type_name = append(store, "gps-native-rational-array");
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_native_version(omc_store *store)
{
    static const omc_u8 version[4] = { 2U, 3U, 0U, 0U };
    omc_entry entry;
    omc_entry_id id;
    omc_byte_ref ifd;
    memset(&entry, 0, sizeof(entry));
    ifd = append(store, "gpsifd");
    omc_key_make_exif_tag(&entry.key, ifd, 0U);
    omc_val_make_array(&entry.value, OMC_ELEM_U8, 4U,
                       append_bytes(store, version, sizeof(version)),
                       OMC_BYTE_ORDER_NATIVE);
    entry.origin.block = 7U;
    entry.origin.order_in_block = 1U;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 1U;
    entry.origin.wire_count = 4U;
    entry.origin.wire_type_name = append(store, "gps-version");
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static const omc_entry *
find_tag_any(const omc_store *store, omc_u16 tag)
{
    omc_size i;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        omc_const_bytes ifd;
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag)
            continue;
        ifd = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd.size == 6U && memcmp(ifd.data, "gpsifd", 6U) == 0)
            return entry;
    }
    return NULL;
}

static void
assert_same_value_and_origin(const omc_store *source,
                             const omc_entry *before,
                             const omc_store *output,
                             const omc_entry *after)
{
    omc_const_bytes a;
    omc_const_bytes b;
    omc_const_bytes wire_a;
    omc_const_bytes wire_b;
    assert(before->value.kind == after->value.kind);
    assert(before->value.elem_type == after->value.elem_type);
    assert(before->value.text_encoding == after->value.text_encoding);
    assert(before->value.count == after->value.count);
    assert(before->value.byte_order == after->value.byte_order);
    if (before->value.kind == OMC_VAL_TEXT
        || before->value.kind == OMC_VAL_BYTES
        || before->value.kind == OMC_VAL_ARRAY) {
        a = omc_arena_view(&source->arena, before->value.u.ref);
        b = omc_arena_view(&output->arena, after->value.u.ref);
        assert(a.size == b.size);
        assert(a.size == 0U || memcmp(a.data, b.data, a.size) == 0);
    } else if (before->value.kind == OMC_VAL_SCALAR
               && before->value.elem_type == OMC_ELEM_URATIONAL) {
        assert(before->value.u.ur.numer == after->value.u.ur.numer);
        assert(before->value.u.ur.denom == after->value.u.ur.denom);
    } else {
        assert(before->value.u.u64 == after->value.u.u64);
    }
    assert(before->origin.block == after->origin.block);
    assert(before->origin.order_in_block == after->origin.order_in_block);
    assert(before->origin.wire_type.family == after->origin.wire_type.family);
    assert(before->origin.wire_type.code == after->origin.wire_type.code);
    assert(before->origin.wire_count == after->origin.wire_count);
    assert(before->origin.name_context_kind == after->origin.name_context_kind);
    assert(before->origin.name_context_variant == after->origin.name_context_variant);
    wire_a = omc_arena_view(&source->arena, before->origin.wire_type_name);
    wire_b = omc_arena_view(&output->arena, after->origin.wire_type_name);
    assert(wire_a.size == wire_b.size);
    assert(wire_a.size == 0U || memcmp(wire_a.data, wire_b.data, wire_a.size) == 0);
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

typedef enum gps_lifecycle_kind {
    GPS_LIFECYCLE_NAVIGATION,
    GPS_LIFECYCLE_DESTINATION,
    GPS_LIFECYCLE_QUALITY,
    GPS_LIFECYCLE_TEXT
} gps_lifecycle_kind;

static omc_u32
lifecycle_exact_mapping(gps_lifecycle_kind kind)
{
    switch (kind) {
    case GPS_LIFECYCLE_NAVIGATION:
        return OMC_GPS_NAV_TRANSLATE_SPEED;
    case GPS_LIFECYCLE_DESTINATION:
        return OMC_GPS_DEST_TRANSLATE_LATITUDE;
    case GPS_LIFECYCLE_QUALITY:
        return OMC_GPS_QUALITY_TRANSLATE_DOP;
    case GPS_LIFECYCLE_TEXT:
        return OMC_GPS_TEXT_TRANSLATE_MAP_DATUM;
    }
    return 0U;
}

static omc_u32
lifecycle_delete_mapping(gps_lifecycle_kind kind)
{
    switch (kind) {
    case GPS_LIFECYCLE_NAVIGATION:
        return OMC_GPS_NAV_TRANSLATE_TIMESTAMP;
    case GPS_LIFECYCLE_DESTINATION:
        return OMC_GPS_DEST_TRANSLATE_LATITUDE;
    case GPS_LIFECYCLE_QUALITY:
        return OMC_GPS_QUALITY_TRANSLATE_STATUS
               | OMC_GPS_QUALITY_TRANSLATE_DOP;
    case GPS_LIFECYCLE_TEXT:
        return OMC_GPS_TEXT_TRANSLATE_SATELLITES
               | OMC_GPS_TEXT_TRANSLATE_MAP_DATUM;
    }
    return 0U;
}

static omc_u32
lifecycle_group_count(gps_lifecycle_kind kind)
{
    return kind == GPS_LIFECYCLE_QUALITY || kind == GPS_LIFECYCLE_TEXT
               ? 2U : 1U;
}

static omc_gps_translation_res
lifecycle_translate(gps_lifecycle_kind kind, const omc_store *source,
                    omc_store *output, const omc_gps_translation_opts *opts)
{
    switch (kind) {
    case GPS_LIFECYCLE_NAVIGATION:
        return omc_translate_xmp_gps_navigation(source, output, opts);
    case GPS_LIFECYCLE_DESTINATION:
        return omc_translate_xmp_gps_destination(source, output, opts);
    case GPS_LIFECYCLE_QUALITY:
        return omc_translate_xmp_gps_quality(source, output, opts);
    case GPS_LIFECYCLE_TEXT:
        return omc_translate_xmp_gps_text(source, output, opts);
    }
    {
        omc_gps_translation_res result;
        memset(&result, 0, sizeof(result));
        result.status = OMC_GPS_TRANSLATION_INTERNAL;
        return result;
    }
}

static void
populate_exact_group(omc_store *source, gps_lifecycle_kind kind)
{
    add_native_version(source);
    if (kind == GPS_LIFECYCLE_NAVIGATION) {
        add_xmp(source, "GPSSpeedRef", "K");
        add_xmp(source, "GPSSpeed", "12.5");
        add_native_text(source, 12U, "K", OMC_ENTRY_FLAG_NONE, 1U);
        add_native_rational(source, 13U, 250U, 20U,
                            OMC_ENTRY_FLAG_NONE);
    } else if (kind == GPS_LIFECYCLE_DESTINATION) {
        add_xmp(source, "GPSDestLatitude", "35,48.125N");
        add_native_text(source, 19U, "N", OMC_ENTRY_FLAG_NONE, 1U);
        add_native_coordinate(source, 20U, OMC_ENTRY_FLAG_NONE);
    } else if (kind == GPS_LIFECYCLE_QUALITY) {
        add_xmp(source, "GPSDOP", "1.25");
        add_native_rational(source, 11U, 10U, 8U,
                            OMC_ENTRY_FLAG_NONE);
    } else {
        add_xmp(source, "GPSMapDatum", "WGS-84");
        add_native_text(source, 18U, "WGS-84", OMC_ENTRY_FLAG_NONE, 6U);
    }
}

static void
populate_deleted_sources(omc_store *source, gps_lifecycle_kind kind,
                         omc_entry_flags native_flags, int with_tombstone)
{
    const omc_entry_flags deleted =
        OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    add_native_version(source);
    if (kind == GPS_LIFECYCLE_NAVIGATION) {
        add_xmp_flags(source, "GPSTimeStamp", "", deleted);
        if (with_tombstone)
            add_native_coordinate(source, 7U, native_flags);
    } else if (kind == GPS_LIFECYCLE_DESTINATION) {
        add_xmp_flags(source, "GPSDestLatitude", "", deleted);
        if (with_tombstone)
            add_native_text(source, 19U, "N", native_flags, 1U);
    } else if (kind == GPS_LIFECYCLE_QUALITY) {
        add_xmp_flags(source, "GPSStatus", "", deleted);
        add_xmp_flags(source, "GPSDOP", "", deleted);
        if (with_tombstone)
            add_native_text(source, 9U, "A", native_flags, 1U);
    } else {
        add_xmp_flags(source, "GPSSatellites", "", deleted);
        add_xmp_flags(source, "GPSMapDatum", "", deleted);
        if (with_tombstone)
            add_native_text(source, 8U, "8", native_flags, 1U);
    }
}

static void
assert_clean_native_group(const omc_store *source, const omc_store *output,
                          gps_lifecycle_kind kind,
                          omc_entry_flags source_flags)
{
    static const omc_u16 nav_tags[] = { 12U, 13U };
    static const omc_u16 destination_tags[] = { 19U, 20U };
    static const omc_u16 quality_tags[] = { 11U };
    static const omc_u16 text_tags[] = { 18U };
    const omc_u16 *tags;
    omc_size count;
    omc_size i;
    if (kind == GPS_LIFECYCLE_NAVIGATION) {
        tags = nav_tags;
        count = sizeof(nav_tags) / sizeof(nav_tags[0]);
    } else if (kind == GPS_LIFECYCLE_DESTINATION) {
        tags = destination_tags;
        count = sizeof(destination_tags) / sizeof(destination_tags[0]);
    } else if (kind == GPS_LIFECYCLE_QUALITY) {
        tags = quality_tags;
        count = sizeof(quality_tags) / sizeof(quality_tags[0]);
    } else {
        tags = text_tags;
        count = sizeof(text_tags) / sizeof(text_tags[0]);
    }
    for (i = 0U; i < count; ++i) {
        const omc_entry *before = find_tag(source, tags[i]);
        const omc_entry *after = find_tag(output, tags[i]);
        assert(before != NULL && after != NULL);
        assert(before->flags == source_flags);
        assert(after->flags == (source_flags | OMC_ENTRY_FLAG_DIRTY));
        assert_same_value_and_origin(source, before, output, after);
    }
}

static void
test_group_exact_promotion(gps_lifecycle_kind kind)
{
    omc_store source;
    omc_store output;
    omc_store repeated;
    omc_store preserve_source;
    omc_store preserve_output;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&repeated);
    omc_store_init(&preserve_source);
    omc_store_init(&preserve_output);
    populate_exact_group(&source, kind);
    omc_gps_translation_opts_init(&opts);
    opts.mappings = lifecycle_exact_mapping(kind);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = lifecycle_translate(kind, &source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.groups_translated == 1U);
    assert(result.groups_unchanged == 0U);
    assert(result.entries_updated ==
           (kind == GPS_LIFECYCLE_NAVIGATION
                || kind == GPS_LIFECYCLE_DESTINATION ? 2U : 1U));
    assert(result.entries_added == 0U && result.entries_removed == 0U);
    assert_clean_native_group(&source, &output, kind, OMC_ENTRY_FLAG_NONE);

    result = lifecycle_translate(kind, &output, &repeated, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.groups_unchanged == 1U);
    assert(result.entries_added == 0U && result.entries_updated == 0U
           && result.entries_removed == 0U);
    assert_clean_native_group(&output, &repeated, kind, OMC_ENTRY_FLAG_DIRTY);

    populate_exact_group(&preserve_source, kind);
    opts.conflict = OMC_TRANSLATION_PRESERVE;
    result = lifecycle_translate(kind, &preserve_source, &preserve_output,
                                 &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OK);
    assert(result.groups_preserved == 1U);
    assert(result.entries_added == 0U && result.entries_updated == 0U
           && result.entries_removed == 0U);
    {
        const omc_store *expected_source = &preserve_source;
        const omc_store *actual_output = &preserve_output;
        if (kind == GPS_LIFECYCLE_NAVIGATION) {
            assert(find_tag(actual_output, 12U)->flags == OMC_ENTRY_FLAG_NONE);
            assert(find_tag(actual_output, 13U)->flags == OMC_ENTRY_FLAG_NONE);
        } else if (kind == GPS_LIFECYCLE_DESTINATION) {
            assert(find_tag(actual_output, 19U)->flags == OMC_ENTRY_FLAG_NONE);
            assert(find_tag(actual_output, 20U)->flags == OMC_ENTRY_FLAG_NONE);
        } else if (kind == GPS_LIFECYCLE_QUALITY) {
            assert(find_tag(actual_output, 11U)->flags == OMC_ENTRY_FLAG_NONE);
        } else {
            assert(find_tag(actual_output, 18U)->flags == OMC_ENTRY_FLAG_NONE);
        }
        (void)expected_source;
    }
    omc_store_fini(&preserve_output);
    omc_store_fini(&preserve_source);
    omc_store_fini(&repeated);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
assert_deleted_intent(const omc_store *store, omc_u16 tag,
                      omc_val_kind kind, omc_elem_type element,
                      omc_u32 count)
{
    const omc_entry *entry = find_tag_any(store, tag);
    assert(entry != NULL);
    assert(entry->flags == (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert(entry->value.kind == kind);
    assert(entry->value.elem_type == element);
    assert(entry->value.count == count);
}

static void
assert_deleted_group_values(const omc_store *store,
                            gps_lifecycle_kind kind)
{
    if (kind == GPS_LIFECYCLE_NAVIGATION) {
        assert_deleted_intent(store, 7U, OMC_VAL_ARRAY,
                              OMC_ELEM_URATIONAL, 3U);
        assert_deleted_intent(store, 29U, OMC_VAL_TEXT, OMC_ELEM_U8, 10U);
    } else if (kind == GPS_LIFECYCLE_DESTINATION) {
        assert_deleted_intent(store, 19U, OMC_VAL_TEXT, OMC_ELEM_U8, 1U);
        assert_deleted_intent(store, 20U, OMC_VAL_ARRAY,
                              OMC_ELEM_URATIONAL, 3U);
    } else if (kind == GPS_LIFECYCLE_QUALITY) {
        assert_deleted_intent(store, 9U, OMC_VAL_TEXT, OMC_ELEM_U8, 1U);
        assert_deleted_intent(store, 11U, OMC_VAL_SCALAR,
                              OMC_ELEM_URATIONAL, 1U);
    } else {
        assert_deleted_intent(store, 8U, OMC_VAL_TEXT, OMC_ELEM_U8, 1U);
        assert_deleted_intent(store, 18U, OMC_VAL_TEXT, OMC_ELEM_U8, 0U);
    }
}

static void
test_group_deletion_lifecycle(gps_lifecycle_kind kind,
                              omc_entry_flags native_flags)
{
    omc_store source;
    omc_store output;
    omc_store repeated;
    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&repeated);
    populate_deleted_sources(&source, kind, native_flags, 1);
    {
        const omc_entry *before =
            find_tag_any(&source, kind == GPS_LIFECYCLE_NAVIGATION ? 7U
                         : kind == GPS_LIFECYCLE_DESTINATION ? 19U
                         : kind == GPS_LIFECYCLE_QUALITY ? 9U : 8U);
        omc_gps_translation_opts opts;
        omc_gps_translation_res result;
        const omc_u16 source_tag =
            kind == GPS_LIFECYCLE_NAVIGATION ? 7U
            : kind == GPS_LIFECYCLE_DESTINATION ? 19U
            : kind == GPS_LIFECYCLE_QUALITY ? 9U : 8U;
        omc_u32 expected_added = 1U;
        omc_u32 expected_translated;
        omc_size repeated_groups;
        assert(before != NULL && before->flags == native_flags);
        omc_gps_translation_opts_init(&opts);
        opts.mappings = lifecycle_delete_mapping(kind);
        result = lifecycle_translate(kind, &source, &output, &opts);
        assert(result.status == OMC_GPS_TRANSLATION_OK);
        expected_translated = lifecycle_group_count(kind);
        if ((kind == GPS_LIFECYCLE_QUALITY || kind == GPS_LIFECYCLE_TEXT)
            && native_flags == (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) {
            --expected_translated;
            assert(result.groups_unchanged == 1U);
        }
        assert(result.groups_translated == expected_translated);
        assert(result.entries_added == expected_added);
        assert(result.entries_updated ==
               (native_flags == OMC_ENTRY_FLAG_DELETED ? 1U : 0U));
        assert(result.entries_removed == 1U);
        assert(find_tag(&output, 0U) == NULL);
        assert_deleted_group_values(&output, kind);
        if (native_flags == OMC_ENTRY_FLAG_DELETED) {
            const omc_entry *after = find_tag_any(&output, source_tag);
            assert(after != NULL);
            assert_same_value_and_origin(&source, before, &output, after);
        }
        repeated_groups = lifecycle_group_count(kind);
        result = lifecycle_translate(kind, &output, &repeated, &opts);
        assert(result.status == OMC_GPS_TRANSLATION_OK);
        assert(result.groups_unchanged == repeated_groups);
        assert(result.entries_added == 0U && result.entries_updated == 0U
               && result.entries_removed == 0U);
        assert(repeated.entry_count == output.entry_count);
        assert_deleted_group_values(&repeated, kind);
    }
    omc_store_fini(&repeated);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
add_sentinel(omc_store *store)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_comment(&entry.key);
    omc_val_make_text(&entry.value, append(store, "keep"), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static void
assert_sentinel(const omc_store *store)
{
    omc_const_bytes bytes;
    assert(store->entry_count == 1U);
    assert(store->entries[0].key.kind == OMC_KEY_COMMENT);
    assert(store->entries[0].value.kind == OMC_VAL_TEXT);
    bytes = omc_arena_view(&store->arena, store->entries[0].value.u.ref);
    assert(bytes.size == 4U && memcmp(bytes.data, "keep", 4U) == 0);
}

static void
test_group_limit_rollback(gps_lifecycle_kind kind)
{
    omc_store source;
    omc_store output;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_entry_flags flags[8];
    omc_size i;
    omc_store_init(&source);
    omc_store_init(&output);
    populate_deleted_sources(&source, kind, OMC_ENTRY_FLAG_NONE, 0);
    add_sentinel(&output);
    assert(source.entry_count <= sizeof(flags) / sizeof(flags[0]));
    for (i = 0U; i < source.entry_count; ++i)
        flags[i] = source.entries[i].flags;

    omc_gps_translation_opts_init(&opts);
    opts.mappings = lifecycle_delete_mapping(kind);
    opts.max_added_entries = 1U;
    result = lifecycle_translate(kind, &source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_ENTRY_LIMIT);
    assert_sentinel(&output);
    assert(source.entry_count <= sizeof(flags) / sizeof(flags[0]));
    for (i = 0U; i < source.entry_count; ++i)
        assert(source.entries[i].flags == flags[i]);

    opts.max_added_entries =
        kind == GPS_LIFECYCLE_NAVIGATION
                || kind == GPS_LIFECYCLE_DESTINATION
            ? 9U
            : kind == GPS_LIFECYCLE_QUALITY ? 6U : 5U;
    opts.max_operations = 1U;
    result = lifecycle_translate(kind, &source, &output, &opts);
    assert(result.status == OMC_GPS_TRANSLATION_OPERATION_LIMIT);
    assert_sentinel(&output);
    for (i = 0U; i < source.entry_count; ++i)
        assert(source.entries[i].flags == flags[i]);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_group_v2_lifecycle(void)
{
    gps_lifecycle_kind kind;
    for (kind = GPS_LIFECYCLE_NAVIGATION;
         kind <= GPS_LIFECYCLE_TEXT; kind = (gps_lifecycle_kind)(kind + 1)) {
        test_group_exact_promotion(kind);
        test_group_deletion_lifecycle(
            kind, OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
        test_group_deletion_lifecycle(kind, OMC_ENTRY_FLAG_DELETED);
        test_group_limit_rollback(kind);
    }
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
    test_group_v2_lifecycle();
    return 0;
}
