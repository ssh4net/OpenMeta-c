#ifdef NDEBUG
#undef NDEBUG
#endif

#include "omc/omc_translation.h"

#include <assert.h>
#include <string.h>

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
add_iptc(omc_store *store, omc_block_id block, omc_u16 record,
         omc_u16 dataset, omc_u32 order, const char *text,
         omc_val_kind kind, omc_text_encoding encoding, omc_entry_flags flags)
{
    omc_entry entry;
    omc_byte_ref ref;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    ref = append_text(store, text);
    omc_key_make_iptc_dataset(&entry.key, record, dataset);
    if (kind == OMC_VAL_TEXT)
        omc_val_make_text(&entry.value, ref, encoding);
    else
        omc_val_make_bytes(&entry.value, ref);
    entry.origin.block = block;
    entry.origin.order_in_block = order;
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_xmp(omc_store *store, omc_block_id block, const char *schema,
        const char *path, const char *text, omc_text_encoding encoding,
        omc_entry_flags flags, omc_u32 order)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(store, schema),
                              append_text(store, path));
    omc_val_make_text(&entry.value, append_text(store, text), encoding);
    entry.origin.block = block;
    entry.origin.order_in_block = order;
    entry.flags = flags;
    assert(omc_arena_append(&store->arena, "xmp-origin", 10U,
                            &entry.origin.wire_type_name) == OMC_STATUS_OK);
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 91U;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static int
find_xmp(const omc_store *store, const char *schema, const char *path,
         omc_entry_id *found)
{
    omc_size i;
    omc_const_bytes ns;
    omc_const_bytes property;
    const omc_entry *entry;
    for (i = 0U; i < store->entry_count; ++i) {
        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        ns = omc_arena_view(&store->arena,
                            entry->key.u.xmp_property.schema_ns);
        property = omc_arena_view(&store->arena,
                                  entry->key.u.xmp_property.property_path);
        if (ns.size == strlen(schema) &&
            memcmp(ns.data, schema, ns.size) == 0 &&
            property.size == strlen(path) &&
            memcmp(property.data, path, property.size) == 0) {
            if (found != NULL)
                *found = (omc_entry_id)i;
            return 1;
        }
    }
    return 0;
}

static void
assert_xmp_text(const omc_store *store, const char *schema, const char *path,
                const char *text)
{
    omc_entry_id id;
    const omc_entry *entry;
    omc_const_bytes value;
    assert(find_xmp(store, schema, path, &id));
    entry = &store->entries[id];
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(entry->value.text_encoding == OMC_TEXT_UTF8);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(value.size == strlen(text));
    assert(memcmp(value.data, text, value.size) == 0);
    assert((entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
}

static void
assert_new_xmp_origin(const omc_store *store, const char *schema,
                      const char *path, omc_block_id block, omc_u32 order)
{
    omc_entry_id id;
    const omc_entry *entry;
    assert(find_xmp(store, schema, path, &id));
    entry = &store->entries[id];
    assert(entry->origin.block == block);
    assert(entry->origin.order_in_block == order);
    assert(entry->origin.wire_type.family == OMC_WIRE_NONE);
    assert(entry->origin.wire_type.code == 0U);
    assert(entry->origin.wire_type_name.size == 0U);
}

static void
assert_store_unchanged(const omc_store *store, omc_size entries,
                       omc_size arena_bytes, omc_size blocks)
{
    assert(store->entry_count == entries);
    assert(store->arena.size == arena_bytes);
    assert(store->block_count == blocks);
}

static void
test_all_twenty_mappings(void)
{
    static const struct source_case {
        omc_u16 dataset;
        const char *text;
    } sources[20] = {
        {5U, "Title"},       {120U, "Description"}, {80U, "Creator"},
        {25U, "Keyword"},    {116U, "Copyright"},  {110U, "Credit"},
        {115U, "Source"},    {90U, "City"},        {92U, "Suburb"},
        {95U, "State"},      {101U, "Canada"},      {100U, "USA"},
        {105U, "Headline"},  {40U, "Instructions"}, {103U, "Ref"},
        {85U, "Editor"},     {122U, "Writer"},      {15U, "ABC"},
        {20U, "Extra"},      {10U, "5"}};
    static const struct expected_case {
        const char *schema;
        const char *path;
        const char *text;
    } expected[20] = {
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
        {"http://ns.adobe.com/photoshop/1.0/", "Urgency", "5"}};
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_iptc_to_xmp_translation_res result;
    omc_size i;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    for (i = 0U; i < 20U; ++i)
        (void)add_iptc(&source, block, 2U, sources[i].dataset, (omc_u32)i,
                       sources[i].text, OMC_VAL_BYTES, OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 20U);
    assert(result.groups_translated == 20U);
    assert(result.entries_added == 20U);
    for (i = 0U; i < 20U; ++i) {
        assert_xmp_text(&output, expected[i].schema, expected[i].path,
                        expected[i].text);
        assert_new_xmp_origin(&output, expected[i].schema, expected[i].path,
                             block, (omc_u32)i);
    }
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_unrelated_paths_and_language_siblings(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    static const char ps[] = "http://ns.adobe.com/photoshop/1.0/";
    static const char *const unrelated[] = {
        "subjective", "subjec", "subject-extra", "titleExtra"};
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;
    omc_size i;
    char long_path[1024];
    omc_entry_id id;
    const omc_entry *entry;
    omc_const_bytes value;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 25U, 0U, "Keyword", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 2U, 5U, 1U, "New title", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    for (i = 0U; i < 4U; ++i)
        (void)add_xmp(&source, block, dc, unrelated[i], "unrelated",
                      OMC_TEXT_UTF8, 0U, 0U);
    strcpy(long_path, "subject");
    memset(long_path + strlen(long_path), 'x', sizeof(long_path) - 8U);
    long_path[sizeof(long_path) - 1U] = '\0';
    (void)add_xmp(&source, block, dc, long_path, "long unrelated",
                  OMC_TEXT_UTF8, 0U, 0U);

    /* These paths share an exact mapped root and a suffix boundary. */
    (void)add_xmp(&source, block, dc, "subject/child", "nested",
                  OMC_TEXT_UTF8, 0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.mappings = OMC_IPTC_TRANSLATE_KEYWORDS | OMC_IPTC_TRANSLATE_TITLE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 25U, 0U, "Keyword", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 2U, 5U, 1U, "New title", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    for (i = 0U; i < 4U; ++i)
        (void)add_xmp(&source, block, dc, unrelated[i], "unrelated",
                      OMC_TEXT_UTF8, 0U, 0U);
    strcpy(long_path, "subject");
    memset(long_path + strlen(long_path), 'x', sizeof(long_path) - 8U);
    long_path[sizeof(long_path) - 1U] = '\0';
    (void)add_xmp(&source, block, dc, long_path, "long unrelated",
                  OMC_TEXT_UTF8, 0U, 0U);
    (void)add_xmp(&source, block, dc, "title[@xml:lang=fr-FR]", "Bonjour",
                  OMC_TEXT_UTF8, 0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.mappings = OMC_IPTC_TRANSLATE_KEYWORDS | OMC_IPTC_TRANSLATE_TITLE;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.entries_added == 2U);
    assert(find_xmp(&output, dc, "subjective", NULL));
    assert(find_xmp(&output, dc, "subjec", NULL));
    assert(find_xmp(&output, dc, "subject-extra", NULL));
    assert(find_xmp(&output, dc, "titleExtra", NULL));
    assert(find_xmp(&output, dc, long_path, NULL));
    assert(find_xmp(&output, dc, "title[@xml:lang=fr-FR]", &id));
    entry = &output.entries[id];
    value = omc_arena_view(&output.arena, entry->value.u.ref);
    assert(value.size == 7U && memcmp(value.data, "Bonjour", 7U) == 0);
    assert(find_xmp(&output, dc, "title[@xml:lang=x-default]", NULL));
    assert(!find_xmp(&output, ps, "subject", NULL));
    omc_store_fini(&output);
    omc_store_fini(&source);

    /* A slash descendant of a scalar root is also an unsupported shape. */
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "New title", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_xmp(&source, block, dc, "title/child", "nested",
                  OMC_TEXT_UTF8, 0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.mappings = OMC_IPTC_TRANSLATE_TITLE;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "New title", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_xmp(&source, block, dc, "title/child", "nested",
                  OMC_TEXT_UTF8, 0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.mappings = OMC_IPTC_TRANSLATE_TITLE;
    opts.conflict = OMC_TRANSLATION_PRESERVE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.groups_preserved == 1U);
    assert(!find_xmp(&output, dc, "title[@xml:lang=x-default]", NULL));
    assert(find_xmp(&output, dc, "title/child", NULL));
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_raw_bytes_ignore_text_hints(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    static const char esc_g[] = "\033%G";
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id marker;
    omc_entry_id title;
    omc_entry_id source_text;
    omc_iptc_to_xmp_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    marker = add_iptc(&source, block, 1U, 90U, 0U, esc_g, OMC_VAL_BYTES,
                      OMC_TEXT_UNKNOWN, 0U);
    title = add_iptc(&source, block, 2U, 5U, 1U, "\303\251", OMC_VAL_BYTES,
                     OMC_TEXT_UNKNOWN, 0U);
    source_text = add_iptc(&source, block, 2U, 115U, 2U, "Text title",
                          OMC_VAL_TEXT, OMC_TEXT_UTF8, 0U);
    source.entries[marker].value.elem_type = OMC_ELEM_SRATIONAL;
    source.entries[marker].value.text_encoding = OMC_TEXT_UTF16BE;
    source.entries[marker].value.byte_order = OMC_BYTE_ORDER_BIG;
    source.entries[title].value.elem_type = OMC_ELEM_I32;
    source.entries[title].value.text_encoding = OMC_TEXT_UTF16LE;
    source.entries[title].value.byte_order = OMC_BYTE_ORDER_BIG;
    source.entries[source_text].value.elem_type = OMC_ELEM_SRATIONAL;
    source.entries[source_text].value.byte_order = OMC_BYTE_ORDER_BIG;
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert_xmp_text(&output, dc, "title[@xml:lang=x-default]", "\303\251");
    assert_xmp_text(&output, "http://ns.adobe.com/photoshop/1.0/", "Source",
                    "Text title");
    assert(output.entries[marker].value.elem_type == OMC_ELEM_SRATIONAL);
    assert(output.entries[marker].value.text_encoding == OMC_TEXT_UTF16BE);
    assert(output.entries[marker].value.byte_order == OMC_BYTE_ORDER_BIG);
    assert(output.entries[title].value.elem_type == OMC_ELEM_I32);
    assert(output.entries[title].value.text_encoding == OMC_TEXT_UTF16LE);
    assert(output.entries[title].value.byte_order == OMC_BYTE_ORDER_BIG);
    assert(output.entries[source_text].value.elem_type == OMC_ELEM_SRATIONAL);
    assert(output.entries[source_text].value.byte_order == OMC_BYTE_ORDER_BIG);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_out_of_enum_hints_fail_transactionally(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id title;
    omc_entry_id destination;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;
    omc_size source_entries;
    omc_size source_arena;
    omc_size source_blocks;
    omc_size output_entries;
    omc_size output_arena;
    omc_size output_blocks;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    title = add_iptc(&source, block, 2U, 5U, 0U, "New", OMC_VAL_BYTES,
                     OMC_TEXT_UNKNOWN, 0U);
    source.entries[title].value.elem_type = (omc_elem_type)99;
    (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:test", "Sentinel",
                  "keep", OMC_TEXT_UTF8, 0U, 0U);
    source_entries = source.entry_count;
    source_arena = source.arena.size;
    source_blocks = source.block_count;
    output_entries = output.entry_count;
    output_arena = output.arena.size;
    output_blocks = output.block_count;

    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL);
    assert_store_unchanged(&source, source_entries, source_arena,
                           source_blocks);
    assert_store_unchanged(&output, output_entries, output_arena,
                           output_blocks);
    assert(source.entries[title].value.elem_type == (omc_elem_type)99);
    assert(find_xmp(&output, "urn:test", "Sentinel", NULL));

    omc_store_fini(&source);
    omc_store_fini(&output);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "New", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    destination = add_xmp(&source, block, dc,
                          "title[@xml:lang=x-default]", "Old",
                          OMC_TEXT_UTF8, 0U, 0U);
    source.entries[destination].value.byte_order = (omc_byte_order)99;
    (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:test", "Sentinel",
                  "keep", OMC_TEXT_UTF8, 0U, 0U);
    output_entries = output.entry_count;
    output_arena = output.arena.size;
    output_blocks = output.block_count;
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.conflict = OMC_TRANSLATION_REPLACE;

    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL);
    assert_store_unchanged(&output, output_entries, output_arena,
                           output_blocks);
    assert(find_xmp(&output, "urn:test", "Sentinel", NULL));
    assert(source.entries[destination].value.byte_order ==
           (omc_byte_order)99);
    omc_store_fini(&source);
    omc_store_fini(&output);
}

static void
test_source_charset_and_field_validation(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    static const char *const invalid_statuses[] = {"BAD", "\033%F"};
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id marker_id;
    omc_entry_id title_id;
    omc_iptc_to_xmp_translation_res result;
    char too_long[66];

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "Title", OMC_VAL_TEXT,
                   OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
    omc_store_fini(&source);
    omc_store_fini(&output);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 1U, 90U, 0U, "\033%G", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 2U, 5U, 1U, "\303\251", OMC_VAL_TEXT,
                   OMC_TEXT_ASCII, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_ENCODING_CONFLICT);
    omc_store_fini(&source);
    omc_store_fini(&output);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "\303\251", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_ENCODING_CONFLICT);
    omc_store_fini(&source);
    omc_store_fini(&output);

    /* Invalid charset records and duplicate active markers reject the group. */
    {
        omc_size i;
        for (i = 0U; i < 2U; ++i) {
            omc_store_init(&source);
            omc_store_init(&output);
            block = add_block(&source);
            (void)add_iptc(&source, block, 1U, 90U, 0U,
                           invalid_statuses[i], OMC_VAL_BYTES,
                           OMC_TEXT_UNKNOWN, 0U);
            (void)add_iptc(&source, block, 2U, 5U, 1U, "Title",
                           OMC_VAL_BYTES, OMC_TEXT_UNKNOWN, 0U);
            result = omc_translate_iptc_to_xmp(&source, &output, NULL);
            assert(result.status ==
                   OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_ENCODING_CONFLICT);
            omc_store_fini(&source);
            omc_store_fini(&output);
        }
    }
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 1U, 90U, 0U, "\033%G", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 1U, 90U, 1U, "\033%G", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 2U, 5U, 2U, "Title", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_ENCODING_CONFLICT);
    omc_store_fini(&source);
    omc_store_fini(&output);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 15U, 0U, "A1", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
    omc_store_fini(&source);
    omc_store_fini(&output);

    memset(too_long, 'T', sizeof(too_long) - 1U);
    too_long[sizeof(too_long) - 1U] = '\0';
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, too_long, OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_VALUE_TOO_LONG);
    omc_store_fini(&source);
    omc_store_fini(&output);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    title_id = add_iptc(&source, block, 2U, 5U, 0U, "\303\251",
                        OMC_VAL_TEXT, OMC_TEXT_UTF8, 0U);
    marker_id = add_iptc(&source, block, 1U, 90U, 1U, "\033%G",
                          OMC_VAL_TEXT, OMC_TEXT_UTF8, 0U);
    source.entries[title_id].value.elem_type = OMC_ELEM_SRATIONAL;
    source.entries[title_id].value.byte_order = OMC_BYTE_ORDER_BIG;
    source.entries[marker_id].value.elem_type = OMC_ELEM_SRATIONAL;
    source.entries[marker_id].value.byte_order = OMC_BYTE_ORDER_BIG;
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert_xmp_text(&output, dc, "title[@xml:lang=x-default]", "\303\251");
    omc_store_fini(&source);
    omc_store_fini(&output);
}

static void
test_preserve_tombstone_and_transaction_failures(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id destination_id;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;
    omc_size before_entries;
    omc_size before_arena;
    omc_size before_blocks;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "New", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    destination_id = add_xmp(&source, block, dc, "title[@xml:lang=fr-FR]",
                             "Bonjour", OMC_TEXT_UTF8, 0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.conflict = OMC_TRANSLATION_PRESERVE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.groups_preserved == 1U);
    assert(result.entries_added == 0U);
    assert((output.entries[destination_id].flags & OMC_ENTRY_FLAG_DELETED) ==
           0U);
    assert(!find_xmp(&output, dc, "title[@xml:lang=x-default]", NULL));
    assert(find_xmp(&output, dc, "title[@xml:lang=fr-FR]", NULL));
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "old", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, OMC_ENTRY_FLAG_DELETED);
    destination_id = add_xmp(&source, block, dc,
                             "title[@xml:lang=x-default]", "retain",
                             OMC_TEXT_UTF8, 0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.all_sources = 0;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 0U);
    assert(result.entries_removed == 0U);
    assert((output.entries[destination_id].flags & OMC_ENTRY_FLAG_DELETED) ==
           0U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 1U, 90U, 0U, "\033%G", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN,
                   OMC_ENTRY_FLAG_DELETED | OMC_ENTRY_FLAG_DIRTY);
    (void)add_iptc(&source, block, 2U, 5U, 1U, "\303\251", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.all_sources = 0;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 0U);
    assert(!find_xmp(&output, dc, "title[@xml:lang=x-default]", NULL));
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:test", "Sentinel",
                  "keep", OMC_TEXT_UTF8, 0U, 0U);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "\377", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    before_entries = output.entry_count;
    before_arena = output.arena.size;
    before_blocks = output.block_count;
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
    assert_store_unchanged(&output, before_entries, before_arena, before_blocks);
    assert(find_xmp(&output, "urn:test", "Sentinel", NULL));
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "\377", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    before_entries = source.entry_count;
    before_arena = source.arena.size;
    before_blocks = source.block_count;
    result = omc_translate_iptc_to_xmp(&source, &source, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
    assert_store_unchanged(&source, before_entries, before_arena, before_blocks);
    omc_store_fini(&source);
}

static void
test_source_and_operation_limits(void)
{
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "Title", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 2U, 90U, 1U, "City", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.max_source_properties = 1U;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.max_added_entries = 1U;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_ENTRY_LIMIT);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.max_operations = 1U;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OPERATION_LIMIT);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "One", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 2U, 5U, 1U, "Two", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_AMBIGUOUS_SOURCE);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, (omc_block_id)99U, 2U, 5U, 0U, "Title",
                   OMC_VAL_BYTES, OMC_TEXT_UNKNOWN, 0U);
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_malformed_source_and_destination_rollback(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_entry_id source_id;
    omc_entry_id destination_id;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;
    omc_size i;
    omc_size before_entries;
    omc_size before_arena;
    omc_size before_blocks;
    static const int source_damage[6] = {0, 1, 2, 3, 4, 5};
    static const int destination_damage[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    for (i = 0U; i < 6U; ++i) {
        omc_store_init(&source);
        omc_store_init(&output);
        block = add_block(&source);
        source_id = add_iptc(&source, block, 2U, 5U, 0U, "Title",
                             source_damage[i] == 5 ? OMC_VAL_TEXT
                                                   : OMC_VAL_BYTES,
                             OMC_TEXT_UNKNOWN,
                             0U);
        switch (source_damage[i]) {
        case 0: source.entries[source_id].value.kind = OMC_VAL_SCALAR; break;
        case 1: ++source.entries[source_id].value.count; break;
        case 2:
            source.entries[source_id].value.u.ref.offset =
                (omc_u32)0xFFFFFFFFU;
            break;
        case 3: source.entries[source_id].flags = OMC_ENTRY_FLAG_TRUNCATED; break;
        case 4: source.entries[source_id].flags = (omc_entry_flags)0x80U; break;
        case 5: break;
        }
        result = omc_translate_iptc_to_xmp(&source, &output, NULL);
        assert(result.status ==
               OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE);
        omc_store_fini(&output);
        omc_store_fini(&source);
    }

    for (i = 0U; i < 8U; ++i) {
        omc_store_init(&source);
        omc_store_init(&output);
        block = add_block(&source);
        (void)add_iptc(&source, block, 2U, 5U, 0U, "Title", OMC_VAL_BYTES,
                       OMC_TEXT_UNKNOWN, 0U);
        destination_id = add_xmp(&source, block, dc,
                                 "title[@xml:lang=x-default]", "Old",
                                 OMC_TEXT_UTF8, 0U, 0U);
        switch (destination_damage[i]) {
        case 0: source.entries[destination_id].value.kind = OMC_VAL_BYTES; break;
        case 1: ++source.entries[destination_id].value.count; break;
        case 2:
            source.entries[destination_id].value.u.ref.offset =
                (omc_u32)0xFFFFFFFFU;
            break;
        case 3:
            source.entries[destination_id].flags = OMC_ENTRY_FLAG_TRUNCATED;
            break;
        case 4:
            source.entries[destination_id].flags = (omc_entry_flags)0x80U;
            break;
        case 5:
            source.entries[destination_id].value.text_encoding =
                OMC_TEXT_UNKNOWN;
            break;
        case 6:
            source.entries[destination_id].value.text_encoding = OMC_TEXT_ASCII;
            source.entries[destination_id].value.elem_type = OMC_ELEM_U8;
            source.entries[destination_id].value.byte_order =
                OMC_BYTE_ORDER_NATIVE;
            source.entries[destination_id].value.u.ref =
                append_text(&source, "\377");
            source.entries[destination_id].value.count = 1U;
            break;
        case 7:
            source.entries[destination_id].value.kind = OMC_VAL_BYTES;
            break;
        }
        (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:test", "Sentinel",
                      "keep", OMC_TEXT_UTF8, 0U, 0U);
        before_entries = output.entry_count;
        before_arena = output.arena.size;
        before_blocks = output.block_count;
        omc_iptc_to_xmp_translation_opts_init(&opts);
        opts.mappings = OMC_IPTC_TRANSLATE_TITLE;
        opts.conflict = OMC_TRANSLATION_REPLACE;
        result = omc_translate_iptc_to_xmp(&source, &output, &opts);
        assert(result.status ==
               OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE);
        assert_store_unchanged(&output, before_entries, before_arena,
                               before_blocks);
        assert(find_xmp(&output, "urn:test", "Sentinel", NULL));
        omc_store_fini(&output);
        omc_store_fini(&source);
    }

    /* Aggregate budget checks precede UTF-8 scans of owned source and dest. */
    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "\377\377", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_xmp(&output, OMC_INVALID_BLOCK_ID, "urn:test", "Sentinel",
                  "keep", OMC_TEXT_UTF8, 0U, 0U);
    before_entries = output.entry_count;
    before_arena = output.arena.size;
    before_blocks = output.block_count;
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.max_total_text_bytes = 1U;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    assert_store_unchanged(&output, before_entries, before_arena, before_blocks);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "abc", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    destination_id = add_xmp(&source, block, dc,
                             "title[@xml:lang=x-default]", "old",
                             OMC_TEXT_UTF8, 0U, 0U);
    source.entries[destination_id].value.u.ref = append_text(&source, "\377\377");
    source.entries[destination_id].value.count = 2U;
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    opts.max_total_text_bytes = 4U;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_repeated_order_duplicates_and_alias(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    omc_store source;
    omc_block_id block0;
    omc_block_id block1;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;
    omc_size original_entries;

    omc_store_init(&source);
    block0 = add_block(&source);
    block1 = add_block(&source);
    (void)add_iptc(&source, block1, 2U, 25U, 5U, "repeat", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block0, 2U, 25U, 10U, "repeat", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block0, 2U, 25U, 1U, "first", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    original_entries = source.entry_count;

    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.mappings = OMC_IPTC_TRANSLATE_KEYWORDS;
    result = omc_translate_iptc_to_xmp(&source, &source, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 3U);
    assert(result.groups_translated == 1U);
    assert(result.entries_added == 3U);
    assert(source.entry_count == original_entries + 3U);
    assert_xmp_text(&source, dc, "subject[1]", "first");
    assert_xmp_text(&source, dc, "subject[2]", "repeat");
    assert_xmp_text(&source, dc, "subject[3]", "repeat");
    assert_new_xmp_origin(&source, dc, "subject[1]", block0, 1U);
    assert_new_xmp_origin(&source, dc, "subject[2]", block0, 10U);
    assert_new_xmp_origin(&source, dc, "subject[3]", block1, 5U);
    omc_store_fini(&source);
}

static void
test_conflict_provenance_dirty_and_tombstones(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_block_id clean_block;
    omc_entry_id destination_id;
    omc_entry_id found_id;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;
    const omc_entry *entry;
    omc_const_bytes value;
    omc_size old_count;
    omc_size old_arena;
    omc_size old_blocks;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 3U, "New", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    destination_id = add_xmp(&source, block, dc,
                             "title[@xml:lang=x-default]", "Old",
                             OMC_TEXT_UTF8, 0U, 91U);
    source.entries[destination_id].value.elem_type = OMC_ELEM_SRATIONAL;
    source.entries[destination_id].value.byte_order = OMC_BYTE_ORDER_BIG;
    old_count = output.entry_count;
    old_arena = output.arena.size;
    old_blocks = output.block_count;
    result = omc_translate_iptc_to_xmp(&source, &output, NULL);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_DESTINATION_CONFLICT);
    assert_store_unchanged(&output, old_count, old_arena, old_blocks);

    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.entries_updated == 1U);
    assert(find_xmp(&output, dc, "title[@xml:lang=x-default]", &found_id));
    assert(found_id == destination_id);
    entry = &output.entries[found_id];
    assert(entry->origin.block == block);
    assert(entry->origin.order_in_block == 91U);
    assert(entry->origin.wire_type.family == OMC_WIRE_OTHER);
    assert(entry->origin.wire_type.code == 91U);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.byte_order == OMC_BYTE_ORDER_NATIVE);
    value = omc_arena_view(&output.arena, entry->origin.wire_type_name);
    assert(value.size == 10U && memcmp(value.data, "xmp-origin", 10U) == 0);
    assert_xmp_text(&output, dc, "title[@xml:lang=x-default]", "New");
    assert(source.entries[destination_id].value.elem_type == OMC_ELEM_SRATIONAL);
    assert(source.entries[destination_id].value.byte_order ==
           OMC_BYTE_ORDER_BIG);

    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    clean_block = add_block(&source);
    (void)add_iptc(&source, block, 1U, 90U, 0U, "\033%G", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, OMC_ENTRY_FLAG_DIRTY);
    (void)add_iptc(&source, block, 2U, 5U, 1U, "Title", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, block, 2U, 90U, 2U, "Osaka", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_iptc(&source, clean_block, 2U, 105U, 3U, "Headline", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.all_sources = 0;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.source_properties == 2U);
    assert(result.entries_added == 2U);
    assert(find_xmp(&output, dc, "title[@xml:lang=x-default]", NULL));
    assert(find_xmp(&output, "http://ns.adobe.com/photoshop/1.0/", "City",
                    NULL));
    assert(!find_xmp(&output, "http://ns.adobe.com/photoshop/1.0/",
                     "Headline", NULL));
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "old", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN,
                   OMC_ENTRY_FLAG_DELETED | OMC_ENTRY_FLAG_DIRTY);
    destination_id = add_xmp(&source, block, dc,
                             "title[@xml:lang=x-default]", "old",
                             OMC_TEXT_UTF8, 0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.all_sources = 0;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_DESTINATION_CONFLICT);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.entries_removed == 1U);
    assert((output.entries[destination_id].flags & OMC_ENTRY_FLAG_DELETED) !=
           0U);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_limits_shape_and_budget_rollback(void)
{
    static const char dc[] = "http://purl.org/dc/elements/1.1/";
    omc_store source;
    omc_store output;
    omc_block_id block;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_translation_res result;
    omc_size before_entries;
    omc_size before_arena;
    omc_size before_blocks;

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 5U, 0U, "abc", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_xmp(&source, block, dc, "title[@xml:lang=x-default]", "old",
                  OMC_TEXT_UTF8, 0U, 0U);
    before_entries = output.entry_count;
    before_arena = output.arena.size;
    before_blocks = output.block_count;
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    opts.max_total_text_bytes = 5U;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT);
    assert_store_unchanged(&output, before_entries, before_arena, before_blocks);

    opts.max_total_text_bytes = 8U;
    opts.max_source_properties = 1U;
    opts.max_added_entries = 1U;
    opts.max_operations = 1U;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status == OMC_IPTC_TO_XMP_TRANSLATION_OK);
    assert(result.entries_updated == 1U);
    omc_store_fini(&output);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&output);
    block = add_block(&source);
    (void)add_iptc(&source, block, 2U, 25U, 0U, "new", OMC_VAL_BYTES,
                   OMC_TEXT_UNKNOWN, 0U);
    (void)add_xmp(&source, block, dc, "subject[1]", "old", OMC_TEXT_UTF8,
                  0U, 0U);
    (void)add_xmp(&source, block, dc, "subject[3]", "gap", OMC_TEXT_UTF8,
                  0U, 0U);
    omc_iptc_to_xmp_translation_opts_init(&opts);
    opts.mappings = OMC_IPTC_TRANSLATE_KEYWORDS;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_iptc_to_xmp(&source, &output, &opts);
    assert(result.status ==
           OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

int
main(void)
{
    test_all_twenty_mappings();
    test_unrelated_paths_and_language_siblings();
    test_raw_bytes_ignore_text_hints();
    test_out_of_enum_hints_fail_transactionally();
    test_source_charset_and_field_validation();
    test_repeated_order_duplicates_and_alias();
    test_conflict_provenance_dirty_and_tombstones();
    test_limits_shape_and_budget_rollback();
    test_preserve_tombstone_and_transaction_failures();
    test_source_and_operation_limits();
    test_malformed_source_and_destination_rollback();
    return 0;
}
