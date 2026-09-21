#include "omc/omc_translation.h"
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
    assert(find_tag(&output, 23U) != NULL && find_tag(&output, 24U) != NULL);
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

int
main(void)
{
    test_navigation();
    test_destination_quality_text();
    return 0;
}
