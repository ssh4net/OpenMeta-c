#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_translation.h"
#include <openmeta/metadata_translation.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
using Status = openmeta::MetadataDateTranslationStatus;
const char* const namespaces[] = {"http://ns.adobe.com/exif/1.0/", "http://ns.adobe.com/xap/1.0/"};
const char* const paths[] = {"DateTimeOriginal", "CreateDate"};
const uint16_t tags[2][3] = {{0x9003U, 0x9011U, 0x9291U}, {0x9004U, 0x9012U, 0x9292U}};
const char* const values[] = {"2026:10:17 12:34:56", "+09:00", "125"};
unsigned cases = 0U;
struct Stores {
    omc_store c{};
    openmeta::MetaStore cpp;
};
omc_byte_ref append(omc_store& store, std::string_view value) {
    omc_byte_ref ref;
    assert(omc_arena_append(&store.arena, value.data(), value.size(), &ref) == OMC_STATUS_OK);
    return ref;
}
void add(Stores& s, unsigned group, int field, std::string_view text, unsigned flags, bool raw = false) {
    omc_entry c{};
    openmeta::Entry cpp;
    if (field < 0) {
        const omc_byte_ref ns = append(s.c, namespaces[group]);
        const omc_byte_ref path = append(s.c, paths[group]);
        omc_key_make_xmp_property(&c.key, ns, path);
        cpp.key = openmeta::make_xmp_property_key(s.cpp.arena(), namespaces[group], paths[group]);
    } else {
        omc_key_make_exif_tag(&c.key, append(s.c, "exififd"), tags[group][field]);
        cpp.key = openmeta::make_exif_tag_key(s.cpp.arena(), "exififd", tags[group][field]);
    }
    const omc_byte_ref payload = append(s.c, text);
    if (raw) {
        omc_val_make_bytes(&c.value, payload);
        cpp.value = openmeta::make_bytes(s.cpp.arena(), {reinterpret_cast<const std::byte*>(text.data()), text.size()});
    } else {
        omc_val_make_text(&c.value, payload, field < 0 ? OMC_TEXT_UTF8 : OMC_TEXT_ASCII);
        cpp.value = openmeta::make_text(s.cpp.arena(), text, field < 0 ? openmeta::TextEncoding::Utf8 : openmeta::TextEncoding::Ascii);
    }
    c.flags = flags;
    assert((flags & ~(OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) == 0U);
    if (flags & OMC_ENTRY_FLAG_DIRTY) cpp.flags |= openmeta::EntryFlags::Dirty;
    if (flags & OMC_ENTRY_FLAG_DELETED) cpp.flags |= openmeta::EntryFlags::Deleted;
    c.origin.block = OMC_INVALID_BLOCK_ID;
    c.origin.order_in_block = 0xFFFFFFFEU;
    cpp.origin.order_in_block = c.origin.order_in_block;
    c.origin.wire_type_name = append(s.c, field < 0 ? "source" : "native");
    cpp.origin.wire_type_name = s.cpp.arena().append_string(field < 0 ? "source" : "native");
    if (field >= 0) {
        c.origin.wire_type.family = OMC_WIRE_TIFF;
        c.origin.wire_type.code = 2U;
        c.origin.wire_count = static_cast<omc_u32>(text.size());
        cpp.origin.wire_type.family = openmeta::WireFamily::Tiff;
        cpp.origin.wire_type.code = 2U;
        cpp.origin.wire_count = c.origin.wire_count;
        c.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_MAIN_Z;
        c.origin.name_context_variant = 7U;
        cpp.origin.name_context_kind = openmeta::EntryNameContextKind::NikonMainZ;
        cpp.origin.name_context_variant = c.origin.name_context_variant;
    }
    assert(omc_store_add_entry(&s.c, &c, nullptr) == OMC_STATUS_OK);
    assert(s.cpp.add_entry(cpp) != openmeta::kInvalidEntryId);
}
void compare_bytes(const omc_arena& arena, omc_byte_ref ref,
                   std::span<const std::byte> expected) {
    const omc_const_bytes actual = omc_arena_view(&arena, ref);
    assert(actual.size == expected.size());
    assert(actual.size == 0U || std::memcmp(actual.data, expected.data(), actual.size) == 0);
}
void compare_entries(const Stores& s) {
    assert(s.c.entry_count == s.cpp.entries().size());
    for (size_t i = 0U; i < s.c.entry_count; ++i) {
        const omc_entry& c = s.c.entries[i];
        const openmeta::Entry& cpp = s.cpp.entries()[i];
        assert((c.flags & ~(OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) == 0U);
        assert(((c.flags & OMC_ENTRY_FLAG_DIRTY) != 0U) == openmeta::any(cpp.flags, openmeta::EntryFlags::Dirty));
        assert(((c.flags & OMC_ENTRY_FLAG_DELETED) != 0U) == openmeta::any(cpp.flags, openmeta::EntryFlags::Deleted));
        assert((static_cast<unsigned>(cpp.flags) & ~static_cast<unsigned>(openmeta::EntryFlags::Dirty | openmeta::EntryFlags::Deleted)) == 0U);
        if (c.key.kind == OMC_KEY_EXIF_TAG) {
            assert(cpp.key.kind == openmeta::MetaKeyKind::ExifTag);
            assert(c.key.u.exif_tag.tag == cpp.key.data.exif_tag.tag);
            compare_bytes(s.c.arena, c.key.u.exif_tag.ifd, s.cpp.arena().span(cpp.key.data.exif_tag.ifd));
        } else {
            assert(c.key.kind == OMC_KEY_XMP_PROPERTY && cpp.key.kind == openmeta::MetaKeyKind::XmpProperty);
            compare_bytes(s.c.arena, c.key.u.xmp_property.schema_ns, s.cpp.arena().span(cpp.key.data.xmp_property.schema_ns));
            compare_bytes(s.c.arena, c.key.u.xmp_property.property_path, s.cpp.arena().span(cpp.key.data.xmp_property.property_path));
        }
        assert((c.value.kind == OMC_VAL_TEXT && cpp.value.kind == openmeta::MetaValueKind::Text) ||
               (c.value.kind == OMC_VAL_BYTES && cpp.value.kind == openmeta::MetaValueKind::Bytes));
        assert(c.value.count == cpp.value.count);
        assert(static_cast<unsigned>(c.value.text_encoding) == static_cast<unsigned>(cpp.value.text_encoding));
        compare_bytes(s.c.arena, c.value.u.ref, s.cpp.arena().span(cpp.value.data.span));
        assert(c.origin.block == cpp.origin.block && c.origin.order_in_block == cpp.origin.order_in_block);
        assert(static_cast<unsigned>(c.origin.wire_type.family) == static_cast<unsigned>(cpp.origin.wire_type.family));
        assert(c.origin.wire_type.code == cpp.origin.wire_type.code && c.origin.wire_count == cpp.origin.wire_count);
        assert((c.origin.name_context_kind == OMC_ENTRY_NAME_CTX_NONE && cpp.origin.name_context_kind == openmeta::EntryNameContextKind::None) ||
            (c.origin.name_context_kind == OMC_ENTRY_NAME_CTX_NIKON_MAIN_Z && cpp.origin.name_context_kind == openmeta::EntryNameContextKind::NikonMainZ));
        assert(c.origin.name_context_variant == cpp.origin.name_context_variant);
        compare_bytes(s.c.arena, c.origin.wire_type_name, s.cpp.arena().span(cpp.origin.wire_type_name));
    }
}
omc_translation_status c_status(Status status) {
    switch (status) {
    case Status::Ok: return OMC_TRANSLATION_OK;
    case Status::NativeConflict: return OMC_TRANSLATION_NATIVE_CONFLICT;
    case Status::UnsupportedPrecision: return OMC_TRANSLATION_UNSUPPORTED_PRECISION;
    case Status::InvalidDateTime: return OMC_TRANSLATION_INVALID_SOURCE;
    case Status::EntryLimitExceeded:
    case Status::OperationLimitExceeded: return OMC_TRANSLATION_LIMIT;
    default: assert(false); return OMC_TRANSLATION_INVALID_SOURCE;
    }
}
bool check(Stores& source, Stores& output, unsigned group, unsigned policy,
            bool all, unsigned additions = 16U, unsigned operations = 1024U,
            bool both = false) {
    omc_translation_opts c;
    omc_translation_opts_init(&c);
    c.mappings = both ? OMC_TRANSLATE_DATE_ORIGINAL | OMC_TRANSLATE_CREATE_EXIF :
        group ? OMC_TRANSLATE_CREATE_EXIF : OMC_TRANSLATE_DATE_ORIGINAL;
    c.conflict = static_cast<omc_translation_conflict>(policy);
    c.all_sources = all;
    c.max_added_entries = additions; c.max_operations = operations;
    openmeta::MetadataDateTranslationOptions cpp;
    cpp.create_date_to_exif_digitized = both || group == 1U;
    cpp.date_time_original_to_exif_original = both || group == 0U;
    cpp.create_date_to_iptc_digital_creation = false;
    cpp.date_created_to_iptc_created = false;
    cpp.conflict_policy = policy == OMC_TRANSLATION_PRESERVE ? openmeta::MetadataDateTranslationConflictPolicy::PreserveExisting :
        policy == OMC_TRANSLATION_REPLACE ? openmeta::MetadataDateTranslationConflictPolicy::ReplaceExisting : openmeta::MetadataDateTranslationConflictPolicy::FailOnConflict;
    cpp.source_mode = all ? openmeta::MetadataDateTranslationSourceMode::All : openmeta::MetadataDateTranslationSourceMode::DirtyOnly;
    cpp.max_added_entries = additions; cpp.max_operations = operations;
    add(output, group, 0, "sentinel", 0U);
    const omc_store before = output.c;
    const omc_entry entry = output.c.entries[0];
    std::array<omc_u8, 128> bytes{};
    assert(before.arena.size <= bytes.size());
    std::memcpy(bytes.data(), before.arena.data, before.arena.size);
    source.cpp.finalize(); output.cpp.finalize();
    const openmeta::MetadataDateTranslationResult reference = openmeta::translate_xmp_creation_dates(source.cpp, cpp, &output.cpp);
    const omc_translation_res actual = omc_translate_xmp(&source.c, &output.c, &c, nullptr);
    ++cases;
    if (actual.status != c_status(reference.status) || actual.entries_added != reference.entries_added ||
        actual.entries_updated != reference.entries_updated || actual.entries_removed != reference.entries_removed)
        std::fprintf(stderr, "EXIF date case %u: status %u/%u counts %u,%u,%u / %u,%u,%u\n", cases,
            unsigned(actual.status), unsigned(reference.status), actual.entries_added, actual.entries_updated,
            actual.entries_removed, reference.entries_added, reference.entries_updated, reference.entries_removed);
    assert(actual.status == c_status(reference.status));
    assert(actual.entries_added == reference.entries_added && actual.entries_updated == reference.entries_updated && actual.entries_removed == reference.entries_removed);
    assert(actual.groups_translated == reference.groups_translated && actual.groups_unchanged == reference.groups_unchanged && actual.groups_preserved == reference.groups_preserved);
    if (actual.status != OMC_TRANSLATION_OK) {
        assert(std::memcmp(&output.c, &before, sizeof(before)) == 0);
        assert(std::memcmp(output.c.entries, &entry, sizeof(entry)) == 0);
        assert(std::memcmp(output.c.arena.data, bytes.data(), before.arena.size) == 0);
        assert(output.cpp.entries().size() == 1U);
        compare_entries(output);
        if (actual.status != OMC_TRANSLATION_LIMIT) {
            assert(actual.failed_source == reference.failed_source_entry);
            if (reference.failed_mapping == openmeta::MetadataDateTranslationMapping::XmpCreateDate)
                assert(actual.failed_mapping == OMC_TRANSLATE_CREATE_EXIF);
            else {
                assert(reference.failed_mapping == openmeta::MetadataDateTranslationMapping::XmpDateTimeOriginal);
                assert(actual.failed_mapping == OMC_TRANSLATE_DATE_ORIGINAL);
            }
        }
    } else compare_entries(output);
    return actual.status == OMC_TRANSLATION_OK;
}
void matrix() {
    for (unsigned group = 0U; group < 2U; ++group)
        for (unsigned policy = 0U; policy < 3U; ++policy)
            for (unsigned mode = 0U; mode < 15U; ++mode) {
                Stores source, output;
                const bool full = mode == 0U || mode == 2U || mode == 3U || mode == 4U || mode == 10U || mode == 11U || mode == 14U;
                add(source, group, -1, mode == 7U ? "deleted" : mode == 12U ? "2026" : mode == 13U ? "bad" :
                    full ? "2026-10-17T12:34:56.125+09:00" : "2026-10-17T12:34:56",
                    mode == 7U ? OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED : mode == 8U || mode == 9U ? 0U : OMC_ENTRY_FLAG_DIRTY);
                if (mode == 2U || mode == 3U || mode == 4U || mode == 10U || mode == 11U || mode == 14U) {
                    for (int field = 0; field < 3; ++field) {
                        std::array<char, 32> value{};
                        const char* text = mode == 11U && field == 0 ? "2000:01:01 00:00:00" : values[field];
                        std::memcpy(value.data(), text, std::strlen(text));
                        const size_t length = std::strlen(text) + (mode == 4U || mode == 14U ? 2U : 0U);
                        add(source, group, field, {value.data(), length}, mode == 3U ? OMC_ENTRY_FLAG_DIRTY : 0U, mode == 4U);
                    }
                    if (mode == 10U) add(source, group, 0, values[0], 0U);
                } else if (mode == 5U || mode == 6U) {
                    add(source, group, 0, values[0], 0U);
                    add(source, group, 1, "retained tombstone", OMC_ENTRY_FLAG_DELETED | (mode == 6U ? OMC_ENTRY_FLAG_DIRTY : 0U));
                    add(source, group, 2, "", OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
                }
                const bool ok = check(source, output, group, policy, mode == 9U);
                if (ok && (policy != OMC_TRANSLATION_PRESERVE || mode == 7U) && mode != 8U) {
                    Stores repeated;
                    assert(check(output, repeated, group, policy, mode == 9U));
                    assert(repeated.c.entry_count == output.c.entry_count);
                    omc_store_fini(&repeated.c);
                }
                omc_store_fini(&output.c); omc_store_fini(&source.c);
            }
}
void limits_and_late_failure() {
    for (unsigned group = 0U; group < 2U; ++group)
        for (unsigned mode = 0U; mode < 5U; ++mode) {
            Stores source, output;
            add(source, group, -1, "2026-10-17T12:34:56", OMC_ENTRY_FLAG_DIRTY);
            if (mode == 2U) {
                add(source, group, 0, values[0], 0U);
                add(source, group, 1, "", OMC_ENTRY_FLAG_DELETED);
                add(source, group, 2, "", OMC_ENTRY_FLAG_DELETED);
            }
            if (mode == 3U) add(source, 1U - group, -1, "2026-10-17T12:34:56", OMC_ENTRY_FLAG_DIRTY);
            if (mode == 4U) add(source, 1U - group, -1, "2026", OMC_ENTRY_FLAG_DIRTY);
            const bool ok = check(source, output, group, OMC_TRANSLATION_REPLACE, false,
                mode == 0U ? 2U : 16U, mode == 1U || mode == 2U ? 2U : 1024U, mode >= 3U);
            assert(ok == (mode == 3U));
            omc_store_fini(&output.c); omc_store_fini(&source.c);
        }
}
}
int main() {
    matrix(); limits_and_late_failure();
    std::printf("EXIF creation lifecycle parity: %u paired calls\n", cases);
    return 0;
}
