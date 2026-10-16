#ifdef NDEBUG
#undef NDEBUG
#endif

#include "omc/omc_translation.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char ns_xmp[] = "http://ns.adobe.com/xap/1.0/";
static const char ns_exif[] = "http://ns.adobe.com/exif/1.0/";
static const char expected_datetime[] = "2024:02:29 03:04:05";
static const char expected_original_datetime[] = "2020:01:02 03:04:05";

typedef struct test_store_snapshot {
    omc_store state;
    omc_entry *entries;
    omc_block_info *blocks;
    omc_u8 *arena_bytes;
} test_store_snapshot;

static omc_byte_ref
append_text(omc_store *store, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, text, strlen(text), &ref) == OMC_STATUS_OK);
    return ref;
}

static omc_entry_id
add_xmp_property(omc_store *store, const char *ns, const char *name,
                 const char *value, omc_entry_flags flags)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(store, ns),
                              append_text(store, name));
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_xmp(omc_store *store, const char *name, const char *value)
{
    return add_xmp_property(store, ns_xmp, name, value, OMC_ENTRY_FLAG_DIRTY);
}

static omc_entry_id
add_exif_value(omc_store *store, omc_u16 tag, const void *data, omc_size size,
               int is_text, omc_text_encoding encoding, omc_entry_flags flags,
               omc_u32 order, const char *wire_name)
{
    omc_entry entry;
    omc_byte_ref ifd;
    omc_byte_ref value;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    ifd = append_text(store, "exififd");
    assert(omc_arena_append(&store->arena, data, size, &value) == OMC_STATUS_OK);
    omc_key_make_exif_tag(&entry.key, ifd, tag);
    if (is_text)
        omc_val_make_text(&entry.value, value, encoding);
    else
        omc_val_make_bytes(&entry.value, value);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.order_in_block = order;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 2U;
    entry.origin.wire_count = (omc_u32)size;
    if (wire_name != NULL)
        entry.origin.wire_type_name = append_text(store, wire_name);
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_exif_bytes(omc_store *store, omc_u16 tag, const omc_u8 *bytes, omc_size size,
               omc_u32 order)
{
    return add_exif_value(store, tag, bytes, size, 0, OMC_TEXT_UNKNOWN,
                          OMC_ENTRY_FLAG_NONE, order, NULL);
}

static omc_entry_id
add_exif_text(omc_store *store, omc_u16 tag, const char *value,
              omc_entry_flags flags, omc_u32 order, const char *wire_name)
{
    return add_exif_value(store, tag, value, (omc_size)strlen(value), 1,
                          OMC_TEXT_ASCII, flags, order, wire_name);
}

static omc_entry_id
add_ifd0_text(omc_store *store, omc_u16 tag, const char *value,
              omc_entry_flags flags)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(store, "ifd0"), tag);
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_ASCII);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_exif_delete_intent(omc_store *store, omc_u16 tag)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(store, "exififd"), tag);
    omc_val_make_text(&entry.value, append_text(store, ""), OMC_TEXT_ASCII);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static const omc_entry *
find_exif_ifd(const omc_store *store, const char *ifd_name, omc_u16 tag,
             omc_size ordinal)
{
    omc_size i;
    omc_const_bytes ifd;
    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind != OMC_KEY_EXIF_TAG ||
            store->entries[i].key.u.exif_tag.tag != tag)
            continue;
        ifd = omc_arena_view(&store->arena,
                             store->entries[i].key.u.exif_tag.ifd);
        if (ifd.size == strlen(ifd_name) &&
            memcmp(ifd.data, ifd_name, ifd.size) == 0) {
            if (ordinal == 0U)
                return &store->entries[i];
            ordinal--;
        }
    }
    return NULL;
}

static const omc_entry *
find_exif(const omc_store *store, omc_u16 tag)
{
    return find_exif_ifd(store, "exififd", tag, 0U);
}

static const omc_entry *
find_exif_nth(const omc_store *store, omc_u16 tag, omc_size ordinal)
{
    return find_exif_ifd(store, "exififd", tag, ordinal);
}

static omc_size
count_exif(const omc_store *store, omc_u16 tag, omc_entry_flags required,
           omc_entry_flags forbidden)
{
    omc_size i;
    omc_size count;
    omc_const_bytes ifd;
    count = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind != OMC_KEY_EXIF_TAG ||
            store->entries[i].key.u.exif_tag.tag != tag ||
            (store->entries[i].flags & required) != required ||
            (store->entries[i].flags & forbidden) != 0U)
            continue;
        ifd = omc_arena_view(&store->arena,
                             store->entries[i].key.u.exif_tag.ifd);
        if (ifd.size == sizeof("exififd") - 1U &&
            memcmp(ifd.data, "exififd", ifd.size) == 0)
            count++;
    }
    return count;
}

static omc_entry_id
add_iptc_bytes(omc_store *store, omc_u16 dataset, const char *value,
               omc_entry_flags flags)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, dataset);
    omc_val_make_bytes(&entry.value,
                       append_text(store, value));
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static const omc_entry *
find_iptc(const omc_store *store, omc_u16 dataset)
{
    omc_size i;
    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind == OMC_KEY_IPTC_DATASET &&
            store->entries[i].key.u.iptc_dataset.record == 2U &&
            store->entries[i].key.u.iptc_dataset.dataset == dataset &&
            (store->entries[i].flags & OMC_ENTRY_FLAG_DELETED) == 0U)
            return &store->entries[i];
    }
    return NULL;
}

static void
snapshot_store(test_store_snapshot *snapshot, const omc_store *store)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->state = *store;
    if (store->entry_count != 0U) {
        snapshot->entries = (omc_entry *)malloc(store->entry_count * sizeof(*store->entries));
        assert(snapshot->entries != NULL);
        memcpy(snapshot->entries, store->entries,
               store->entry_count * sizeof(*store->entries));
    }
    if (store->block_count != 0U) {
        snapshot->blocks = (omc_block_info *)malloc(store->block_count * sizeof(*store->blocks));
        assert(snapshot->blocks != NULL);
        memcpy(snapshot->blocks, store->blocks,
               store->block_count * sizeof(*store->blocks));
    }
    if (store->arena.size != 0U) {
        snapshot->arena_bytes = (omc_u8 *)malloc(store->arena.size);
        assert(snapshot->arena_bytes != NULL);
        memcpy(snapshot->arena_bytes, store->arena.data, store->arena.size);
    }
}

static void
assert_store_unchanged(const test_store_snapshot *snapshot,
                       const omc_store *store)
{
    assert(memcmp(&snapshot->state, store, sizeof(*store)) == 0);
    if (store->entry_count != 0U)
        assert(memcmp(snapshot->entries, store->entries,
                      store->entry_count * sizeof(*store->entries)) == 0);
    if (store->block_count != 0U)
        assert(memcmp(snapshot->blocks, store->blocks,
                      store->block_count * sizeof(*store->blocks)) == 0);
    if (store->arena.size != 0U)
        assert(memcmp(snapshot->arena_bytes, store->arena.data,
                      store->arena.size) == 0);
}

static void
free_snapshot(test_store_snapshot *snapshot)
{
    free(snapshot->arena_bytes);
    free(snapshot->blocks);
    free(snapshot->entries);
    memset(snapshot, 0, sizeof(*snapshot));
}

static void
test_exact_native_value_promotes_and_absences_get_intents(void)
{
    omc_store source;
    omc_store out;
    omc_store repeated;
    omc_translation_opts opts;
    omc_translation_res result;
    const omc_entry *entry;
    omc_const_bytes value;
    omc_u8 native_value[sizeof(expected_datetime)];
    omc_entry_id native_id;

    omc_store_init(&source);
    omc_store_init(&out);
    omc_store_init(&repeated);
    add_xmp(&source, "CreateDate", "2024-02-29T03:04:05");
    source.entries[0].origin.order_in_block = 17U;
    memcpy(native_value, expected_datetime, sizeof(expected_datetime) - 1U);
    native_value[sizeof(expected_datetime) - 1U] = 0U;
    native_id = add_exif_bytes(&source, 0x9004U, native_value,
                               (omc_size)sizeof(native_value), 17U);

    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_EXIF;
    result = omc_translate_xmp(&source, &out, &opts, NULL);

    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 1U);
    assert(result.entries_updated == 1U);
    assert(result.entries_added == 2U);
    entry = find_exif(&out, 0x9004U);
    assert(entry != NULL);
    assert((entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
    assert(entry->value.kind == OMC_VAL_BYTES);
    value = omc_arena_view(&out.arena, entry->value.u.ref);
    assert(value.size == sizeof(native_value));
    assert(memcmp(value.data, native_value, sizeof(native_value)) == 0);
    assert(entry->origin.order_in_block == 17U);

    entry = find_exif(&out, 0x9012U);
    assert(entry != NULL);
    assert((entry->flags & (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(entry->value.text_encoding == OMC_TEXT_ASCII);
    assert(entry->value.count == 0U);
    assert(omc_arena_view(&out.arena, entry->value.u.ref).size == 0U);
    assert(entry->origin.order_in_block == 19U);
    entry = find_exif(&out, 0x9292U);
    assert(entry != NULL);
    assert((entry->flags & (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(entry->value.text_encoding == OMC_TEXT_ASCII);
    assert(entry->value.count == 0U);
    assert(omc_arena_view(&out.arena, entry->value.u.ref).size == 0U);
    assert(entry->origin.order_in_block == 20U);

    assert(source.entries[native_id].flags == OMC_ENTRY_FLAG_NONE);
    assert(source.entries[native_id].value.kind == OMC_VAL_BYTES);
    result = omc_translate_xmp(&out, &repeated, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_unchanged == 1U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    omc_store_fini(&repeated);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
test_deleted_marker_only_group_obeys_policy(void)
{
    omc_store source;
    omc_store preserve_out;
    omc_store fail_out;
    omc_store replace_out;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id source_id;

    omc_store_init(&source);
    omc_store_init(&preserve_out);
    omc_store_init(&fail_out);
    omc_store_init(&replace_out);
    source_id = add_xmp(&source, "CreateDate", "ignored");
    source.entries[source_id].flags |= OMC_ENTRY_FLAG_DELETED;
    add_exif_delete_intent(&source, 0x9004U);
    add_exif_delete_intent(&source, 0x9004U);
    add_exif_delete_intent(&source, 0x9012U);
    add_exif_delete_intent(&source, 0x9292U);

    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_EXIF;
    opts.conflict = OMC_TRANSLATION_PRESERVE;
    result = omc_translate_xmp(&source, &preserve_out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 1U);
    assert(result.groups_unchanged == 0U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    assert(count_exif(&preserve_out, 0x9004U,
                      OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED,
                      OMC_ENTRY_FLAG_NONE) == 2U);

    opts.conflict = OMC_TRANSLATION_FAIL;
    result = omc_translate_xmp(&source, &fail_out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 0U);
    assert(result.groups_unchanged == 1U);
    assert(count_exif(&fail_out, 0x9004U,
                      OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED,
                      OMC_ENTRY_FLAG_NONE) == 2U);

    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp(&source, &replace_out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 0U);
    assert(result.groups_unchanged == 1U);
    assert(count_exif(&replace_out, 0x9004U,
                      OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED,
                      OMC_ENTRY_FLAG_NONE) == 2U);

    omc_store_fini(&replace_out);
    omc_store_fini(&fail_out);
    omc_store_fini(&preserve_out);
    omc_store_fini(&source);
}

static void
assert_promoted_value(const omc_store *source, const omc_store *out,
                      omc_entry_id id, omc_u16 tag)
{
    const omc_entry *before;
    const omc_entry *after;
    omc_const_bytes before_value;
    omc_const_bytes after_value;
    omc_const_bytes before_name;
    omc_const_bytes after_name;
    before = &source->entries[id];
    after = &out->entries[id];
    assert(after->key.kind == OMC_KEY_EXIF_TAG);
    assert(after->key.u.exif_tag.tag == tag);
    assert((after->flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
    assert(after->value.kind == before->value.kind);
    assert(after->value.elem_type == before->value.elem_type);
    assert(after->value.text_encoding == before->value.text_encoding);
    assert(after->value.count == before->value.count);
    assert(after->value.byte_order == before->value.byte_order);
    before_value = omc_arena_view(&source->arena, before->value.u.ref);
    after_value = omc_arena_view(&out->arena, after->value.u.ref);
    assert(before_value.size == after_value.size);
    assert(memcmp(before_value.data, after_value.data, before_value.size) == 0);
    assert(after->origin.block == before->origin.block);
    assert(after->origin.order_in_block == before->origin.order_in_block);
    assert(after->origin.wire_type.family == before->origin.wire_type.family);
    assert(after->origin.wire_type.code == before->origin.wire_type.code);
    assert(after->origin.wire_count == before->origin.wire_count);
    assert(after->origin.name_context_kind == before->origin.name_context_kind);
    assert(after->origin.name_context_variant == before->origin.name_context_variant);
    before_name = omc_arena_view(&source->arena, before->origin.wire_type_name);
    after_name = omc_arena_view(&out->arena, after->origin.wire_type_name);
    assert(before_name.size == after_name.size);
    assert(before_name.size == 0U ||
           memcmp(before_name.data, after_name.data, before_name.size) == 0);
}

static void
test_both_creation_mappings_promote_full_clean_groups(void)
{
    static const omc_u8 date_value[] = "2024:02:29 03:04:05\0\0";
    static const omc_u8 offset_value[] = "+05:30\0";
    static const omc_u8 fraction_value[] = "123\0\0";
    static const struct {
        omc_u32 mapping;
        const char *ns;
        const char *property;
        omc_u16 date_tag;
        omc_u16 offset_tag;
        omc_u16 fraction_tag;
    } cases[] = {
        {OMC_TRANSLATE_CREATE_EXIF, ns_xmp, "CreateDate", 0x9004U, 0x9012U,
         0x9292U},
        {OMC_TRANSLATE_DATE_ORIGINAL, ns_exif, "DateTimeOriginal", 0x9003U,
         0x9011U, 0x9291U}
    };
    omc_size c;
    for (c = 0U; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        omc_store source;
        omc_store out;
        omc_store repeated;
        omc_store preserve_out;
        omc_store limit_out;
        omc_translation_opts opts;
        omc_translation_res result;
        test_store_snapshot snapshot;
        omc_entry_id date_id;
        omc_entry_id offset_id;
        omc_entry_id fraction_id;

        omc_store_init(&source);
        omc_store_init(&out);
        omc_store_init(&repeated);
        omc_store_init(&preserve_out);
        omc_store_init(&limit_out);
        add_xmp_property(&source, cases[c].ns, cases[c].property,
                         "2024-02-29T03:04:05.123+05:30",
                         OMC_ENTRY_FLAG_DIRTY);
        date_id = add_exif_value(&source, cases[c].date_tag, date_value,
                                 (omc_size)sizeof(date_value) - 1U, 0,
                                 OMC_TEXT_UNKNOWN, OMC_ENTRY_FLAG_NONE, 31U,
                                 "DateWire");
        offset_id = add_exif_value(&source, cases[c].offset_tag, offset_value,
                                   (omc_size)sizeof(offset_value) - 1U, 1,
                                   OMC_TEXT_UTF16BE, OMC_ENTRY_FLAG_NONE, 32U,
                                   "OffsetWire");
        fraction_id = add_exif_value(&source, cases[c].fraction_tag,
                                     fraction_value,
                                     (omc_size)sizeof(fraction_value) - 1U, 0,
                                     OMC_TEXT_UNKNOWN, OMC_ENTRY_FLAG_NONE, 33U,
                                     "FractionWire");
        omc_translation_opts_init(&opts);
        opts.mappings = cases[c].mapping;
        result = omc_translate_xmp(&source, &out, &opts, NULL);
        assert(result.status == OMC_TRANSLATION_OK);
        assert(result.groups_translated == 1U);
        assert(result.entries_updated == 3U);
        assert(result.entries_added == 0U);
        assert(result.entries_removed == 0U);
        assert_promoted_value(&source, &out, date_id, cases[c].date_tag);
        assert_promoted_value(&source, &out, offset_id, cases[c].offset_tag);
        assert_promoted_value(&source, &out, fraction_id, cases[c].fraction_tag);

        result = omc_translate_xmp(&out, &repeated, &opts, NULL);
        assert(result.status == OMC_TRANSLATION_OK);
        assert(result.groups_unchanged == 1U);
        assert(result.entries_updated == 0U);
        assert(result.entries_added == 0U);
        assert(result.entries_removed == 0U);

        opts.conflict = OMC_TRANSLATION_PRESERVE;
        result = omc_translate_xmp(&source, &preserve_out, &opts, NULL);
        assert(result.status == OMC_TRANSLATION_OK);
        assert(result.groups_preserved == 1U);
        assert(result.entries_updated == 0U);
        assert((preserve_out.entries[date_id].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
        assert((preserve_out.entries[offset_id].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
        assert((preserve_out.entries[fraction_id].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);

        add_xmp(&limit_out, "Sentinel", "unchanged");
        snapshot_store(&snapshot, &limit_out);
        opts.conflict = OMC_TRANSLATION_FAIL;
        opts.max_operations = 2U;
        result = omc_translate_xmp(&source, &limit_out, &opts, NULL);
        assert(result.status == OMC_TRANSLATION_LIMIT);
        assert(result.entries_added == 0U);
        assert(result.entries_updated == 0U);
        assert(result.entries_removed == 0U);
        assert(result.groups_translated == 0U);
        assert_store_unchanged(&snapshot, &limit_out);
        assert((source.entries[date_id].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
        assert((source.entries[offset_id].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
        assert((source.entries[fraction_id].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
        free_snapshot(&snapshot);

        omc_store_fini(&limit_out);
        omc_store_fini(&preserve_out);
        omc_store_fini(&repeated);
        omc_store_fini(&out);
        omc_store_fini(&source);
    }
}

static void
test_deleted_group_promotes_only_first_clean_markers(void)
{
    static const omc_u8 stale_date[] = "stale date\0";
    static const omc_u8 stale_duplicate[] = "duplicate\0";
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id source_id;
    omc_entry_id first_date;
    omc_entry_id second_date;
    omc_entry_id offset_id;
    omc_entry_id dirty_date;
    omc_entry_id clean_date;
    omc_entry_id original_offset;
    const omc_entry *entry;

    omc_store_init(&source);
    omc_store_init(&out);
    source_id = add_xmp(&source, "CreateDate", "deleted");
    source.entries[source_id].flags |= OMC_ENTRY_FLAG_DELETED;
    first_date = add_exif_value(&source, 0x9004U, stale_date,
                                (omc_size)sizeof(stale_date) - 1U, 0,
                                OMC_TEXT_UNKNOWN, OMC_ENTRY_FLAG_DELETED, 50U,
                                "FirstOldWire");
    second_date = add_exif_value(&source, 0x9004U, stale_duplicate,
                                 (omc_size)sizeof(stale_duplicate) - 1U, 1,
                                 OMC_TEXT_UTF8, OMC_ENTRY_FLAG_DELETED, 51U,
                                 "SecondOldWire");
    offset_id = add_exif_value(&source, 0x9012U, "old offset", 10U, 1,
                               OMC_TEXT_ASCII, OMC_ENTRY_FLAG_DELETED, 52U,
                               "OffsetOldWire");

    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_EXIF;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 1U);
    assert(result.entries_updated == 2U);
    assert(result.entries_added == 1U);
    assert(result.entries_removed == 0U);
    assert(find_exif_nth(&out, 0x9004U, 0U) == &out.entries[first_date]);
    assert(find_exif_nth(&out, 0x9004U, 1U) == &out.entries[second_date]);
    assert_promoted_value(&source, &out, first_date, 0x9004U);
    assert((out.entries[first_date].flags & OMC_ENTRY_FLAG_DELETED) != 0U);
    assert((out.entries[second_date].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
    assert((out.entries[second_date].flags & OMC_ENTRY_FLAG_DELETED) != 0U);
    assert_promoted_value(&source, &out, offset_id, 0x9012U);
    assert((out.entries[offset_id].flags & OMC_ENTRY_FLAG_DELETED) != 0U);
    entry = find_exif(&out, 0x9292U);
    assert(entry != NULL);
    assert((entry->flags & (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(entry->value.text_encoding == OMC_TEXT_ASCII);
    assert(entry->value.count == 0U);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    source_id = add_xmp_property(&source, ns_exif, "DateTimeOriginal", "deleted",
                                 OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
    dirty_date = add_exif_delete_intent(&source, 0x9003U);
    clean_date = add_exif_value(&source, 0x9003U, stale_duplicate,
                                (omc_size)sizeof(stale_duplicate) - 1U, 0,
                                OMC_TEXT_UNKNOWN, OMC_ENTRY_FLAG_DELETED, 72U,
                                "CleanDuplicate");
    original_offset = add_exif_value(&source, 0x9011U, "old offset", 10U, 0,
                                     OMC_TEXT_UNKNOWN, OMC_ENTRY_FLAG_DELETED,
                                     73U, "OffsetClean");
    (void)source_id;
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_ORIGINAL;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.entries_updated == 1U);
    assert(result.entries_added == 1U);
    assert((out.entries[dirty_date].flags &
            (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert((out.entries[clean_date].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
    assert((out.entries[clean_date].flags & OMC_ENTRY_FLAG_DELETED) != 0U);
    assert_promoted_value(&source, &out, original_offset, 0x9011U);
    assert((out.entries[original_offset].flags & OMC_ENTRY_FLAG_DELETED) != 0U);
    assert(count_exif(&out, 0x9003U, OMC_ENTRY_FLAG_DIRTY,
                      OMC_ENTRY_FLAG_NONE) == 1U);
    assert(count_exif(&out, 0x9011U, OMC_ENTRY_FLAG_DIRTY,
                      OMC_ENTRY_FLAG_NONE) == 1U);
    assert(count_exif(&out, 0x9291U,
                      OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED,
                      OMC_ENTRY_FLAG_NONE) == 1U);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
test_deleted_group_tombstones_all_active_members(void)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id source_id;
    omc_entry_id date_id;
    omc_entry_id offset_id;
    omc_entry_id fraction_id;

    omc_store_init(&source);
    omc_store_init(&out);
    source_id = add_xmp_property(&source, ns_exif, "DateTimeOriginal", "deleted",
                                 OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
    (void)source_id;
    date_id = add_exif_text(&source, 0x9003U, "2020:01:02 03:04:05",
                            OMC_ENTRY_FLAG_NONE, 81U, "DateWire");
    offset_id = add_exif_text(&source, 0x9011U, "+00:00", OMC_ENTRY_FLAG_NONE,
                              82U, "OffsetWire");
    fraction_id = add_exif_text(&source, 0x9291U, "123", OMC_ENTRY_FLAG_NONE,
                                83U, "FractionWire");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_ORIGINAL;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.entries_removed == 3U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert((out.entries[date_id].flags &
            (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert((out.entries[offset_id].flags &
            (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert((out.entries[fraction_id].flags &
            (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert(count_exif(&out, 0x9003U, OMC_ENTRY_FLAG_NONE,
                      OMC_ENTRY_FLAG_DELETED) == 0U);
    assert(count_exif(&out, 0x9011U, OMC_ENTRY_FLAG_NONE,
                      OMC_ENTRY_FLAG_DELETED) == 0U);
    assert(count_exif(&out, 0x9291U, OMC_ENTRY_FLAG_NONE,
                      OMC_ENTRY_FLAG_DELETED) == 0U);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
test_conflict_policies_and_duplicate_replacement(void)
{
    omc_store source;
    omc_store fail_out;
    omc_store preserve_out;
    omc_store replace_out;
    omc_store repeated;
    omc_translation_opts opts;
    omc_translation_res result;
    test_store_snapshot source_snapshot;
    test_store_snapshot output_snapshot;
    omc_entry_id first_date;
    omc_entry_id duplicate_date;
    omc_entry_id stale_offset;
    const omc_entry *entry;

    omc_store_init(&source);
    omc_store_init(&fail_out);
    omc_store_init(&preserve_out);
    omc_store_init(&replace_out);
    omc_store_init(&repeated);
    add_xmp_property(&source, ns_exif, "DateTimeOriginal",
                     "2020-01-02T03:04:05", OMC_ENTRY_FLAG_DIRTY);
    first_date = add_exif_text(&source, 0x9003U, "1999:12:31 23:59:59",
                               OMC_ENTRY_FLAG_NONE, 11U, "FirstDate");
    duplicate_date = add_exif_text(&source, 0x9003U,
                                   "2020:01:02 03:04:05",
                                   OMC_ENTRY_FLAG_NONE, 12U, "DuplicateDate");
    stale_offset = add_exif_text(&source, 0x9011U, "+01:00",
                                 OMC_ENTRY_FLAG_NONE, 13U, "StaleOffset");
    add_xmp(&fail_out, "Sentinel", "unchanged");
    snapshot_store(&source_snapshot, &source);
    snapshot_store(&output_snapshot, &fail_out);

    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_ORIGINAL;
    result = omc_translate_xmp(&source, &fail_out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_NATIVE_CONFLICT);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_ORIGINAL);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    assert(result.groups_translated == 0U);
    assert_store_unchanged(&source_snapshot, &source);
    assert_store_unchanged(&output_snapshot, &fail_out);
    free_snapshot(&output_snapshot);
    free_snapshot(&source_snapshot);

    opts.conflict = OMC_TRANSLATION_PRESERVE;
    result = omc_translate_xmp(&source, &preserve_out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_preserved == 1U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    assert((preserve_out.entries[first_date].flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
    assert((preserve_out.entries[duplicate_date].flags & OMC_ENTRY_FLAG_DELETED) == 0U);
    assert((preserve_out.entries[stale_offset].flags & OMC_ENTRY_FLAG_DELETED) == 0U);

    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp(&source, &replace_out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 1U);
    assert(result.entries_updated == 1U);
    assert(result.entries_removed == 2U);
    assert(result.entries_added == 1U);
    entry = &replace_out.entries[first_date];
    assert((entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(entry->value.text_encoding == OMC_TEXT_ASCII);
    {
        omc_const_bytes value;
        value = omc_arena_view(&replace_out.arena, entry->value.u.ref);
        assert(value.size == sizeof(expected_original_datetime) - 1U);
        assert(memcmp(value.data, expected_original_datetime, value.size) == 0);
    }
    assert((replace_out.entries[duplicate_date].flags &
            (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert((replace_out.entries[stale_offset].flags &
            (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) ==
           (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED));
    assert(count_exif(&replace_out, 0x9291U,
                      OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED,
                      OMC_ENTRY_FLAG_NONE) == 1U);

    result = omc_translate_xmp(&replace_out, &repeated, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_unchanged == 1U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);

    omc_store_fini(&repeated);
    omc_store_fini(&replace_out);
    omc_store_fini(&preserve_out);
    omc_store_fini(&fail_out);
    omc_store_fini(&source);
}

static void
expect_failed_transaction_preserves(const omc_store *source, omc_store *out,
                                    const omc_translation_opts *opts,
                                    omc_translation_status expected_status,
                                    omc_u32 expected_mapping)
{
    test_store_snapshot source_snapshot;
    test_store_snapshot output_snapshot;
    omc_translation_res result;
    snapshot_store(&source_snapshot, source);
    snapshot_store(&output_snapshot, out);
    result = omc_translate_xmp(source, out, opts, NULL);
    assert(result.status == expected_status);
    assert(result.failed_mapping == expected_mapping);
    assert(result.groups_translated == 0U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    assert_store_unchanged(&source_snapshot, source);
    assert_store_unchanged(&output_snapshot, out);
    free_snapshot(&output_snapshot);
    free_snapshot(&source_snapshot);
}

static void
test_partial_creation_dates_reject_and_preserve_output(void)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp(&source, "CreateDate", "2024");
    add_xmp(&out, "Sentinel", "kept");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_EXIF;
    expect_failed_transaction_preserves(&source, &out, &opts,
                                        OMC_TRANSLATION_UNSUPPORTED_PRECISION,
                                        OMC_TRANSLATE_CREATE_EXIF);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp_property(&source, ns_exif, "DateTimeOriginal", "2024-02",
                     OMC_ENTRY_FLAG_DIRTY);
    add_xmp(&out, "Sentinel", "kept");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_ORIGINAL;
    expect_failed_transaction_preserves(&source, &out, &opts,
                                        OMC_TRANSLATION_UNSUPPORTED_PRECISION,
                                        OMC_TRANSLATE_DATE_ORIGINAL);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp_property(&source, ns_exif, "DateTimeOriginal", "2024-13",
                     OMC_ENTRY_FLAG_DIRTY);
    add_xmp(&out, "Sentinel", "kept");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_ORIGINAL;
    expect_failed_transaction_preserves(&source, &out, &opts,
                                        OMC_TRANSLATION_INVALID_SOURCE,
                                        OMC_TRANSLATE_DATE_ORIGINAL);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp(&source, "ModifyDate", "2024");
    add_xmp(&out, "Sentinel", "kept");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_MODIFY_DATE;
    expect_failed_transaction_preserves(&source, &out, &opts,
                                        OMC_TRANSLATION_INVALID_SOURCE,
                                        OMC_TRANSLATE_MODIFY_DATE);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
test_modify_date_and_iptc_only_reconciliations_stay_generic(void)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id modify_id;
    const omc_entry *entry;

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp(&source, "ModifyDate", "2024-02-29T03:04:05");
    modify_id = add_ifd0_text(&source, 0x0132U, expected_datetime,
                              OMC_ENTRY_FLAG_NONE);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_MODIFY_DATE;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_unchanged == 1U);
    assert(result.entries_updated == 0U);
    assert(result.entries_added == 0U);
    entry = find_exif_ifd(&out, "ifd0", 0x0132U, 0U);
    assert(entry != NULL);
    assert((entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
    assert(find_exif_ifd(&out, "exififd", 0x9010U, 0U) == NULL);
    assert(find_exif_ifd(&out, "exififd", 0x9290U, 0U) == NULL);
    assert(out.entries[modify_id].value.kind == OMC_VAL_TEXT);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp(&source, "CreateDate", "2024-02-29T03:04:05");
    add_iptc_bytes(&source, 62U, "20240229", OMC_ENTRY_FLAG_NONE);
    add_iptc_bytes(&source, 63U, "030405", OMC_ENTRY_FLAG_NONE);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_IPTC;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_unchanged == 1U);
    assert(result.entries_updated == 0U);
    assert(result.entries_added == 0U);
    entry = find_iptc(&out, 62U);
    assert(entry != NULL);
    assert((entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
    entry = find_iptc(&out, 63U);
    assert(entry != NULL);
    assert((entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U);
    assert(find_exif(&out, 0x9004U) == NULL);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
test_added_entry_limit_preserves_seeded_output(void)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    test_store_snapshot source_snapshot;
    test_store_snapshot output_snapshot;

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp_property(&source, ns_exif, "DateTimeOriginal",
                     "2020-01-02T03:04:05", OMC_ENTRY_FLAG_DIRTY);
    add_xmp(&out, "Sentinel", "keep exact output");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_ORIGINAL;
    opts.max_added_entries = 2U;
    snapshot_store(&source_snapshot, &source);
    snapshot_store(&output_snapshot, &out);
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_LIMIT);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_ORIGINAL);
    assert(result.groups_translated == 0U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    assert_store_unchanged(&source_snapshot, &source);
    assert_store_unchanged(&output_snapshot, &out);
    free_snapshot(&output_snapshot);
    free_snapshot(&source_snapshot);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
test_commit_failure_after_planning_preserves_output(void)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    test_store_snapshot source_snapshot;
    test_store_snapshot output_snapshot;
    omc_entry_id malformed_id;

    omc_store_init(&source);
    omc_store_init(&out);
    add_xmp_property(&source, ns_exif, "DateTimeOriginal",
                     "2020-01-02T03:04:05", OMC_ENTRY_FLAG_DIRTY);
    malformed_id = add_ifd0_text(&source, 0x010FU, "unrelated retained field",
                                 OMC_ENTRY_FLAG_NONE);
    source.entries[malformed_id].origin.wire_type_name.offset =
        (omc_u32)(source.arena.size + 1U);
    source.entries[malformed_id].origin.wire_type_name.size = 1U;
    add_xmp(&out, "Sentinel", "keep exact output");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_ORIGINAL;
    snapshot_store(&source_snapshot, &source);
    snapshot_store(&output_snapshot, &out);
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_INVALID_SOURCE);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_ORIGINAL);
    assert(result.groups_translated == 0U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    assert_store_unchanged(&source_snapshot, &source);
    assert_store_unchanged(&output_snapshot, &out);
    free_snapshot(&output_snapshot);
    free_snapshot(&source_snapshot);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
test_source_output_alias_precondition_preserves_source(void)
{
    omc_store source;
    omc_translation_opts opts;
    omc_translation_res result;
    test_store_snapshot source_snapshot;

    omc_store_init(&source);
    add_xmp(&source, "CreateDate", "2024-02-29T03:04:05");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_EXIF;
    snapshot_store(&source_snapshot, &source);
    result = omc_translate_xmp(&source, &source, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_INVALID_OPTIONS);
    assert(result.failed_mapping == 0U);
    assert(result.groups_translated == 0U);
    assert(result.entries_added == 0U);
    assert(result.entries_updated == 0U);
    assert(result.entries_removed == 0U);
    assert_store_unchanged(&source_snapshot, &source);
    free_snapshot(&source_snapshot);
    omc_store_fini(&source);
}

int
main(void)
{
    test_exact_native_value_promotes_and_absences_get_intents();
    test_deleted_marker_only_group_obeys_policy();
    test_both_creation_mappings_promote_full_clean_groups();
    test_deleted_group_promotes_only_first_clean_markers();
    test_deleted_group_tombstones_all_active_members();
    test_conflict_policies_and_duplicate_replacement();
    test_partial_creation_dates_reject_and_preserve_output();
    test_modify_date_and_iptc_only_reconciliations_stay_generic();
    test_added_entry_limit_preserves_seeded_output();
    test_commit_failure_after_planning_preserves_output();
    test_source_output_alias_precondition_preserves_source();
    return 0;
}
