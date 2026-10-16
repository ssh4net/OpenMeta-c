#include "omc/omc_translation.h"

#include <openmeta/meta_edit.h>
#include <openmeta/meta_flags.h>
#include <openmeta/meta_key.h>
#include <openmeta/meta_store.h>
#include <openmeta/meta_value.h>
#include <openmeta/metadata_translation.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace {

using CppStatus = openmeta::MetadataIptcToXmpStatus;

static omc_byte_ref
c_append(omc_store &store, std::string_view text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store.arena, text.data(), text.size(), &ref) ==
           OMC_STATUS_OK);
    return ref;
}

static std::string_view
c_view(const omc_store &store, omc_byte_ref ref)
{
    const omc_const_bytes bytes = omc_arena_view(&store.arena, ref);
    return {reinterpret_cast<const char *>(bytes.data), bytes.size};
}

static std::string_view
cpp_view(const openmeta::MetaStore &store, openmeta::ByteSpan ref)
{
    const auto bytes = store.arena().span(ref);
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

static void
add_pair_bytes(omc_store &c_store, openmeta::MetaStore &cpp_store,
               omc_block_id c_block, openmeta::BlockId cpp_block,
               std::uint16_t record, std::uint16_t dataset, std::uint32_t order,
               std::string_view bytes)
{
    omc_entry entry{};
    const omc_byte_ref ref = c_append(c_store, bytes);
    omc_key_make_iptc_dataset(&entry.key, record, dataset);
    omc_val_make_bytes(&entry.value, ref);
    entry.origin.block = c_block;
    entry.origin.order_in_block = order;
    assert(omc_store_add_entry(&c_store, &entry, nullptr) == OMC_STATUS_OK);

    openmeta::Entry cpp_entry;
    cpp_entry.key = openmeta::make_iptc_dataset_key(record, dataset);
    cpp_entry.value = openmeta::make_bytes(
        cpp_store.arena(), std::span<const std::byte>(
                               reinterpret_cast<const std::byte *>(bytes.data()),
                               bytes.size()));
    cpp_entry.origin.block = cpp_block;
    cpp_entry.origin.order_in_block = order;
    assert(cpp_store.add_entry(cpp_entry) != openmeta::kInvalidEntryId);
}

static void
add_pair_xmp(omc_store &c_store, openmeta::MetaStore &cpp_store,
             omc_block_id c_block, openmeta::BlockId cpp_block,
             std::string_view schema, std::string_view path,
             std::string_view text)
{
    omc_entry entry{};
    omc_key_make_xmp_property(&entry.key, c_append(c_store, schema),
                              c_append(c_store, path));
    omc_val_make_text(&entry.value, c_append(c_store, text), OMC_TEXT_UTF8);
    entry.origin.block = c_block;
    assert(omc_store_add_entry(&c_store, &entry, nullptr) == OMC_STATUS_OK);

    openmeta::Entry cpp_entry;
    cpp_entry.key = openmeta::make_xmp_property_key(cpp_store.arena(), schema,
                                                    path);
    cpp_entry.value = openmeta::make_text(cpp_store.arena(), text,
                                          openmeta::TextEncoding::Utf8);
    cpp_entry.origin.block = cpp_block;
    assert(cpp_store.add_entry(cpp_entry) != openmeta::kInvalidEntryId);
}

static bool
find_c_xmp(const omc_store &store, std::string_view schema,
           std::string_view path, std::string_view *text)
{
    for (omc_size i = 0U; i < store.entry_count; ++i) {
        const omc_entry &entry = store.entries[i];
        if (entry.key.kind != OMC_KEY_XMP_PROPERTY ||
            (entry.flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            entry.value.kind != OMC_VAL_TEXT)
            continue;
        if (c_view(store, entry.key.u.xmp_property.schema_ns) != schema ||
            c_view(store, entry.key.u.xmp_property.property_path) != path)
            continue;
        *text = c_view(store, entry.value.u.ref);
        return true;
    }
    return false;
}

static bool
find_cpp_xmp(const openmeta::MetaStore &store, std::string_view schema,
             std::string_view path, std::string_view *text)
{
    for (const openmeta::Entry &entry : store.entries()) {
        if (entry.key.kind != openmeta::MetaKeyKind::XmpProperty ||
            openmeta::any(entry.flags, openmeta::EntryFlags::Deleted) ||
            entry.value.kind != openmeta::MetaValueKind::Text)
            continue;
        if (cpp_view(store, entry.key.data.xmp_property.schema_ns) != schema ||
            cpp_view(store, entry.key.data.xmp_property.property_path) != path)
            continue;
        *text = cpp_view(store, entry.value.data.span);
        return true;
    }
    return false;
}

static void
assert_pair_text(const omc_store &c_store,
                 const openmeta::MetaStore &cpp_store,
                 std::string_view schema, std::string_view path,
                 std::string_view expected)
{
    std::string_view c_text;
    std::string_view cpp_text;
    assert(find_c_xmp(c_store, schema, path, &c_text));
    assert(find_cpp_xmp(cpp_store, schema, path, &cpp_text));
    assert(c_text == expected);
    assert(cpp_text == expected);
    assert(c_text == cpp_text);
}

static void
run_all_mapping_parity(void)
{
    struct Source final {
        std::uint16_t dataset;
        std::string_view value;
    };
    struct Expected final {
        std::string_view schema;
        std::string_view path;
        std::string_view value;
    };
    static constexpr std::array<Source, 20U> sources = {{
        {5U, "Title"}, {120U, "Description"}, {80U, "Creator"},
        {25U, "Keyword"}, {116U, "Copyright"}, {110U, "Credit"},
        {115U, "Source"}, {90U, "City"}, {92U, "Suburb"}, {95U, "State"},
        {101U, "Canada"}, {100U, "USA"}, {105U, "Headline"},
        {40U, "Instructions"}, {103U, "Ref"}, {85U, "Editor"},
        {122U, "Writer"}, {15U, "ABC"}, {20U, "Extra"}, {10U, "5"}}};
    static constexpr std::array<Expected, 20U> expected = {{
        {"http://purl.org/dc/elements/1.1/", "title[@xml:lang=x-default]", "Title"},
        {"http://purl.org/dc/elements/1.1/", "description[@xml:lang=x-default]", "Description"},
        {"http://purl.org/dc/elements/1.1/", "creator[1]", "Creator"},
        {"http://purl.org/dc/elements/1.1/", "subject[1]", "Keyword"},
        {"http://purl.org/dc/elements/1.1/", "rights[@xml:lang=x-default]", "Copyright"},
        {"http://ns.adobe.com/photoshop/1.0/", "Credit", "Credit"},
        {"http://ns.adobe.com/photoshop/1.0/", "Source", "Source"},
        {"http://ns.adobe.com/photoshop/1.0/", "City", "City"},
        {"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "Location", "Suburb"},
        {"http://ns.adobe.com/photoshop/1.0/", "State", "State"},
        {"http://ns.adobe.com/photoshop/1.0/", "Country", "Canada"},
        {"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "CountryCode", "USA"},
        {"http://ns.adobe.com/photoshop/1.0/", "Headline", "Headline"},
        {"http://ns.adobe.com/photoshop/1.0/", "Instructions", "Instructions"},
        {"http://ns.adobe.com/photoshop/1.0/", "TransmissionReference", "Ref"},
        {"http://ns.adobe.com/photoshop/1.0/", "AuthorsPosition", "Editor"},
        {"http://ns.adobe.com/photoshop/1.0/", "CaptionWriter", "Writer"},
        {"http://ns.adobe.com/photoshop/1.0/", "Category", "ABC"},
        {"http://ns.adobe.com/photoshop/1.0/", "SupplementalCategories[1]", "Extra"},
        {"http://ns.adobe.com/photoshop/1.0/", "Urgency", "5"}}};

    omc_store c_source;
    omc_store c_output;
    omc_block_info c_info{};
    omc_block_id c_block;
    openmeta::MetaStore cpp_source;
    openmeta::MetaStore cpp_output;
    const openmeta::BlockId cpp_block = cpp_source.add_block({});

    omc_store_init(&c_source);
    omc_store_init(&c_output);
    assert(omc_store_add_block(&c_source, &c_info, &c_block) == OMC_STATUS_OK);
    assert(c_block == cpp_block);
    for (std::size_t i = 0U; i < sources.size(); ++i)
        add_pair_bytes(c_source, cpp_source, c_block, cpp_block, 2U,
                       sources[i].dataset, static_cast<std::uint32_t>(i),
                       sources[i].value);
    cpp_source.finalize();

    const omc_iptc_to_xmp_translation_res c_result
        = omc_translate_iptc_to_xmp(&c_source, &c_output, nullptr);
    const openmeta::MetadataIptcToXmpResult cpp_result
        = openmeta::translate_iptc_to_xmp(
            cpp_source, openmeta::MetadataIptcToXmpOptions {}, &cpp_output);
    assert(c_result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(cpp_result.status == CppStatus::Ok);
    assert(c_result.source_properties == cpp_result.source_properties);
    assert(c_result.groups_translated == cpp_result.groups_translated);
    assert(c_result.entries_added == cpp_result.entries_added);
    assert(c_result.entries_added == expected.size());
    for (const Expected &item : expected)
        assert_pair_text(c_output, cpp_output, item.schema, item.path,
                         item.value);

    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

static void
run_conflict_shape_budget_parity(void)
{
    static constexpr std::string_view dc
        = "http://purl.org/dc/elements/1.1/";
    omc_store c_source;
    omc_store c_output;
    omc_block_info c_info{};
    omc_block_id c_block;
    openmeta::MetaStore cpp_source;
    openmeta::MetaStore cpp_output;
    const openmeta::BlockId cpp_block = cpp_source.add_block({});

    omc_store_init(&c_source);
    omc_store_init(&c_output);
    assert(omc_store_add_block(&c_source, &c_info, &c_block) == OMC_STATUS_OK);
    add_pair_bytes(c_source, cpp_source, c_block, cpp_block, 2U, 5U, 0U,
                   "New");
    add_pair_xmp(c_source, cpp_source, c_block, cpp_block, dc,
                 "title[@xml:lang=x-default]", "Old");
    cpp_source.finalize();

    omc_iptc_to_xmp_translation_opts c_options;
    omc_iptc_to_xmp_translation_opts_init(&c_options);
    c_options.conflict = OMC_TRANSLATION_REPLACE;
    openmeta::MetadataIptcToXmpOptions cpp_options;
    cpp_options.conflict_policy
        = openmeta::MetadataIptcToXmpConflictPolicy::ReplaceExisting;
    const omc_iptc_to_xmp_translation_res c_result
        = omc_translate_iptc_to_xmp(&c_source, &c_output, &c_options);
    const openmeta::MetadataIptcToXmpResult cpp_result
        = openmeta::translate_iptc_to_xmp(cpp_source, cpp_options, &cpp_output);
    assert(c_result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(cpp_result.status == CppStatus::Ok);
    assert(c_result.entries_updated == 1U && cpp_result.entries_updated == 1U);
    assert_pair_text(c_output, cpp_output, dc,
                     "title[@xml:lang=x-default]", "New");
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);

    omc_store_init(&c_source);
    omc_store_init(&c_output);
    c_block = 0U;
    c_info = {};
    assert(omc_store_add_block(&c_source, &c_info, &c_block) == OMC_STATUS_OK);
    openmeta::MetaStore shape_source;
    openmeta::MetaStore shape_output;
    const openmeta::BlockId shape_block = shape_source.add_block({});
    add_pair_bytes(c_source, shape_source, c_block, shape_block, 2U, 25U, 0U,
                   "New");
    add_pair_xmp(c_source, shape_source, c_block, shape_block, dc,
                 "subject[1]", "One");
    add_pair_xmp(c_source, shape_source, c_block, shape_block, dc,
                 "subject[3]", "Three");
    shape_source.finalize();
    omc_iptc_to_xmp_translation_opts_init(&c_options);
    c_options.mappings = OMC_IPTC_TRANSLATE_KEYWORDS;
    c_options.conflict = OMC_TRANSLATION_REPLACE;
    cpp_options = openmeta::MetadataIptcToXmpOptions {};
    cpp_options.conflict_policy
        = openmeta::MetadataIptcToXmpConflictPolicy::ReplaceExisting;
    const omc_iptc_to_xmp_translation_res c_shape
        = omc_translate_iptc_to_xmp(&c_source, &c_output, &c_options);
    const openmeta::MetadataIptcToXmpResult cpp_shape
        = openmeta::translate_iptc_to_xmp(shape_source, cpp_options,
                                          &shape_output);
    assert(c_shape.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE);
    assert(cpp_shape.status == CppStatus::UnsupportedDestinationShape);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);

    omc_store_init(&c_source);
    omc_store_init(&c_output);
    c_info = {};
    assert(omc_store_add_block(&c_source, &c_info, &c_block) == OMC_STATUS_OK);
    openmeta::MetaStore budget_source;
    openmeta::MetaStore budget_output;
    const openmeta::BlockId budget_block = budget_source.add_block({});
    add_pair_bytes(c_source, budget_source, c_block, budget_block, 2U, 5U, 0U,
                   "abc");
    add_pair_xmp(c_source, budget_source, c_block, budget_block, dc,
                 "title[@xml:lang=x-default]", "old");
    budget_source.finalize();
    omc_iptc_to_xmp_translation_opts_init(&c_options);
    c_options.conflict = OMC_TRANSLATION_REPLACE;
    c_options.max_total_text_bytes = 5U;
    cpp_options = openmeta::MetadataIptcToXmpOptions {};
    cpp_options.conflict_policy
        = openmeta::MetadataIptcToXmpConflictPolicy::ReplaceExisting;
    cpp_options.max_total_text_bytes = 5U;
    const omc_iptc_to_xmp_translation_res c_budget
        = omc_translate_iptc_to_xmp(&c_source, &c_output, &c_options);
    const openmeta::MetadataIptcToXmpResult cpp_budget
        = openmeta::translate_iptc_to_xmp(budget_source, cpp_options,
                                          &budget_output);
    assert(c_budget.status == OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    assert(cpp_budget.status == CppStatus::SourceLimitExceeded);
    omc_store_fini(&c_output);
    omc_store_fini(&c_source);
}

} // namespace

int
main()
{
    run_all_mapping_parity();
    run_conflict_shape_budget_parity();
    return 0;
}
