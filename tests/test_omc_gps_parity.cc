#include "omc/omc_translation.h"
#include "omc_test_assert.h"

#include <openmeta/meta_edit.h>
#include <openmeta/meta_key.h>
#include <openmeta/meta_value.h>
#include <openmeta/metadata_translation.h>

#include <cassert>
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
                            32U, &ns) == OMC_STATUS_OK);
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

}  // namespace

int
main()
{
    run_case(false);
    run_case(true);
    return 0;
}
