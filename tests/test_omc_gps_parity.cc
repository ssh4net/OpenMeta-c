#include "omc/omc_translation.h"
#include "omc_test_assert.h"

#include <openmeta/meta_edit.h>
#include <openmeta/meta_key.h>
#include <openmeta/meta_value.h>
#include <openmeta/metadata_translation.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace {

using CppStatus = openmeta::MetadataGpsTranslationStatus;

static std::string_view
view(const omc_store &store, omc_byte_ref ref)
{
    const auto bytes = omc_arena_view(&store.arena, ref);
    return {reinterpret_cast<const char *>(bytes.data), bytes.size};
}

static void
add_c_xmp(omc_store &store, const char *path, const char *value, bool dirty)
{
    omc_entry entry{};
    omc_byte_ref ns;
    omc_byte_ref name;
    omc_byte_ref text;
    assert(omc_arena_append(&store.arena, "http://ns.adobe.com/exif/1.0/",
                            std::strlen("http://ns.adobe.com/exif/1.0/"),
                            &ns) == OMC_STATUS_OK);
    assert(omc_arena_append(&store.arena, path, std::strlen(path), &name) ==
           OMC_STATUS_OK);
    assert(omc_arena_append(&store.arena, value, std::strlen(value), &text) ==
           OMC_STATUS_OK);
    omc_key_make_xmp_property(&entry.key, ns, name);
    omc_val_make_text(&entry.value, text, OMC_TEXT_UTF8);
    entry.origin.wire_type_name = text;
    entry.flags = dirty ? OMC_ENTRY_FLAG_DIRTY : 0U;
    assert(omc_store_add_entry(&store, &entry, nullptr) == OMC_STATUS_OK);
}

static openmeta::MetaStore
make_cpp_source(bool altitude_only)
{
    openmeta::MetaStore store;
    static constexpr std::string_view ns = "http://ns.adobe.com/exif/1.0/";
    auto add = [&](std::string_view path, std::string_view value) {
        openmeta::Entry entry;
        entry.key = openmeta::make_xmp_property_key(store.arena(), ns, path);
        entry.value = openmeta::make_text(store.arena(), value,
                                          openmeta::TextEncoding::Utf8);
        entry.flags = openmeta::EntryFlags::Dirty;
        entry.origin.wire_type_name = store.arena().append_string("gps-source");
        assert(store.add_entry(entry) != openmeta::kInvalidEntryId);
    };
    if (!altitude_only) {
        add("GPSLatitude", "35,48.125N");
        add("GPSLongitude", "139,34,55.25W");
    }
    add("GPSAltitude", "12345/100");
    add("GPSAltitudeRef", "1");
    store.finalize();
    return store;
}

static omc_gps_translation_status
map_status(CppStatus status)
{
    switch (status) {
    case CppStatus::Ok: return OMC_GPS_TRANSLATION_OK;
    case CppStatus::NullOutput: return OMC_GPS_TRANSLATION_NULL_OUTPUT;
    case CppStatus::SourceNotFinalized: return OMC_GPS_TRANSLATION_INVALID_OPTIONS;
    case CppStatus::InvalidOptions: return OMC_GPS_TRANSLATION_INVALID_OPTIONS;
    case CppStatus::AmbiguousSource: return OMC_GPS_TRANSLATION_AMBIGUOUS_SOURCE;
    case CppStatus::IncompleteSource: return OMC_GPS_TRANSLATION_INCOMPLETE_SOURCE;
    case CppStatus::InvalidSourceValue: return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    case CppStatus::ValueOutOfRange: return OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE;
    case CppStatus::UnsupportedPrecision:
        return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
    case CppStatus::UnsupportedGpsVersion:
        return OMC_GPS_TRANSLATION_UNSUPPORTED_VERSION;
    case CppStatus::ValueTooLong: return OMC_GPS_TRANSLATION_VALUE_TOO_LONG;
    case CppStatus::SourceLimitExceeded: return OMC_GPS_TRANSLATION_SOURCE_LIMIT;
    case CppStatus::NativeConflict: return OMC_GPS_TRANSLATION_NATIVE_CONFLICT;
    case CppStatus::EntryLimitExceeded: return OMC_GPS_TRANSLATION_ENTRY_LIMIT;
    case CppStatus::OperationLimitExceeded:
        return OMC_GPS_TRANSLATION_OPERATION_LIMIT;
    case CppStatus::InternalError: return OMC_GPS_TRANSLATION_INTERNAL;
    }
    return OMC_GPS_TRANSLATION_INTERNAL;
}

static const omc_entry *
find_c_gps(const omc_store &store, omc_u16 tag)
{
    for (omc_size i = 0U; i < store.entry_count; ++i) {
        const omc_entry &entry = store.entries[i];
        if (entry.key.kind == OMC_KEY_EXIF_TAG &&
            entry.key.u.exif_tag.tag == tag &&
            (entry.flags & OMC_ENTRY_FLAG_DELETED) == 0U &&
            view(store, entry.key.u.exif_tag.ifd) == "gpsifd")
            return &entry;
    }
    return nullptr;
}

static const openmeta::Entry *
find_cpp_gps(const openmeta::MetaStore &store, uint16_t tag)
{
    for (const auto &entry : store.entries()) {
        if (openmeta::any(entry.flags, openmeta::EntryFlags::Deleted) ||
            entry.key.kind != openmeta::MetaKeyKind::ExifTag ||
            entry.key.data.exif_tag.tag != tag ||
            store.arena().span(entry.key.data.exif_tag.ifd).size() != 6U)
            continue;
        const auto ifd = store.arena().span(entry.key.data.exif_tag.ifd);
        if (std::memcmp(ifd.data(), "gpsifd", 6U) == 0)
            return &entry;
    }
    return nullptr;
}

static void
compare_primary(const omc_store &c, const openmeta::MetaStore &cpp)
{
    for (uint16_t tag = 0U; tag <= 6U; ++tag) {
        const omc_entry *ce = find_c_gps(c, tag);
        const openmeta::Entry *pe = find_cpp_gps(cpp, tag);
        assert((ce == nullptr) == (pe == nullptr));
        if (ce == nullptr)
            continue;
        assert(ce->value.kind == OMC_VAL_ARRAY || ce->value.kind == OMC_VAL_SCALAR ||
               ce->value.kind == OMC_VAL_TEXT);
        if (tag == 1U || tag == 3U) {
            assert(pe->value.kind == openmeta::MetaValueKind::Text);
            const auto p = cpp.arena().span(pe->value.data.span);
            const auto q = omc_arena_view(&c.arena, ce->value.u.ref);
            assert(p.size() == q.size &&
                   std::memcmp(p.data(), q.data, q.size) == 0);
        } else if (tag == 5U) {
            assert(pe->value.data.u64 == ce->value.u.u64);
        } else if (tag == 6U) {
            assert(pe->value.data.ur.numer == ce->value.u.ur.numer &&
                   pe->value.data.ur.denom == ce->value.u.ur.denom);
        } else if (tag == 2U || tag == 4U) {
            const auto p = cpp.arena().span(pe->value.data.span);
            const auto q = omc_arena_view(&c.arena, ce->value.u.ref);
            assert(p.size() == q.size &&
                   std::memcmp(p.data(), q.data, q.size) == 0);
        }
    }
}

static void
run_case(bool altitude_only)
{
    omc_store c_source;
    omc_store c_out;
    omc_gps_translation_opts c_opts;
    omc_gps_translation_res c_result;
    openmeta::MetaStore cpp_source = make_cpp_source(altitude_only);
    openmeta::MetaStore cpp_out;
    openmeta::MetadataGpsTranslationOptions cpp_opts;
    openmeta::MetadataGpsTranslationResult cpp_result;
    omc_store_init(&c_source);
    omc_store_init(&c_out);
    if (!altitude_only) {
        add_c_xmp(c_source, "GPSLatitude", "35,48.125N", true);
        add_c_xmp(c_source, "GPSLongitude", "139,34,55.25W", true);
    }
    add_c_xmp(c_source, "GPSAltitude", "12345/100", true);
    add_c_xmp(c_source, "GPSAltitudeRef", "1", true);
    omc_gps_translation_opts_init(&c_opts);
    if (altitude_only)
        c_opts.mappings = OMC_GPS_TRANSLATE_ALTITUDE;
    c_result = omc_translate_xmp_gps(&c_source, &c_out, &c_opts);
    cpp_opts.latitude_to_exif = !altitude_only;
    cpp_opts.longitude_to_exif = !altitude_only;
    cpp_result = openmeta::translate_xmp_gps_metadata(cpp_source, cpp_opts,
                                                       &cpp_out);
    assert(c_result.status == map_status(cpp_result.status));
    assert(c_result.groups_translated == cpp_result.groups_translated);
    assert(c_result.entries_added == cpp_result.entries_added);
    assert(c_result.entries_updated == cpp_result.entries_updated);
    assert(c_result.entries_removed == cpp_result.entries_removed);
    compare_primary(c_out, cpp_out);
    omc_store_fini(&c_out);
    omc_store_fini(&c_source);
}

enum class GroupKind {
    Navigation,
    Destination,
    Quality,
    Text,
};

static openmeta::MetaStore
make_cpp_group_source(GroupKind kind)
{
    openmeta::MetaStore store;
    static constexpr std::string_view ns = "http://ns.adobe.com/exif/1.0/";
    auto add = [&](std::string_view path, std::string_view value) {
        openmeta::Entry entry;
        entry.key = openmeta::make_xmp_property_key(store.arena(), ns, path);
        entry.value = openmeta::make_text(store.arena(), value,
                                          openmeta::TextEncoding::Utf8);
        entry.flags = openmeta::EntryFlags::Dirty;
        entry.origin.wire_type_name = store.arena().append_string("gps-source");
        assert(store.add_entry(entry) != openmeta::kInvalidEntryId);
    };
    switch (kind) {
    case GroupKind::Navigation:
        add("GPSTimeStamp", "2024-03-01T00:30:12.125+01:00");
        add("GPSSpeedRef", "K");
        add("GPSSpeed", "12345/100");
        add("GPSTrackRef", "T");
        add("GPSTrack", "359.99");
        add("GPSImgDirectionRef", "M");
        add("GPSImgDirection", "45.5");
        break;
    case GroupKind::Destination:
        add("GPSDestLatitude", "35,48.125S");
        add("GPSDestLongitude", "139,34,55.25E");
        add("GPSDestBearingRef", "True North");
        add("GPSDestBearing", "359.99");
        add("GPSDestDistanceRef", "Nautical miles");
        add("GPSDestDistance", "12345/100");
        break;
    case GroupKind::Quality:
        add("GPSStatus", "Measurement Void");
        add("GPSMeasureMode", "3");
        add("GPSDOP", "1.25");
        add("GPSDifferential", "Differential Corrected");
        add("GPSHPositioningError", "4.5");
        break;
    case GroupKind::Text:
        add("GPSSatellites", "04 07 12");
        add("GPSMapDatum", "WGS-84");
        add("GPSProcessingMethod", "GPS WLAN");
        add("GPSAreaInformation",
            "Tokyo \xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\x8c\x90");
        break;
    }
    store.finalize();
    return store;
}

static void
add_c_group_source(omc_store &store, GroupKind kind)
{
    switch (kind) {
    case GroupKind::Navigation:
        add_c_xmp(store, "GPSTimeStamp", "2024-03-01T00:30:12.125+01:00", true);
        add_c_xmp(store, "GPSSpeedRef", "K", true);
        add_c_xmp(store, "GPSSpeed", "12345/100", true);
        add_c_xmp(store, "GPSTrackRef", "T", true);
        add_c_xmp(store, "GPSTrack", "359.99", true);
        add_c_xmp(store, "GPSImgDirectionRef", "M", true);
        add_c_xmp(store, "GPSImgDirection", "45.5", true);
        break;
    case GroupKind::Destination:
        add_c_xmp(store, "GPSDestLatitude", "35,48.125S", true);
        add_c_xmp(store, "GPSDestLongitude", "139,34,55.25E", true);
        add_c_xmp(store, "GPSDestBearingRef", "True North", true);
        add_c_xmp(store, "GPSDestBearing", "359.99", true);
        add_c_xmp(store, "GPSDestDistanceRef", "Nautical miles", true);
        add_c_xmp(store, "GPSDestDistance", "12345/100", true);
        break;
    case GroupKind::Quality:
        add_c_xmp(store, "GPSStatus", "Measurement Void", true);
        add_c_xmp(store, "GPSMeasureMode", "3", true);
        add_c_xmp(store, "GPSDOP", "1.25", true);
        add_c_xmp(store, "GPSDifferential", "Differential Corrected", true);
        add_c_xmp(store, "GPSHPositioningError", "4.5", true);
        break;
    case GroupKind::Text:
        add_c_xmp(store, "GPSSatellites", "04 07 12", true);
        add_c_xmp(store, "GPSMapDatum", "WGS-84", true);
        add_c_xmp(store, "GPSProcessingMethod", "GPS WLAN", true);
        add_c_xmp(store, "GPSAreaInformation",
                  "Tokyo \xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\x8c\x90", true);
        break;
    }
}

static void
compare_result(const omc_gps_translation_res &c,
               const openmeta::MetadataGpsTranslationResult &cpp)
{
    assert(c.status == map_status(cpp.status));
    assert(c.source_properties == cpp.source_properties);
    assert(c.groups_translated == cpp.groups_translated);
    assert(c.groups_preserved == cpp.groups_preserved);
    assert(c.groups_unchanged == cpp.groups_unchanged);
    assert(c.entries_added == cpp.entries_added);
    assert(c.entries_updated == cpp.entries_updated);
    assert(c.entries_removed == cpp.entries_removed);
}

static void
compare_payload(const omc_store &c, const openmeta::MetaStore &cpp,
                uint16_t tag, omc_val_kind c_kind,
                openmeta::MetaValueKind cpp_kind)
{
    const omc_entry *ce = find_c_gps(c, tag);
    const openmeta::Entry *pe = find_cpp_gps(cpp, tag);
    assert(ce != nullptr && pe != nullptr);
    assert(ce->value.kind == c_kind);
    assert(pe->value.kind == cpp_kind);
    const omc_const_bytes c_bytes =
        omc_arena_view(&c.arena, ce->value.u.ref);
    const auto cpp_bytes = cpp.arena().span(pe->value.data.span);
    assert(c_bytes.size == cpp_bytes.size());
    assert(c_bytes.size == 0U ||
           std::memcmp(c_bytes.data, cpp_bytes.data(), c_bytes.size) == 0);
}

static void
compare_text(const omc_store &c, const openmeta::MetaStore &cpp, uint16_t tag)
{
    compare_payload(c, cpp, tag, OMC_VAL_TEXT, openmeta::MetaValueKind::Text);
}

static void
compare_bytes(const omc_store &c, const openmeta::MetaStore &cpp, uint16_t tag,
              omc_val_kind c_kind, openmeta::MetaValueKind cpp_kind)
{
    compare_payload(c, cpp, tag, c_kind, cpp_kind);
}

static void
compare_rational(const omc_store &c, const openmeta::MetaStore &cpp,
                 uint16_t tag)
{
    const omc_entry *ce = find_c_gps(c, tag);
    const openmeta::Entry *pe = find_cpp_gps(cpp, tag);
    assert(ce != nullptr && pe != nullptr);
    assert(ce->value.kind == OMC_VAL_SCALAR);
    assert(ce->value.elem_type == OMC_ELEM_URATIONAL);
    assert(pe->value.kind == openmeta::MetaValueKind::Scalar);
    assert(pe->value.elem_type == openmeta::MetaElementType::URational);
    assert(ce->value.u.ur.numer == pe->value.data.ur.numer);
    assert(ce->value.u.ur.denom == pe->value.data.ur.denom);
}

static void
compare_u16(const omc_store &c, const openmeta::MetaStore &cpp, uint16_t tag)
{
    const omc_entry *ce = find_c_gps(c, tag);
    const openmeta::Entry *pe = find_cpp_gps(cpp, tag);
    assert(ce != nullptr && pe != nullptr);
    assert(ce->value.kind == OMC_VAL_SCALAR);
    assert(ce->value.elem_type == OMC_ELEM_U16);
    assert(pe->value.kind == openmeta::MetaValueKind::Scalar);
    assert(pe->value.elem_type == openmeta::MetaElementType::U16);
    assert(ce->value.u.u64 == pe->value.data.u64);
}

static void
compare_rational_array(const omc_store &c, const openmeta::MetaStore &cpp,
                       uint16_t tag)
{
    const omc_entry *ce = find_c_gps(c, tag);
    const openmeta::Entry *pe = find_cpp_gps(cpp, tag);
    openmeta::URational cpp_values[3];
    omc_urational c_values[3];
    assert(ce != nullptr && pe != nullptr);
    assert(ce->value.kind == OMC_VAL_ARRAY);
    assert(ce->value.elem_type == OMC_ELEM_URATIONAL);
    assert(ce->value.count == 3U);
    assert(pe->value.kind == openmeta::MetaValueKind::Array);
    assert(pe->value.elem_type == openmeta::MetaElementType::URational);
    assert(pe->value.count == 3U);
    const omc_const_bytes c_bytes =
        omc_arena_view(&c.arena, ce->value.u.ref);
    const auto cpp_bytes = cpp.arena().span(pe->value.data.span);
    assert(c_bytes.size == sizeof(c_values));
    assert(cpp_bytes.size() == sizeof(cpp_values));
    std::memcpy(c_values, c_bytes.data, sizeof(c_values));
    std::memcpy(cpp_values, cpp_bytes.data(), sizeof(cpp_values));
    for (unsigned i = 0U; i < 3U; ++i) {
        assert(c_values[i].numer == cpp_values[i].numer);
        assert(c_values[i].denom == cpp_values[i].denom);
    }
}

static void
compare_version(const omc_store &c, const openmeta::MetaStore &cpp)
{
    compare_bytes(c, cpp, 0U, OMC_VAL_ARRAY, openmeta::MetaValueKind::Array);
    const omc_entry *ce = find_c_gps(c, 0U);
    const openmeta::Entry *pe = find_cpp_gps(cpp, 0U);
    assert(ce->value.elem_type == OMC_ELEM_U8 && ce->value.count == 4U);
    assert(pe->value.elem_type == openmeta::MetaElementType::U8 &&
           pe->value.count == 4U);
}

static void
compare_navigation_output(const omc_store &c, const openmeta::MetaStore &cpp)
{
    compare_version(c, cpp);
    compare_rational_array(c, cpp, 7U);
    compare_text(c, cpp, 29U);
    compare_text(c, cpp, 12U);
    compare_rational(c, cpp, 13U);
    compare_text(c, cpp, 14U);
    compare_rational(c, cpp, 15U);
    compare_text(c, cpp, 16U);
    compare_rational(c, cpp, 17U);
}

static void
compare_destination_output(const omc_store &c, const openmeta::MetaStore &cpp)
{
    compare_version(c, cpp);
    compare_text(c, cpp, 19U);
    compare_rational_array(c, cpp, 20U);
    compare_text(c, cpp, 21U);
    compare_rational_array(c, cpp, 22U);
    compare_text(c, cpp, 23U);
    compare_rational(c, cpp, 24U);
    compare_text(c, cpp, 25U);
    compare_rational(c, cpp, 26U);
}

static void
compare_quality_output(const omc_store &c, const openmeta::MetaStore &cpp)
{
    compare_version(c, cpp);
    compare_text(c, cpp, 9U);
    compare_text(c, cpp, 10U);
    compare_rational(c, cpp, 11U);
    compare_u16(c, cpp, 30U);
    compare_rational(c, cpp, 31U);
}

static void
compare_text_output(const omc_store &c, const openmeta::MetaStore &cpp)
{
    compare_version(c, cpp);
    compare_text(c, cpp, 8U);
    compare_text(c, cpp, 18U);
    compare_bytes(c, cpp, 27U, OMC_VAL_BYTES, openmeta::MetaValueKind::Bytes);
    compare_bytes(c, cpp, 28U, OMC_VAL_BYTES, openmeta::MetaValueKind::Bytes);
}

static void
run_group_case(GroupKind kind)
{
    omc_store c_source;
    omc_store c_out;
    omc_gps_translation_opts c_opts;
    openmeta::MetaStore cpp_source = make_cpp_group_source(kind);
    openmeta::MetaStore cpp_out;
    omc_store_init(&c_source);
    omc_store_init(&c_out);
    add_c_group_source(c_source, kind);
    omc_gps_translation_opts_init(&c_opts);
    c_opts.mappings = kind == GroupKind::Navigation
                          ? OMC_GPS_NAV_TRANSLATE_ALL
                          : kind == GroupKind::Destination
                                ? OMC_GPS_DEST_TRANSLATE_ALL
                                : kind == GroupKind::Quality
                                      ? OMC_GPS_QUALITY_TRANSLATE_ALL
                                      : OMC_GPS_TEXT_TRANSLATE_ALL;
    const omc_gps_translation_res c_result =
        kind == GroupKind::Navigation
            ? omc_translate_xmp_gps_navigation(&c_source, &c_out, &c_opts)
            : kind == GroupKind::Destination
                  ? omc_translate_xmp_gps_destination(&c_source, &c_out,
                                                       &c_opts)
                  : kind == GroupKind::Quality
                        ? omc_translate_xmp_gps_quality(&c_source, &c_out,
                                                         &c_opts)
                        : omc_translate_xmp_gps_text(&c_source, &c_out,
                                                      &c_opts);
    openmeta::MetadataGpsTranslationResult cpp_result;
    if (kind == GroupKind::Navigation) {
        cpp_result = openmeta::translate_xmp_gps_navigation_metadata(
            cpp_source, {}, &cpp_out);
    } else if (kind == GroupKind::Destination) {
        cpp_result = openmeta::translate_xmp_gps_destination_metadata(
            cpp_source, {}, &cpp_out);
    } else if (kind == GroupKind::Quality) {
        cpp_result = openmeta::translate_xmp_gps_quality_metadata(cpp_source,
                                                                   {},
                                                                   &cpp_out);
    } else {
        cpp_result = openmeta::translate_xmp_gps_text_metadata(cpp_source, {},
                                                                &cpp_out);
    }
    compare_result(c_result, cpp_result);
    assert(c_result.status == OMC_GPS_TRANSLATION_OK);
    if (kind == GroupKind::Navigation)
        compare_navigation_output(c_out, cpp_out);
    else if (kind == GroupKind::Destination)
        compare_destination_output(c_out, cpp_out);
    else if (kind == GroupKind::Quality)
        compare_quality_output(c_out, cpp_out);
    else
        compare_text_output(c_out, cpp_out);
    omc_store_fini(&c_out);
    omc_store_fini(&c_source);
}

}  // namespace

int
main()
{
    run_case(false);
    run_case(true);
    run_group_case(GroupKind::Navigation);
    run_group_case(GroupKind::Destination);
    run_group_case(GroupKind::Quality);
    run_group_case(GroupKind::Text);
    return 0;
}
