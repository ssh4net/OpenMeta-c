#include "omc_iptc_fixture.h"
#include "omc_test_assert.h"
#include <cstdio>
#include <cstring>
#include <openmeta/metadata_translation.h>
#include <string_view>

static std::string_view
text(const omc_store &s, omc_byte_ref ref)
{
    const auto b = omc_arena_view(&s.arena, ref);
    return {reinterpret_cast<const char *>(b.data), b.size};
}

static openmeta::MetaStore
copy_source(const omc_store &s)
{
    openmeta::MetaStore result;
    for (omc_size i = 0; i < s.entry_count; ++i) {
        const auto &e = s.entries[i];
        openmeta::Entry ce;
        if (e.key.kind == OMC_KEY_XMP_PROPERTY)
            ce.key = openmeta::make_xmp_property_key(
                result.arena(), text(s, e.key.u.xmp_property.schema_ns),
                text(s, e.key.u.xmp_property.property_path));
        else
            ce.key = openmeta::make_iptc_dataset_key(e.key.u.iptc_dataset.record,
                                                     e.key.u.iptc_dataset.dataset);
        if (e.value.kind == OMC_VAL_TEXT)
            ce.value = openmeta::make_text(result.arena(), text(s, e.value.u.ref),
                                           openmeta::TextEncoding::Utf8);
        else if (e.value.kind == OMC_VAL_BYTES) {
            const auto b = omc_arena_view(&s.arena, e.value.u.ref);
            ce.value = openmeta::make_bytes(
                result.arena(), {reinterpret_cast<const std::byte *>(b.data), b.size});
        } else if (e.value.kind == OMC_VAL_ARRAY) {
            const auto b = omc_arena_view(&s.arena, e.value.u.ref);
            assert(e.value.elem_type == OMC_ELEM_U8);
            ce.value = openmeta::make_array(
                result.arena(), openmeta::MetaElementType::U8,
                {reinterpret_cast<const std::byte *>(b.data), b.size}, 1U);
        } else if (e.value.elem_type == OMC_ELEM_I64)
            ce.value = openmeta::make_i64(e.value.u.i64);
        else if (e.value.elem_type == OMC_ELEM_F64_BITS)
            ce.value = openmeta::make_f64_bits(e.value.u.f64_bits);
        else if (e.value.elem_type == OMC_ELEM_U64)
            ce.value = openmeta::make_u64(e.value.u.u64);
        else
            ce.value = openmeta::make_u32(static_cast<uint32_t>(e.value.u.u64));
        if ((e.flags & OMC_ENTRY_FLAG_DIRTY) != 0U)
            ce.flags = ce.flags | openmeta::EntryFlags::Dirty;
        if ((e.flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            ce.flags = ce.flags | openmeta::EntryFlags::Deleted;
        ce.origin.order_in_block = e.origin.order_in_block;
        const auto wire = text(s, e.origin.wire_type_name);
        ce.origin.wire_type_name = result.arena().append(
            {reinterpret_cast<const std::byte *>(wire.data()), wire.size()});
        assert(result.add_entry(ce) != openmeta::kInvalidEntryId);
    }
    result.finalize();
    return result;
}

static omc_translation_status
mapped_status(openmeta::MetadataDescriptiveTranslationStatus status)
{
    using S = openmeta::MetadataDescriptiveTranslationStatus;
    switch (status) {
    case S::Ok:
        return OMC_TRANSLATION_OK;
    case S::InvalidOptions:
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case S::AmbiguousSource:
        return OMC_TRANSLATION_AMBIGUOUS_SOURCE;
    case S::InvalidSourceValue:
        return OMC_TRANSLATION_INVALID_SOURCE;
    case S::ValueTooLong:
        return OMC_TRANSLATION_VALUE_TOO_LONG;
    case S::NativeConflict:
        return OMC_TRANSLATION_NATIVE_CONFLICT;
    case S::NativeEncodingConflict:
        return OMC_TRANSLATION_ENCODING_CONFLICT;
    case S::SourceLimitExceeded:
    case S::EntryLimitExceeded:
    case S::OperationLimitExceeded:
        return OMC_TRANSLATION_LIMIT;
    default:
        assert(false);
        return OMC_TRANSLATION_NO_MEMORY;
    }
}

static omc_u32
mapped_field(openmeta::MetadataDescriptiveTranslationMapping mapping)
{
    const auto ordinal = static_cast<unsigned>(mapping);
    assert(ordinal <= 20U);
    return ordinal == 0U ? 0U : 1U << (ordinal - 1U);
}

static void
compare_native(const omc_store &s, const openmeta::MetaStore &cpp)
{
    size_t ordinal = 0;
    for (const auto &e : cpp.entries()) {
        if (e.key.kind != openmeta::MetaKeyKind::IptcDataset ||
            openmeta::any(e.flags, openmeta::EntryFlags::Deleted))
            continue;
        size_t seen = 0;
        const omc_entry *ce = nullptr;
        for (omc_size i = 0; i < s.entry_count; ++i) {
            if (s.entries[i].key.kind == OMC_KEY_IPTC_DATASET &&
                (s.entries[i].flags & OMC_ENTRY_FLAG_DELETED) == 0U &&
                seen++ == ordinal) {
                ce = &s.entries[i];
                break;
            }
        }
        assert(ce != nullptr);
        assert(ce->key.u.iptc_dataset.record == e.key.data.iptc_dataset.record);
        assert(ce->key.u.iptc_dataset.dataset == e.key.data.iptc_dataset.dataset);
        const auto bytes = cpp.arena().span(e.value.data.span);
        const auto cbytes = omc_arena_view(&s.arena, ce->value.u.ref);
        assert(cbytes.size == bytes.size());
        assert(std::memcmp(cbytes.data, bytes.data(), bytes.size()) == 0);
        assert(ce->origin.order_in_block == e.origin.order_in_block);
        const auto wire = cpp.arena().span(e.origin.wire_type_name);
        const auto cwire = text(s, ce->origin.wire_type_name);
        assert(cwire.size() == wire.size());
        if (!wire.empty())
            assert(std::memcmp(cwire.data(), wire.data(), wire.size()) == 0);
        assert(((ce->flags & OMC_ENTRY_FLAG_DIRTY) != 0U) ==
               openmeta::any(e.flags, openmeta::EntryFlags::Dirty));
        ++ordinal;
    }
    size_t count = 0;
    for (omc_size i = 0; i < s.entry_count; ++i)
        if (s.entries[i].key.kind == OMC_KEY_IPTC_DATASET &&
            (s.entries[i].flags & OMC_ENTRY_FLAG_DELETED) == 0U)
            ++count;
    assert(count == ordinal);
}

int
main()
{
    for (omc_u32 i = 0; i < OMC_IPTC_CASE_COUNT; ++i) {
        std::printf("IPTC parity case %u\n", i);
        std::fflush(stdout);
        omc_store s;
        omc_store out;
        omc_store_init(&s);
        omc_store_init(&out);
        omc_iptc_translation_opts opts;
        const auto expected = omc_iptc_fixture(i, &s, &opts);
        auto cpp_source = copy_source(s);
        openmeta::MetadataIptcTranslationOptions cp;
        cp.title_to_iptc_object_name = (opts.mappings & (1U << 0U)) != 0U;
        cp.description_to_iptc_caption = (opts.mappings & (1U << 1U)) != 0U;
        cp.creators_to_iptc_bylines = (opts.mappings & (1U << 2U)) != 0U;
        cp.keywords_to_iptc_keywords = (opts.mappings & (1U << 3U)) != 0U;
        cp.copyright_to_iptc_copyright = (opts.mappings & (1U << 4U)) != 0U;
        cp.credit_to_iptc_credit = (opts.mappings & (1U << 5U)) != 0U;
        cp.source_to_iptc_source = (opts.mappings & (1U << 6U)) != 0U;
        cp.city_to_iptc = (opts.mappings & (1U << 7U)) != 0U;
        cp.sublocation_to_iptc = (opts.mappings & (1U << 8U)) != 0U;
        cp.state_to_iptc = (opts.mappings & (1U << 9U)) != 0U;
        cp.country_to_iptc = (opts.mappings & (1U << 10U)) != 0U;
        cp.country_code_to_iptc = (opts.mappings & (1U << 11U)) != 0U;
        cp.headline_to_iptc = (opts.mappings & (1U << 12U)) != 0U;
        cp.instructions_to_iptc = (opts.mappings & (1U << 13U)) != 0U;
        cp.transmission_reference_to_iptc = (opts.mappings & (1U << 14U)) != 0U;
        cp.authors_position_to_iptc = (opts.mappings & (1U << 15U)) != 0U;
        cp.caption_writer_to_iptc = (opts.mappings & (1U << 16U)) != 0U;
        cp.category_to_iptc = (opts.mappings & (1U << 17U)) != 0U;
        cp.supplemental_categories_to_iptc = (opts.mappings & (1U << 18U)) != 0U;
        cp.urgency_to_iptc = (opts.mappings & (1U << 19U)) != 0U;
        cp.source_mode =
            static_cast<openmeta::MetadataDescriptiveTranslationSourceMode>(
                opts.all_sources);
        cp.conflict_policy =
            static_cast<openmeta::MetadataDescriptiveTranslationConflictPolicy>(
                opts.conflict);
        cp.max_source_properties = opts.max_source_properties;
        cp.max_added_entries = opts.max_added_entries;
        cp.max_operations = opts.max_operations;
        cp.max_total_text_bytes = opts.max_total_text_bytes;
        const auto res = omc_translate_xmp_iptc(&s, &out, &opts);
        assert(res.status == expected);
        /* Unknown C mapping bits have no C++ boolean representation. */
        if (i != 113U) {
            openmeta::MetaStore cpp_out;
            const auto cr =
                openmeta::translate_xmp_iptc_metadata(cpp_source, cp, &cpp_out);
            assert(res.status == mapped_status(cr.status));
            assert(res.failed_mapping == mapped_field(cr.failed_mapping));
            assert(res.failed_source == cr.failed_source_entry);
            assert(res.groups_translated == cr.groups_translated);
            assert(res.groups_preserved == cr.groups_preserved);
            assert(res.groups_unchanged == cr.groups_unchanged);
            assert(res.entries_added == cr.entries_added);
            assert(res.entries_updated == cr.entries_updated);
            assert(res.entries_removed == cr.entries_removed);
            assert((res.utf8_charset_added != 0) == cr.utf8_charset_added);
            if (res.status == OMC_TRANSLATION_OK)
                compare_native(out, cpp_out);
        }
        omc_store_fini(&out);
        omc_store_fini(&s);
    }
    std::puts("119 paired IPTC cases passed; one additional C mask rejection passed");
}
