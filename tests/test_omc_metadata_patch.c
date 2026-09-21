#include "omc/omc_metadata_patch.h"
#include "omc_test_assert.h"

#include <string.h>

#if defined(OMC_PATCH_TEST_WRAP_HEAP)
static int omc_patch_count_allocations;
static omc_size omc_patch_allocations;
void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* pointer, size_t size);
void*
__wrap_malloc(size_t size)
{
    if (omc_patch_count_allocations)
        ++omc_patch_allocations;
    return __real_malloc(size);
}
void*
__wrap_calloc(size_t count, size_t size)
{
    if (omc_patch_count_allocations)
        ++omc_patch_allocations;
    return __real_calloc(count, size);
}
void*
__wrap_realloc(void* pointer, size_t size)
{
    if (omc_patch_count_allocations)
        ++omc_patch_allocations;
    return __real_realloc(pointer, size);
}
#endif

static const char k_exif_ns[] = "http://ns.adobe.com/exif/1.0/";

static omc_byte_ref
append_text(omc_store *store, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, text, strlen(text), &ref)
           == OMC_STATUS_OK);
    return ref;
}

static omc_byte_ref
append_raw(omc_store *store, const void *data, omc_size size)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, data, size, &ref)
           == OMC_STATUS_OK);
    return ref;
}

static omc_entry_id
add_xmp(omc_store *store, const char *path, const char *value)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(store, k_exif_ns),
                              append_text(store, path));
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_xmp_namespace(omc_store *store, const char *schema, const char *path,
                  const char *value)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(store, schema),
                              append_text(store, path));
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_native(omc_store *store, omc_u16 tag, const char *value)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(store, "ifd0"), tag);
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_ASCII);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 2U;
    entry.origin.wire_count = (omc_u32)strlen(value) + 1U;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
add_native_array(omc_store *store, omc_u16 tag, const omc_u16 *values,
                 omc_u32 count)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(store, "ifd0"), tag);
    omc_val_make_array(&entry.value, OMC_ELEM_U16, count,
                       append_raw(store, values, (omc_size)count * sizeof(*values)),
                       OMC_BYTE_ORDER_NATIVE);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

typedef struct replay_state {
    omc_u32 calls;
    omc_metadata_patch_payload family[2];
    omc_u64 size[2];
} replay_state;

static int
contains_bytes(omc_const_bytes haystack, const char *needle)
{
    omc_size needle_size = strlen(needle);
    omc_size i;
    if (needle_size > haystack.size) {
        return 0;
    }
    for (i = 0U; i + needle_size <= haystack.size; ++i) {
        if (memcmp(haystack.data + i, needle, needle_size) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
replay(void *user, omc_metadata_patch_payload family, omc_const_bytes payload)
{
    replay_state *state = (replay_state *)user;
    assert(state->calls < 2U);
    state->family[state->calls] = family;
    state->size[state->calls] = payload.size;
    ++state->calls;
    return 1;
}

static void
test_patch_plan_and_apply(void)
{
    omc_store store;
    omc_metadata_patch_opts opts;
    omc_metadata_patch_plan plan;
    omc_metadata_patch_instance instance;
    omc_metadata_patch_request requests[2];
    omc_metadata_patch_handle handles[2];
    omc_metadata_patch_update updates[2];
    omc_metadata_patch_result result;
    omc_entry_id xmp_id;
    omc_entry_id native_id;
    omc_val xmp_value;
    omc_val native_value;
    omc_byte_ref value_ref;
    omc_const_bytes payload;
    replay_state replay_result;
    const char replacement_xmp[] = "C & D";
    const char replacement_native[] = "New";

    omc_store_init(&store);
    xmp_id = add_xmp(&store, "UserComment", "A & B");
    native_id = add_native(&store, 0x010FU, "Cam");
    omc_metadata_patch_opts_init(&opts);
    opts.plan_id = 37U;
    opts.xmp.include_existing_xmp = 1;
    omc_metadata_patch_plan_init(&plan);
    omc_metadata_patch_instance_init(&instance);
    memset(requests, 0, sizeof(requests));
    requests[0].key = store.entries[xmp_id].key;
    requests[0].occurrence = 0U;
    requests[0].expected.kind = OMC_VAL_TEXT;
    requests[0].expected.elem_type = OMC_ELEM_U8;
    requests[0].expected.text_encoding = OMC_TEXT_UTF8;
    requests[0].expected.count = 5U;
    requests[0].escaped_width = 9U;
    requests[1].key = store.entries[native_id].key;
    requests[1].occurrence = 0U;
    requests[1].expected.kind = OMC_VAL_TEXT;
    requests[1].expected.elem_type = OMC_ELEM_U8;
    requests[1].expected.text_encoding = OMC_TEXT_ASCII;
    requests[1].expected.count = 3U;
    result = omc_metadata_patch_prepare(&store, requests, 2U, &opts,
                                        handles, 2U, &plan);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    assert(result.handle_count == 2U);
    assert(omc_metadata_patch_plan_payload(&plan, OMC_METADATA_PATCH_XMP).size
           != 0U);
    result = omc_metadata_patch_instance_create(&plan, &instance);
    assert(result.code == OMC_METADATA_PATCH_NONE);

    value_ref = append_text(&store, replacement_xmp);
    omc_val_make_text(&xmp_value, value_ref, OMC_TEXT_UTF8);
    updates[0].handle = handles[0];
    updates[0].value = &xmp_value;
    updates[0].arena = &store.arena;
    value_ref = append_text(&store, replacement_native);
    omc_val_make_text(&native_value, value_ref, OMC_TEXT_ASCII);
    updates[1].handle = handles[1];
    updates[1].value = &native_value;
    updates[1].arena = &store.arena;
    result = omc_metadata_patch_apply(&instance, updates, 2U);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    payload = omc_metadata_patch_instance_payload(&instance,
                                                   OMC_METADATA_PATCH_XMP);
    assert(payload.size != 0U);
    assert(contains_bytes(payload, "C &amp; D"));
    payload = omc_metadata_patch_instance_payload(&instance,
                                                   OMC_METADATA_PATCH_EXIF_TIFF);
    assert(payload.size != 0U);
    assert(contains_bytes(payload, "New"));
    memset(&replay_result, 0, sizeof(replay_result));
    result = omc_metadata_patch_replay(&instance, replay, &replay_result);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    assert(replay_result.calls == 2U);
    assert(replay_result.family[0] == OMC_METADATA_PATCH_EXIF_TIFF);
    assert(replay_result.family[1] == OMC_METADATA_PATCH_XMP);

    omc_metadata_patch_instance_reset(&instance);
    omc_metadata_patch_plan_reset(&plan);
    omc_store_fini(&store);
}

static void
test_patch_transaction_workers_and_aliases(void)
{
    omc_store store;
    omc_metadata_patch_opts opts;
    omc_metadata_patch_plan plan;
    omc_metadata_patch_plan second_plan;
    omc_metadata_patch_instance first;
    omc_metadata_patch_instance second;
    omc_metadata_patch_request requests[2];
    omc_metadata_patch_handle handles[2];
    omc_metadata_patch_handle second_handles[2];
    omc_metadata_patch_update updates[2];
    omc_metadata_patch_result result;
    omc_entry_id xmp_id;
    omc_entry_id native_id;
    omc_val xmp_value;
    omc_val native_value;
    omc_val alias_xmp_value;
    omc_val alias_native_value;
    omc_byte_ref ref;
    omc_const_bytes before_xmp;
    omc_const_bytes before_exif;
    omc_const_bytes first_xmp;
    omc_const_bytes first_exif;
    omc_u8 xmp_copy[1024];
    omc_u8 exif_copy[1024];
    omc_arena alias_arena;
    omc_metadata_patch_update alias_update;
    omc_metadata_patch_replay_fn callback;
    const char replacement_xmp[] = "C & D";
    const char replacement_native[] = "New";
    const char short_native[] = "x";
    omc_u32 i;

    omc_store_init(&store);
    xmp_id = add_xmp(&store, "UserComment", "A & B");
    native_id = add_native(&store, 0x010FU, "Cam");
    omc_metadata_patch_opts_init(&opts);
    opts.plan_id = 41U;
    opts.xmp.include_existing_xmp = 1;
    omc_metadata_patch_plan_init(&plan);
    omc_metadata_patch_plan_init(&second_plan);
    omc_metadata_patch_instance_init(&first);
    omc_metadata_patch_instance_init(&second);
    memset(requests, 0, sizeof(requests));
    requests[0].key = store.entries[xmp_id].key;
    requests[0].expected.kind = OMC_VAL_TEXT;
    requests[0].expected.elem_type = OMC_ELEM_U8;
    requests[0].expected.text_encoding = OMC_TEXT_UTF8;
    requests[0].expected.count = 5U;
    requests[0].escaped_width = 9U;
    requests[1].key = store.entries[native_id].key;
    requests[1].expected.kind = OMC_VAL_TEXT;
    requests[1].expected.elem_type = OMC_ELEM_U8;
    requests[1].expected.text_encoding = OMC_TEXT_ASCII;
    requests[1].expected.count = 3U;
    result = omc_metadata_patch_prepare(&store, requests, 2U, &opts,
                                        handles, 2U, &plan);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    result = omc_metadata_patch_instance_create(&plan, &first);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    result = omc_metadata_patch_instance_create(&plan, &second);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    first_xmp = omc_metadata_patch_instance_payload(&first,
                                                    OMC_METADATA_PATCH_XMP);
    first_exif = omc_metadata_patch_instance_payload(
        &first, OMC_METADATA_PATCH_EXIF_TIFF);
    assert(first_xmp.size < sizeof(xmp_copy) && first_exif.size < sizeof(exif_copy));

    memcpy(xmp_copy, first_xmp.data, first_xmp.size);
    memcpy(exif_copy, first_exif.data, first_exif.size);
    before_xmp.data = xmp_copy;
    before_xmp.size = first_xmp.size;
    before_exif.data = exif_copy;
    before_exif.size = first_exif.size;

    ref = append_text(&store, replacement_xmp);
    omc_val_make_text(&xmp_value, ref, OMC_TEXT_UTF8);
    updates[0].handle = handles[0];
    updates[0].value = &xmp_value;
    updates[0].arena = &store.arena;
    ref = append_text(&store, short_native);
    omc_val_make_text(&native_value, ref, OMC_TEXT_ASCII);
    updates[1].handle = handles[1];
    updates[1].value = &native_value;
    updates[1].arena = &store.arena;
    result = omc_metadata_patch_apply(&first, updates, 2U);
    assert(result.code == OMC_METADATA_PATCH_VALUE_TYPE_MISMATCH);
    first_xmp = omc_metadata_patch_instance_payload(&first,
                                                    OMC_METADATA_PATCH_XMP);
    first_exif = omc_metadata_patch_instance_payload(
        &first, OMC_METADATA_PATCH_EXIF_TIFF);
    assert(memcmp(first_xmp.data, before_xmp.data, before_xmp.size) == 0);
    assert(memcmp(first_exif.data, before_exif.data, before_exif.size) == 0);

    ref = append_text(&store, replacement_native);
    omc_val_make_text(&native_value, ref, OMC_TEXT_ASCII);
    updates[1].value = &native_value;
    result = omc_metadata_patch_apply(&first, updates, 2U);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    first_xmp = omc_metadata_patch_instance_payload(&first,
                                                    OMC_METADATA_PATCH_XMP);
    first_exif = omc_metadata_patch_instance_payload(
        &first, OMC_METADATA_PATCH_EXIF_TIFF);
    assert(contains_bytes(first_xmp, "C &amp; D"));
    assert(memcmp(first_xmp.data, before_xmp.data, before_xmp.size) != 0);

    omc_arena_init(&alias_arena);
    alias_arena.data = (omc_u8 *)(void *)first_xmp.data;
    alias_arena.size = first_xmp.size;
    alias_arena.capacity = first_xmp.size;
    ref.offset = 0U;
    ref.size = 3U;
    omc_val_make_text(&alias_native_value, ref, OMC_TEXT_ASCII);
    alias_update.handle = handles[1];
    alias_update.value = &alias_native_value;
    alias_update.arena = &alias_arena;
    result = omc_metadata_patch_apply(&first, &alias_update, 1U);
    assert(result.code == OMC_METADATA_PATCH_VALUE_ALIASES_INSTANCE);

    ref.size = 5U;
    omc_val_make_text(&alias_xmp_value, ref, OMC_TEXT_UTF8);
    alias_update.handle = handles[0];
    alias_update.value = &alias_xmp_value;
    result = omc_metadata_patch_apply(&first, &alias_update, 1U);
    assert(result.code == OMC_METADATA_PATCH_VALUE_ALIASES_INSTANCE);

    updates[0].handle = handles[0];
    updates[0].value = &xmp_value;
    updates[0].arena = &store.arena;
    updates[1] = updates[0];
    result = omc_metadata_patch_apply(&first, updates, 2U);
    assert(result.code == OMC_METADATA_PATCH_DUPLICATE_HANDLE);

    omc_metadata_patch_opts_init(&opts);
    opts.plan_id = 42U;
    opts.xmp.include_existing_xmp = 1;
    result = omc_metadata_patch_prepare(&store, requests, 2U, &opts,
                                        second_handles, 2U, &second_plan);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    updates[0].handle = second_handles[0];
    result = omc_metadata_patch_apply(&first, updates, 1U);
    assert(result.code == OMC_METADATA_PATCH_FOREIGN_HANDLE);

    callback = (omc_metadata_patch_replay_fn)0;
    result = omc_metadata_patch_replay(&first, callback, (void *)0);
    assert(result.code == OMC_METADATA_PATCH_NULL_REPLAY_CALLBACK);

    omc_metadata_patch_plan_reset(&plan);
    first_xmp = omc_metadata_patch_instance_payload(&first,
                                                    OMC_METADATA_PATCH_XMP);
    first_exif = omc_metadata_patch_instance_payload(
        &first, OMC_METADATA_PATCH_EXIF_TIFF);
#if defined(OMC_PATCH_TEST_WRAP_HEAP)
    omc_patch_allocations = 0U;
    omc_patch_count_allocations = 1;
#endif
    for (i = 0U; i < 1000U; ++i) {
        updates[0].handle = handles[0];
        updates[0].value = &xmp_value;
        updates[0].arena = &store.arena;
        result = omc_metadata_patch_apply(&first, updates, 1U);
        assert(result.code == OMC_METADATA_PATCH_NONE);
        assert(omc_metadata_patch_instance_payload(&first,
                                                   OMC_METADATA_PATCH_XMP).data
               == first_xmp.data);
        assert(omc_metadata_patch_instance_payload(&first,
                                                   OMC_METADATA_PATCH_EXIF_TIFF).data
               == first_exif.data);
    }
#if defined(OMC_PATCH_TEST_WRAP_HEAP)
    omc_patch_count_allocations = 0;
    assert(omc_patch_allocations == 0U);
#endif
    {
        replay_state replay_result;
        memset(&replay_result, 0, sizeof(replay_result));
#if defined(OMC_PATCH_TEST_WRAP_HEAP)
        omc_patch_allocations = 0U;
        omc_patch_count_allocations = 1;
#endif
        result = omc_metadata_patch_replay(&first, replay, &replay_result);
#if defined(OMC_PATCH_TEST_WRAP_HEAP)
        omc_patch_count_allocations = 0;
        assert(omc_patch_allocations == 0U);
#endif
        assert(result.code == OMC_METADATA_PATCH_NONE);
        assert(replay_result.calls == 2U);
    }
    assert(omc_metadata_patch_instance_payload(&second,
                                               OMC_METADATA_PATCH_XMP).data
           != first_xmp.data);

    omc_metadata_patch_instance_reset(&second);
    omc_metadata_patch_instance_reset(&first);
    omc_metadata_patch_plan_reset(&second_plan);
    omc_store_fini(&store);
}

static void
test_patch_xmp_prefix_and_variable_width(void)
{
    omc_store store;
    omc_metadata_patch_opts opts;
    omc_metadata_patch_plan plan;
    omc_metadata_patch_instance instance;
    omc_metadata_patch_request request;
    omc_metadata_patch_handle handle;
    omc_metadata_patch_update update;
    omc_metadata_patch_result result;
    omc_entry_id other_id;
    omc_entry_id value_id;
    omc_val value;
    omc_byte_ref ref;
    omc_const_bytes payload;
    const char initial[] = "000000000000000000000000000000000000";
    const char logical[] = "&<>\"'\r\xC3\xA9\xF0\x9F\x98\x80";
    const char encoded[] = "&amp;&lt;&gt;&quot;&apos;&#xD;\xC3\xA9\xF0\x9F\x98\x80";

    omc_store_init(&store);
    other_id = add_xmp_namespace(&store, "https://a.example.test/",
                                 "Other", "static");
    value_id = add_xmp_namespace(&store, "https://example.test/capture/1.0/",
                                 "Value", initial);
    omc_metadata_patch_opts_init(&opts);
    opts.plan_id = 51U;
    opts.xmp.include_existing_xmp = 1;
    opts.xmp.existing_namespace_policy = OMC_XMP_NS_PRESERVE_CUSTOM;
    omc_metadata_patch_plan_init(&plan);
    omc_metadata_patch_instance_init(&instance);
    memset(&request, 0, sizeof(request));
    request.key = store.entries[value_id].key;
    request.expected.kind = OMC_VAL_TEXT;
    request.expected.elem_type = OMC_ELEM_U8;
    request.expected.text_encoding = OMC_TEXT_UTF8;
    request.expected.count = (omc_u32)strlen(initial);
    request.escaped_width = (omc_u32)strlen(encoded);
    result = omc_metadata_patch_prepare(&store, &request, 1U, &opts,
                                        &handle, 1U, &plan);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    result = omc_metadata_patch_instance_create(&plan, &instance);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    ref = append_text(&store, logical);
    omc_val_make_text(&value, ref, OMC_TEXT_UTF8);
    update.handle = handle;
    update.value = &value;
    update.arena = &store.arena;
    result = omc_metadata_patch_apply(&instance, &update, 1U);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    payload = omc_metadata_patch_instance_payload(&instance,
                                                  OMC_METADATA_PATCH_XMP);
    assert(contains_bytes(payload, encoded));
    assert(contains_bytes(payload, "static"));
    assert(store.entries[other_id].key.kind == OMC_KEY_XMP_PROPERTY);
    omc_metadata_patch_instance_reset(&instance);
    omc_metadata_patch_plan_reset(&plan);
    omc_store_fini(&store);
}

static void
test_patch_typed_array(void)
{
    omc_store store;
    omc_metadata_patch_opts opts;
    omc_metadata_patch_plan plan;
    omc_metadata_patch_instance instance;
    omc_metadata_patch_request request;
    omc_metadata_patch_handle handle;
    omc_metadata_patch_update update;
    omc_metadata_patch_result result;
    omc_entry_id id;
    omc_u16 initial[3] = { 1U, 2U, 3U };
    omc_u16 replacement[3] = { 4U, 5U, 6U };
    omc_val value;
    omc_byte_ref ref;
    omc_const_bytes before;
    omc_const_bytes after;
    omc_u8 snapshot[1024];

    omc_store_init(&store);
    id = add_native_array(&store, 0xF001U, initial, 3U);
    omc_metadata_patch_opts_init(&opts);
    opts.plan_id = 61U;
    omc_metadata_patch_plan_init(&plan);
    omc_metadata_patch_instance_init(&instance);
    memset(&request, 0, sizeof(request));
    request.key = store.entries[id].key;
    request.expected.kind = OMC_VAL_ARRAY;
    request.expected.elem_type = OMC_ELEM_U16;
    request.expected.count = 3U;
    result = omc_metadata_patch_prepare(&store, &request, 1U, &opts,
                                        &handle, 1U, &plan);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    result = omc_metadata_patch_instance_create(&plan, &instance);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    before = omc_metadata_patch_instance_payload(&instance,
                                                 OMC_METADATA_PATCH_EXIF_TIFF);
    assert(before.size < sizeof(snapshot));
    memcpy(snapshot, before.data, before.size);
    ref = append_raw(&store, replacement, sizeof(replacement));
    omc_val_make_array(&value, OMC_ELEM_U16, 3U, ref, OMC_BYTE_ORDER_NATIVE);
    update.handle = handle;
    update.value = &value;
    update.arena = &store.arena;
    result = omc_metadata_patch_apply(&instance, &update, 1U);
    assert(result.code == OMC_METADATA_PATCH_NONE);
    after = omc_metadata_patch_instance_payload(&instance,
                                                OMC_METADATA_PATCH_EXIF_TIFF);
    assert(after.size == before.size);
    assert(memcmp(after.data, snapshot, before.size) != 0);
    omc_metadata_patch_instance_reset(&instance);
    omc_metadata_patch_plan_reset(&plan);
    omc_store_fini(&store);
}

int
main(void)
{
    test_patch_plan_and_apply();
    test_patch_transaction_workers_and_aliases();
    test_patch_xmp_prefix_and_variable_width();
    test_patch_typed_array();
    return omc_test_finish();
}
