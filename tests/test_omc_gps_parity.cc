#include "omc/omc_translation.h"
#include "omc_test_assert.h"

#include <openmeta/meta_edit.h>
#include <openmeta/meta_key.h>
#include <openmeta/meta_value.h>
#include <openmeta/metadata_translation.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

using CppStatus = openmeta::MetadataGpsTranslationStatus;

static std::string_view
view(const omc_store &store, omc_byte_ref ref)
{
    const auto bytes = omc_arena_view(&store.arena, ref);
    return {reinterpret_cast<const char *>(bytes.data), bytes.size};
}

static std::string_view
view(const openmeta::MetaStore &store, openmeta::ByteSpan ref)
{
    const auto bytes = store.arena().span(ref);
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
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
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_arena_append(&store.arena, "gps-source",
                            std::strlen("gps-source"),
                            &entry.origin.wire_type_name) == OMC_STATUS_OK);
    entry.flags = dirty ? OMC_ENTRY_FLAG_DIRTY : 0U;
    assert(omc_store_add_entry(&store, &entry, nullptr) == OMC_STATUS_OK);
}

static void
add_c_xmp_flags(omc_store &store, const char *path, const char *value,
                omc_entry_flags flags)
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
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_arena_append(&store.arena, "gps-source",
                            std::strlen("gps-source"),
                            &entry.origin.wire_type_name) == OMC_STATUS_OK);
    entry.flags = flags;
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

static const omc_entry *
find_c_gps_any(const omc_store &store, uint16_t tag)
{
    for (omc_size i = 0U; i < store.entry_count; ++i) {
        const omc_entry &entry = store.entries[i];
        if (entry.key.kind == OMC_KEY_EXIF_TAG &&
            entry.key.u.exif_tag.tag == tag &&
            view(store, entry.key.u.exif_tag.ifd) == "gpsifd")
            return &entry;
    }
    return nullptr;
}

static const openmeta::Entry *
find_cpp_gps_any(const openmeta::MetaStore &store, uint16_t tag)
{
    for (const auto &entry : store.entries()) {
        if (entry.key.kind != openmeta::MetaKeyKind::ExifTag ||
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

using CppPolicy = openmeta::MetadataGpsTranslationConflictPolicy;

static void
add_cpp_xmp_flags(openmeta::MetaStore &store, std::string_view path,
                  std::string_view value, openmeta::EntryFlags flags)
{
    openmeta::Entry entry;
    entry.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/exif/1.0/", path);
    entry.value = openmeta::make_text(store.arena(), value,
                                      openmeta::TextEncoding::Utf8);
    entry.flags = flags;
    entry.origin.wire_type_name = store.arena().append_string("gps-source");
    assert(store.add_entry(entry) != openmeta::kInvalidEntryId);
}

static void
add_cpp_gps_entry(openmeta::MetaStore &store, uint16_t tag,
                  openmeta::MetaValue value, openmeta::EntryFlags flags,
                  uint16_t wire_code, uint32_t wire_count,
                  std::string_view wire_name)
{
    openmeta::Entry entry;
    entry.key = openmeta::make_exif_tag_key(store.arena(), "gpsifd", tag);
    entry.value = value;
    entry.flags = flags;
    entry.origin.block = 7U;
    entry.origin.order_in_block = 100U + tag;
    entry.origin.wire_type = {openmeta::WireFamily::Tiff, wire_code};
    entry.origin.wire_count = wire_count;
    entry.origin.wire_type_name = store.arena().append_string(wire_name);
    assert(store.add_entry(entry) != openmeta::kInvalidEntryId);
}

static void
add_cpp_gps_text(openmeta::MetaStore &store, uint16_t tag,
                 std::string_view value, openmeta::EntryFlags flags,
                 uint32_t wire_count = 1U)
{
    add_cpp_gps_entry(
        store, tag,
        openmeta::make_text(store.arena(), value,
                            openmeta::TextEncoding::Ascii),
        flags, 2U, wire_count, "gps-native-text");
}

static void
add_cpp_gps_rational(openmeta::MetaStore &store, uint16_t tag,
                     uint32_t numer, uint32_t denom,
                     openmeta::EntryFlags flags)
{
    add_cpp_gps_entry(store, tag,
                      openmeta::make_urational(numer, denom), flags, 5U,
                      1U, "gps-native-rational");
}

static void
add_cpp_gps_u8(openmeta::MetaStore &store, uint16_t tag, uint8_t value,
               openmeta::EntryFlags flags)
{
    add_cpp_gps_entry(store, tag, openmeta::make_u8(value), flags, 1U, 1U,
                      "gps-native-u8");
}

static void
add_cpp_gps_coordinate(openmeta::MetaStore &store, uint16_t tag,
                       openmeta::EntryFlags flags)
{
    const std::array<openmeta::URational, 3> values {{
        {70U, 2U}, {96U, 2U}, {150U, 20U}
    }};
    add_cpp_gps_entry(
        store, tag, openmeta::make_urational_array(store.arena(), values),
        flags, 5U, 3U, "gps-native-rational-array");
}

static void
add_cpp_primary_be_coordinate(openmeta::MetaStore &store, uint16_t tag,
                              openmeta::EntryFlags flags)
{
    const std::array<openmeta::URational, 3> values {{
        {7000U, 200U}, {8200U, 200U}, {6000U, 200U}
    }};
    add_cpp_gps_entry(
        store, tag, openmeta::make_urational_array(store.arena(), values),
        flags, 5U, 3U, "gps-native-rational-array");
}

static void
add_cpp_gps_version(
    openmeta::MetaStore &store,
    openmeta::EntryFlags flags = openmeta::EntryFlags::None)
{
    const std::array<uint8_t, 4> version {{2U, 3U, 0U, 0U}};
    add_cpp_gps_entry(store, 0U,
                      openmeta::make_u8_array(store.arena(), version),
                      flags, 1U, 4U, "gps-version");
}

static omc_byte_ref
append_c_bytes(omc_store &store, const uint8_t *bytes, size_t size)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store.arena, bytes, size, &ref) == OMC_STATUS_OK);
    return ref;
}

static void
add_c_gps_entry(omc_store &store, uint16_t tag, const omc_val &value,
                omc_entry_flags flags, uint16_t wire_code,
                uint32_t wire_count, const char *wire_name)
{
    omc_entry entry{};
    omc_byte_ref ifd;
    assert(omc_arena_append(&store.arena, "gpsifd", 6U, &ifd) ==
           OMC_STATUS_OK);
    omc_key_make_exif_tag(&entry.key, ifd, tag);
    entry.value = value;
    entry.origin.block = 7U;
    entry.origin.order_in_block = 100U + tag;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = wire_code;
    entry.origin.wire_count = wire_count;
    assert(omc_arena_append(&store.arena, wire_name, std::strlen(wire_name),
                            &entry.origin.wire_type_name) == OMC_STATUS_OK);
    entry.flags = flags;
    assert(omc_store_add_entry(&store, &entry, nullptr) == OMC_STATUS_OK);
}

static void
add_c_gps_text(omc_store &store, uint16_t tag, const char *text,
               omc_entry_flags flags, uint32_t wire_count = 1U)
{
    omc_val value{};
    omc_byte_ref ref;
    assert(omc_arena_append(&store.arena, text, std::strlen(text), &ref) ==
           OMC_STATUS_OK);
    omc_val_make_text(&value, ref, OMC_TEXT_ASCII);
    add_c_gps_entry(store, tag, value, flags, 2U, wire_count,
                    "gps-native-text");
}

static void
add_c_gps_rational(omc_store &store, uint16_t tag, uint32_t numer,
                   uint32_t denom, omc_entry_flags flags)
{
    omc_val value{};
    omc_urational rational{numer, denom};
    omc_val_make_urational(&value, rational);
    add_c_gps_entry(store, tag, value, flags, 5U, 1U,
                    "gps-native-rational");
}

static void
add_c_gps_u8(omc_store &store, uint16_t tag, uint8_t value,
             omc_entry_flags flags)
{
    omc_val native{};
    omc_val_make_u8(&native, value);
    add_c_gps_entry(store, tag, native, flags, 1U, 1U, "gps-native-u8");
}

static void
add_c_gps_coordinate(omc_store &store, uint16_t tag,
                     omc_entry_flags flags)
{
    static const uint8_t big_endian[] = {
        0U, 0U, 0U, 70U, 0U, 0U, 0U, 2U,
        0U, 0U, 0U, 96U, 0U, 0U, 0U, 2U,
        0U, 0U, 0U, 150U, 0U, 0U, 0U, 20U
    };
    omc_val value{};
    omc_val_make_array(&value, OMC_ELEM_URATIONAL, 3U,
                       append_c_bytes(store, big_endian,
                                      sizeof(big_endian)),
                       OMC_BYTE_ORDER_BIG);
    add_c_gps_entry(store, tag, value, flags, 5U, 3U,
                    "gps-native-rational-array");
}

static void
add_c_primary_be_coordinate(omc_store &store, uint16_t tag,
                            omc_entry_flags flags)
{
    static const uint8_t big_endian[] = {
        0U, 0U, 0x1BU, 0x58U, 0U, 0U, 0U, 0xC8U,
        0U, 0U, 0x20U, 0x08U, 0U, 0U, 0U, 0xC8U,
        0U, 0U, 0x17U, 0x70U, 0U, 0U, 0U, 0xC8U
    };
    omc_val value{};
    omc_val_make_array(&value, OMC_ELEM_URATIONAL, 3U,
                       append_c_bytes(store, big_endian,
                                      sizeof(big_endian)),
                       OMC_BYTE_ORDER_BIG);
    add_c_gps_entry(store, tag, value, flags, 5U, 3U,
                    "gps-native-rational-array");
}

static void
add_c_gps_version(omc_store &store,
                  omc_entry_flags flags = OMC_ENTRY_FLAG_NONE)
{
    static const uint8_t version[4] = {2U, 3U, 0U, 0U};
    omc_val value{};
    omc_val_make_array(&value, OMC_ELEM_U8, 4U,
                       append_c_bytes(store, version, sizeof(version)),
                       OMC_BYTE_ORDER_NATIVE);
    add_c_gps_entry(store, 0U, value, flags, 1U, 4U, "gps-version");
}

static void
populate_c_primary_lifecycle_source(omc_store &store, bool exact,
                                    omc_entry_flags native_flags,
                                    bool with_native_tombstone,
                                    bool dirty_version = false)
{
    add_c_gps_version(store, dirty_version ? OMC_ENTRY_FLAG_DIRTY
                                           : OMC_ENTRY_FLAG_NONE);
    if (exact) {
        add_c_xmp_flags(store, "GPSAltitude", "12345/100",
                        OMC_ENTRY_FLAG_DIRTY);
        add_c_xmp_flags(store, "GPSAltitudeRef", "1",
                        OMC_ENTRY_FLAG_DIRTY);
        add_c_gps_u8(store, 5U, 1U, native_flags);
        add_c_gps_rational(store, 6U, 2469U, 20U, native_flags);
    } else {
        const omc_entry_flags deleted =
            OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
        add_c_xmp_flags(store, "GPSAltitude", "", deleted);
        add_c_xmp_flags(store, "GPSAltitudeRef", "", deleted);
        if (with_native_tombstone)
            add_c_gps_u8(store, 5U, 1U, native_flags);
    }
}

static openmeta::MetaStore
make_cpp_primary_lifecycle_source(bool exact,
                                  openmeta::EntryFlags native_flags,
                                  bool with_native_tombstone,
                                  bool dirty_version = false)
{
    openmeta::MetaStore store;
    add_cpp_gps_version(store, dirty_version
                                   ? openmeta::EntryFlags::Dirty
                                   : openmeta::EntryFlags::None);
    if (exact) {
        add_cpp_xmp_flags(store, "GPSAltitude", "12345/100",
                          openmeta::EntryFlags::Dirty);
        add_cpp_xmp_flags(store, "GPSAltitudeRef", "1",
                          openmeta::EntryFlags::Dirty);
        add_cpp_gps_u8(store, 5U, 1U, native_flags);
        add_cpp_gps_rational(store, 6U, 2469U, 20U, native_flags);
    } else {
        const auto deleted = openmeta::EntryFlags::Dirty
                             | openmeta::EntryFlags::Deleted;
        add_cpp_xmp_flags(store, "GPSAltitude", "", deleted);
        add_cpp_xmp_flags(store, "GPSAltitudeRef", "", deleted);
        if (with_native_tombstone)
            add_cpp_gps_u8(store, 5U, 1U, native_flags);
    }
    store.finalize();
    return store;
}

static void
populate_c_primary_be_coordinate_source(omc_store &store,
                                        omc_entry_flags native_flags)
{
    add_c_gps_version(store);
    add_c_xmp_flags(store, "GPSLatitude", "35,41,30N",
                    OMC_ENTRY_FLAG_DIRTY);
    add_c_gps_text(store, 1U, "N", native_flags);
    add_c_primary_be_coordinate(store, 2U, native_flags);
}

static openmeta::MetaStore
make_cpp_primary_be_coordinate_source(openmeta::EntryFlags native_flags)
{
    openmeta::MetaStore store;
    add_cpp_gps_version(store);
    add_cpp_xmp_flags(store, "GPSLatitude", "35,41,30N",
                      openmeta::EntryFlags::Dirty);
    add_cpp_gps_text(store, 1U, "N", native_flags);
    add_cpp_primary_be_coordinate(store, 2U, native_flags);
    store.finalize();
    return store;
}

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

static uint32_t
read_u32(const uint8_t *bytes, omc_byte_order order)
{
    if (order == OMC_BYTE_ORDER_BIG) {
        return (static_cast<uint32_t>(bytes[0]) << 24U)
               | (static_cast<uint32_t>(bytes[1]) << 16U)
               | (static_cast<uint32_t>(bytes[2]) << 8U)
               | static_cast<uint32_t>(bytes[3]);
    }
    if (order == OMC_BYTE_ORDER_LITTLE) {
        return static_cast<uint32_t>(bytes[0])
               | (static_cast<uint32_t>(bytes[1]) << 8U)
               | (static_cast<uint32_t>(bytes[2]) << 16U)
               | (static_cast<uint32_t>(bytes[3]) << 24U);
    }
    uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
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
    assert(c_bytes.size == 3U * sizeof(omc_urational));
    assert(cpp_bytes.size() == sizeof(cpp_values));
    std::memcpy(cpp_values, cpp_bytes.data(), sizeof(cpp_values));
    for (unsigned i = 0U; i < 3U; ++i) {
        const uint8_t *component = c_bytes.data + i * 8U;
        assert(read_u32(component, ce->value.byte_order)
               == cpp_values[i].numer);
        assert(read_u32(component + 4U, ce->value.byte_order)
               == cpp_values[i].denom);
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

static uint32_t
lifecycle_mapping(GroupKind kind, bool deletion)
{
    switch (kind) {
    case GroupKind::Navigation:
        return deletion ? OMC_GPS_NAV_TRANSLATE_TIMESTAMP
                        : OMC_GPS_NAV_TRANSLATE_SPEED;
    case GroupKind::Destination:
        return OMC_GPS_DEST_TRANSLATE_LATITUDE;
    case GroupKind::Quality:
        return deletion ? OMC_GPS_QUALITY_TRANSLATE_STATUS
                              | OMC_GPS_QUALITY_TRANSLATE_DOP
                        : OMC_GPS_QUALITY_TRANSLATE_DOP;
    case GroupKind::Text:
        return deletion ? OMC_GPS_TEXT_TRANSLATE_SATELLITES
                              | OMC_GPS_TEXT_TRANSLATE_MAP_DATUM
                        : OMC_GPS_TEXT_TRANSLATE_MAP_DATUM;
    }
    return 0U;
}

static uint32_t
lifecycle_max_added_entries(GroupKind kind)
{
    switch (kind) {
    case GroupKind::Navigation:
    case GroupKind::Destination:
        return 9U;
    case GroupKind::Quality:
        return 6U;
    case GroupKind::Text:
        return 5U;
    }
    return 0U;
}

static uint32_t
lifecycle_group_count(GroupKind kind)
{
    return kind == GroupKind::Quality || kind == GroupKind::Text ? 2U : 1U;
}

static void
populate_c_lifecycle_source(omc_store &store, GroupKind kind,
                            bool exact, omc_entry_flags native_flags,
                            bool with_native)
{
    static constexpr omc_entry_flags deleted =
        OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    add_c_gps_version(store);
    if (exact) {
        switch (kind) {
        case GroupKind::Navigation:
            add_c_xmp(store, "GPSSpeedRef", "K", true);
            add_c_xmp(store, "GPSSpeed", "12.5", true);
            add_c_gps_text(store, 12U, "K", OMC_ENTRY_FLAG_NONE);
            add_c_gps_rational(store, 13U, 250U, 20U,
                               OMC_ENTRY_FLAG_NONE);
            break;
        case GroupKind::Destination:
            add_c_xmp(store, "GPSDestLatitude", "35,48.125N", true);
            add_c_gps_text(store, 19U, "N", OMC_ENTRY_FLAG_NONE);
            add_c_gps_coordinate(store, 20U, OMC_ENTRY_FLAG_NONE);
            break;
        case GroupKind::Quality:
            add_c_xmp(store, "GPSDOP", "1.25", true);
            add_c_gps_rational(store, 11U, 10U, 8U,
                               OMC_ENTRY_FLAG_NONE);
            break;
        case GroupKind::Text:
            add_c_xmp(store, "GPSMapDatum", "WGS-84", true);
            add_c_gps_text(store, 18U, "WGS-84", OMC_ENTRY_FLAG_NONE, 6U);
            break;
        }
        return;
    }
    switch (kind) {
    case GroupKind::Navigation:
        add_c_xmp_flags(store, "GPSTimeStamp", "", deleted);
        if (with_native)
            add_c_gps_coordinate(store, 7U, native_flags);
        break;
    case GroupKind::Destination:
        add_c_xmp_flags(store, "GPSDestLatitude", "", deleted);
        if (with_native)
            add_c_gps_text(store, 19U, "N", native_flags);
        break;
    case GroupKind::Quality:
        add_c_xmp_flags(store, "GPSStatus", "", deleted);
        add_c_xmp_flags(store, "GPSDOP", "", deleted);
        if (with_native)
            add_c_gps_text(store, 9U, "A", native_flags);
        break;
    case GroupKind::Text:
        add_c_xmp_flags(store, "GPSSatellites", "", deleted);
        add_c_xmp_flags(store, "GPSMapDatum", "", deleted);
        if (with_native)
            add_c_gps_text(store, 8U, "8", native_flags);
        break;
    }
}

static openmeta::MetaStore
make_cpp_lifecycle_source(GroupKind kind, bool exact,
                          openmeta::EntryFlags native_flags,
                          bool with_native)
{
    openmeta::MetaStore store;
    const auto deleted =
        openmeta::EntryFlags::Dirty | openmeta::EntryFlags::Deleted;
    add_cpp_gps_version(store);
    if (exact) {
        switch (kind) {
        case GroupKind::Navigation:
            add_cpp_xmp_flags(store, "GPSSpeedRef", "K",
                              openmeta::EntryFlags::Dirty);
            add_cpp_xmp_flags(store, "GPSSpeed", "12.5",
                              openmeta::EntryFlags::Dirty);
            add_cpp_gps_text(store, 12U, "K", openmeta::EntryFlags::None);
            add_cpp_gps_rational(store, 13U, 250U, 20U,
                                 openmeta::EntryFlags::None);
            break;
        case GroupKind::Destination:
            add_cpp_xmp_flags(store, "GPSDestLatitude", "35,48.125N",
                              openmeta::EntryFlags::Dirty);
            add_cpp_gps_text(store, 19U, "N", openmeta::EntryFlags::None);
            add_cpp_gps_coordinate(store, 20U, openmeta::EntryFlags::None);
            break;
        case GroupKind::Quality:
            add_cpp_xmp_flags(store, "GPSDOP", "1.25",
                              openmeta::EntryFlags::Dirty);
            add_cpp_gps_rational(store, 11U, 10U, 8U,
                                 openmeta::EntryFlags::None);
            break;
        case GroupKind::Text:
            add_cpp_xmp_flags(store, "GPSMapDatum", "WGS-84",
                              openmeta::EntryFlags::Dirty);
            add_cpp_gps_text(store, 18U, "WGS-84", openmeta::EntryFlags::None,
                             6U);
            break;
        }
    } else {
        switch (kind) {
        case GroupKind::Navigation:
            add_cpp_xmp_flags(store, "GPSTimeStamp", "", deleted);
            if (with_native)
                add_cpp_gps_coordinate(store, 7U, native_flags);
            break;
        case GroupKind::Destination:
            add_cpp_xmp_flags(store, "GPSDestLatitude", "", deleted);
            if (with_native)
                add_cpp_gps_text(store, 19U, "N", native_flags);
            break;
        case GroupKind::Quality:
            add_cpp_xmp_flags(store, "GPSStatus", "", deleted);
            add_cpp_xmp_flags(store, "GPSDOP", "", deleted);
            if (with_native)
                add_cpp_gps_text(store, 9U, "A", native_flags);
            break;
        case GroupKind::Text:
            add_cpp_xmp_flags(store, "GPSSatellites", "", deleted);
            add_cpp_xmp_flags(store, "GPSMapDatum", "", deleted);
            if (with_native)
                add_cpp_gps_text(store, 8U, "8", native_flags);
            break;
        }
    }
    store.finalize();
    return store;
}

static omc_gps_translation_res
translate_c_lifecycle(GroupKind kind, const omc_store &source,
                      omc_store &output, uint32_t mapping,
                      omc_translation_conflict conflict,
                      uint32_t max_added = 0U, uint32_t max_operations = 0U)
{
    omc_gps_translation_opts opts;
    omc_gps_translation_opts_init(&opts);
    opts.mappings = mapping;
    opts.conflict = conflict;
    if (max_added != 0U)
        opts.max_added_entries = max_added;
    if (max_operations != 0U)
        opts.max_operations = max_operations;
    switch (kind) {
    case GroupKind::Navigation:
        return omc_translate_xmp_gps_navigation(&source, &output, &opts);
    case GroupKind::Destination:
        return omc_translate_xmp_gps_destination(&source, &output, &opts);
    case GroupKind::Quality:
        return omc_translate_xmp_gps_quality(&source, &output, &opts);
    case GroupKind::Text:
        return omc_translate_xmp_gps_text(&source, &output, &opts);
    }
    omc_gps_translation_res result{};
    result.status = OMC_GPS_TRANSLATION_INTERNAL;
    return result;
}

static openmeta::MetadataGpsTranslationResult
translate_cpp_lifecycle(GroupKind kind, const openmeta::MetaStore &source,
                        openmeta::MetaStore &output, CppPolicy policy,
                        uint32_t max_added = 0U,
                        uint32_t max_operations = 0U)
{
    switch (kind) {
    case GroupKind::Navigation: {
        openmeta::MetadataGpsNavigationTranslationOptions opts;
        opts.conflict_policy = policy;
        if (max_added != 0U)
            opts.max_added_entries = max_added;
        if (max_operations != 0U)
            opts.max_operations = max_operations;
        return openmeta::translate_xmp_gps_navigation_metadata(source, opts,
                                                               &output);
    }
    case GroupKind::Destination: {
        openmeta::MetadataGpsDestinationTranslationOptions opts;
        opts.conflict_policy = policy;
        if (max_added != 0U)
            opts.max_added_entries = max_added;
        if (max_operations != 0U)
            opts.max_operations = max_operations;
        return openmeta::translate_xmp_gps_destination_metadata(source, opts,
                                                                &output);
    }
    case GroupKind::Quality: {
        openmeta::MetadataGpsQualityTranslationOptions opts;
        opts.conflict_policy = policy;
        if (max_added != 0U)
            opts.max_added_entries = max_added;
        if (max_operations != 0U)
            opts.max_operations = max_operations;
        return openmeta::translate_xmp_gps_quality_metadata(source, opts,
                                                             &output);
    }
    case GroupKind::Text: {
        openmeta::MetadataGpsTextTranslationOptions opts;
        opts.conflict_policy = policy;
        if (max_added != 0U)
            opts.max_added_entries = max_added;
        if (max_operations != 0U)
            opts.max_operations = max_operations;
        return openmeta::translate_xmp_gps_text_metadata(source, opts,
                                                          &output);
    }
    }
    openmeta::MetadataGpsTranslationResult result;
    result.status = CppStatus::InternalError;
    return result;
}

static void
compare_lifecycle_entry(const omc_store &c,
                        const openmeta::MetaStore &cpp, uint16_t tag)
{
    const omc_entry *ce = find_c_gps_any(c, tag);
    const openmeta::Entry *pe = find_cpp_gps_any(cpp, tag);
    assert(ce != nullptr && pe != nullptr);
    assert(((ce->flags & OMC_ENTRY_FLAG_DIRTY) != 0U)
           == openmeta::any(pe->flags, openmeta::EntryFlags::Dirty));
    assert(((ce->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
           == openmeta::any(pe->flags, openmeta::EntryFlags::Deleted));
    assert(ce->origin.block == pe->origin.block);
    assert(ce->origin.order_in_block == pe->origin.order_in_block);
    assert(ce->origin.wire_type.family
           == static_cast<omc_wire_family>(pe->origin.wire_type.family));
    assert(ce->origin.wire_type.code == pe->origin.wire_type.code);
    assert(ce->origin.wire_count == pe->origin.wire_count);
    assert(static_cast<uint8_t>(ce->origin.name_context_kind)
           == static_cast<uint8_t>(pe->origin.name_context_kind));
    assert(ce->origin.name_context_variant == pe->origin.name_context_variant);
    assert(view(c, ce->origin.wire_type_name)
           == view(cpp, pe->origin.wire_type_name));
    assert(ce->value.count == pe->value.count);
    if (ce->value.kind == OMC_VAL_TEXT || ce->value.kind == OMC_VAL_BYTES) {
        assert(pe->value.kind == (ce->value.kind == OMC_VAL_TEXT
                                      ? openmeta::MetaValueKind::Text
                                      : openmeta::MetaValueKind::Bytes));
        const auto c_bytes = omc_arena_view(&c.arena, ce->value.u.ref);
        const auto cpp_bytes = cpp.arena().span(pe->value.data.span);
        assert(c_bytes.size == cpp_bytes.size());
        assert(c_bytes.size == 0U ||
               std::memcmp(c_bytes.data, cpp_bytes.data(), c_bytes.size) == 0);
    } else if (ce->value.kind == OMC_VAL_SCALAR) {
        assert(pe->value.kind == openmeta::MetaValueKind::Scalar);
        assert(ce->value.elem_type ==
               static_cast<omc_elem_type>(pe->value.elem_type));
        if (ce->value.elem_type == OMC_ELEM_URATIONAL) {
            assert(ce->value.u.ur.numer == pe->value.data.ur.numer);
            assert(ce->value.u.ur.denom == pe->value.data.ur.denom);
        } else {
            assert(ce->value.u.u64 == pe->value.data.u64);
        }
    } else {
        assert(ce->value.kind == OMC_VAL_ARRAY);
        assert(pe->value.kind == openmeta::MetaValueKind::Array);
        assert(ce->value.elem_type == static_cast<omc_elem_type>(
                                          pe->value.elem_type));
        const auto c_bytes = omc_arena_view(&c.arena, ce->value.u.ref);
        const auto cpp_bytes = cpp.arena().span(pe->value.data.span);
        if (ce->value.elem_type == OMC_ELEM_URATIONAL) {
            assert(c_bytes.size == cpp_bytes.size());
            for (uint32_t i = 0U; i < ce->value.count; ++i) {
                const uint8_t *component = c_bytes.data + i * 8U;
                openmeta::URational value;
                std::memcpy(&value, cpp_bytes.data() + i * sizeof(value),
                            sizeof(value));
                assert(read_u32(component, ce->value.byte_order)
                       == value.numer);
                assert(read_u32(component + 4U, ce->value.byte_order)
                       == value.denom);
            }
        } else {
            assert(c_bytes.size == cpp_bytes.size());
            assert(c_bytes.size == 0U ||
                   std::memcmp(c_bytes.data, cpp_bytes.data(), c_bytes.size)
                       == 0);
        }
    }
}

static void
compare_lifecycle_output(const omc_store &c,
                         const openmeta::MetaStore &cpp, GroupKind kind,
                         bool exact)
{
    if (exact) {
        if (kind == GroupKind::Navigation) {
            compare_lifecycle_entry(c, cpp, 12U);
            compare_lifecycle_entry(c, cpp, 13U);
        } else if (kind == GroupKind::Destination) {
            compare_lifecycle_entry(c, cpp, 19U);
            compare_lifecycle_entry(c, cpp, 20U);
        } else if (kind == GroupKind::Quality) {
            compare_lifecycle_entry(c, cpp, 11U);
        } else {
            compare_lifecycle_entry(c, cpp, 18U);
        }
    } else if (kind == GroupKind::Navigation) {
        compare_lifecycle_entry(c, cpp, 7U);
        compare_lifecycle_entry(c, cpp, 29U);
    } else if (kind == GroupKind::Destination) {
        compare_lifecycle_entry(c, cpp, 19U);
        compare_lifecycle_entry(c, cpp, 20U);
    } else if (kind == GroupKind::Quality) {
        compare_lifecycle_entry(c, cpp, 9U);
        compare_lifecycle_entry(c, cpp, 11U);
    } else {
        compare_lifecycle_entry(c, cpp, 8U);
        compare_lifecycle_entry(c, cpp, 18U);
    }
    compare_lifecycle_entry(c, cpp, 0U);
}

static void
assert_c_gps_entry_preserved(const omc_store &source,
                             const omc_store &output, uint16_t tag,
                             omc_entry_flags source_flags,
                             omc_entry_flags output_flags)
{
    const omc_entry *before = find_c_gps_any(source, tag);
    const omc_entry *after = find_c_gps_any(output, tag);
    assert(before != nullptr && after != nullptr);
    assert(before->flags == source_flags);
    assert(after->flags == output_flags);
    assert(view(source, before->key.u.exif_tag.ifd)
           == view(output, after->key.u.exif_tag.ifd));
    assert(before->key.u.exif_tag.tag == after->key.u.exif_tag.tag);
    assert(before->value.kind == after->value.kind);
    assert(before->value.elem_type == after->value.elem_type);
    assert(before->value.count == after->value.count);
    assert(before->value.byte_order == after->value.byte_order);
    assert(before->value.text_encoding == after->value.text_encoding);
    assert(before->origin.block == after->origin.block);
    assert(before->origin.order_in_block == after->origin.order_in_block);
    assert(before->origin.wire_type.family == after->origin.wire_type.family);
    assert(before->origin.wire_type.code == after->origin.wire_type.code);
    assert(before->origin.wire_count == after->origin.wire_count);
    assert(before->origin.name_context_kind ==
           after->origin.name_context_kind);
    assert(before->origin.name_context_variant ==
           after->origin.name_context_variant);
    assert(view(source, before->origin.wire_type_name)
           == view(output, after->origin.wire_type_name));
    if (before->value.kind == OMC_VAL_ARRAY
        || before->value.kind == OMC_VAL_TEXT
        || before->value.kind == OMC_VAL_BYTES) {
        const auto before_bytes =
            omc_arena_view(&source.arena, before->value.u.ref);
        const auto after_bytes =
            omc_arena_view(&output.arena, after->value.u.ref);
        assert(before_bytes.size == after_bytes.size);
        assert(before_bytes.size == 0U
               || std::memcmp(before_bytes.data, after_bytes.data,
                              before_bytes.size) == 0);
    } else if (before->value.elem_type == OMC_ELEM_URATIONAL) {
        assert(before->value.u.ur.numer == after->value.u.ur.numer);
        assert(before->value.u.ur.denom == after->value.u.ur.denom);
    } else {
        assert(before->value.u.u64 == after->value.u.u64);
    }
}

static void
assert_c_group_native_preserved(const omc_store &source,
                                const omc_store &output, GroupKind kind,
                                omc_entry_flags source_flags,
                                omc_entry_flags output_flags)
{
    uint16_t tags[2];
    size_t count;
    if (kind == GroupKind::Navigation) {
        tags[0] = 12U;
        tags[1] = 13U;
        count = 2U;
    } else if (kind == GroupKind::Destination) {
        tags[0] = 19U;
        tags[1] = 20U;
        count = 2U;
    } else if (kind == GroupKind::Quality) {
        tags[0] = 11U;
        count = 1U;
    } else {
        tags[0] = 18U;
        count = 1U;
    }
    for (size_t i = 0U; i < count; ++i) {
        assert_c_gps_entry_preserved(source, output, tags[i], source_flags,
                                     output_flags);
    }
}

static void
run_lifecycle_exact(GroupKind kind)
{
    omc_store c_source;
    omc_store c_output;
    omc_store c_repeat;
    omc_store c_preserve_source;
    omc_store c_preserve_output;
    omc_store_init(&c_source);
    omc_store_init(&c_output);
    omc_store_init(&c_repeat);
    omc_store_init(&c_preserve_source);
    omc_store_init(&c_preserve_output);
    populate_c_lifecycle_source(c_source, kind, true, OMC_ENTRY_FLAG_NONE,
                                false);
    auto cpp_source = make_cpp_lifecycle_source(
        kind, true, openmeta::EntryFlags::None, false);
    openmeta::MetaStore cpp_output;
    openmeta::MetaStore cpp_repeat;
    const auto c_result = translate_c_lifecycle(
        kind, c_source, c_output, lifecycle_mapping(kind, false),
        OMC_TRANSLATION_REPLACE);
    const auto cpp_result = translate_cpp_lifecycle(
        kind, cpp_source, cpp_output, CppPolicy::ReplaceExisting);
    compare_result(c_result, cpp_result);
    assert(c_result.groups_translated == 1U);
    assert(c_result.entries_updated ==
           (kind == GroupKind::Navigation || kind == GroupKind::Destination
                ? 2U : 1U));
    assert(c_result.entries_added == 0U && c_result.entries_removed == 0U);
    compare_lifecycle_output(c_output, cpp_output, kind, true);
    assert_c_gps_entry_preserved(c_source, c_output, 0U,
                                 OMC_ENTRY_FLAG_NONE, OMC_ENTRY_FLAG_NONE);
    assert_c_group_native_preserved(c_source, c_output, kind,
                                    OMC_ENTRY_FLAG_NONE,
                                    OMC_ENTRY_FLAG_DIRTY);

    const auto c_repeat_result = translate_c_lifecycle(
        kind, c_output, c_repeat, lifecycle_mapping(kind, false),
        OMC_TRANSLATION_REPLACE);
    const auto cpp_repeat_result = translate_cpp_lifecycle(
        kind, cpp_output, cpp_repeat, CppPolicy::ReplaceExisting);
    compare_result(c_repeat_result, cpp_repeat_result);
    assert(c_repeat_result.groups_unchanged == 1U);
    assert(c_repeat_result.entries_added == 0U
           && c_repeat_result.entries_updated == 0U
           && c_repeat_result.entries_removed == 0U);
    compare_lifecycle_output(c_repeat, cpp_repeat, kind, true);
    assert_c_group_native_preserved(c_output, c_repeat, kind,
                                    OMC_ENTRY_FLAG_DIRTY,
                                    OMC_ENTRY_FLAG_DIRTY);

    populate_c_lifecycle_source(c_preserve_source, kind, true,
                                OMC_ENTRY_FLAG_NONE, false);
    auto cpp_preserve_source = make_cpp_lifecycle_source(
        kind, true, openmeta::EntryFlags::None, false);
    const auto c_preserve_result = translate_c_lifecycle(
        kind, c_preserve_source, c_preserve_output,
        lifecycle_mapping(kind, false), OMC_TRANSLATION_PRESERVE);
    openmeta::MetaStore cpp_preserve_output;
    const auto cpp_preserve_result = translate_cpp_lifecycle(
        kind, cpp_preserve_source, cpp_preserve_output,
        CppPolicy::PreserveExisting);
    compare_result(c_preserve_result, cpp_preserve_result);
    assert(c_preserve_result.groups_preserved == 1U);
    compare_lifecycle_output(c_preserve_output, cpp_preserve_output, kind,
                             true);
    assert_c_group_native_preserved(c_preserve_source, c_preserve_output,
                                    kind, OMC_ENTRY_FLAG_NONE,
                                    OMC_ENTRY_FLAG_NONE);
    omc_store_fini(&c_preserve_output);
    omc_store_fini(&c_preserve_source);
    omc_store_fini(&c_repeat);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
run_lifecycle_deletion(GroupKind kind, bool clean_tombstone)
{
    omc_store c_source;
    omc_store c_output;
    omc_store c_repeat;
    const omc_entry_flags c_native_flags =
        clean_tombstone ? OMC_ENTRY_FLAG_DELETED
                        : OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    const openmeta::EntryFlags cpp_native_flags =
        clean_tombstone ? openmeta::EntryFlags::Deleted
                        : openmeta::EntryFlags::Dirty
                              | openmeta::EntryFlags::Deleted;
    omc_store_init(&c_source);
    omc_store_init(&c_output);
    omc_store_init(&c_repeat);
    populate_c_lifecycle_source(c_source, kind, false, c_native_flags, true);
    auto cpp_source = make_cpp_lifecycle_source(
        kind, false, cpp_native_flags, true);
    openmeta::MetaStore cpp_output;
    openmeta::MetaStore cpp_repeat;
    const auto c_result = translate_c_lifecycle(
        kind, c_source, c_output, lifecycle_mapping(kind, true),
        OMC_TRANSLATION_REPLACE);
    const auto cpp_result = translate_cpp_lifecycle(
        kind, cpp_source, cpp_output, CppPolicy::ReplaceExisting);
    compare_result(c_result, cpp_result);
    assert(c_result.entries_added == 1U);
    assert(c_result.entries_updated == (clean_tombstone ? 1U : 0U));
    assert(c_result.entries_removed == 1U);
    compare_lifecycle_output(c_output, cpp_output, kind, false);
    const uint16_t native_tag = kind == GroupKind::Navigation ? 7U
                                : kind == GroupKind::Destination ? 19U
                                : kind == GroupKind::Quality ? 9U : 8U;
    assert_c_gps_entry_preserved(c_source, c_output, native_tag,
                                 c_native_flags,
                                 c_native_flags | OMC_ENTRY_FLAG_DIRTY);

    const auto c_repeat_result = translate_c_lifecycle(
        kind, c_output, c_repeat, lifecycle_mapping(kind, true),
        OMC_TRANSLATION_REPLACE);
    const auto cpp_repeat_result = translate_cpp_lifecycle(
        kind, cpp_output, cpp_repeat, CppPolicy::ReplaceExisting);
    compare_result(c_repeat_result, cpp_repeat_result);
    assert(c_repeat_result.groups_unchanged == lifecycle_group_count(kind));
    assert(c_repeat_result.entries_added == 0U
           && c_repeat_result.entries_updated == 0U
           && c_repeat_result.entries_removed == 0U);
    compare_lifecycle_output(c_repeat, cpp_repeat, kind, false);
    omc_store_fini(&c_repeat);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
add_c_sentinel(omc_store &store)
{
    omc_entry entry{};
    omc_byte_ref value;
    assert(omc_arena_append(&store.arena, "keep", 4U, &value) ==
           OMC_STATUS_OK);
    omc_key_make_comment(&entry.key);
    omc_val_make_text(&entry.value, value, OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(&store, &entry, nullptr) == OMC_STATUS_OK);
}

static void
assert_c_sentinel(const omc_store &store)
{
    assert(store.entry_count == 1U);
    assert(store.entries[0].key.kind == OMC_KEY_COMMENT);
    assert(view(store, store.entries[0].value.u.ref) == "keep");
}

static openmeta::MetaStore
make_cpp_sentinel()
{
    openmeta::MetaStore store;
    openmeta::Entry entry;
    entry.key = openmeta::make_comment_key();
    entry.value = openmeta::make_text(store.arena(), "keep",
                                      openmeta::TextEncoding::Utf8);
    entry.origin.block = openmeta::kInvalidBlockId;
    assert(store.add_entry(entry) != openmeta::kInvalidEntryId);
    store.finalize();
    return store;
}

static void
assert_cpp_sentinel(const openmeta::MetaStore &store)
{
    assert(store.entries().size() == 1U);
    assert(store.entries()[0].key.kind == openmeta::MetaKeyKind::Comment);
    assert(view(store, store.entries()[0].value.data.span) == "keep");
}

static omc_gps_translation_res
translate_c_primary_lifecycle(const omc_store &source, omc_store &output,
                              omc_translation_conflict conflict,
                              uint32_t max_added = 0U,
                              uint32_t max_operations = 0U)
{
    omc_gps_translation_opts opts;
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_TRANSLATE_ALTITUDE;
    opts.conflict = conflict;
    if (max_added != 0U)
        opts.max_added_entries = max_added;
    if (max_operations != 0U)
        opts.max_operations = max_operations;
    return omc_translate_xmp_gps(&source, &output, &opts);
}

static openmeta::MetadataGpsTranslationResult
translate_cpp_primary_lifecycle(
    const openmeta::MetaStore &source, openmeta::MetaStore &output,
    CppPolicy policy, uint32_t max_added = 0U,
    uint32_t max_operations = 0U)
{
    openmeta::MetadataGpsTranslationOptions opts;
    opts.latitude_to_exif = false;
    opts.longitude_to_exif = false;
    opts.conflict_policy = policy;
    if (max_added != 0U)
        opts.max_added_entries = max_added;
    if (max_operations != 0U)
        opts.max_operations = max_operations;
    return openmeta::translate_xmp_gps_metadata(source, opts, &output);
}

static omc_gps_translation_res
translate_c_primary_coordinate(const omc_store &source, omc_store &output,
                               omc_translation_conflict conflict)
{
    omc_gps_translation_opts opts;
    omc_gps_translation_opts_init(&opts);
    opts.mappings = OMC_GPS_TRANSLATE_LATITUDE;
    opts.conflict = conflict;
    return omc_translate_xmp_gps(&source, &output, &opts);
}

static openmeta::MetadataGpsTranslationResult
translate_cpp_primary_coordinate(const openmeta::MetaStore &source,
                                 openmeta::MetaStore &output,
                                 CppPolicy policy)
{
    openmeta::MetadataGpsTranslationOptions opts;
    opts.latitude_to_exif = true;
    opts.longitude_to_exif = false;
    opts.altitude_to_exif = false;
    opts.conflict_policy = policy;
    return openmeta::translate_xmp_gps_metadata(source, opts, &output);
}

static void
compare_primary_lifecycle_output(const omc_store &c,
                                 const openmeta::MetaStore &cpp)
{
    compare_lifecycle_entry(c, cpp, 0U);
    compare_lifecycle_entry(c, cpp, 5U);
    compare_lifecycle_entry(c, cpp, 6U);
}

static void
compare_primary_coordinate_output(const omc_store &c,
                                  const openmeta::MetaStore &cpp)
{
    compare_lifecycle_entry(c, cpp, 0U);
    compare_lifecycle_entry(c, cpp, 1U);
    compare_lifecycle_entry(c, cpp, 2U);
}

static void
run_primary_be_coordinate_policy(omc_translation_conflict c_policy,
                                 CppPolicy cpp_policy, bool preserve)
{
    omc_store c_source;
    omc_store c_output;
    omc_store c_repeat;
    omc_store_init(&c_source);
    omc_store_init(&c_output);
    omc_store_init(&c_repeat);
    populate_c_primary_be_coordinate_source(c_source, OMC_ENTRY_FLAG_NONE);
    auto cpp_source = make_cpp_primary_be_coordinate_source(
        openmeta::EntryFlags::None);
    openmeta::MetaStore cpp_output;
    const auto c_result = translate_c_primary_coordinate(
        c_source, c_output, c_policy);
    const auto cpp_result = translate_cpp_primary_coordinate(
        cpp_source, cpp_output, cpp_policy);
    compare_result(c_result, cpp_result);
    assert(c_result.status == OMC_GPS_TRANSLATION_OK);
    if (preserve) {
        assert(c_result.groups_preserved == 1U);
        assert(c_result.groups_translated == 0U);
        assert(c_result.entries_updated == 0U && c_result.entries_added == 0U);
    } else {
        assert(c_result.groups_translated == 1U);
        assert(c_result.groups_preserved == 0U);
        assert(c_result.entries_updated == 2U && c_result.entries_added == 0U);
    }
    compare_primary_coordinate_output(c_output, cpp_output);
    assert_c_gps_entry_preserved(c_source, c_output, 0U,
                                 OMC_ENTRY_FLAG_NONE, OMC_ENTRY_FLAG_NONE);
    assert_c_gps_entry_preserved(c_source, c_output, 1U,
                                 OMC_ENTRY_FLAG_NONE,
                                 preserve ? OMC_ENTRY_FLAG_NONE
                                          : OMC_ENTRY_FLAG_DIRTY);
    assert_c_gps_entry_preserved(c_source, c_output, 2U,
                                 OMC_ENTRY_FLAG_NONE,
                                 preserve ? OMC_ENTRY_FLAG_NONE
                                          : OMC_ENTRY_FLAG_DIRTY);

    if (!preserve) {
        const auto c_repeat_result = translate_c_primary_coordinate(
            c_output, c_repeat, c_policy);
        openmeta::MetaStore cpp_repeat;
        const auto cpp_repeat_result = translate_cpp_primary_coordinate(
            cpp_output, cpp_repeat, cpp_policy);
        compare_result(c_repeat_result, cpp_repeat_result);
        assert(c_repeat_result.status == OMC_GPS_TRANSLATION_OK);
        assert(c_repeat_result.groups_unchanged == 1U);
        assert(c_repeat_result.groups_translated == 0U);
        assert(c_repeat_result.entries_updated == 0U &&
               c_repeat_result.entries_added == 0U &&
               c_repeat_result.entries_removed == 0U);
        compare_primary_coordinate_output(c_repeat, cpp_repeat);
        assert_c_gps_entry_preserved(c_output, c_repeat, 1U,
                                     OMC_ENTRY_FLAG_DIRTY,
                                     OMC_ENTRY_FLAG_DIRTY);
        assert_c_gps_entry_preserved(c_output, c_repeat, 2U,
                                     OMC_ENTRY_FLAG_DIRTY,
                                     OMC_ENTRY_FLAG_DIRTY);
    }
    omc_store_fini(&c_repeat);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
run_primary_be_coordinate_parity()
{
    run_primary_be_coordinate_policy(OMC_TRANSLATION_FAIL,
                                     CppPolicy::FailOnConflict, false);
    run_primary_be_coordinate_policy(OMC_TRANSLATION_REPLACE,
                                     CppPolicy::ReplaceExisting, false);
    run_primary_be_coordinate_policy(OMC_TRANSLATION_PRESERVE,
                                     CppPolicy::PreserveExisting, true);
}

static void
run_primary_lifecycle_exact()
{
    omc_store c_source;
    omc_store c_output;
    omc_store c_repeat;
    omc_store c_preserve_source;
    omc_store c_preserve_output;
    omc_store c_dirty_version_source;
    omc_store c_dirty_version_output;
    omc_store c_limit_source;
    omc_store c_limit_output;
    omc_gps_translation_res c_result;
    omc_gps_translation_res c_repeat_result;
    omc_gps_translation_res c_preserve_result;
    omc_gps_translation_res c_limit_result;
    std::vector<omc_entry_flags> c_limit_flags;
    std::vector<openmeta::EntryFlags> cpp_limit_flags;
    omc_store_init(&c_source);
    omc_store_init(&c_output);
    omc_store_init(&c_repeat);
    omc_store_init(&c_preserve_source);
    omc_store_init(&c_preserve_output);
    omc_store_init(&c_dirty_version_source);
    omc_store_init(&c_dirty_version_output);
    omc_store_init(&c_limit_source);
    omc_store_init(&c_limit_output);
    populate_c_primary_lifecycle_source(c_source, true, OMC_ENTRY_FLAG_NONE,
                                        false);
    auto cpp_source = make_cpp_primary_lifecycle_source(
        true, openmeta::EntryFlags::None, false);
    openmeta::MetaStore cpp_output;
    c_result = translate_c_primary_lifecycle(
        c_source, c_output, OMC_TRANSLATION_REPLACE);
    auto cpp_result = translate_cpp_primary_lifecycle(
        cpp_source, cpp_output, CppPolicy::ReplaceExisting);
    compare_result(c_result, cpp_result);
    assert(c_result.groups_translated == 1U);
    assert(c_result.entries_updated == 2U);
    assert(c_result.entries_added == 0U && c_result.entries_removed == 0U);
    compare_primary_lifecycle_output(c_output, cpp_output);
    assert_c_gps_entry_preserved(c_source, c_output, 0U,
                                 OMC_ENTRY_FLAG_NONE, OMC_ENTRY_FLAG_NONE);
    assert_c_gps_entry_preserved(c_source, c_output, 5U,
                                 OMC_ENTRY_FLAG_NONE, OMC_ENTRY_FLAG_DIRTY);
    assert_c_gps_entry_preserved(c_source, c_output, 6U,
                                 OMC_ENTRY_FLAG_NONE, OMC_ENTRY_FLAG_DIRTY);

    c_repeat_result = translate_c_primary_lifecycle(
        c_output, c_repeat, OMC_TRANSLATION_REPLACE);
    openmeta::MetaStore cpp_repeat;
    auto cpp_repeat_result = translate_cpp_primary_lifecycle(
        cpp_output, cpp_repeat, CppPolicy::ReplaceExisting);
    compare_result(c_repeat_result, cpp_repeat_result);
    assert(c_repeat_result.groups_unchanged == 1U);
    assert(c_repeat_result.entries_added == 0U
           && c_repeat_result.entries_updated == 0U
           && c_repeat_result.entries_removed == 0U);
    compare_primary_lifecycle_output(c_repeat, cpp_repeat);
    assert_c_gps_entry_preserved(c_output, c_repeat, 5U,
                                 OMC_ENTRY_FLAG_DIRTY,
                                 OMC_ENTRY_FLAG_DIRTY);
    assert_c_gps_entry_preserved(c_output, c_repeat, 6U,
                                 OMC_ENTRY_FLAG_DIRTY,
                                 OMC_ENTRY_FLAG_DIRTY);

    populate_c_primary_lifecycle_source(c_preserve_source, true,
                                        OMC_ENTRY_FLAG_NONE, false);
    auto cpp_preserve_source = make_cpp_primary_lifecycle_source(
        true, openmeta::EntryFlags::None, false);
    c_preserve_result = translate_c_primary_lifecycle(
        c_preserve_source, c_preserve_output, OMC_TRANSLATION_PRESERVE);
    openmeta::MetaStore cpp_preserve_output;
    auto cpp_preserve_result = translate_cpp_primary_lifecycle(
        cpp_preserve_source, cpp_preserve_output,
        CppPolicy::PreserveExisting);
    compare_result(c_preserve_result, cpp_preserve_result);
    assert(c_preserve_result.groups_preserved == 1U);
    assert(c_preserve_result.entries_updated == 0U
           && c_preserve_result.entries_added == 0U);
    compare_primary_lifecycle_output(c_preserve_output,
                                     cpp_preserve_output);
    assert_c_gps_entry_preserved(c_preserve_source, c_preserve_output, 5U,
                                 OMC_ENTRY_FLAG_NONE, OMC_ENTRY_FLAG_NONE);
    assert_c_gps_entry_preserved(c_preserve_source, c_preserve_output, 6U,
                                 OMC_ENTRY_FLAG_NONE, OMC_ENTRY_FLAG_NONE);

    populate_c_primary_lifecycle_source(
        c_dirty_version_source, true, OMC_ENTRY_FLAG_NONE, false, true);
    auto cpp_dirty_version_source = make_cpp_primary_lifecycle_source(
        true, openmeta::EntryFlags::None, false, true);
    openmeta::MetaStore cpp_dirty_version_output;
    c_result = translate_c_primary_lifecycle(
        c_dirty_version_source, c_dirty_version_output,
        OMC_TRANSLATION_REPLACE);
    cpp_result = translate_cpp_primary_lifecycle(
        cpp_dirty_version_source, cpp_dirty_version_output,
        CppPolicy::ReplaceExisting);
    compare_result(c_result, cpp_result);
    assert(c_result.status == OMC_GPS_TRANSLATION_OK);
    compare_primary_lifecycle_output(c_dirty_version_output,
                                     cpp_dirty_version_output);
    assert_c_gps_entry_preserved(c_dirty_version_source,
                                 c_dirty_version_output, 0U,
                                 OMC_ENTRY_FLAG_DIRTY,
                                 OMC_ENTRY_FLAG_DIRTY);

    populate_c_primary_lifecycle_source(c_limit_source, true,
                                        OMC_ENTRY_FLAG_NONE, false);
    add_c_sentinel(c_limit_output);
    auto cpp_limit_source = make_cpp_primary_lifecycle_source(
        true, openmeta::EntryFlags::None, false);
    auto cpp_limit_output = make_cpp_sentinel();
    for (omc_size i = 0U; i < c_limit_source.entry_count; ++i)
        c_limit_flags.push_back(c_limit_source.entries[i].flags);
    for (const auto &entry : cpp_limit_source.entries())
        cpp_limit_flags.push_back(entry.flags);
    c_limit_result = translate_c_primary_lifecycle(
        c_limit_source, c_limit_output, OMC_TRANSLATION_REPLACE, 7U, 1U);
    auto cpp_limit_result = translate_cpp_primary_lifecycle(
        cpp_limit_source, cpp_limit_output, CppPolicy::ReplaceExisting,
        7U, 1U);
    assert(c_limit_result.status == map_status(cpp_limit_result.status));
    assert(c_limit_result.status == OMC_GPS_TRANSLATION_OPERATION_LIMIT);
    assert_c_sentinel(c_limit_output);
    assert_cpp_sentinel(cpp_limit_output);
    for (omc_size i = 0U; i < c_limit_source.entry_count; ++i)
        assert(c_limit_source.entries[i].flags == c_limit_flags[i]);
    for (size_t i = 0U; i < cpp_limit_source.entries().size(); ++i)
        assert(cpp_limit_source.entries()[i].flags == cpp_limit_flags[i]);

    c_limit_result = translate_c_primary_lifecycle(
        c_limit_source, c_limit_output, OMC_TRANSLATION_REPLACE, 8U);
    cpp_limit_result = translate_cpp_primary_lifecycle(
        cpp_limit_source, cpp_limit_output, CppPolicy::ReplaceExisting, 8U);
    assert(c_limit_result.status == map_status(cpp_limit_result.status));
    assert(c_limit_result.status == OMC_GPS_TRANSLATION_INVALID_OPTIONS);
    assert_c_sentinel(c_limit_output);
    assert_cpp_sentinel(cpp_limit_output);
    for (omc_size i = 0U; i < c_limit_source.entry_count; ++i)
        assert(c_limit_source.entries[i].flags == c_limit_flags[i]);
    for (size_t i = 0U; i < cpp_limit_source.entries().size(); ++i)
        assert(cpp_limit_source.entries()[i].flags == cpp_limit_flags[i]);

    omc_store_fini(&c_limit_output);
    omc_store_fini(&c_limit_source);
    omc_store_fini(&c_dirty_version_output);
    omc_store_fini(&c_dirty_version_source);
    omc_store_fini(&c_preserve_output);
    omc_store_fini(&c_preserve_source);
    omc_store_fini(&c_repeat);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
run_primary_lifecycle_deletion(omc_entry_flags native_flags,
                               bool with_native_tombstone)
{
    omc_store c_source;
    omc_store c_output;
    omc_store c_repeat;
    omc_store_init(&c_source);
    omc_store_init(&c_output);
    omc_store_init(&c_repeat);
    populate_c_primary_lifecycle_source(c_source, false, native_flags,
                                        with_native_tombstone);
    auto cpp_source = make_cpp_primary_lifecycle_source(
        false, native_flags == OMC_ENTRY_FLAG_NONE
                   ? openmeta::EntryFlags::None
                   : (native_flags == OMC_ENTRY_FLAG_DELETED
                          ? openmeta::EntryFlags::Deleted
                          : openmeta::EntryFlags::Dirty
                                | openmeta::EntryFlags::Deleted),
        with_native_tombstone);
    auto cpp_output = openmeta::MetaStore{};
    const auto c_result = translate_c_primary_lifecycle(
        c_source, c_output, OMC_TRANSLATION_REPLACE);
    const auto cpp_result = translate_cpp_primary_lifecycle(
        cpp_source, cpp_output, CppPolicy::ReplaceExisting);
    compare_result(c_result, cpp_result);
    assert(c_result.entries_added == (with_native_tombstone ? 1U : 2U));
    assert(c_result.entries_updated ==
           (native_flags == OMC_ENTRY_FLAG_DELETED ? 1U : 0U));
    assert(c_result.entries_removed == 1U);
    compare_primary_lifecycle_output(c_output, cpp_output);
    if (with_native_tombstone) {
        assert_c_gps_entry_preserved(c_source, c_output, 5U, native_flags,
                                     native_flags | OMC_ENTRY_FLAG_DIRTY);
    }

    const auto c_repeat_result = translate_c_primary_lifecycle(
        c_output, c_repeat, OMC_TRANSLATION_REPLACE);
    auto cpp_repeat = openmeta::MetaStore{};
    const auto cpp_repeat_result = translate_cpp_primary_lifecycle(
        cpp_output, cpp_repeat, CppPolicy::ReplaceExisting);
    compare_result(c_repeat_result, cpp_repeat_result);
    assert(c_repeat_result.groups_unchanged == 1U);
    assert(c_repeat_result.entries_added == 0U
           && c_repeat_result.entries_updated == 0U
           && c_repeat_result.entries_removed == 0U);
    compare_primary_lifecycle_output(c_repeat, cpp_repeat);
    omc_store_fini(&c_repeat);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
run_primary_lifecycle_limit_rollback()
{
    omc_store c_source;
    omc_store c_output;
    omc_gps_translation_res c_result;
    std::vector<omc_entry_flags> c_flags;
    std::vector<openmeta::EntryFlags> cpp_flags;
    omc_store_init(&c_source);
    omc_store_init(&c_output);
    populate_c_primary_lifecycle_source(c_source, false, OMC_ENTRY_FLAG_NONE,
                                        false);
    add_c_sentinel(c_output);
    auto cpp_source = make_cpp_primary_lifecycle_source(
        false, openmeta::EntryFlags::None, false);
    auto cpp_output = make_cpp_sentinel();
    for (omc_size i = 0U; i < c_source.entry_count; ++i)
        c_flags.push_back(c_source.entries[i].flags);
    for (const auto &entry : cpp_source.entries())
        cpp_flags.push_back(entry.flags);

    c_result = translate_c_primary_lifecycle(
        c_source, c_output, OMC_TRANSLATION_REPLACE, 1U);
    auto cpp_result = translate_cpp_primary_lifecycle(
        cpp_source, cpp_output, CppPolicy::ReplaceExisting, 1U);
    assert(c_result.status == map_status(cpp_result.status));
    assert(c_result.status == OMC_GPS_TRANSLATION_ENTRY_LIMIT);
    assert_c_sentinel(c_output);
    assert_cpp_sentinel(cpp_output);
    for (omc_size i = 0U; i < c_source.entry_count; ++i)
        assert(c_source.entries[i].flags == c_flags[i]);
    for (size_t i = 0U; i < cpp_source.entries().size(); ++i)
        assert(cpp_source.entries()[i].flags == cpp_flags[i]);

    c_result = translate_c_primary_lifecycle(
        c_source, c_output, OMC_TRANSLATION_REPLACE, 7U, 1U);
    cpp_result = translate_cpp_primary_lifecycle(
        cpp_source, cpp_output, CppPolicy::ReplaceExisting, 7U, 1U);
    assert(c_result.status == map_status(cpp_result.status));
    assert(c_result.status == OMC_GPS_TRANSLATION_OPERATION_LIMIT);
    assert_c_sentinel(c_output);
    assert_cpp_sentinel(cpp_output);
    for (omc_size i = 0U; i < c_source.entry_count; ++i)
        assert(c_source.entries[i].flags == c_flags[i]);
    for (size_t i = 0U; i < cpp_source.entries().size(); ++i)
        assert(cpp_source.entries()[i].flags == cpp_flags[i]);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
run_lifecycle_limit_rollback(GroupKind kind)
{
    omc_store c_source;
    omc_store c_output;
    omc_gps_translation_res c_result;
    uint32_t invalid_max_added;
    omc_store_init(&c_source);
    omc_store_init(&c_output);
    populate_c_lifecycle_source(c_source, kind, false, OMC_ENTRY_FLAG_NONE,
                                false);
    add_c_sentinel(c_output);
    auto cpp_source = make_cpp_lifecycle_source(
        kind, false, openmeta::EntryFlags::None, false);
    auto cpp_output = make_cpp_sentinel();
    std::vector<omc_entry_flags> c_flags;
    std::vector<openmeta::EntryFlags> cpp_flags;
    for (omc_size i = 0U; i < c_source.entry_count; ++i)
        c_flags.push_back(c_source.entries[i].flags);
    for (const auto &entry : cpp_source.entries())
        cpp_flags.push_back(entry.flags);

    c_result = translate_c_lifecycle(
        kind, c_source, c_output, lifecycle_mapping(kind, true),
        OMC_TRANSLATION_REPLACE, 1U);
    auto cpp_result = translate_cpp_lifecycle(
        kind, cpp_source, cpp_output, CppPolicy::ReplaceExisting, 1U);
    assert(c_result.status == map_status(cpp_result.status));
    assert(c_result.status == OMC_GPS_TRANSLATION_ENTRY_LIMIT);
    assert_c_sentinel(c_output);
    assert_cpp_sentinel(cpp_output);
    for (omc_size i = 0U; i < c_source.entry_count; ++i)
        assert(c_source.entries[i].flags == c_flags[i]);
    for (size_t i = 0U; i < cpp_source.entries().size(); ++i)
        assert(cpp_source.entries()[i].flags == cpp_flags[i]);

    c_result = translate_c_lifecycle(
        kind, c_source, c_output, lifecycle_mapping(kind, true),
        OMC_TRANSLATION_REPLACE, lifecycle_max_added_entries(kind), 1U);
    cpp_result = translate_cpp_lifecycle(
        kind, cpp_source, cpp_output, CppPolicy::ReplaceExisting,
        lifecycle_max_added_entries(kind), 1U);
    assert(c_result.status == map_status(cpp_result.status));
    assert(c_result.status == OMC_GPS_TRANSLATION_OPERATION_LIMIT);
    assert_c_sentinel(c_output);
    assert_cpp_sentinel(cpp_output);
    for (omc_size i = 0U; i < c_source.entry_count; ++i)
        assert(c_source.entries[i].flags == c_flags[i]);
    for (size_t i = 0U; i < cpp_source.entries().size(); ++i)
        assert(cpp_source.entries()[i].flags == cpp_flags[i]);

    invalid_max_added = lifecycle_max_added_entries(kind) + 1U;
    if (invalid_max_added == 7U)
        ++invalid_max_added;
    c_result = translate_c_lifecycle(
        kind, c_source, c_output, lifecycle_mapping(kind, true),
        OMC_TRANSLATION_REPLACE, invalid_max_added);
    cpp_result = translate_cpp_lifecycle(
        kind, cpp_source, cpp_output, CppPolicy::ReplaceExisting,
        invalid_max_added);
    assert(c_result.status == map_status(cpp_result.status));
    assert(c_result.status == OMC_GPS_TRANSLATION_INVALID_OPTIONS);
    assert_c_sentinel(c_output);
    assert_cpp_sentinel(cpp_output);
    for (omc_size i = 0U; i < c_source.entry_count; ++i)
        assert(c_source.entries[i].flags == c_flags[i]);
    for (size_t i = 0U; i < cpp_source.entries().size(); ++i)
        assert(cpp_source.entries()[i].flags == cpp_flags[i]);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
run_lifecycle_parity()
{
    run_primary_be_coordinate_parity();
    run_primary_lifecycle_exact();
    run_primary_lifecycle_deletion(OMC_ENTRY_FLAG_NONE, false);
    run_primary_lifecycle_deletion(
        OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED, true);
    run_primary_lifecycle_deletion(OMC_ENTRY_FLAG_DELETED, true);
    run_primary_lifecycle_limit_rollback();
    for (const GroupKind kind : {GroupKind::Navigation,
                                 GroupKind::Destination,
                                 GroupKind::Quality, GroupKind::Text}) {
        run_lifecycle_exact(kind);
        run_lifecycle_deletion(kind, false);
        run_lifecycle_deletion(kind, true);
        run_lifecycle_limit_rollback(kind);
    }
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
    run_lifecycle_parity();
    return 0;
}
