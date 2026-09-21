#include "omc/omc_metadata_patch.h"
#include "omc_test_assert.h"

#include <string.h>

static const char k_exif_ns[] = "http://ns.adobe.com/exif/1.0/";

static omc_byte_ref
append_text(omc_store *store, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, text, strlen(text), &ref)
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

int
main(void)
{
    test_patch_plan_and_apply();
    return omc_test_finish();
}
