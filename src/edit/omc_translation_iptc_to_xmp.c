#include "omc/omc_translation.h"
#include "../core/omc_value_internal.h"
#include "omc_translation_iptc_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct omc_iptc_to_xmp_group {
    omc_u32 candidate_count;
    omc_u32 source_start;
    omc_u32 source_count;
    omc_u32 destination_start;
    omc_u32 destination_count;
    int selected;
    int preserve;
    int unchanged;
} omc_iptc_to_xmp_group;

typedef struct omc_iptc_to_xmp_source {
    omc_entry_id id;
    omc_block_id block;
    omc_u32 order;
    omc_const_bytes text;
} omc_iptc_to_xmp_source;

typedef struct omc_iptc_to_xmp_destination {
    omc_entry_id id;
    omc_u32 index;
    omc_const_bytes text;
} omc_iptc_to_xmp_destination;

typedef struct omc_iptc_to_xmp_charset {
    omc_block_id block;
    int present;
    int valid;
} omc_iptc_to_xmp_charset;

typedef enum omc_iptc_to_xmp_path_kind {
    OMC_IPTC_TO_XMP_PATH_NO_MATCH = 0,
    OMC_IPTC_TO_XMP_PATH_TARGET,
    OMC_IPTC_TO_XMP_PATH_LANGUAGE_SIBLING,
    OMC_IPTC_TO_XMP_PATH_UNSUPPORTED
} omc_iptc_to_xmp_path_kind;

static int
omc_iptc_to_xmp_source_flags_valid(const omc_entry *entry)
{
    omc_entry_flags known;
    omc_entry_flags damaged;
    known = (omc_entry_flags)0x7FU;
    damaged = OMC_ENTRY_FLAG_TRUNCATED | OMC_ENTRY_FLAG_UNREADABLE;
    return (entry->flags & ~known) == 0U && (entry->flags & damaged) == 0U;
}

static int
omc_iptc_to_xmp_source_candidate(const omc_entry *entry,
                                 const omc_iptc_mapping *mapping)
{
    return entry->key.kind == OMC_KEY_IPTC_DATASET &&
           entry->key.u.iptc_dataset.record == 2U &&
           entry->key.u.iptc_dataset.dataset == mapping->dataset &&
           ((entry->flags & OMC_ENTRY_FLAG_DELETED) == 0U ||
            (entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
}

static int
omc_iptc_to_xmp_dirty_charset_marker(const omc_entry *entry)
{
    return entry->key.kind == OMC_KEY_IPTC_DATASET &&
           entry->key.u.iptc_dataset.record == 1U &&
           entry->key.u.iptc_dataset.dataset == 90U &&
           (entry->flags & OMC_ENTRY_FLAG_DELETED) == 0U &&
           (entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U;
}

static int
omc_iptc_to_xmp_schema_matches(const omc_store *store, const omc_entry *entry,
                               const omc_iptc_mapping *mapping)
{
    omc_const_bytes schema;
    omc_size n;
    if (entry->key.kind != OMC_KEY_XMP_PROPERTY ||
        !omc_ref_valid(&store->arena, entry->key.u.xmp_property.schema_ns))
        return 0;
    schema = omc_arena_view(&store->arena,
                            entry->key.u.xmp_property.schema_ns);
    n = strlen(mapping->ns);
    return schema.size == n &&
           (n == 0U || memcmp(schema.data, mapping->ns, n) == 0);
}

static omc_iptc_to_xmp_path_kind
omc_iptc_to_xmp_classify_path(const omc_store *store, const omc_entry *entry,
                              const omc_iptc_mapping *mapping,
                              omc_u32 *index)
{
    omc_const_bytes path;
    omc_size root_size;
    omc_size i;
    omc_size suffix_size;
    omc_u32 parsed;
    omc_u32 digit;
    int language_alt;

    if (!omc_iptc_to_xmp_schema_matches(store, entry, mapping))
        return OMC_IPTC_TO_XMP_PATH_NO_MATCH;
    if (!omc_ref_valid(&store->arena,
                       entry->key.u.xmp_property.property_path))
        return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
    path = omc_arena_view(&store->arena,
                          entry->key.u.xmp_property.property_path);
    root_size = 0U;
    while (mapping->path[root_size] != '\0' && mapping->path[root_size] != '[')
        ++root_size;
    language_alt = strchr(mapping->path, '[') != NULL;
    if (path.size == root_size &&
        memcmp(path.data, mapping->path, root_size) == 0) {
        if (mapping->repeated || language_alt)
            return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
        *index = 0U;
        return OMC_IPTC_TO_XMP_PATH_TARGET;
    }
    if (path.size <= root_size ||
        memcmp(path.data, mapping->path, root_size) != 0)
        return OMC_IPTC_TO_XMP_PATH_NO_MATCH;
    if (path.data[root_size] != '[' && path.data[root_size] != '/')
        return OMC_IPTC_TO_XMP_PATH_NO_MATCH;
    suffix_size = path.size - root_size;
    if (language_alt) {
        static const char prefix[] = "[@xml:lang=";
        const omc_size prefix_size = sizeof(prefix) - 1U;
        omc_size language_size;
        if (suffix_size <= prefix_size + 1U || path.data[root_size] != '[' ||
            memcmp(path.data + root_size, prefix, prefix_size) != 0 ||
            path.data[path.size - 1U] != ']')
            return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
        language_size = suffix_size - prefix_size - 1U;
        for (i = 0U; i < language_size; ++i) {
            const omc_u8 c = path.data[root_size + prefix_size + i];
            if (c == '[' || c == ']')
                return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
        }
        if (language_size == 9U &&
            memcmp(path.data + root_size + prefix_size, "x-default", 9U) == 0) {
            *index = 0U;
            return OMC_IPTC_TO_XMP_PATH_TARGET;
        }
        return OMC_IPTC_TO_XMP_PATH_LANGUAGE_SIBLING;
    }
    if (!mapping->repeated)
        return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
    if (path.data[root_size] != '[' || suffix_size < 3U ||
        path.data[path.size - 1U] != ']' ||
        path.data[root_size + 1U] == '0')
        return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
    parsed = 0U;
    for (i = root_size + 1U; i + 1U < path.size; ++i) {
        if (path.data[i] < '0' || path.data[i] > '9')
            return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
        digit = (omc_u32)(path.data[i] - '0');
        if (parsed > (0xFFFFFFFFU - digit) / 10U)
            return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
        parsed = parsed * 10U + digit;
    }
    if (parsed == 0U)
        return OMC_IPTC_TO_XMP_PATH_UNSUPPORTED;
    *index = parsed;
    return OMC_IPTC_TO_XMP_PATH_TARGET;
}

static int
omc_iptc_to_xmp_any_destination_root(const omc_store *store,
                                    const omc_iptc_mapping *mapping)
{
    omc_size i;
    omc_u32 index;
    omc_iptc_to_xmp_path_kind kind;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            !omc_iptc_to_xmp_schema_matches(store, entry, mapping))
            continue;
        kind = omc_iptc_to_xmp_classify_path(store, entry, mapping, &index);
        if (kind == OMC_IPTC_TO_XMP_PATH_TARGET ||
            kind == OMC_IPTC_TO_XMP_PATH_LANGUAGE_SIBLING ||
            kind == OMC_IPTC_TO_XMP_PATH_UNSUPPORTED)
            return 1;
    }
    return 0;
}

static int
omc_iptc_to_xmp_value_bytes(const omc_store *store, const omc_entry *entry,
                            int destination, omc_const_bytes *bytes)
{
    omc_byte_ref ref;
    if (destination) {
        if (entry->value.kind != OMC_VAL_TEXT ||
            (entry->value.text_encoding != OMC_TEXT_ASCII &&
             entry->value.text_encoding != OMC_TEXT_UTF8))
            return 0;
    } else {
        if (entry->value.kind != OMC_VAL_TEXT &&
            entry->value.kind != OMC_VAL_BYTES)
            return 0;
        if (entry->value.kind == OMC_VAL_TEXT &&
            entry->value.text_encoding != OMC_TEXT_ASCII &&
            entry->value.text_encoding != OMC_TEXT_UTF8)
            return 0;
        if (!omc_iptc_to_xmp_source_flags_valid(entry))
            return 0;
    }
    ref = entry->value.u.ref;
    if (!omc_ref_valid(&store->arena, ref) ||
        entry->value.count != ref.size)
        return 0;
    bytes->data = ref.size == 0U
                      ? NULL
                      : store->arena.data + ref.offset;
    bytes->size = ref.size;
    return 1;
}

static int
omc_iptc_to_xmp_add_text_budget(omc_u64 *total, omc_size amount,
                                const omc_iptc_to_xmp_translation_opts *opts)
{
    if ((omc_u64)amount > opts->max_total_text_bytes ||
        *total > opts->max_total_text_bytes - (omc_u64)amount ||
        (omc_u64)amount > OMC_IPTC_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES ||
        *total > OMC_IPTC_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES -
                     (omc_u64)amount)
        return 0;
    *total += (omc_u64)amount;
    return 1;
}

static int
omc_iptc_to_xmp_has_non_ascii(omc_const_bytes bytes)
{
    omc_size i;
    for (i = 0U; i < bytes.size; ++i)
        if (bytes.data[i] >= 0x80U)
            return 1;
    return 0;
}

static int
omc_iptc_to_xmp_xml_text_valid(omc_const_bytes bytes, int ascii)
{
    omc_size i;
    if (!omc_utf8_valid(bytes.data, bytes.size, ascii))
        return 0;
    for (i = 0U; i < bytes.size; ++i) {
        if (bytes.data[i] < 32U && bytes.data[i] != 9U &&
            bytes.data[i] != 10U && bytes.data[i] != 13U)
            return 0;
        if (bytes.size - i >= 3U && bytes.data[i] == 0xEFU &&
            bytes.data[i + 1U] == 0xBFU && bytes.data[i + 2U] >= 0xBEU)
            return 0;
    }
    return 1;
}

static int
omc_iptc_to_xmp_field_value_valid(const omc_iptc_mapping *mapping,
                                  omc_const_bytes bytes)
{
    omc_size i;
    if (mapping->bit == OMC_IPTC_TRANSLATE_CATEGORY) {
        for (i = 0U; i < bytes.size; ++i)
            if (!((bytes.data[i] >= 'A' && bytes.data[i] <= 'Z') ||
                  (bytes.data[i] >= 'a' && bytes.data[i] <= 'z')))
                return 0;
    } else if (mapping->bit == OMC_IPTC_TRANSLATE_COUNTRY_CODE) {
        if (bytes.size != 2U && bytes.size != 3U)
            return 0;
        for (i = 0U; i < bytes.size; ++i)
            if (bytes.data[i] < 'A' || bytes.data[i] > 'Z')
                return 0;
    } else if (mapping->bit == OMC_IPTC_TRANSLATE_URGENCY) {
        if (bytes.size != 1U || bytes.data[0] < '1' || bytes.data[0] > '8')
            return 0;
    }
    return 1;
}

static void
omc_iptc_to_xmp_inspect_charset(const omc_store *store, omc_block_id block,
                                omc_iptc_to_xmp_charset *result)
{
    omc_size i;
    omc_byte_ref ref;
    omc_const_bytes bytes;
    const omc_entry *entry;
    static const omc_u8 utf8_escape[3] = {0x1BU, 0x25U, 0x47U};

    result->block = block;
    result->present = 0;
    result->valid = 1;
    for (i = 0U; i < store->entry_count; ++i) {
        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_IPTC_DATASET ||
            entry->key.u.iptc_dataset.record != 1U ||
            entry->key.u.iptc_dataset.dataset != 90U ||
            entry->origin.block != block ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        if (!omc_iptc_to_xmp_source_flags_valid(entry)) {
            result->valid = 0;
        }
        if (result->present) {
            result->valid = 0;
            continue;
        }
        result->present = 1;
        if ((entry->value.kind != OMC_VAL_BYTES &&
             entry->value.kind != OMC_VAL_TEXT) ||
            (entry->value.kind == OMC_VAL_TEXT &&
             entry->value.text_encoding != OMC_TEXT_ASCII &&
             entry->value.text_encoding != OMC_TEXT_UTF8)) {
            result->valid = 0;
            continue;
        }
        ref = entry->value.u.ref;
        if (!omc_ref_valid(&store->arena, ref) ||
            entry->value.count != ref.size) {
            result->valid = 0;
            continue;
        }
        bytes.data = ref.size == 0U
                         ? NULL
                         : store->arena.data + ref.offset;
        bytes.size = ref.size;
        if (bytes.size != sizeof(utf8_escape) ||
            memcmp(bytes.data, utf8_escape, sizeof(utf8_escape)) != 0)
            result->valid = 0;
    }
}

static omc_iptc_to_xmp_charset *
omc_iptc_to_xmp_charset_for_block(const omc_store *store, omc_block_id block,
                                  omc_iptc_to_xmp_charset *cache,
                                  omc_u32 *cache_count, omc_u32 capacity)
{
    omc_u32 i;
    for (i = 0U; i < *cache_count; ++i)
        if (cache[i].block == block)
            return &cache[i];
    if (*cache_count >= capacity)
        return NULL;
    omc_iptc_to_xmp_inspect_charset(store, block, &cache[*cache_count]);
    ++*cache_count;
    return &cache[*cache_count - 1U];
}

static int
omc_iptc_to_xmp_destination_text_valid(const omc_entry *entry,
                                       omc_const_bytes bytes)
{
    if (entry->value.text_encoding == OMC_TEXT_ASCII &&
        omc_iptc_to_xmp_has_non_ascii(bytes))
        return 0;
    return omc_iptc_to_xmp_xml_text_valid(
        bytes, entry->value.text_encoding == OMC_TEXT_ASCII);
}

static int
omc_iptc_to_xmp_source_text_valid(const omc_store *store,
                                  const omc_iptc_mapping *mapping,
                                  const omc_entry *entry,
                                  omc_const_bytes bytes,
                                  omc_iptc_to_xmp_charset *cache,
                                  omc_u32 *cache_count,
                                  omc_u32 cache_capacity,
                                  omc_iptc_to_xmp_translation_status *status)
{
    omc_iptc_to_xmp_charset *charset;
    int non_ascii;
    non_ascii = omc_iptc_to_xmp_has_non_ascii(bytes);
    if (entry->value.kind == OMC_VAL_TEXT &&
        entry->value.text_encoding == OMC_TEXT_ASCII && non_ascii) {
        *status = OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_ENCODING_CONFLICT;
        return 0;
    }
    if (!omc_iptc_to_xmp_xml_text_valid(bytes, 0) ||
        !omc_iptc_to_xmp_field_value_valid(mapping, bytes)) {
        *status = OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
        return 0;
    }
    charset = omc_iptc_to_xmp_charset_for_block(
        store, entry->origin.block, cache, cache_count, cache_capacity);
    if (charset == NULL || !charset->valid ||
        (non_ascii && !charset->present)) {
        *status = OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_ENCODING_CONFLICT;
        return 0;
    }
    *status = OMC_IPTC_TO_XMP_TRANSLATION_OK;
    return 1;
}

static int
omc_iptc_to_xmp_source_compare(const void *left, const void *right)
{
    const omc_iptc_to_xmp_source *a;
    const omc_iptc_to_xmp_source *b;
    a = (const omc_iptc_to_xmp_source *)left;
    b = (const omc_iptc_to_xmp_source *)right;
    if (a->block != b->block)
        return a->block < b->block ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return a->id < b->id ? -1 : a->id != b->id;
}

static int
omc_iptc_to_xmp_destination_compare(const void *left, const void *right)
{
    const omc_iptc_to_xmp_destination *a;
    const omc_iptc_to_xmp_destination *b;
    a = (const omc_iptc_to_xmp_destination *)left;
    b = (const omc_iptc_to_xmp_destination *)right;
    if (a->index != b->index)
        return a->index < b->index ? -1 : 1;
    return a->id < b->id ? -1 : a->id != b->id;
}

static int
omc_iptc_to_xmp_text_equal(omc_const_bytes left, omc_const_bytes right)
{
    return left.size == right.size &&
           (left.size == 0U || memcmp(left.data, right.data, left.size) == 0);
}

static int
omc_iptc_to_xmp_make_path(const omc_iptc_mapping *mapping, omc_u32 ordinal,
                          char *buffer, omc_size capacity,
                          omc_size *path_size)
{
    omc_size root_size;
    omc_size digit_count;
    omc_size i;
    omc_u32 remaining;
    char digits[10];
    if (!mapping->repeated) {
        root_size = strlen(mapping->path);
        if (root_size > capacity)
            return 0;
        memcpy(buffer, mapping->path, root_size);
        *path_size = root_size;
        return 1;
    }
    root_size = strlen(mapping->path);
    if (root_size + 13U > capacity || ordinal == 0U)
        return 0;
    digit_count = 0U;
    remaining = ordinal;
    while (remaining != 0U) {
        digits[digit_count++] = (char)('0' + remaining % 10U);
        remaining /= 10U;
    }
    memcpy(buffer, mapping->path, root_size);
    buffer[root_size] = '[';
    for (i = 0U; i < digit_count; ++i)
        buffer[root_size + 1U + i] = digits[digit_count - i - 1U];
    buffer[root_size + 1U + digit_count] = ']';
    *path_size = root_size + digit_count + 2U;
    return 1;
}

static omc_status
omc_iptc_to_xmp_emit_entry(omc_edit *edit, const omc_iptc_mapping *mapping,
                           const omc_iptc_to_xmp_source *source,
                           omc_u32 ordinal)
{
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref path_ref;
    omc_byte_ref text_ref;
    omc_size path_size;
    char path[96];
    omc_status status;
    if (!omc_iptc_to_xmp_make_path(mapping, ordinal, path, sizeof(path),
                                   &path_size))
        return OMC_STATUS_OVERFLOW;
    status = omc_arena_append(&edit->arena, mapping->ns, strlen(mapping->ns),
                              &schema_ref);
    if (status != OMC_STATUS_OK)
        return status;
    status = omc_arena_append(&edit->arena, path, path_size, &path_ref);
    if (status != OMC_STATUS_OK)
        return status;
    status = omc_arena_append(&edit->arena, source->text.data,
                              source->text.size, &text_ref);
    if (status != OMC_STATUS_OK)
        return status;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, path_ref);
    omc_val_make_text(&entry.value, text_ref, OMC_TEXT_UTF8);
    entry.origin.block = source->block;
    entry.origin.order_in_block = source->order;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    return omc_edit_add_entry(edit, &entry);
}

void
omc_iptc_to_xmp_translation_opts_init(
    omc_iptc_to_xmp_translation_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->mappings = OMC_IPTC_TRANSLATE_ALL;
    opts->all_sources = 1;
    opts->conflict = OMC_TRANSLATION_FAIL;
    opts->max_source_properties =
        OMC_IPTC_TO_XMP_TRANSLATION_MAX_SOURCE_PROPERTIES;
    opts->max_added_entries = OMC_IPTC_TO_XMP_TRANSLATION_MAX_ADDED_ENTRIES;
    opts->max_operations = OMC_IPTC_TO_XMP_TRANSLATION_MAX_OPERATIONS;
    opts->max_total_text_bytes =
        OMC_IPTC_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES;
}

omc_iptc_to_xmp_translation_res
omc_translate_iptc_to_xmp(const omc_store *source, omc_store *out,
                          const omc_iptc_to_xmp_translation_opts *options)
{
    omc_iptc_to_xmp_translation_res result;
    omc_iptc_to_xmp_translation_opts opts;
    omc_iptc_to_xmp_group groups[OMC_IPTC_MAPPING_COUNT];
    omc_iptc_to_xmp_source *sources;
    omc_iptc_to_xmp_destination *destinations;
    omc_iptc_to_xmp_charset *charset_cache;
    omc_u8 *candidate_blocks;
    omc_size candidate_block_bytes;
    omc_size i;
    omc_size j;
    omc_u32 k;
    omc_u32 candidate_count;
    omc_u32 selected_properties;
    omc_u32 source_used;
    omc_u32 destination_used;
    omc_u32 charset_count;
    omc_u32 operation_count;
    omc_u32 addition_count;
    omc_u32 update_count;
    omc_u32 removal_count;
    omc_u32 translated_count;
    omc_u32 preserved_count;
    omc_u32 unchanged_count;
    omc_u32 overlap;
    omc_u32 ordinal;
    omc_u32 digit;
    omc_u64 total_text_bytes;
    omc_u64 edit_arena_bytes;
    omc_entry_id first_candidate;
    omc_entry_id first_active;
    omc_const_bytes bytes;
    omc_iptc_to_xmp_path_kind path_kind;
    omc_iptc_to_xmp_translation_status source_text_status;
    omc_status status;
    omc_edit edit;
    omc_store built;
    int dirty_member;
    int dirty_block_marker;
    int sentinel_candidate;
    int sentinel_dirty_marker;
    int has_root;
    int exact;
    int changed;
    int built_moved;

    memset(&result, 0, sizeof(result));
    result.status = OMC_IPTC_TO_XMP_TRANSLATION_OK;
    result.failed_source = OMC_INVALID_ENTRY_ID;
    sources = NULL;
    destinations = NULL;
    charset_cache = NULL;
    candidate_blocks = NULL;
    candidate_block_bytes = 0U;
    source_used = 0U;
    destination_used = 0U;
    charset_count = 0U;
    total_text_bytes = 0U;
    edit_arena_bytes = 0U;
    operation_count = 0U;
    addition_count = 0U;
    update_count = 0U;
    removal_count = 0U;
    translated_count = 0U;
    preserved_count = 0U;
    unchanged_count = 0U;
    built_moved = 0;
    omc_edit_init(&edit);
    omc_store_init(&built);

    if (out == NULL) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_NULL_OUTPUT;
        goto done;
    }
    omc_iptc_to_xmp_translation_opts_init(&opts);
    if (options != NULL)
        opts = *options;
    if (!omc_store_shape_valid(source)) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE;
        goto done;
    }
    if (!omc_store_shape_valid(out)) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL;
        goto done;
    }
    if (opts.mappings == 0U ||
        (opts.mappings & ~OMC_IPTC_TRANSLATE_ALL) != 0U ||
        (opts.all_sources != 0 && opts.all_sources != 1) ||
        opts.conflict < OMC_TRANSLATION_PRESERVE ||
        opts.conflict > OMC_TRANSLATION_REPLACE ||
        opts.max_source_properties == 0U ||
        opts.max_source_properties >
            OMC_IPTC_TO_XMP_TRANSLATION_MAX_SOURCE_PROPERTIES ||
        opts.max_added_entries == 0U ||
        opts.max_added_entries >
            OMC_IPTC_TO_XMP_TRANSLATION_MAX_ADDED_ENTRIES ||
        opts.max_operations == 0U ||
        opts.max_operations > OMC_IPTC_TO_XMP_TRANSLATION_MAX_OPERATIONS ||
        opts.max_total_text_bytes == 0U ||
        opts.max_total_text_bytes >
            OMC_IPTC_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_INVALID_OPTIONS;
        goto done;
    }

    memset(groups, 0, sizeof(groups));
    sources = (omc_iptc_to_xmp_source *)calloc(
        opts.max_source_properties, sizeof(*sources));
    destinations = (omc_iptc_to_xmp_destination *)calloc(
        opts.max_operations, sizeof(*destinations));
    charset_cache = (omc_iptc_to_xmp_charset *)calloc(
        opts.max_source_properties, sizeof(*charset_cache));
    if (sources == NULL || destinations == NULL || charset_cache == NULL) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_NO_MEMORY;
        goto done;
    }
    if (!opts.all_sources && source->block_count != 0U) {
        candidate_block_bytes = source->block_count / 8U +
                                (source->block_count % 8U != 0U ? 1U : 0U);
        candidate_blocks = (omc_u8 *)calloc(candidate_block_bytes, 1U);
        if (candidate_blocks == NULL) {
            result.status = OMC_IPTC_TO_XMP_TRANSLATION_NO_MEMORY;
            goto done;
        }
    }

    selected_properties = 0U;
    sentinel_dirty_marker = 0;
    if (!opts.all_sources) {
        for (i = 0U; i < source->entry_count; ++i)
            if (omc_iptc_to_xmp_dirty_charset_marker(&source->entries[i]) &&
                source->entries[i].origin.block == OMC_INVALID_BLOCK_ID)
                sentinel_dirty_marker = 1;
    }
    for (k = 0U; k < OMC_IPTC_MAPPING_COUNT; ++k) {
        const omc_iptc_mapping *mapping = &omc_iptc_mappings[k];
        omc_iptc_to_xmp_group *group = &groups[k];
        if ((opts.mappings & mapping->bit) == 0U)
            continue;
        if (candidate_blocks != NULL)
            memset(candidate_blocks, 0, candidate_block_bytes);
        candidate_count = 0U;
        dirty_member = 0;
        sentinel_candidate = 0;
        first_candidate = OMC_INVALID_ENTRY_ID;
        for (i = 0U; i < source->entry_count; ++i) {
            const omc_entry *entry = &source->entries[i];
            if (!omc_iptc_to_xmp_source_candidate(entry, mapping))
                continue;
            if (first_candidate == OMC_INVALID_ENTRY_ID)
                first_candidate = (omc_entry_id)i;
            if (candidate_count <= opts.max_source_properties)
                ++candidate_count;
            if ((entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U)
                dirty_member = 1;
            if (entry->origin.block == OMC_INVALID_BLOCK_ID) {
                sentinel_candidate = 1;
            } else if (entry->origin.block < source->block_count &&
                       candidate_blocks != NULL) {
                const omc_size block = entry->origin.block;
                candidate_blocks[block / 8U] |=
                    (omc_u8)(1U << (block % 8U));
            }
        }
        if (candidate_count == 0U)
            continue;
        group->candidate_count = candidate_count;
        group->selected = opts.all_sources || dirty_member;
        if (!group->selected) {
            dirty_block_marker = 0;
            if (sentinel_candidate && sentinel_dirty_marker)
                dirty_block_marker = 1;
            for (i = 0U; !dirty_block_marker && i < source->entry_count; ++i) {
                const omc_entry *entry = &source->entries[i];
                omc_block_id block;
                if (!omc_iptc_to_xmp_dirty_charset_marker(entry))
                    continue;
                block = entry->origin.block;
                if (block == OMC_INVALID_BLOCK_ID && sentinel_candidate) {
                    dirty_block_marker = 1;
                } else if (candidate_blocks != NULL &&
                           block < source->block_count &&
                           (candidate_blocks[block / 8U] &
                            (omc_u8)(1U << (block % 8U))) != 0U) {
                    dirty_block_marker = 1;
                }
            }
            group->selected = dirty_block_marker;
        }
        if (!group->selected)
            continue;
        if (candidate_count > opts.max_source_properties ||
            selected_properties >
                opts.max_source_properties - candidate_count) {
            result.status = OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT;
            result.failed_mapping = mapping->bit;
            goto done;
        }
        selected_properties += candidate_count;
        for (i = 0U; i < source->entry_count; ++i) {
            const omc_entry *entry = &source->entries[i];
            if (!omc_iptc_to_xmp_source_candidate(entry, mapping))
                continue;
            if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
                continue;
            if (entry->origin.block != OMC_INVALID_BLOCK_ID &&
                entry->origin.block >= source->block_count) {
                result.status =
                    OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
                result.failed_mapping = mapping->bit;
                result.failed_source = (omc_entry_id)i;
                goto done;
            }
        }
    }

    /* Collect selected sources and structural lengths before any UTF-8 scan. */
    for (k = 0U; k < OMC_IPTC_MAPPING_COUNT; ++k) {
        const omc_iptc_mapping *mapping = &omc_iptc_mappings[k];
        omc_iptc_to_xmp_group *group = &groups[k];
        if (!group->selected)
            continue;
        group->source_start = source_used;
        first_active = OMC_INVALID_ENTRY_ID;
        for (i = 0U; i < source->entry_count; ++i) {
            const omc_entry *entry = &source->entries[i];
            if (!omc_iptc_to_xmp_source_candidate(entry, mapping) ||
                (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
                continue;
            if (source_used >= opts.max_source_properties) {
                result.status = OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT;
                result.failed_mapping = mapping->bit;
                goto done;
            }
            if (!omc_iptc_to_xmp_value_bytes(source, entry, 0, &bytes)) {
                result.status =
                    OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
                result.failed_mapping = mapping->bit;
                result.failed_source = (omc_entry_id)i;
                goto done;
            }
            if (bytes.size == 0U) {
                result.status =
                    OMC_IPTC_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
                result.failed_mapping = mapping->bit;
                result.failed_source = (omc_entry_id)i;
                goto done;
            }
            if (bytes.size > mapping->max_bytes) {
                result.status = OMC_IPTC_TO_XMP_TRANSLATION_VALUE_TOO_LONG;
                result.failed_mapping = mapping->bit;
                result.failed_source = (omc_entry_id)i;
                goto done;
            }
            if (!omc_iptc_to_xmp_add_text_budget(&total_text_bytes,
                                                  bytes.size, &opts)) {
                result.status = OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT;
                result.failed_mapping = mapping->bit;
                result.failed_source = (omc_entry_id)i;
                goto done;
            }
            sources[source_used].id = (omc_entry_id)i;
            sources[source_used].block = entry->origin.block;
            sources[source_used].order = entry->origin.order_in_block;
            sources[source_used].text = bytes;
            ++source_used;
            ++group->source_count;
            if (first_active == OMC_INVALID_ENTRY_ID)
                first_active = (omc_entry_id)i;
        }
        if (!mapping->repeated && group->source_count > 1U) {
            result.status = OMC_IPTC_TO_XMP_TRANSLATION_AMBIGUOUS_SOURCE;
            result.failed_mapping = mapping->bit;
            result.failed_source = first_active;
            goto done;
        }
        if (mapping->repeated && group->source_count > 1U)
            qsort(sources + group->source_start, group->source_count,
                  sizeof(*sources), omc_iptc_to_xmp_source_compare);
    }

    /* Inspect only destination values owned by selected, non-preserved groups. */
    for (k = 0U; k < OMC_IPTC_MAPPING_COUNT; ++k) {
        const omc_iptc_mapping *mapping = &omc_iptc_mappings[k];
        omc_iptc_to_xmp_group *group = &groups[k];
        if (!group->selected)
            continue;
        group->destination_start = destination_used;
        if (opts.conflict == OMC_TRANSLATION_PRESERVE) {
            has_root = omc_iptc_to_xmp_any_destination_root(source, mapping);
            if (has_root) {
                group->preserve = 1;
                continue;
            }
        }
        for (i = 0U; i < source->entry_count; ++i) {
            const omc_entry *entry = &source->entries[i];
            if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
                !omc_iptc_to_xmp_schema_matches(source, entry, mapping))
                continue;
            digit = 0U;
            path_kind = omc_iptc_to_xmp_classify_path(source, entry, mapping,
                                                      &digit);
            if (path_kind == OMC_IPTC_TO_XMP_PATH_UNSUPPORTED) {
                result.status =
                    OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
                result.failed_mapping = mapping->bit;
                goto done;
            }
            if (path_kind != OMC_IPTC_TO_XMP_PATH_TARGET)
                continue;
            if (group->destination_count >= opts.max_operations ||
                destination_used >= opts.max_operations) {
                result.status = OMC_IPTC_TO_XMP_TRANSLATION_OPERATION_LIMIT;
                result.failed_mapping = mapping->bit;
                goto done;
            }
            if (!omc_iptc_to_xmp_value_bytes(source, entry, 1, &bytes) ||
                !omc_iptc_to_xmp_source_flags_valid(entry)) {
                result.status =
                    OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
                result.failed_mapping = mapping->bit;
                goto done;
            }
            if (!omc_iptc_to_xmp_add_text_budget(&total_text_bytes,
                                                  bytes.size, &opts)) {
                result.status = OMC_IPTC_TO_XMP_TRANSLATION_SOURCE_LIMIT;
                result.failed_mapping = mapping->bit;
                goto done;
            }
            destinations[destination_used].id = (omc_entry_id)i;
            destinations[destination_used].index = digit;
            destinations[destination_used].text = bytes;
            ++destination_used;
            ++group->destination_count;
        }
        if (!mapping->repeated && group->destination_count > 1U) {
            result.status =
                OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
            result.failed_mapping = mapping->bit;
            goto done;
        }
        if (mapping->repeated && group->destination_count > 1U) {
            omc_iptc_to_xmp_destination *group_destinations =
                destinations + group->destination_start;
            qsort(group_destinations, group->destination_count,
                  sizeof(*destinations), omc_iptc_to_xmp_destination_compare);
            for (j = 0U; j < group->destination_count; ++j) {
                if (group_destinations[j].index != j + 1U) {
                    result.status =
                        OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
                    result.failed_mapping = mapping->bit;
                    goto done;
                }
            }
        } else if (mapping->repeated && group->destination_count == 1U &&
                   destinations[group->destination_start].index != 1U) {
            result.status =
                OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
            result.failed_mapping = mapping->bit;
            goto done;
        }
    }

    /* The complete inspected text budget is now known; validate source text. */
    for (k = 0U; k < OMC_IPTC_MAPPING_COUNT; ++k) {
        const omc_iptc_mapping *mapping = &omc_iptc_mappings[k];
        omc_iptc_to_xmp_group *group = &groups[k];
        if (!group->selected)
            continue;
        for (j = 0U; j < group->source_count; ++j) {
            omc_iptc_to_xmp_source *selected =
                &sources[group->source_start + j];
            const omc_entry *entry = &source->entries[selected->id];
            source_text_status = OMC_IPTC_TO_XMP_TRANSLATION_OK;
            if (!omc_iptc_to_xmp_source_text_valid(
                    source, mapping, entry, selected->text, charset_cache,
                    &charset_count, opts.max_source_properties,
                    &source_text_status)) {
                result.status = source_text_status;
                result.failed_mapping = mapping->bit;
                result.failed_source = selected->id;
                goto done;
            }
        }
    }
    for (i = 0U; i < destination_used; ++i) {
        const omc_entry *entry = &source->entries[destinations[i].id];
        if (!omc_iptc_to_xmp_destination_text_valid(entry,
                                                    destinations[i].text)) {
            result.status =
                OMC_IPTC_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
            goto done;
        }
    }

    /* Plan the entire transaction and its bounded arena before building edits. */
    for (k = 0U; k < OMC_IPTC_MAPPING_COUNT; ++k) {
        const omc_iptc_mapping *mapping = &omc_iptc_mappings[k];
        omc_iptc_to_xmp_group *group = &groups[k];
        omc_iptc_to_xmp_source *group_sources;
        omc_iptc_to_xmp_destination *group_destinations;
        if (!group->selected)
            continue;
        if (group->preserve) {
            ++preserved_count;
            continue;
        }
        group_sources = sources + group->source_start;
        group_destinations = destinations + group->destination_start;
        exact = group->source_count == group->destination_count;
        if (exact) {
            for (j = 0U; j < group->source_count; ++j)
                if (!omc_iptc_to_xmp_text_equal(group_sources[j].text,
                                                group_destinations[j].text)) {
                    exact = 0;
                    break;
                }
        }
        if (exact) {
            group->unchanged = 1;
            ++unchanged_count;
            continue;
        }
        if (opts.conflict == OMC_TRANSLATION_FAIL &&
            group->destination_count != 0U) {
            result.status = OMC_IPTC_TO_XMP_TRANSLATION_DESTINATION_CONFLICT;
            result.failed_mapping = mapping->bit;
            goto done;
        }
        overlap = group->source_count < group->destination_count
                      ? group->source_count
                      : group->destination_count;
        changed = 0;
        for (j = 0U; j < overlap; ++j) {
            if (omc_iptc_to_xmp_text_equal(group_sources[j].text,
                                           group_destinations[j].text))
                continue;
            ++update_count;
            ++operation_count;
            edit_arena_bytes += group_sources[j].text.size;
            changed = 1;
        }
        if (group->destination_count > overlap) {
            removal_count += group->destination_count - overlap;
            operation_count += group->destination_count - overlap;
            changed = 1;
        }
        if (group->source_count > overlap) {
            for (j = overlap; j < group->source_count; ++j) {
                omc_size schema_size;
                omc_size path_size;
                char path[96];
                schema_size = strlen(mapping->ns);
                if (!omc_iptc_to_xmp_make_path(
                        mapping, (omc_u32)(j + 1U), path, sizeof(path),
                        &path_size)) {
                    result.status = OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL;
                    goto done;
                }
                addition_count += 1U;
                operation_count += 1U;
                edit_arena_bytes += group_sources[j].text.size;
                edit_arena_bytes += (omc_u64)schema_size;
                edit_arena_bytes += (omc_u64)path_size;
                changed = 1;
            }
        }
        if (changed)
            ++translated_count;
        else
            ++unchanged_count;
    }
    if (addition_count > opts.max_added_entries ||
        addition_count > OMC_IPTC_TO_XMP_TRANSLATION_MAX_ADDED_ENTRIES) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_ENTRY_LIMIT;
        goto done;
    }
    if (operation_count > opts.max_operations ||
        operation_count > OMC_IPTC_TO_XMP_TRANSLATION_MAX_OPERATIONS) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_OPERATION_LIMIT;
        goto done;
    }
    if (source->entry_count > (omc_size)0xFFFFFFFFU - addition_count) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_ENTRY_LIMIT;
        goto done;
    }

    status = omc_edit_reserve_ops(&edit, operation_count);
    if (status == OMC_STATUS_OK)
        status = omc_arena_reserve(&edit.arena, (omc_size)edit_arena_bytes);
    if (status != OMC_STATUS_OK) {
        result.status = status == OMC_STATUS_NO_MEMORY
                            ? OMC_IPTC_TO_XMP_TRANSLATION_NO_MEMORY
                            : OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL;
        goto done;
    }
    for (k = 0U; k < OMC_IPTC_MAPPING_COUNT; ++k) {
        const omc_iptc_mapping *mapping = &omc_iptc_mappings[k];
        omc_iptc_to_xmp_group *group = &groups[k];
        omc_iptc_to_xmp_source *group_sources;
        omc_iptc_to_xmp_destination *group_destinations;
        if (!group->selected || group->preserve || group->unchanged)
            continue;
        group_sources = sources + group->source_start;
        group_destinations = destinations + group->destination_start;
        overlap = group->source_count < group->destination_count
                      ? group->source_count
                      : group->destination_count;
        for (j = 0U; j < overlap; ++j) {
            omc_byte_ref ref;
            omc_val value;
            if (omc_iptc_to_xmp_text_equal(group_sources[j].text,
                                           group_destinations[j].text))
                continue;
            status = omc_arena_append(&edit.arena,
                                      group_sources[j].text.data,
                                      group_sources[j].text.size, &ref);
            if (status != OMC_STATUS_OK)
                goto edit_failed;
            omc_val_make_text(&value, ref, OMC_TEXT_UTF8);
            status = omc_edit_set_value(&edit, group_destinations[j].id,
                                        &value);
            if (status != OMC_STATUS_OK)
                goto edit_failed;
        }
        for (j = overlap; j < group->destination_count; ++j) {
            status = omc_edit_tombstone(&edit, group_destinations[j].id);
            if (status != OMC_STATUS_OK)
                goto edit_failed;
        }
        for (j = overlap; j < group->source_count; ++j) {
            ordinal = (omc_u32)(j + 1U);
            status = omc_iptc_to_xmp_emit_entry(&edit, mapping,
                                                &group_sources[j], ordinal);
            if (status != OMC_STATUS_OK)
                goto edit_failed;
        }
    }
    if (edit.op_count != operation_count) {
        result.status = OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL;
        goto done;
    }
    status = omc_edit_commit(source, &edit, 1U, &built);
    if (status != OMC_STATUS_OK) {
        result.status = status == OMC_STATUS_NO_MEMORY
                            ? OMC_IPTC_TO_XMP_TRANSLATION_NO_MEMORY
                            : OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL;
        goto done;
    }
    omc_store_fini(out);
    *out = built;
    built_moved = 1;
    result.status = OMC_IPTC_TO_XMP_TRANSLATION_OK;
    result.source_properties = selected_properties;
    result.groups_translated = translated_count;
    result.groups_preserved = preserved_count;
    result.groups_unchanged = unchanged_count;
    result.entries_added = addition_count;
    result.entries_updated = update_count;
    result.entries_removed = removal_count;
    goto done;

edit_failed:
    result.status = status == OMC_STATUS_NO_MEMORY
                        ? OMC_IPTC_TO_XMP_TRANSLATION_NO_MEMORY
                        : OMC_IPTC_TO_XMP_TRANSLATION_INTERNAL;

done:
    free(candidate_blocks);
    free(charset_cache);
    free(destinations);
    free(sources);
    omc_edit_fini(&edit);
    if (!built_moved)
        omc_store_fini(&built);
    if (result.status != OMC_IPTC_TO_XMP_TRANSLATION_OK) {
        result.source_properties = 0U;
        result.groups_translated = 0U;
        result.groups_preserved = 0U;
        result.groups_unchanged = 0U;
        result.entries_added = 0U;
        result.entries_updated = 0U;
        result.entries_removed = 0U;
    }
    return result;
}
