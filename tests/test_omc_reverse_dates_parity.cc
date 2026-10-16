#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_translation.h"
#include "omc/omc_iptc_serialize.h"
#include <openmeta/metadata_translation.h>
#include <openmeta/iptc_iim_serialize.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
using Status = openmeta::MetadataDateTranslationStatus;
const char* const ns[] = {"http://ns.adobe.com/photoshop/1.0/", "http://ns.adobe.com/xap/1.0/", "http://ns.adobe.com/exif/1.0/"};
const char* const paths[] = {"DateCreated", "CreateDate", "DateTimeOriginal"};
unsigned cases = 0U;
struct Fixture {
    omc_store source{};
    omc_store output{};
    openmeta::MetaStore cpp;
    openmeta::MetaStore cpp_output;
};
omc_byte_ref bytes(omc_store& s, std::string_view text) {
    omc_byte_ref ref;
    assert(omc_arena_append(&s.arena, text.data(), text.size(), &ref) == OMC_STATUS_OK);
    return ref;
}
void native_c(omc_store& s, unsigned dataset, std::string_view text) {
    omc_entry e{};
    omc_key_make_iptc_dataset(&e.key, 2U, static_cast<omc_u16>(dataset));
    omc_val_make_bytes(&e.value, bytes(s, text));
    e.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(&s, &e, nullptr) == OMC_STATUS_OK);
}
void native_cpp(openmeta::MetaStore& s, unsigned dataset, std::string_view text) {
    openmeta::Entry e;
    e.key = openmeta::make_iptc_dataset_key(2U, static_cast<uint16_t>(dataset));
    e.value = openmeta::make_bytes(s.arena(), {reinterpret_cast<const std::byte*>(text.data()), text.size()});
    assert(s.add_entry(e) != openmeta::kInvalidEntryId);
}
void native(Fixture& f, unsigned dataset, std::string_view text) {
    native_c(f.source, dataset, text); native_cpp(f.cpp, dataset, text);
}
void source(Fixture& f, unsigned group, std::string_view text, bool dirty = true, bool deleted = false) {
    omc_entry c{};
    const omc_byte_ref schema = bytes(f.source, ns[group]);
    const omc_byte_ref path = bytes(f.source, paths[group]);
    omc_key_make_xmp_property(&c.key, schema, path);
    omc_val_make_text(&c.value, bytes(f.source, text), OMC_TEXT_UTF8);
    c.origin.block = OMC_INVALID_BLOCK_ID;
    c.flags = (dirty ? OMC_ENTRY_FLAG_DIRTY : 0U) | (deleted ? OMC_ENTRY_FLAG_DELETED : 0U);
    assert(omc_store_add_entry(&f.source, &c, nullptr) == OMC_STATUS_OK);
    openmeta::Entry e;
    e.key = openmeta::make_xmp_property_key(f.cpp.arena(), ns[group], paths[group]);
    e.value = openmeta::make_text(f.cpp.arena(), text, openmeta::TextEncoding::Utf8);
    if (dirty) e.flags |= openmeta::EntryFlags::Dirty;
    if (deleted) e.flags |= openmeta::EntryFlags::Deleted;
    assert(f.cpp.add_entry(e) != openmeta::kInvalidEntryId);
}
omc_translation_opts options(unsigned group) {
    omc_translation_opts o; omc_translation_opts_init(&o);
    o.mappings = group ? OMC_TRANSLATE_CREATE_IPTC : OMC_TRANSLATE_DATE_CREATED;
    return o;
}
void check(Fixture& f, const omc_translation_opts& o, omc_translation_status expected, Status reference) {
    openmeta::MetadataDateTranslationOptions r;
    r.create_date_to_exif_digitized = (o.mappings & OMC_TRANSLATE_CREATE_EXIF) != 0U;
    r.create_date_to_iptc_digital_creation = (o.mappings & OMC_TRANSLATE_CREATE_IPTC) != 0U;
    r.date_created_to_iptc_created = (o.mappings & OMC_TRANSLATE_DATE_CREATED) != 0U;
    r.date_time_original_to_exif_original = (o.mappings & OMC_TRANSLATE_DATE_ORIGINAL) != 0U;
    r.source_mode = o.all_sources ? openmeta::MetadataDateTranslationSourceMode::All : openmeta::MetadataDateTranslationSourceMode::DirtyOnly;
    r.conflict_policy = o.conflict == OMC_TRANSLATION_PRESERVE ? openmeta::MetadataDateTranslationConflictPolicy::PreserveExisting :
        o.conflict == OMC_TRANSLATION_REPLACE ? openmeta::MetadataDateTranslationConflictPolicy::ReplaceExisting : openmeta::MetadataDateTranslationConflictPolicy::FailOnConflict;
    r.max_added_entries = o.max_added_entries < 16U ? o.max_added_entries : 16U;
    r.max_operations = o.max_operations < 1024U ? o.max_operations : 1024U;
    native_c(f.output, 5U, "sentinel"); native_cpp(f.cpp_output, 5U, "sentinel");
    f.cpp.finalize(); f.cpp_output.finalize();
    const omc_store before = f.output;
    const omc_entry sentinel = f.output.entries[0];
    const omc_translation_res c = omc_translate_xmp(&f.source, &f.output, &o, nullptr);
    const openmeta::MetadataDateTranslationResult cpp = openmeta::translate_xmp_creation_dates(f.cpp, r, &f.cpp_output);
    ++cases;
    if (c.status != expected || cpp.status != reference)
        std::fprintf(stderr, "reverse date case %u: C %d/%d, C++ %d/%d\n", cases, int(c.status), int(expected), int(cpp.status), int(reference));
    assert(c.status == expected && cpp.status == reference);
    assert(c.entries_added == cpp.entries_added && c.entries_updated == cpp.entries_updated && c.entries_removed == cpp.entries_removed);
    assert(c.groups_translated == cpp.groups_translated);
    if (expected == OMC_TRANSLATION_OK) {
        assert(c.groups_unchanged == cpp.groups_unchanged && c.groups_preserved == cpp.groups_preserved);
        omc_u8 wire[1024];
        std::array<std::byte, 1024> reference_wire{};
        const omc_iptc_iim_serialize_res a = omc_serialize_iptc_iim(&f.output, {wire, sizeof(wire)}, nullptr);
        const openmeta::IptcIimSerializeResult b = openmeta::serialize_iptc_iim(f.cpp_output, reference_wire);
        if (a.status == OMC_IPTC_IIM_SERIALIZE_NO_IPTC_DATA) {
            assert(b.status == openmeta::IptcIimSerializeStatus::NoIptcData);
            assert(a.written == 0U && b.written == 0U);
            assert(a.needed == 0U && b.needed == 0U);
            assert(a.entries_serialized == 0U && b.entries_serialized == 0U);
        } else {
            assert(a.status == OMC_IPTC_IIM_SERIALIZE_OK && b.status == openmeta::IptcIimSerializeStatus::Ok);
            assert(a.written == b.written && a.entries_serialized == b.entries_serialized);
            assert(std::memcmp(wire, reference_wire.data(), static_cast<size_t>(a.written)) == 0);
        }
    } else {
        if (c.failed_source != cpp.failed_source_entry && expected != OMC_TRANSLATION_LIMIT && expected != OMC_TRANSLATION_AMBIGUOUS_SOURCE)
            std::fprintf(stderr, "reverse date case %u: failure source C %u C++ %u\n", cases, c.failed_source, cpp.failed_source_entry);
        if (expected == OMC_TRANSLATION_AMBIGUOUS_SOURCE) {
            /* Existing C diagnostics identify the duplicate; C++ identifies the first source. */
            assert(c.failed_source == 1U && cpp.failed_source_entry == 0U);
        } else assert(c.failed_source == cpp.failed_source_entry || expected == OMC_TRANSLATION_LIMIT);
        if (expected != OMC_TRANSLATION_LIMIT) {
            if (cpp.failed_mapping == openmeta::MetadataDateTranslationMapping::PhotoshopDateCreated)
                assert(c.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
            else if (cpp.failed_mapping == openmeta::MetadataDateTranslationMapping::XmpCreateDate)
                assert(c.failed_mapping == OMC_TRANSLATE_CREATE_IPTC || c.failed_mapping == OMC_TRANSLATE_CREATE_EXIF);
            else assert(cpp.failed_mapping == openmeta::MetadataDateTranslationMapping::XmpDateTimeOriginal && c.failed_mapping == OMC_TRANSLATE_DATE_ORIGINAL);
        }
        assert(std::memcmp(&f.output, &before, sizeof(before)) == 0);
        assert(std::memcmp(&f.output.entries[0], &sentinel, sizeof(sentinel)) == 0);
        assert(std::memcmp(f.output.arena.data, "sentinel", 8U) == 0);
        assert(f.cpp_output.entries().size() == 1U);
        const std::span<const std::byte> v = f.cpp_output.arena().span(f.cpp_output.entries()[0].value.data.span);
        assert(v.size() == 8U && std::memcmp(v.data(), "sentinel", 8U) == 0);
    }
    omc_store_fini(&f.source); omc_store_fini(&f.output);
}
void syntax_cases() {
    const char* const valid[] = {"0001", "1861", "9999", "2024-02", "2026-10", "9999-12", "2000-02-29", "2026-10-17T12:34:56", "2026-10-17T23:59:60-00:00", "2026-10-17T12:34:56+23:59"};
    const char* const invalid[] = {"", "0000", "123", "12345", "20x4", "2024-0", "2024-00", "2024-13", "2024-0x", "2024/01", "2024-01-00", "2024-00-01", "1900-02-29", "2024 ", "2024-01 ", "2024T12:00:00", "2024Z", "2024-01Z", "2024-01+09:00", "2024-01T12:00:00", "2024-01.1"};
    for (unsigned group = 0; group < 2U; ++group) {
        for (const char* text : valid) { Fixture f; source(f, group, text); check(f, options(group), OMC_TRANSLATION_OK, Status::Ok); }
        for (const char* text : invalid) {
            Fixture f; source(f, group, text);
            check(f, options(group), OMC_TRANSLATION_INVALID_SOURCE, text[0] ? Status::InvalidDateTime : Status::InvalidSourceValue);
        }
        Fixture fraction; source(fraction, group, "2026-10-17T12:34:56.1Z");
        check(fraction, options(group), OMC_TRANSLATION_UNSUPPORTED_PRECISION, Status::UnsupportedPrecision);
    }
}
void policies() {
    for (unsigned group = 0; group < 2U; ++group) for (unsigned mode = 0; mode < 9U; ++mode) {
        Fixture f; omc_translation_opts o = options(group);
        omc_translation_status expected = OMC_TRANSLATION_OK; Status cpp = Status::Ok;
        source(f, group, "2024-02", mode != 4U && mode != 5U, mode == 6U || mode == 7U);
        if (mode < 3U || mode == 6U) {
            native(f, group ? 62U : 55U, "20000101"); native(f, group ? 63U : 60U, "010203+0900");
            if (mode == 0U) { expected = OMC_TRANSLATION_NATIVE_CONFLICT; cpp = Status::NativeConflict; }
            else o.conflict = mode == 1U ? OMC_TRANSLATION_PRESERVE : OMC_TRANSLATION_REPLACE;
        } else if (mode == 3U) native(f, group ? 62U : 55U, "20240200");
        else if (mode == 5U) o.all_sources = 1;
        else if (mode == 8U) {
            source(f, group, "2024-02"); expected = OMC_TRANSLATION_AMBIGUOUS_SOURCE; cpp = Status::AmbiguousSource;
        }
        check(f, o, expected, cpp);
    }
}
void atomic_cases() {
    for (unsigned mode = 0; mode < 6U; ++mode) {
        Fixture f; omc_translation_opts o = options(0U);
        omc_translation_status expected = OMC_TRANSLATION_UNSUPPORTED_PRECISION; Status cpp = Status::UnsupportedPrecision;
        if (mode == 0U) { source(f, 1U, "2024"); o.mappings = OMC_TRANSLATE_CREATE_EXIF | OMC_TRANSLATE_CREATE_IPTC; }
        else if (mode == 1U) { source(f, 0U, "2024-02"); source(f, 2U, "2025"); o.mappings |= OMC_TRANSLATE_DATE_ORIGINAL; }
        else {
            source(f, 0U, "2024"); source(f, 1U, "2025-09"); o.mappings |= OMC_TRANSLATE_CREATE_IPTC;
            if (mode == 2U) { o.max_added_entries = 1U; expected = OMC_TRANSLATION_LIMIT; cpp = Status::EntryLimitExceeded; }
            else if (mode == 3U) { o.max_operations = 1U; expected = OMC_TRANSLATION_LIMIT; cpp = Status::OperationLimitExceeded; }
            else if (mode == 4U) { expected = OMC_TRANSLATION_OK; cpp = Status::Ok; }
            else { source(f, 2U, "2025-00"); o.mappings |= OMC_TRANSLATE_DATE_ORIGINAL; expected = OMC_TRANSLATION_INVALID_SOURCE; cpp = Status::InvalidDateTime; }
        }
        check(f, o, expected, cpp);
    }
}
}
int main() {
    syntax_cases(); policies(); atomic_cases();
    std::printf("reverse IPTC date parity: %u paired cases\n", cases);
    return 0;
}
