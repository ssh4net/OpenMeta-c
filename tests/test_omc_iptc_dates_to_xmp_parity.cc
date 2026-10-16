#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_translation.h"
#include <openmeta/meta_flags.h>
#include <openmeta/meta_key.h>
#include <openmeta/meta_store.h>
#include <openmeta/meta_value.h>
#include <openmeta/metadata_translation.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

namespace {
using CStatus = omc_iptc_date_to_xmp_translation_status;
using Status = openmeta::MetadataIptcDateToXmpStatus;
constexpr const char* namespaces[] = {"http://ns.adobe.com/photoshop/1.0/", "http://ns.adobe.com/xap/1.0/"};
constexpr const char* paths[] = {"DateCreated", "CreateDate"};
unsigned cases = 0;

struct Stores {
    omc_store source{};
    omc_store output{};
    openmeta::MetaStore reference;
    openmeta::MetaStore reference_output;
};

omc_byte_ref append(omc_store& s, std::string_view value) {
    omc_byte_ref ref;
    assert(omc_arena_append(&s.arena, value.data(), value.size(), &ref) == OMC_STATUS_OK);
    return ref;
}
std::string_view view(const omc_store& s, omc_byte_ref ref) {
    const omc_const_bytes b = omc_arena_view(&s.arena, ref);
    return {reinterpret_cast<const char*>(b.data), b.size};
}
std::string_view view(const openmeta::MetaStore& s, openmeta::ByteSpan ref) {
    const auto b = s.arena().span(ref);
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}
openmeta::EntryFlags flags(omc_entry_flags f) {
    openmeta::EntryFlags result = openmeta::EntryFlags::None;
    if (f & OMC_ENTRY_FLAG_DIRTY) result |= openmeta::EntryFlags::Dirty;
    if (f & OMC_ENTRY_FLAG_DELETED) result |= openmeta::EntryFlags::Deleted;
    if (f & OMC_ENTRY_FLAG_DERIVED) result |= openmeta::EntryFlags::Derived;
    if (f & OMC_ENTRY_FLAG_TRUNCATED) result |= openmeta::EntryFlags::Truncated;
    if (f & OMC_ENTRY_FLAG_UNREADABLE) result |= openmeta::EntryFlags::Unreadable;
    if (f & OMC_ENTRY_FLAG_CONTEXTUAL_NAME) result |= openmeta::EntryFlags::ContextualName;
    const omc_entry_flags known = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED |
        OMC_ENTRY_FLAG_DERIVED | OMC_ENTRY_FLAG_TRUNCATED | OMC_ENTRY_FLAG_UNREADABLE |
        OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
    if (f & ~known) result |= static_cast<openmeta::EntryFlags>(0x80U);
    return result;
}
void init(Stores& s) {
    omc_store_init(&s.source); omc_store_init(&s.output);
    omc_block_info info{};
    for (unsigned i = 0; i < 2; ++i) {
        assert(omc_store_add_block(&s.source, &info, nullptr) == OMC_STATUS_OK);
        assert(s.reference.add_block({}) == i);
    }
}
void fini(Stores& s) { omc_store_fini(&s.output); omc_store_fini(&s.source); }

void native(Stores& s, unsigned dataset, std::string_view text,
            omc_entry_flags entry_flags = 0U, unsigned block = 0U,
            bool broken_tombstone = false) {
    omc_entry c{};
    omc_key_make_iptc_dataset(&c.key, 2U, static_cast<omc_u16>(dataset));
    omc_val_make_bytes(&c.value, append(s.source, text));
    c.flags = entry_flags; c.origin.block = block; c.origin.order_in_block = dataset;
    openmeta::Entry cpp;
    cpp.key = openmeta::make_iptc_dataset_key(2U, static_cast<uint16_t>(dataset));
    cpp.value = openmeta::make_bytes(s.reference.arena(), std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size()));
    cpp.flags = flags(entry_flags); cpp.origin.block = block; cpp.origin.order_in_block = dataset;
    if (broken_tombstone) {
        c.value.u.ref.offset = 0xfffffffeU; c.value.u.ref.size = 10U; c.value.count = 99U;
        c.origin.wire_type_name.offset = 0xfffffffeU; c.origin.wire_type_name.size = 5U;
        cpp.value.data.span.offset = 0xfffffffeU; cpp.value.data.span.size = 10U; cpp.value.count = 99U;
        cpp.origin.wire_type_name.offset = 0xfffffffeU; cpp.origin.wire_type_name.size = 5U;
    }
    assert(omc_store_add_entry(&s.source, &c, nullptr) == OMC_STATUS_OK);
    assert(s.reference.add_entry(cpp) != openmeta::kInvalidEntryId);
}
void target(Stores& s, unsigned group, std::string_view text, const char* path = nullptr) {
    if (!path) path = paths[group];
    omc_entry c{};
    const omc_byte_ref ns = append(s.source, namespaces[group]);
    const omc_byte_ref key = append(s.source, path);
    omc_key_make_xmp_property(&c.key, ns, key);
    omc_val_make_text(&c.value, append(s.source, text), OMC_TEXT_UTF8);
    c.origin.block = 1U; c.origin.order_in_block = 321U;
    assert(omc_store_add_entry(&s.source, &c, nullptr) == OMC_STATUS_OK);
    openmeta::Entry cpp;
    cpp.key = openmeta::make_xmp_property_key(s.reference.arena(), namespaces[group], path);
    cpp.value = openmeta::make_text(s.reference.arena(), text, openmeta::TextEncoding::Utf8);
    cpp.origin.block = 1U; cpp.origin.order_in_block = 321U;
    assert(s.reference.add_entry(cpp) != openmeta::kInvalidEntryId);
}
void compare_target(const Stores& s, unsigned group, const char* expected) {
    const omc_entry* c = nullptr;
    const openmeta::Entry* cpp = nullptr;
    for (omc_size i = 0; i < s.output.entry_count; ++i) {
        const omc_entry& e = s.output.entries[i];
        if (e.key.kind == OMC_KEY_XMP_PROPERTY && !(e.flags & OMC_ENTRY_FLAG_DELETED) &&
            view(s.output, e.key.u.xmp_property.schema_ns) == namespaces[group] &&
            view(s.output, e.key.u.xmp_property.property_path) == paths[group]) {
            assert(!c); c = &e;
        }
    }
    for (const openmeta::Entry& e : s.reference_output.entries()) {
        if (e.key.kind == openmeta::MetaKeyKind::XmpProperty && !openmeta::any(e.flags, openmeta::EntryFlags::Deleted) &&
            view(s.reference_output, e.key.data.xmp_property.schema_ns) == namespaces[group] &&
            view(s.reference_output, e.key.data.xmp_property.property_path) == paths[group]) {
            assert(!cpp); cpp = &e;
        }
    }
    if (!expected) { assert(!c && !cpp); return; }
    assert(c && cpp);
    assert(view(s.output, c->value.u.ref) == expected);
    assert(view(s.reference_output, cpp->value.data.span) == expected);
    assert(c->origin.block == cpp->origin.block);
    assert(c->origin.order_in_block == cpp->origin.order_in_block);
    assert(((c->flags & OMC_ENTRY_FLAG_DIRTY) != 0U) == openmeta::any(cpp->flags, openmeta::EntryFlags::Dirty));
}
omc_iptc_date_to_xmp_translation_opts defaults() {
    omc_iptc_date_to_xmp_translation_opts o;
    omc_iptc_date_to_xmp_translation_opts_init(&o); return o;
}
void check(Stores& s, const omc_iptc_date_to_xmp_translation_opts& o,
           CStatus expected, Status reference_expected, const char* created = nullptr,
           const char* digital = nullptr) {
    openmeta::MetadataIptcDateToXmpOptions r;
    r.created = o.created != 0; r.digital_creation = o.digital_creation != 0;
    r.source_mode = o.all_sources ? openmeta::MetadataDateTranslationSourceMode::All : openmeta::MetadataDateTranslationSourceMode::DirtyOnly;
    r.conflict_policy = o.conflict == OMC_TRANSLATION_REPLACE ? openmeta::MetadataIptcToXmpConflictPolicy::ReplaceExisting :
        o.conflict == OMC_TRANSLATION_PRESERVE ? openmeta::MetadataIptcToXmpConflictPolicy::PreserveExisting : openmeta::MetadataIptcToXmpConflictPolicy::FailOnConflict;
    r.max_source_properties = o.max_source_properties; r.max_added_entries = o.max_added_entries;
    r.max_operations = o.max_operations; r.max_total_text_bytes = o.max_total_text_bytes;
    s.reference.finalize();
    const auto c = omc_translate_iptc_dates_to_xmp(&s.source, &s.output, &o);
    const auto cpp = openmeta::translate_iptc_dates_to_xmp(s.reference, r, &s.reference_output);
    ++cases;
    if (c.status != expected || cpp.status != reference_expected)
        std::fprintf(stderr, "date parity case %u: C=%d expected=%d C++=%d expected=%d\n", cases, int(c.status), int(expected), int(cpp.status), int(reference_expected));
    assert(c.status == expected && cpp.status == reference_expected);
    assert(c.failed_source == cpp.failed_source_entry);
    assert(c.source_properties == cpp.source_properties);
    assert(c.groups_translated == cpp.groups_translated && c.groups_preserved == cpp.groups_preserved && c.groups_unchanged == cpp.groups_unchanged);
    assert(c.entries_added == cpp.entries_added && c.entries_updated == cpp.entries_updated && c.entries_removed == cpp.entries_removed);
    if (cpp.failed_mapping == openmeta::MetadataDateTranslationMapping::None) assert(c.failed_mapping == 0U);
    else if (cpp.failed_mapping == openmeta::MetadataDateTranslationMapping::PhotoshopDateCreated) assert(c.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
    else { assert(cpp.failed_mapping == openmeta::MetadataDateTranslationMapping::XmpCreateDate); assert(c.failed_mapping == OMC_TRANSLATE_CREATE_IPTC); }
    if (expected == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK) {
        assert(s.output.entry_count == s.reference_output.entries().size());
        assert(s.output.arena.size >= s.source.arena.size);
        if (s.source.arena.size)
            assert(std::memcmp(s.source.arena.data, s.output.arena.data, s.source.arena.size) == 0);
        compare_target(s, 0U, created); compare_target(s, 1U, digital);
    } else { assert(s.output.entry_count == 0U && s.reference_output.entries().empty()); }
}

void lexical_cases() {
    struct Case { const char* date; const char* time; const char* output; CStatus c; Status cpp; };
    const Case rows[] = {
        {"20260000", nullptr, "2026", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"20261000", nullptr, "2026-10", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"00010101", nullptr, "0001-01-01", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"20000229", "235960-0000", "2000-02-29T23:59:60-00:00", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"99991231", "000000+2359", "9999-12-31T00:00:00+23:59", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"20261016", "123456", "2026-10-16T12:34:56", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"20261016", "123456+0000", "2026-10-16T12:34:56+00:00", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"20261016", "123456-2359", "2026-10-16T12:34:56-23:59", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, Status::Ok},
        {"00000000", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION, Status::UnsupportedPrecision},
        {"20260001", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION, Status::UnsupportedPrecision},
        {"20260000", "123456", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION, Status::UnsupportedPrecision},
        {"20261000", "123456", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION, Status::UnsupportedPrecision},
        {"19000229", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20260230", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261301", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20260132", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"2026010x", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016 ", nullptr, nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "240000", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "126000", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "125961", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "120000+2400", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "120000-0060", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "120000Z", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "120000.1", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {"20261016", "", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME, Status::InvalidDateTime},
        {nullptr, "120000", nullptr, OMC_IPTC_DATE_TO_XMP_TRANSLATION_INCOMPLETE_SOURCE, Status::IncompleteSource}
    };
    for (unsigned group = 0; group < 2; ++group) for (const Case& row : rows) {
        Stores s; init(s);
        if (row.date) native(s, group ? 62U : 55U, row.date);
        if (row.time) native(s, group ? 63U : 60U, row.time);
        check(s, defaults(), row.c, row.cpp, group ? nullptr : row.output, group ? row.output : nullptr);
        fini(s);
    }
}

void policies_and_deletion() {
    const omc_entry_flags removed = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    for (unsigned group = 0; group < 2; ++group) for (unsigned mode = 0; mode < 10; ++mode) {
        Stores s; init(s); auto o = defaults();
        const unsigned date = group ? 62U : 55U; const unsigned time = group ? 63U : 60U;
        const char* expected = "2026-10-16";
        CStatus status = OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK; Status cpp_status = Status::Ok;
        if (mode < 3U) {
            native(s, date, "20261016"); target(s, group, "old");
            if (mode == 0U) { status = OMC_IPTC_DATE_TO_XMP_TRANSLATION_DESTINATION_CONFLICT; cpp_status = Status::DestinationConflict; }
            else if (mode == 1U) { o.conflict = OMC_TRANSLATION_PRESERVE; expected = "old"; }
            else o.conflict = OMC_TRANSLATION_REPLACE;
        } else if (mode == 3U) {
            native(s, date, "20261016"); native(s, time, "", removed | OMC_ENTRY_FLAG_UNREADABLE, 77U, true);
        } else if (mode == 4U) {
            native(s, date, "", removed | OMC_ENTRY_FLAG_TRUNCATED, 77U, true);
            target(s, group, "old"); o.conflict = OMC_TRANSLATION_REPLACE; expected = nullptr;
        } else if (mode == 5U) {
            native(s, date, "", removed, 77U, true); native(s, date, "20261016");
        } else if (mode == 6U) {
            native(s, date, "20261016"); native(s, time, "123456", OMC_ENTRY_FLAG_DIRTY);
            o.all_sources = 0; expected = "2026-10-16T12:34:56";
        } else if (mode == 7U) {
            native(s, date, "20261016"); o.all_sources = 0; expected = nullptr;
        } else if (mode == 8U) {
            native(s, date, "", removed | 0x80U, 77U, true);
            status = OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE; cpp_status = Status::InvalidSourceValue;
        } else {
            native(s, date, "20261016"); native(s, time, "123456", 0U, 1U);
            status = OMC_IPTC_DATE_TO_XMP_TRANSLATION_AMBIGUOUS_SOURCE; cpp_status = Status::AmbiguousSource;
        }
        check(s, o, status, cpp_status, group ? nullptr : expected, group ? expected : nullptr);
        if (mode >= 3U && mode <= 5U) assert(std::memcmp(&s.source.entries[mode == 3U ? 1U : 0U], &s.output.entries[mode == 3U ? 1U : 0U], sizeof(omc_entry)) == 0);
        fini(s);
    }
}
void bounds_and_shapes() {
    for (unsigned mode = 0; mode < 8; ++mode) {
        Stores s; init(s); auto o = defaults();
        native(s, 55U, mode == 3U ? "bad_date" : "20261016");
        CStatus c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK; Status cpp = Status::Ok;
        const char* expected = "2026-10-16"; const char* digital = nullptr;
        if (mode < 2U) {
            native(s, 62U, "20260000");
            if (mode == 0U) { o.max_added_entries = 1U; c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_ENTRY_LIMIT; cpp = Status::EntryLimitExceeded; }
            else { o.max_operations = 1U; c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_OPERATION_LIMIT; cpp = Status::OperationLimitExceeded; }
        } else if (mode == 2U) {
            native(s, 55U, "20261016"); o.max_source_properties = 1U;
            c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT; cpp = Status::SourceLimitExceeded;
        } else if (mode == 3U) {
            target(s, 0U, "old"); o.max_total_text_bytes = 10U;
            c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT; cpp = Status::SourceLimitExceeded;
        } else if (mode == 4U || mode == 5U) {
            target(s, 0U, "old", "DateCreated/child");
            if (mode == 4U) { c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE; cpp = Status::UnsupportedDestinationShape; }
            else { o.conflict = OMC_TRANSLATION_PRESERVE; expected = nullptr; }
        } else if (mode == 6U) {
            target(s, 0U, "unrelated", "DateCreatedElsewhere");
        } else { o.created = 0; o.digital_creation = 0; expected = nullptr; }
        check(s, o, c, cpp, expected, digital); fini(s);
    }
}
void diagnostic_order() {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Stores s; init(s); auto o = defaults();
        CStatus c; Status cpp;
        if (mode == 0U) {
            native(s, 55U, "20260230"); target(s, 0U, "\xff");
            c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME; cpp = Status::InvalidDateTime;
        } else if (mode == 1U) {
            native(s, 55U, "20261016"); native(s, 62U, "20260230"); target(s, 0U, "old");
            c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_DESTINATION_CONFLICT; cpp = Status::DestinationConflict;
        } else if (mode == 2U) {
            native(s, 55U, "20261016"); native(s, 62U, "20261016");
            target(s, 0U, "\xff"); target(s, 1U, "old"); o.max_total_text_bytes = 18U;
            c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT; cpp = Status::SourceLimitExceeded;
        } else if (mode == 3U) {
            native(s, 55U, "20261016"); native(s, 62U, "20260230"); target(s, 0U, "\xff");
            c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE; cpp = Status::UnsupportedDestinationShape;
        } else {
            native(s, 55U, "", OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED | 0x40U, 77U, true);
            c = OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE; cpp = Status::InvalidSourceValue;
        }
        check(s, o, c, cpp); fini(s);
    }
}
} // namespace
int main() {
    lexical_cases(); policies_and_deletion(); bounds_and_shapes(); diagnostic_order();
    std::printf("native IPTC date parity: %u paired cases\n", cases);
    return 0;
}
