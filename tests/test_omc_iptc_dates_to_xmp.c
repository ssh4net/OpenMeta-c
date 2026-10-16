#ifdef NDEBUG
#undef NDEBUG
#endif

#include "omc/omc_translation.h"

#include <assert.h>
#include <string.h>

static const char k_photoshop_ns[] = "http://ns.adobe.com/photoshop/1.0/";
static const char k_xmp_ns[] = "http://ns.adobe.com/xap/1.0/";

static omc_block_id
add_block(omc_store *store)
{
    omc_block_info info;
    omc_block_id id;
    memset(&info, 0, sizeof(info));
    assert(omc_store_add_block(store, &info, &id) == OMC_STATUS_OK);
    return id;
}

static omc_byte_ref
append_text(omc_store *store, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, text, strlen(text), &ref) ==
           OMC_STATUS_OK);
    return ref;
}

static omc_entry_id
add_iptc(omc_store *store, omc_block_id block, omc_u16 dataset,
         omc_u32 order, const char *text, omc_entry_flags flags)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, dataset);
    omc_val_make_bytes(&entry.value, append_text(store, text));
    entry.origin.block = block;
    entry.origin.order_in_block = order;
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_xmp(omc_store *store, omc_block_id block, const char *schema,
        const char *path, const char *text)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(store, schema),
                              append_text(store, path));
    omc_val_make_text(&entry.value, append_text(store, text), OMC_TEXT_UTF8);
    entry.origin.block = block;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static int
find_xmp(const omc_store *store, const char *schema, const char *path,
         omc_entry_id *out_id)
{
    omc_size i;
    omc_const_bytes actual_schema;
    omc_const_bytes actual_path;
    const omc_entry *entry;
    for (i = 0U; i < store->entry_count; ++i) {
        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        actual_schema = omc_arena_view(
            &store->arena, entry->key.u.xmp_property.schema_ns);
        actual_path = omc_arena_view(
            &store->arena, entry->key.u.xmp_property.property_path);
        if (actual_schema.size == strlen(schema) &&
            memcmp(actual_schema.data, schema, actual_schema.size) == 0 &&
            actual_path.size == strlen(path) &&
            memcmp(actual_path.data, path, actual_path.size) == 0) {
            if (out_id != NULL)
                *out_id = (omc_entry_id)i;
            return 1;
        }
    }
    return 0;
}

static void
assert_xmp_text(const omc_store *store, const char *schema,
                const char *path, const char *text)
{
    omc_entry_id id;
    omc_const_bytes value;
    assert(find_xmp(store, schema, path, &id));
    assert(store->entries[id].value.kind == OMC_VAL_TEXT);
    value = omc_arena_view(&store->arena, store->entries[id].value.u.ref);
    assert(value.size == strlen(text));
    assert(memcmp(value.data, text, value.size) == 0);
    assert(store->entries[id].value.text_encoding == OMC_TEXT_UTF8);
    assert((store->entries[id].flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
}

static void
test_both_pairs_and_date_time_formatting(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_iptc_date_to_xmp_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 4U, "20240229", 0U);
    (void)add_iptc(&source, block, 60U, 5U, "235960-0000", 0U);
    (void)add_iptc(&source, block, 62U, 6U, "20250100", 0U);

    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.failed_source == OMC_INVALID_ENTRY_ID);
    assert(result.source_properties == 3U);
    assert(result.groups_translated == 2U);
    assert(result.entries_added == 2U);
    assert_xmp_text(&output, k_photoshop_ns, "DateCreated",
                    "2024-02-29T23:59:60-00:00");
    assert_xmp_text(&output, k_xmp_ns, "CreateDate", "2025-01");
    assert(output.entries[0].origin.block == block);
    assert(output.entries[0].origin.order_in_block == 4U);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_raw_dirty_tombstone_clone_and_destination_removal(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id tombstone;
    omc_entry_id native_keep;
    omc_entry_id unselected_date;
    omc_entry_id destination;
    omc_iptc_date_to_xmp_translation_opts opts;
    omc_iptc_date_to_xmp_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    tombstone = add_iptc(&source, block, 55U, 3U, "invalid tombstone",
                         OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED |
                             OMC_ENTRY_FLAG_TRUNCATED |
                             OMC_ENTRY_FLAG_UNREADABLE);
    source.entries[tombstone].value.u.ref.offset = 0xFFFFFFFFU;
    source.entries[tombstone].value.u.ref.size = 7U;
    source.entries[tombstone].value.count = 7U;
    source.entries[tombstone].origin.block = 999U;
    source.entries[tombstone].origin.wire_type_name.offset = 0xFFFFFFFFU;
    source.entries[tombstone].origin.wire_type_name.size = 5U;
    native_keep = add_iptc(&source, block, 25U, 8U, "unrelated native", 0U);
    unselected_date = add_iptc(&source, block, 62U, 9U, "20250101", 0U);
    destination = add_xmp(&source, block, k_photoshop_ns, "DateCreated",
                          "2024-02-29");
    (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:sentinel", "Sentinel",
                  "old output");

    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.all_sources = 0;
    opts.digital_creation = 0;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 1U);
    assert(result.groups_translated == 1U);
    assert(result.entries_removed == 1U);
    assert(output.entry_count == source.entry_count);
    assert((output.entries[destination].flags & OMC_ENTRY_FLAG_DELETED) != 0U);
    assert((output.entries[destination].flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
    assert(!find_xmp(&output, k_photoshop_ns, "DateCreated", NULL));
    assert(!find_xmp(&output, "urn:sentinel", "Sentinel", NULL));
    assert(output.entries[tombstone].key.u.iptc_dataset.dataset == 55U);
    assert(output.entries[tombstone].value.u.ref.offset == 0xFFFFFFFFU);
    assert(output.entries[tombstone].value.u.ref.size == 7U);
    assert(output.entries[tombstone].value.count == 7U);
    assert(output.entries[tombstone].origin.block == 999U);
    assert(output.entries[tombstone].origin.wire_type_name.offset ==
           0xFFFFFFFFU);
    assert(output.entries[tombstone].flags == source.entries[tombstone].flags);
    assert(memcmp(&output.entries[tombstone], &source.entries[tombstone],
                  sizeof(omc_entry)) == 0);
    assert(output.entries[native_keep].key.u.iptc_dataset.dataset == 25U);
    assert(output.entries[native_keep].origin.block == block);
    assert(memcmp(&output.entries[native_keep], &source.entries[native_keep],
                  sizeof(omc_entry)) == 0);
    assert(memcmp(&output.entries[unselected_date],
                  &source.entries[unselected_date], sizeof(omc_entry)) == 0);
    assert(output.arena.size == source.arena.size);
    assert(memcmp(output.arena.data, source.arena.data,
                  source.arena.size) == 0);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_alias_and_rollback(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_iptc_date_to_xmp_translation_opts opts;
    omc_iptc_date_to_xmp_translation_res result;
    omc_size before_entries;
    omc_size before_arena;
    omc_size before_blocks;

    omc_store_init(&source);
    block = add_block(&source);
    (void)add_iptc(&source, block, 62U, 9U, "20240600", 0U);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.created = 0;
    result = omc_translate_iptc_dates_to_xmp(&source, &source, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert_xmp_text(&source, k_xmp_ns, "CreateDate", "2024-06");
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240230", 0U);
    (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:sentinel", "Sentinel",
                  "retain");
    before_entries = output.entry_count;
    before_arena = output.arena.size;
    before_blocks = output.block_count;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME);
    assert(output.entry_count == before_entries);
    assert(output.arena.size == before_arena);
    assert(output.block_count == before_blocks);
    assert_xmp_text(&output, "urn:sentinel", "Sentinel", "retain");
    {
        omc_entry entry_before;
        omc_u8 arena_before[8];
        omc_size arena_size_before;
        omc_size block_count_before;
        entry_before = source.entries[0];
        arena_size_before = source.arena.size;
        block_count_before = source.block_count;
        assert(arena_size_before <= sizeof(arena_before));
        memcpy(arena_before, source.arena.data, arena_size_before);
        result = omc_translate_iptc_dates_to_xmp(&source, &source, NULL);
        assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME);
        assert(source.entry_count == 1U);
        assert(source.arena.size == arena_size_before);
        assert(source.block_count == block_count_before);
        assert(memcmp(&source.entries[0], &entry_before,
                      sizeof(entry_before)) == 0);
        assert(memcmp(source.arena.data, arena_before,
                      arena_size_before) == 0);
    }
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_reference_failure_ordering(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id created_date;
    omc_entry_id digital_date;
    omc_entry_id destination;
    omc_iptc_date_to_xmp_translation_res result;
    omc_iptc_date_to_xmp_translation_opts opts;
    char long_text[8177];
    omc_size i;

    /* Source date parsing precedes logical validation of its target value. */
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    created_date = add_iptc(&source, block, 55U, 0U, "20240230", 0U);
    destination = add_xmp(&source, block, k_photoshop_ns, "DateCreated", "x");
    source.arena.data[source.entries[destination].value.u.ref.offset] = 0xFFU;
    (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:sentinel", "Sentinel",
                  "retain");
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
    assert(result.failed_source == created_date);
    assert(output.entry_count == 1U);
    assert_xmp_text(&output, "urn:sentinel", "Sentinel", "retain");
    omc_store_fini(&output);
    omc_store_fini(&source);

    /* Pair zero resolves its conflict before parsing pair one's bad date. */
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    created_date = add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    (void)add_iptc(&source, block, 62U, 1U, "20240230", 0U);
    (void)add_xmp(&source, block, k_photoshop_ns, "DateCreated", "2023-01-01");
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_DATE_TO_XMP_TRANSLATION_DESTINATION_CONFLICT);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
    assert(result.failed_source == created_date);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    /* Later destination-budget exhaustion precedes earlier bad destination text. */
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    digital_date = add_iptc(&source, block, 62U, 1U, "20250101", 0U);
    destination = add_xmp(&source, block, k_photoshop_ns, "DateCreated", "x");
    source.arena.data[source.entries[destination].value.u.ref.offset] = 0xFFU;
    for (i = 0U; i < sizeof(long_text) - 1U; ++i)
        long_text[i] = 'x';
    long_text[sizeof(long_text) - 1U] = '\0';
    (void)add_xmp(&source, block, k_xmp_ns, "CreateDate", long_text);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    assert(result.failed_mapping == OMC_TRANSLATE_CREATE_IPTC);
    assert(result.failed_source == digital_date);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

}

static void
test_dirty_only_and_irrelevant_byte_hints(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id date_id;
    omc_entry_id time_id;
    omc_entry_id unselected_id;
    omc_iptc_date_to_xmp_translation_opts opts;
    omc_iptc_date_to_xmp_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    date_id = add_iptc(&source, block, 55U, 1U, "20240229",
                       OMC_ENTRY_FLAG_DIRTY);
    source.entries[date_id].value.text_encoding = OMC_TEXT_UTF16LE;
    source.entries[date_id].value.byte_order = OMC_BYTE_ORDER_BIG;
    source.entries[date_id].value.elem_type = OMC_ELEM_I16;
    time_id = add_iptc(&source, block, 60U, 2U, "120000", 0U);
    unselected_id = add_iptc(&source, block, 62U, 3U, "20240230", 0U);
    source.entries[unselected_id].value.text_encoding = OMC_TEXT_UTF16BE;
    source.entries[unselected_id].value.byte_order = OMC_BYTE_ORDER_LITTLE;
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.all_sources = 0;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 2U);
    assert(result.groups_translated == 1U);
    assert_xmp_text(&output, k_photoshop_ns, "DateCreated",
                    "2024-02-29T12:00:00");
    assert(memcmp(&output.entries[date_id], &source.entries[date_id],
                  sizeof(omc_entry)) == 0);
    assert(memcmp(&output.entries[time_id], &source.entries[time_id],
                  sizeof(omc_entry)) == 0);
    assert(memcmp(&output.entries[unselected_id],
                  &source.entries[unselected_id], sizeof(omc_entry)) == 0);
    assert(!find_xmp(&output, k_xmp_ns, "CreateDate", NULL));
    omc_store_fini(&output);
    omc_store_fini(&source);

    /* Relevant Text encoding hints remain strict even when Bytes hints do not. */
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    date_id = add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    source.entries[date_id].value.kind = OMC_VAL_TEXT;
    source.entries[date_id].value.text_encoding = OMC_TEXT_UTF16LE;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    date_id = add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    omc_val_make_text(&source.entries[date_id].value,
                      source.entries[date_id].value.u.ref, OMC_TEXT_UTF8);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert_xmp_text(&output, k_photoshop_ns, "DateCreated", "2024-02-29");
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_clean_and_dirty_tombstone_selection(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id date_tombstone;
    omc_entry_id time_entry;
    omc_iptc_date_to_xmp_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    date_tombstone = add_iptc(
        &source, block, 55U, 0U, "broken",
        OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED |
            OMC_ENTRY_FLAG_TRUNCATED | OMC_ENTRY_FLAG_UNREADABLE);
    source.entries[date_tombstone].value.u.ref.offset = 0xFFFFFFFFU;
    source.entries[date_tombstone].value.count = 5U;
    time_entry = add_iptc(&source, block, 60U, 1U, "120000", 0U);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_INCOMPLETE_SOURCE);
    assert(result.failed_source == time_entry);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 3U, "20240229", 0U);
    date_tombstone = add_iptc(
        &source, block, 60U, 4U, "broken",
        OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED |
            OMC_ENTRY_FLAG_TRUNCATED | OMC_ENTRY_FLAG_UNREADABLE);
    source.entries[date_tombstone].value.u.ref.offset = 0xFFFFFFFFU;
    source.entries[date_tombstone].value.count = 5U;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 2U);
    assert_xmp_text(&output, k_photoshop_ns, "DateCreated", "2024-02-29");
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    date_tombstone = add_iptc(&source, block, 55U, 0U, "ignored",
                              OMC_ENTRY_FLAG_DELETED);
    source.entries[date_tombstone].value.u.ref.offset = 0xFFFFFFFFU;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 0U);
    assert(result.groups_translated == 0U);
    assert(output.entry_count == source.entry_count);
    assert(output.entries[date_tombstone].value.u.ref.offset == 0xFFFFFFFFU);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_calendar_and_incomplete_sources(void)
{
    struct date_case {
        const char *input;
        const char *output;
        omc_iptc_date_to_xmp_translation_status status;
    };
    static const struct date_case cases[] = {
        {"20240000", "2024", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK},
        {"20240100", "2024-01", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK},
        {"20000229", "2000-02-29", OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK},
        {"19000229", NULL,
         OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME},
        {"20240230", NULL,
         OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME},
        {"20241301", NULL,
         OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME},
        {"00000000", NULL,
         OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION},
        {"20240015", NULL,
         OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION}
    };
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_block_id other_block;
    omc_entry_id date_id;
    omc_entry_id time_id;
    omc_iptc_date_to_xmp_translation_res result;
    omc_size i;

    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        omc_store_init(&source);
        omc_store_init(&output);
        block = add_block(&source);
        (void)add_iptc(&source, block, 55U, 0U, cases[i].input, 0U);
        result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
        assert(result.status == cases[i].status);
        if (cases[i].output != NULL)
            assert_xmp_text(&output, k_photoshop_ns, "DateCreated",
                            cases[i].output);
        else
            assert(output.entry_count == 0U);
        omc_store_fini(&output);
        omc_store_fini(&source);
    }

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240100", 0U);
    (void)add_iptc(&source, block, 60U, 1U, "120000", 0U);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION);
    assert(result.failed_source == 1U);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 60U, 0U, "120000", 0U);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_INCOMPLETE_SOURCE);
    assert(result.failed_source == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    other_block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    time_id = add_iptc(&source, other_block, 60U, 1U, "120000", 0U);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_AMBIGUOUS_SOURCE);
    assert(result.failed_source == time_id);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    date_id = add_iptc(&source, block, 55U, 1U, "20240301", 0U);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_AMBIGUOUS_SOURCE);
    assert(result.failed_source == date_id);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    date_id = add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    time_id = add_iptc(&source, block, 60U, 1U, "240000", 0U);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME);
    assert(result.failed_source == time_id);
    assert(result.failed_source != date_id);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_conflicts_and_destination_shapes(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id destination;
    omc_iptc_date_to_xmp_translation_opts opts;
    omc_iptc_date_to_xmp_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 7U, "20240229", 0U);
    destination = add_xmp(&source, block, k_photoshop_ns, "DateCreated",
                          "old value");
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_DATE_TO_XMP_TRANSLATION_DESTINATION_CONFLICT);
    assert(output.entry_count == 0U);

    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.conflict = OMC_TRANSLATION_PRESERVE;
    source.arena.data[source.entries[destination].value.u.ref.offset] = 0xFFU;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.groups_preserved == 1U);
    assert(result.entries_updated == 0U);
    assert(output.arena.data[
               output.entries[destination].value.u.ref.offset] == 0xFFU);
    omc_store_fini(&output);

    omc_store_init(&output);
    source.arena.data[source.entries[destination].value.u.ref.offset] = 'o';
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.entries_updated == 1U);
    assert_xmp_text(&output, k_photoshop_ns, "DateCreated", "2024-02-29");
    assert(output.entries[destination].origin.block == block);
    assert(output.entries[destination].origin.order_in_block == 0U);
    omc_store_fini(&output);
    omc_store_init(&output);
    omc_val_make_text(&source.entries[destination].value,
                      append_text(&source, "2024-02-29"), OMC_TEXT_UTF8);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    assert(result.groups_unchanged == 1U);
    assert(result.entries_updated == 0U);
    assert_xmp_text(&output, k_photoshop_ns, "DateCreated", "2024-02-29");
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    (void)add_xmp(&source, block, k_photoshop_ns, "DateCreated[1]",
                  "2024-02-29");
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    destination = add_iptc(&source, block, 55U, 0U, "20240229",
                           OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED |
                               (omc_entry_flags)0x40U);
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
    assert(result.failed_source == destination);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    (void)add_xmp(&source, block, k_photoshop_ns, "DateCreated", "2024-02-29");
    (void)add_xmp(&source, block, k_photoshop_ns, "DateCreated", "2024-02-29");
    result = omc_translate_iptc_dates_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_source_entry_operation_and_combined_text_limits(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id digital_date;
    omc_iptc_date_to_xmp_translation_opts opts;
    omc_iptc_date_to_xmp_translation_res result;
    char long_text[8186];
    omc_size target_size;
    omc_size i;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    digital_date = add_iptc(&source, block, 62U, 1U, "20240301", 0U);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.max_source_properties = 1U;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    assert(result.failed_source == digital_date);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.max_added_entries = 1U;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_ENTRY_LIMIT);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.max_operations = 1U;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OPERATION_LIMIT);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.max_operations =
        OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_OPERATIONS + 1U;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_OPTIONS);
    omc_store_fini(&output);
    omc_store_fini(&source);

    for (target_size = 8184U; target_size <= 8185U; ++target_size) {
        omc_store_init(&source);
        omc_store_init(&output);
        block = add_block(&source);
        (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
        for (i = 0U; i < target_size; ++i)
            long_text[i] = 'x';
        long_text[target_size] = '\0';
        (void)add_xmp(&source, block, k_photoshop_ns, "DateCreated",
                      long_text);
        (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:sentinel", "Sentinel",
                      "old output");
        omc_iptc_date_to_xmp_translation_opts_init(&opts);
        opts.conflict = OMC_TRANSLATION_REPLACE;
        result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
        if (target_size == 8184U) {
            assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
            assert(result.entries_updated == 1U);
            assert(output.entry_count == 2U);
            assert_xmp_text(&output, k_photoshop_ns, "DateCreated",
                            "2024-02-29");
        } else {
            assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT);
            assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
            assert(output.entry_count == 1U);
            assert_xmp_text(&output, "urn:sentinel", "Sentinel", "old output");
        }
        omc_store_fini(&output);
        omc_store_fini(&source);
    }

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 55U, 0U, "20240229", 0U);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    opts.max_total_text_bytes = 8U;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK);
    omc_store_fini(&output);
    omc_store_init(&output);
    opts.max_total_text_bytes = 7U;
    result = omc_translate_iptc_dates_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    assert(output.entry_count == 0U);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

int
main(void)
{
    test_both_pairs_and_date_time_formatting();
    test_raw_dirty_tombstone_clone_and_destination_removal();
    test_alias_and_rollback();
    test_reference_failure_ordering();
    test_dirty_only_and_irrelevant_byte_hints();
    test_clean_and_dirty_tombstone_selection();
    test_calendar_and_incomplete_sources();
    test_conflicts_and_destination_shapes();
    test_source_entry_operation_and_combined_text_limits();
    return 0;
}
