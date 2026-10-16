#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_translation.h"
#include <assert.h>
#include <string.h>

#define XMP_NS "http://ns.adobe.com/xap/1.0/"
#define EXIF_NS "http://ns.adobe.com/exif/1.0/"
#define PS_NS "http://ns.adobe.com/photoshop/1.0/"
#define TEST_SNAPSHOT_MAX_ENTRIES 16U
#define TEST_SNAPSHOT_MAX_ARENA 4096U

typedef struct test_store_snapshot {
    omc_store shallow;
    omc_entry entries[TEST_SNAPSHOT_MAX_ENTRIES];
    omc_u8 arena[TEST_SNAPSHOT_MAX_ARENA];
    omc_size entry_count;
    omc_size arena_size;
} test_store_snapshot;

static omc_byte_ref
append(omc_store *store, const void *bytes, omc_size size)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, bytes, size, &ref) == OMC_STATUS_OK);
    return ref;
}

static omc_entry_id
add_xmp_flags(omc_store *store, const char *ns, const char *path,
              const char *value, omc_entry_flags flags)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append(store, ns, strlen(ns)),
                              append(store, path, strlen(path)));
    omc_val_make_text(&entry.value, append(store, value, strlen(value)), OMC_TEXT_UTF8);
    entry.flags = flags;
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.order_in_block = (omc_u32)store->entry_count;
    entry.origin.wire_type_name = append(store, path, strlen(path));
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_xmp(omc_store *store, const char *ns, const char *path, const char *value)
{
    return add_xmp_flags(store, ns, path, value, OMC_ENTRY_FLAG_DIRTY);
}

static void
add_iptc(omc_store *store, omc_u16 dataset, const char *value)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, dataset);
    omc_val_make_bytes(&entry.value, append(store, value, strlen(value)));
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.order_in_block = (omc_u32)store->entry_count;
    entry.origin.wire_type_name = append(store, "IPTC", 4U);
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
}

static int
has_iptc(const omc_store *store, omc_u16 dataset, const char *value)
{
    omc_size i;
    omc_const_bytes bytes;
    const omc_entry *entry;
    omc_size size;
    size = strlen(value);
    for (i = 0U; i < store->entry_count; ++i) {
        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_IPTC_DATASET ||
            entry->key.u.iptc_dataset.record != 2U ||
            entry->key.u.iptc_dataset.dataset != dataset ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        bytes = omc_arena_view(&store->arena, entry->value.u.ref);
        if (bytes.size == size && memcmp(bytes.data, value, size) == 0)
            return 1;
    }
    return 0;
}

static int
has_exif_text(const omc_store *store, const char *ifd, omc_u16 tag,
              const char *value)
{
    omc_size i;
    omc_const_bytes ifd_bytes;
    omc_const_bytes text_bytes;
    const omc_entry *entry;
    omc_size ifd_size;
    omc_size text_size;
    ifd_size = strlen(ifd);
    text_size = strlen(value);
    for (i = 0U; i < store->entry_count; ++i) {
        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG ||
            entry->key.u.exif_tag.tag != tag ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            entry->value.kind != OMC_VAL_TEXT)
            continue;
        ifd_bytes = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_bytes.size != ifd_size ||
            memcmp(ifd_bytes.data, ifd, ifd_size) != 0)
            continue;
        text_bytes = omc_arena_view(&store->arena, entry->value.u.ref);
        if (text_bytes.size == text_size &&
            memcmp(text_bytes.data, value, text_size) == 0)
            return 1;
    }
    return 0;
}

static omc_size
iptc_count(const omc_store *store, omc_u16 dataset)
{
    omc_size i;
    omc_size count;
    count = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind == OMC_KEY_IPTC_DATASET &&
            store->entries[i].key.u.iptc_dataset.record == 2U &&
            store->entries[i].key.u.iptc_dataset.dataset == dataset &&
            (store->entries[i].flags & OMC_ENTRY_FLAG_DELETED) == 0U)
            ++count;
    }
    return count;
}

static void
take_snapshot(const omc_store *store, test_store_snapshot *snapshot)
{
    assert(store->entry_count <= TEST_SNAPSHOT_MAX_ENTRIES);
    assert(store->arena.size <= TEST_SNAPSHOT_MAX_ARENA);
    snapshot->shallow = *store;
    snapshot->entry_count = store->entry_count;
    snapshot->arena_size = store->arena.size;
    if (store->entry_count != 0U)
        memcpy(snapshot->entries, store->entries,
               store->entry_count * sizeof(*store->entries));
    if (store->arena.size != 0U)
        memcpy(snapshot->arena, store->arena.data, store->arena.size);
}

static void
assert_unchanged(const omc_store *store, const test_store_snapshot *snapshot)
{
    assert(memcmp(store, &snapshot->shallow, sizeof(*store)) == 0);
    assert(store->entry_count == snapshot->entry_count);
    assert(store->arena.size == snapshot->arena_size);
    if (store->entry_count != 0U)
        assert(memcmp(store->entries, snapshot->entries,
                      store->entry_count * sizeof(*store->entries)) == 0);
    if (store->arena.size != 0U)
        assert(memcmp(store->arena.data, snapshot->arena,
                      store->arena.size) == 0);
}

static void
check_partial_iptc(const char *ns, const char *path, const char *source_text,
                   omc_u32 mapping, omc_u16 dataset, const char *expected)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp(&source, ns, path, source_text);
    omc_translation_opts_init(&opts);
    opts.mappings = mapping;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 1U && result.entries_added == 1U);
    assert(has_iptc(&out, dataset, expected));
    assert(iptc_count(&out, dataset == 55U ? 60U : 63U) == 0U);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
check_full_iptc(const char *ns, const char *path, const char *source_text,
                omc_u32 mapping, omc_u16 date_dataset, const char *date_value,
                omc_u16 time_dataset, const char *time_value)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp(&source, ns, path, source_text);
    omc_translation_opts_init(&opts);
    opts.mappings = mapping;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 1U);
    assert(has_iptc(&out, date_dataset, date_value));
    if (time_value == NULL)
        assert(iptc_count(&out, time_dataset) == 0U);
    else
        assert(has_iptc(&out, time_dataset, time_value));
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
check_late_partial_exif_failure_is_atomic(void)
{
    omc_store source;
    omc_store out;
    test_store_snapshot source_before;
    test_store_snapshot out_before;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id failing_source;

    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp(&source, XMP_NS, "CreateDate", "2024-09-30");
    failing_source = add_xmp(&source, EXIF_NS, "DateTimeOriginal", "2025");
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&source, &source_before);
    take_snapshot(&out, &out_before);

    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_IPTC | OMC_TRANSLATE_DATE_ORIGINAL;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_UNSUPPORTED_PRECISION);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_ORIGINAL);
    assert(result.failed_source == failing_source);
    assert(result.groups_translated == 0U);
    assert(result.entries_added == 0U && result.entries_updated == 0U &&
           result.entries_removed == 0U);
    assert_unchanged(&source, &source_before);
    assert_unchanged(&out, &out_before);
    assert(has_iptc(&out, 5U, "sentinel"));
    assert(iptc_count(&out, 62U) == 0U);

    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
check_partial_exif_and_iptc_selection_fails_atomically(void)
{
    omc_store source;
    omc_store out;
    test_store_snapshot out_before;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id source_id;

    omc_store_init(&source);
    omc_store_init(&out);
    source_id = add_xmp(&source, XMP_NS, "CreateDate", "2025-09");
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&out, &out_before);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_EXIF | OMC_TRANSLATE_CREATE_IPTC;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_UNSUPPORTED_PRECISION);
    assert(result.failed_mapping == OMC_TRANSLATE_CREATE_EXIF);
    assert(result.failed_source == source_id);
    assert(result.groups_translated == 0U && result.entries_added == 0U &&
           result.entries_updated == 0U && result.entries_removed == 0U);
    assert_unchanged(&out, &out_before);

    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
check_malformed_creation_dates(void)
{
    static const char *const invalid[] = {
        "0000",          "0000-01",        "2024-00",      "2024-13",
        "2024-09-00",    "2024-09T01:02:03Z", "2024-09x",    "2024x",
        " 2024",         "2024 ",          "2024/09",      "2024-9",
        "2024-09.1"};
    omc_size i;
    omc_store source;
    omc_store out;
    test_store_snapshot out_before;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id source_id;

    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        omc_store_init(&source);
        omc_store_init(&out);
        source_id = add_xmp(&source, PS_NS, "DateCreated", invalid[i]);
        add_iptc(&out, 5U, "sentinel");
        take_snapshot(&out, &out_before);
        omc_translation_opts_init(&opts);
        opts.mappings = OMC_TRANSLATE_DATE_CREATED;
        result = omc_translate_xmp(&source, &out, &opts, NULL);
        assert(result.status == OMC_TRANSLATION_INVALID_SOURCE);
        assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
        assert(result.failed_source == source_id);
        assert(result.groups_translated == 0U && result.entries_added == 0U &&
               result.entries_updated == 0U && result.entries_removed == 0U);
        assert_unchanged(&out, &out_before);
        omc_store_fini(&out);
        omc_store_fini(&source);
    }
}

static void
check_modify_date_remains_full_precision_only(void)
{
    omc_store source;
    omc_store out;
    test_store_snapshot out_before;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id source_id;

    omc_store_init(&source);
    omc_store_init(&out);
    source_id = add_xmp(&source, XMP_NS, "ModifyDate",
                        "2024-06-07T08:09:10Z");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_MODIFY_DATE;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(has_exif_text(&out, "ifd0", 0x0132U,
                         "2024:06:07 08:09:10"));
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    source_id = add_xmp(&source, XMP_NS, "ModifyDate", "2024");
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&out, &out_before);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_MODIFY_DATE;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_INVALID_SOURCE);
    assert(result.failed_mapping == OMC_TRANSLATE_MODIFY_DATE);
    assert(result.failed_source == source_id);
    assert_unchanged(&out, &out_before);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
check_conflict_policies_and_stale_time_removal(void)
{
    static const omc_translation_conflict policies[] = {
        OMC_TRANSLATION_PRESERVE,
        OMC_TRANSLATION_FAIL,
        OMC_TRANSLATION_REPLACE};
    omc_size i;
    omc_store source;
    omc_store out;
    test_store_snapshot out_before;
    omc_translation_opts opts;
    omc_translation_res result;

    for (i = 0U; i < sizeof(policies) / sizeof(policies[0]); ++i) {
        omc_store_init(&source);
        omc_store_init(&out);
        (void)add_xmp(&source, PS_NS, "DateCreated", "2025-09");
        add_iptc(&source, 55U, "20200102");
        add_iptc(&source, 60U, "010203");
        add_iptc(&out, 5U, "sentinel");
        take_snapshot(&out, &out_before);
        omc_translation_opts_init(&opts);
        opts.mappings = OMC_TRANSLATE_DATE_CREATED;
        opts.conflict = policies[i];
        result = omc_translate_xmp(&source, &out, &opts, NULL);
        if (policies[i] == OMC_TRANSLATION_PRESERVE) {
            assert(result.status == OMC_TRANSLATION_OK);
            assert(result.groups_preserved == 1U);
            assert(has_iptc(&out, 55U, "20200102"));
            assert(has_iptc(&out, 60U, "010203"));
        } else if (policies[i] == OMC_TRANSLATION_FAIL) {
            assert(result.status == OMC_TRANSLATION_NATIVE_CONFLICT);
            assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
            assert_unchanged(&out, &out_before);
        } else {
            assert(result.status == OMC_TRANSLATION_OK);
            assert(result.entries_updated == 1U && result.entries_removed == 1U);
            assert(has_iptc(&out, 55U, "20250900"));
            assert(iptc_count(&out, 60U) == 0U);
        }
        omc_store_fini(&out);
        omc_store_fini(&source);
    }
}

static void
check_dirty_only_all_and_deleted_source(void)
{
    omc_store source;
    omc_store out;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id deleted_id;

    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp_flags(&source, PS_NS, "DateCreated", "2025-09",
                        OMC_ENTRY_FLAG_NONE);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_CREATED;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 0U && iptc_count(&out, 55U) == 0U);
    opts.all_sources = 1;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.groups_translated == 1U);
    assert(has_iptc(&out, 55U, "20250900"));
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    deleted_id = add_xmp_flags(&source, PS_NS, "DateCreated", "deleted payload",
                               OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
    (void)deleted_id;
    add_iptc(&source, 55U, "20200102");
    add_iptc(&source, 60U, "010203");
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_CREATED;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    assert(result.entries_removed == 2U);
    assert(iptc_count(&out, 55U) == 0U && iptc_count(&out, 60U) == 0U);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
check_options_alias_and_budget_failures(void)
{
    omc_store source;
    omc_store out;
    test_store_snapshot source_before;
    test_store_snapshot out_before;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_entry_id later_source;

    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp(&source, PS_NS, "DateCreated", "2024");
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&source, &source_before);
    take_snapshot(&out, &out_before);
    omc_translation_opts_init(&opts);
    opts.mappings |= 0x400000U;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_INVALID_OPTIONS);
    assert_unchanged(&source, &source_before);
    assert_unchanged(&out, &out_before);
    opts.mappings = OMC_TRANSLATE_DATE_CREATED;
    result = omc_translate_xmp(&source, &source, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_INVALID_OPTIONS);
    assert_unchanged(&source, &source_before);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp(&source, XMP_NS, "CreateDate", "2024");
    later_source = add_xmp(&source, PS_NS, "DateCreated", "2024");
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&out, &out_before);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_IPTC | OMC_TRANSLATE_DATE_CREATED;
    opts.max_text_bytes_per_property = 4U;
    opts.max_total_text_bytes = 7U;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_LIMIT);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
    assert(result.failed_source == later_source);
    assert(result.groups_translated == 0U && result.entries_added == 0U &&
           result.entries_updated == 0U && result.entries_removed == 0U);
    assert_unchanged(&out, &out_before);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp(&source, XMP_NS, "CreateDate", "2024");
    (void)add_xmp(&source, XMP_NS, "CreateDate", "2025");
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&out, &out_before);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_IPTC;
    opts.max_source_properties = 1U;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_LIMIT);
    assert(result.failed_mapping == OMC_TRANSLATE_CREATE_IPTC);
    assert(result.groups_translated == 0U && result.entries_added == 0U &&
           result.entries_updated == 0U && result.entries_removed == 0U);
    assert_unchanged(&out, &out_before);
    omc_store_fini(&out);
    omc_store_fini(&source);

    omc_store_init(&source);
    omc_store_init(&out);
    (void)add_xmp(&source, XMP_NS, "CreateDate", "2025-09-01T12:30:45Z");
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&out, &out_before);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_CREATE_IPTC;
    opts.max_operations = 1U;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_LIMIT);
    assert(result.groups_translated == 0U && result.entries_added == 0U &&
           result.entries_updated == 0U && result.entries_removed == 0U);
    assert_unchanged(&out, &out_before);
    opts.max_operations = 4096U;
    opts.max_added_entries = 1U;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_LIMIT);
    assert(result.groups_translated == 0U && result.entries_added == 0U &&
           result.entries_updated == 0U && result.entries_removed == 0U);
    assert_unchanged(&out, &out_before);
    omc_store_fini(&out);
    omc_store_fini(&source);
}

static void
check_unrelated_malformed_entry_commit_failure(void)
{
    omc_store source;
    omc_store out;
    test_store_snapshot source_before;
    test_store_snapshot out_before;
    omc_entry bad_entry;
    omc_entry_id selected_source;
    omc_byte_ref bad_ref;
    omc_translation_opts opts;
    omc_translation_res result;

    omc_store_init(&source);
    omc_store_init(&out);
    selected_source = add_xmp(&source, PS_NS, "DateCreated", "2025-09");
    memset(&bad_entry, 0, sizeof(bad_entry));
    omc_key_make_iptc_dataset(&bad_entry.key, 2U, 5U);
    bad_ref.offset = (omc_u32)source.arena.size + 10U;
    bad_ref.size = 4U;
    omc_val_make_bytes(&bad_entry.value, bad_ref);
    bad_entry.origin.block = OMC_INVALID_BLOCK_ID;
    bad_entry.origin.wire_type_name = append(&source, "retained", 8U);
    assert(omc_store_add_entry(&source, &bad_entry, NULL) == OMC_STATUS_OK);
    add_iptc(&out, 5U, "sentinel");
    take_snapshot(&source, &source_before);
    take_snapshot(&out, &out_before);

    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_CREATED;
    result = omc_translate_xmp(&source, &out, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_INVALID_SOURCE);
    assert(result.failed_mapping == OMC_TRANSLATE_DATE_CREATED);
    assert(result.failed_source == selected_source);
    assert(result.groups_translated == 0U && result.entries_added == 0U &&
           result.entries_updated == 0U && result.entries_removed == 0U);
    assert_unchanged(&source, &source_before);
    assert_unchanged(&out, &out_before);
    assert(has_iptc(&out, 5U, "sentinel"));

    omc_store_fini(&out);
    omc_store_fini(&source);
}

int
main(void)
{
    check_partial_iptc(PS_NS, "DateCreated", "2024", OMC_TRANSLATE_DATE_CREATED,
                       55U, "20240000");
    check_partial_iptc(PS_NS, "DateCreated", "2025-09", OMC_TRANSLATE_DATE_CREATED,
                       55U, "20250900");
    check_partial_iptc(XMP_NS, "CreateDate", "2024", OMC_TRANSLATE_CREATE_IPTC,
                       62U, "20240000");
    check_partial_iptc(XMP_NS, "CreateDate", "2025-09", OMC_TRANSLATE_CREATE_IPTC,
                       62U, "20250900");
    check_full_iptc(PS_NS, "DateCreated", "2024-02-29",
                    OMC_TRANSLATE_DATE_CREATED, 55U, "20240229", 60U, NULL);
    check_full_iptc(XMP_NS, "CreateDate", "2025-09-08T06:07:08Z",
                    OMC_TRANSLATE_CREATE_IPTC, 62U, "20250908", 63U,
                    "060708+0000");
    check_late_partial_exif_failure_is_atomic();
    check_partial_exif_and_iptc_selection_fails_atomically();
    check_malformed_creation_dates();
    check_modify_date_remains_full_precision_only();
    check_conflict_policies_and_stale_time_removal();
    check_dirty_only_all_and_deleted_source();
    check_options_alias_and_budget_failures();
    check_unrelated_malformed_entry_commit_failure();
    return 0;
}
