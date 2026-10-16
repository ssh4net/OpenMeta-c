#ifdef NDEBUG
#undef NDEBUG
#endif

#include "omc/omc_iptc_serialize.h"
#include "../src/edit/omc_iptc_serialize_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
add_value_dataset(omc_store *store, omc_u16 record, omc_u16 dataset,
                  omc_u32 order, const omc_val *value,
                  omc_entry_flags flags)
{
    omc_entry entry;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, record, dataset);
    entry.value = *value;
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.order_in_block = order;
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static omc_val
make_bytes_value(omc_store *store, const omc_u8 *bytes, omc_size size)
{
    omc_val value;
    omc_byte_ref ref;

    assert(omc_arena_append(&store->arena, bytes, size, &ref)
           == OMC_STATUS_OK);
    omc_val_make_bytes(&value, ref);
    return value;
}

static omc_val
make_text_value(omc_store *store, const omc_u8 *bytes, omc_size size)
{
    omc_val value;
    omc_byte_ref ref;

    assert(omc_arena_append(&store->arena, bytes, size, &ref)
           == OMC_STATUS_OK);
    omc_val_make_text(&value, ref, OMC_TEXT_UTF8);
    return value;
}

static omc_val
make_array_value(omc_store *store, omc_elem_type type, omc_u32 count,
                 const omc_u8 *bytes, omc_size size,
                 omc_byte_order byte_order)
{
    omc_val value;
    omc_byte_ref ref;

    assert(omc_arena_append(&store->arena, bytes, size, &ref)
           == OMC_STATUS_OK);
    omc_val_make_array(&value, type, count, ref, byte_order);
    return value;
}

static void
append_expected_dataset(omc_u8 *output, omc_size *size, omc_u8 record,
                        omc_u8 dataset, const omc_u8 *payload,
                        omc_size payload_size)
{
    output[*size] = 0x1CU;
    output[*size + 1U] = record;
    output[*size + 2U] = dataset;
    output[*size + 3U] = (omc_u8)(payload_size >> 8U);
    output[*size + 4U] = (omc_u8)payload_size;
    *size += 5U;
    if (payload_size != 0U) {
        memcpy(output + *size, payload, payload_size);
        *size += payload_size;
    }
}

static void
test_canonical_order_duplicates_and_raw_array(void)
{
    static const omc_u8 late[] = { 'l', 'a', 't', 'e' };
    static const omc_u8 charset[] = { 'U', 'T', 'F', '8' };
    static const omc_u8 duplicate_a[] = { 'd', 'u', 'p', 'A' };
    static const omc_u8 middle[] = { 'm', 'i', 'd', 'd', 'l', 'e' };
    static const omc_u8 duplicate_b[] = { 'd', 'u', 'p', 'B' };
    static const omc_u8 early[] = { 'e', 'a', 'r', 'l', 'y' };
    static const omc_u8 raw_array[] = { 0x12U, 0x34U };
    omc_store store;
    omc_iptc_iim_serialize_res measured;
    omc_iptc_iim_serialize_res written;
    omc_mut_bytes output;
    omc_val value;
    omc_u8 actual[128];
    omc_u8 expected[128];
    omc_size expected_size;
    omc_size i;

    omc_store_init(&store);
    value = make_bytes_value(&store, late, sizeof(late));
    add_value_dataset(&store, 2U, 5U, 3U, &value, OMC_ENTRY_FLAG_NONE);
    value = make_bytes_value(&store, charset, sizeof(charset));
    add_value_dataset(&store, 1U, 90U, 9U, &value, OMC_ENTRY_FLAG_NONE);
    value = make_bytes_value(&store, duplicate_a, sizeof(duplicate_a));
    add_value_dataset(&store, 1U, 9U, 2U, &value, OMC_ENTRY_FLAG_NONE);
    value = make_bytes_value(&store, middle, sizeof(middle));
    add_value_dataset(&store, 1U, 4U, 2U, &value, OMC_ENTRY_FLAG_NONE);
    value = make_bytes_value(&store, duplicate_b, sizeof(duplicate_b));
    add_value_dataset(&store, 1U, 9U, 2U, &value, OMC_ENTRY_FLAG_NONE);
    value = make_bytes_value(&store, early, sizeof(early));
    add_value_dataset(&store, 1U, 5U, 1U, &value, OMC_ENTRY_FLAG_NONE);
    value = make_array_value(&store, OMC_ELEM_U16, 1U, raw_array,
                             sizeof(raw_array), OMC_BYTE_ORDER_BIG);
    add_value_dataset(&store, 1U, 8U, 2U, &value, OMC_ENTRY_FLAG_NONE);

    output.data = NULL;
    output.size = 0U;
    measured = omc_serialize_iptc_iim(&store, output, NULL);
    assert(measured.status == OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED);
    assert(measured.entries_serialized == 7U);
    assert(measured.entries_skipped == 0U);

    expected_size = 0U;
    append_expected_dataset(expected, &expected_size, 1U, 90U, charset,
                            sizeof(charset));
    append_expected_dataset(expected, &expected_size, 1U, 5U, early,
                            sizeof(early));
    append_expected_dataset(expected, &expected_size, 1U, 4U, middle,
                            sizeof(middle));
    append_expected_dataset(expected, &expected_size, 1U, 8U, raw_array,
                            sizeof(raw_array));
    append_expected_dataset(expected, &expected_size, 1U, 9U, duplicate_a,
                            sizeof(duplicate_a));
    append_expected_dataset(expected, &expected_size, 1U, 9U, duplicate_b,
                            sizeof(duplicate_b));
    append_expected_dataset(expected, &expected_size, 2U, 5U, late,
                            sizeof(late));
    assert(measured.needed == expected_size);

    memset(actual, 0xA5, sizeof(actual));
    output.data = actual;
    output.size = sizeof(actual);
    written = omc_serialize_iptc_iim(&store, output, NULL);
    assert(written.status == OMC_IPTC_IIM_SERIALIZE_OK);
    assert(written.written == expected_size);
    assert(memcmp(actual, expected, expected_size) == 0);
    for (i = expected_size; i < sizeof(actual); ++i) {
        assert(actual[i] == 0xA5U);
    }
    omc_store_fini(&store);
}

static void
test_empty_payload_tombstones_and_no_data(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref empty_ref;
    omc_val value;
    omc_mut_bytes output;
    omc_iptc_iim_serialize_res result;
    omc_u8 bytes[8];
    omc_u8 expected[5];
    omc_u8 saved;

    omc_store_init(&store);
    assert(omc_arena_append(&store.arena, NULL, 0U, &empty_ref)
           == OMC_STATUS_OK);
    omc_val_make_bytes(&value, empty_ref);
    add_value_dataset(&store, 1U, 5U, 0U, &value, OMC_ENTRY_FLAG_NONE);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 300U, 300U);
    omc_val_make_u8(&entry.value, 7U);
    entry.flags = OMC_ENTRY_FLAG_DELETED | OMC_ENTRY_FLAG_TRUNCATED;
    assert(omc_store_add_entry(&store, &entry, NULL) == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_comment(&entry.key);
    omc_val_make_u8(&entry.value, 3U);
    entry.flags = OMC_ENTRY_FLAG_TRUNCATED | OMC_ENTRY_FLAG_UNREADABLE;
    assert(omc_store_add_entry(&store, &entry, NULL) == OMC_STATUS_OK);

    memset(bytes, 0xA5, sizeof(bytes));
    output.data = bytes;
    output.size = sizeof(bytes);
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_OK);
    assert(result.entries_serialized == 1U);
    assert(result.needed == 5U && result.written == 5U);
    expected[0] = 0x1CU;
    expected[1] = 1U;
    expected[2] = 5U;
    expected[3] = 0U;
    expected[4] = 0U;
    assert(memcmp(bytes, expected, sizeof(expected)) == 0);
    assert(bytes[5] == 0xA5U && bytes[7] == 0xA5U);
    omc_store_fini(&store);

    omc_store_init(&store);
    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 1U, 5U);
    omc_val_make_u8(&entry.value, 7U);
    entry.flags = OMC_ENTRY_FLAG_DELETED;
    assert(omc_store_add_entry(&store, &entry, NULL) == OMC_STATUS_OK);
    memset(bytes, 0xA5, sizeof(bytes));
    saved = bytes[0];
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_NO_IPTC_DATA);
    assert(result.needed == 0U && result.written == 0U);
    assert(bytes[0] == saved);
    omc_store_fini(&store);
}

typedef enum invalid_entry_case {
    INVALID_SCALAR,
    INVALID_EMPTY,
    INVALID_BYTES_COUNT,
    INVALID_TEXT_COUNT,
    INVALID_ARRAY_TYPE,
    INVALID_ARRAY_COUNT,
    INVALID_SPAN,
    INVALID_TRUNCATED,
    INVALID_UNREADABLE,
    INVALID_RECORD,
    INVALID_DATASET
} invalid_entry_case;

static void
test_invalid_case(invalid_entry_case which)
{
    static const omc_u8 valid_bytes[] = { 'o', 'k' };
    static const omc_u8 array_bytes[] = { 0x12U, 0x34U };
    omc_store store;
    omc_val value;
    omc_entry invalid;
    omc_byte_ref bad_ref;
    omc_iptc_iim_serialize_res result;
    omc_mut_bytes output;
    omc_u8 bytes[24];
    omc_u8 before[24];
    omc_u16 record;
    omc_u16 dataset;
    omc_entry_flags flags;
    omc_size i;

    omc_store_init(&store);
    value = make_bytes_value(&store, valid_bytes, sizeof(valid_bytes));
    add_value_dataset(&store, 1U, 5U, 0U, &value, OMC_ENTRY_FLAG_NONE);

    memset(&invalid, 0, sizeof(invalid));
    record = 1U;
    dataset = 6U;
    flags = OMC_ENTRY_FLAG_NONE;
    switch (which) {
    case INVALID_SCALAR:
        omc_val_make_u8(&invalid.value, 1U);
        break;
    case INVALID_EMPTY:
        omc_val_init(&invalid.value);
        break;
    case INVALID_BYTES_COUNT:
        invalid.value = make_bytes_value(&store, valid_bytes, 1U);
        invalid.value.count = 2U;
        break;
    case INVALID_TEXT_COUNT:
        invalid.value = make_text_value(&store, valid_bytes, 1U);
        invalid.value.count = 2U;
        break;
    case INVALID_ARRAY_TYPE:
        invalid.value = make_array_value(&store, OMC_ELEM_U16, 1U,
                                         array_bytes, sizeof(array_bytes),
                                         OMC_BYTE_ORDER_BIG);
        invalid.value.elem_type = (omc_elem_type)99;
        break;
    case INVALID_ARRAY_COUNT:
        invalid.value = make_array_value(&store, OMC_ELEM_U16, 1U,
                                         array_bytes, sizeof(array_bytes),
                                         OMC_BYTE_ORDER_BIG);
        invalid.value.count = 2U;
        break;
    case INVALID_SPAN:
        bad_ref.offset = (omc_u32)store.arena.size + 10U;
        bad_ref.size = 1U;
        omc_val_make_bytes(&invalid.value, bad_ref);
        break;
    case INVALID_TRUNCATED:
        invalid.value = make_bytes_value(&store, valid_bytes, 1U);
        flags = OMC_ENTRY_FLAG_TRUNCATED;
        break;
    case INVALID_UNREADABLE:
        invalid.value = make_bytes_value(&store, valid_bytes, 1U);
        flags = OMC_ENTRY_FLAG_UNREADABLE;
        break;
    case INVALID_RECORD:
        invalid.value = make_bytes_value(&store, valid_bytes, 1U);
        record = 256U;
        break;
    case INVALID_DATASET:
        invalid.value = make_bytes_value(&store, valid_bytes, 1U);
        dataset = 256U;
        break;
    }
    omc_key_make_iptc_dataset(&invalid.key, record, dataset);
    invalid.origin.block = OMC_INVALID_BLOCK_ID;
    invalid.flags = flags;
    assert(omc_store_add_entry(&store, &invalid, NULL) == OMC_STATUS_OK);

    memset(bytes, 0xA5, sizeof(bytes));
    memcpy(before, bytes, sizeof(bytes));
    output.data = bytes;
    output.size = sizeof(bytes);
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_METADATA);
    assert(result.failed_entry == 1U);
    assert(result.needed == 0U && result.written == 0U);
    for (i = 0U; i < sizeof(bytes); ++i) {
        assert(bytes[i] == before[i]);
    }
    omc_store_fini(&store);
}

static void
test_late_invalid_entries_leave_output_unchanged(void)
{
    invalid_entry_case which;

    for (which = INVALID_SCALAR; which <= INVALID_DATASET;
         which = (invalid_entry_case)(which + 1)) {
        test_invalid_case(which);
    }
}

static void
test_length_boundary(omc_size payload_size)
{
    static omc_u8 payload[32768];
    omc_store store;
    omc_val value;
    omc_iptc_iim_serialize_res measured;
    omc_iptc_iim_serialize_res written;
    omc_mut_bytes output;
    omc_u8 *bytes;
    omc_size header_size;
    omc_size needed;
    omc_size i;

    for (i = 0U; i < payload_size; ++i) {
        payload[i] = (omc_u8)(i & 0xFFU);
    }
    omc_store_init(&store);
    value = make_bytes_value(&store, payload, payload_size);
    add_value_dataset(&store, 1U, 5U, 0U, &value, OMC_ENTRY_FLAG_NONE);
    output.data = NULL;
    output.size = 0U;
    measured = omc_serialize_iptc_iim(&store, output, NULL);
    header_size = payload_size <= 0x7FFFU ? 5U : 9U;
    needed = payload_size + header_size;
    assert(measured.status == OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED);
    assert(measured.needed == needed);

    bytes = (omc_u8 *)malloc(needed + 1U);
    assert(bytes != NULL);
    memset(bytes, 0xA5, needed + 1U);
    output.data = bytes;
    output.size = needed + 1U;
    written = omc_serialize_iptc_iim(&store, output, NULL);
    assert(written.status == OMC_IPTC_IIM_SERIALIZE_OK);
    assert(written.written == needed);
    assert(bytes[0] == 0x1CU && bytes[1] == 1U && bytes[2] == 5U);
    if (payload_size <= 0x7FFFU) {
        assert(bytes[3] == 0x7FU && bytes[4] == 0xFFU);
        assert(memcmp(bytes + 5U, payload, payload_size) == 0);
    } else {
        assert(bytes[3] == 0x80U && bytes[4] == 0x04U);
        assert(bytes[5] == 0U && bytes[6] == 0U);
        assert(bytes[7] == 0x80U && bytes[8] == 0U);
        assert(memcmp(bytes + 9U, payload, payload_size) == 0);
    }
    assert(bytes[needed] == 0xA5U);
    free(bytes);
    omc_store_fini(&store);
}

static void
test_limits_and_short_prefix(void)
{
    static const omc_u8 first_bytes[] = { 'a', 'b' };
    static const omc_u8 second_bytes[] = { 'c' };
    omc_store store;
    omc_val value;
    omc_iptc_iim_serialize_opts opts;
    omc_iptc_iim_serialize_res result;
    omc_mut_bytes output;
    omc_u8 bytes[20];
    omc_u8 before[20];

    omc_store_init(&store);
    value = make_bytes_value(&store, first_bytes, sizeof(first_bytes));
    add_value_dataset(&store, 1U, 5U, 0U, &value, OMC_ENTRY_FLAG_NONE);
    value = make_bytes_value(&store, second_bytes, sizeof(second_bytes));
    add_value_dataset(&store, 1U, 6U, 1U, &value, OMC_ENTRY_FLAG_NONE);

    omc_iptc_iim_serialize_opts_init(&opts);
    assert(opts.max_datasets == 200000U);
    assert(opts.max_dataset_bytes == 8U * 1024U * 1024U);
    assert(opts.max_output_bytes == 64U * 1024U * 1024U);
    opts.max_datasets = 2U;
    opts.max_dataset_bytes = 2U;
    opts.max_output_bytes = 13U;
    output.data = bytes;
    output.size = 13U;
    memset(bytes, 0xA5, sizeof(bytes));
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_OK);
    assert(result.entries_serialized == 2U);
    assert(result.needed == 13U && result.written == 13U);
    assert(bytes[13] == 0xA5U);

    memcpy(before, bytes, sizeof(bytes));
    opts.max_datasets = 1U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_LIMIT);
    assert(result.failed_entry == 1U);
    assert(result.needed == 0U && result.written == 0U);
    assert(memcmp(bytes, before, sizeof(bytes)) == 0);

    opts.max_datasets = 2U;
    opts.max_dataset_bytes = 1U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_LIMIT);
    assert(result.failed_entry == 0U);
    assert(memcmp(bytes, before, sizeof(bytes)) == 0);

    opts.max_dataset_bytes = 2U;
    opts.max_output_bytes = 12U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_LIMIT);
    assert(result.failed_entry == 1U);
    assert(memcmp(bytes, before, sizeof(bytes)) == 0);

    opts.max_output_bytes = 13U;
    output.size = 3U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED);
    assert(result.needed == 13U && result.written == 3U);
    assert(bytes[0] == 0x1CU && bytes[1] == 1U && bytes[2] == 5U);
    assert(memcmp(bytes + 3U, before + 3U, sizeof(bytes) - 3U) == 0);

    opts.max_datasets = 0U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    opts.max_datasets = 2U;
    opts.max_dataset_bytes = 0U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    opts.max_dataset_bytes = 2U;
    opts.max_output_bytes = 0U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    opts.max_output_bytes = (omc_u64)(~(omc_u32)0) + 1U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    output.data = NULL;
    output.size = 1U;
    opts.max_output_bytes = 13U;
    result = omc_serialize_iptc_iim(&store, output, &opts);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    omc_store_fini(&store);
}

static void
test_rejects_aliasing_all_store_storage(void)
{
    static const omc_u8 content[] = { 'x' };
    omc_store store;
    omc_block_info block;
    omc_val value;
    omc_iptc_iim_serialize_res result;
    omc_mut_bytes output;
    omc_entry entry_copy;
    omc_block_info block_copy;
    omc_u8 arena_byte;
    omc_u8 store_byte;

    omc_store_init(&store);
    value = make_bytes_value(&store, content, sizeof(content));
    add_value_dataset(&store, 1U, 5U, 0U, &value, OMC_ENTRY_FLAG_NONE);
    memset(&block, 0, sizeof(block));
    assert(omc_store_add_block(&store, &block, NULL) == OMC_STATUS_OK);
    output.size = 1U;

    output.data = (omc_u8 *)store.entries;
    output.size = 0U;
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED);
    assert(result.needed == 6U && result.written == 0U);

    memcpy(&entry_copy, store.entries, sizeof(entry_copy));
    output.data = (omc_u8 *)store.entries;
    output.size = 1U;
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    assert(memcmp(&entry_copy, store.entries, sizeof(entry_copy)) == 0);

    memcpy(&block_copy, store.blocks, sizeof(block_copy));
    output.data = (omc_u8 *)store.blocks;
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    assert(memcmp(&block_copy, store.blocks, sizeof(block_copy)) == 0);

    store_byte = ((omc_u8 *)&store)[0];
    output.data = (omc_u8 *)&store;
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    assert(((omc_u8 *)&store)[0] == store_byte);

    arena_byte = store.arena.data[0];
    output.data = store.arena.data;
    result = omc_serialize_iptc_iim(&store, output, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    assert(store.arena.data[0] == arena_byte);
    if (store.arena.capacity > store.arena.size) {
        store.arena.data[store.arena.size] = 0x5AU;
        output.data = store.arena.data + store.arena.size;
        result = omc_serialize_iptc_iim(&store, output, NULL);
        assert(result.status == OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
        assert(store.arena.data[store.arena.size] == 0x5AU);
    }
    omc_store_fini(&store);
}

static void
test_internal_skip_reports_damaged_entries_and_keeps_limits_fatal(void)
{
    static const omc_u8 valid[] = { 'o', 'k' };
    omc_store store;
    omc_val value;
    omc_iptc_iim_serialize_opts opts;
    omc_iptc_iim_serialize_res result;
    omc_mut_bytes output;
    omc_u8 bytes[16];
    omc_u8 expected[7];
    omc_size expected_size;
    omc_entry damaged;
    omc_byte_ref ref;

    omc_store_init(&store);
    value = make_bytes_value(&store, valid, sizeof(valid));
    add_value_dataset(&store, 1U, 5U, 0U, &value, OMC_ENTRY_FLAG_NONE);
    ref.offset = 0U;
    ref.size = 0U;
    memset(&damaged, 0, sizeof(damaged));
    omc_key_make_iptc_dataset(&damaged.key, 1U, 6U);
    omc_val_make_bytes(&damaged.value, ref);
    damaged.flags = OMC_ENTRY_FLAG_UNREADABLE;
    assert(omc_store_add_entry(&store, &damaged, NULL) == OMC_STATUS_OK);

    expected_size = 0U;
    append_expected_dataset(expected, &expected_size, 1U, 5U, valid,
                            sizeof(valid));
    memset(bytes, 0xA5, sizeof(bytes));
    output.data = bytes;
    output.size = sizeof(bytes);
    result = omc_iptc_iim_serialize_internal(&store, NULL, 1, output);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_OK);
    assert(result.entries_serialized == 1U && result.entries_skipped == 1U);
    assert(result.needed == expected_size && result.written == expected_size);
    assert(memcmp(bytes, expected, expected_size) == 0);

    omc_iptc_iim_serialize_opts_init(&opts);
    opts.max_datasets = 1U;
    memset(bytes, 0xA5, sizeof(bytes));
    result = omc_iptc_iim_serialize_internal(&store, &opts, 1, output);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_LIMIT);
    assert(result.failed_entry == 1U);
    assert(result.needed == 0U && result.written == 0U);
    assert(bytes[0] == 0xA5U && bytes[sizeof(bytes) - 1U] == 0xA5U);

    omc_store_fini(&store);
}

int
main(void)
{
    test_canonical_order_duplicates_and_raw_array();
    test_empty_payload_tombstones_and_no_data();
    test_late_invalid_entries_leave_output_unchanged();
    test_length_boundary(32767U);
    test_length_boundary(32768U);
    test_limits_and_short_prefix();
    test_rejects_aliasing_all_store_storage();
    test_internal_skip_reports_damaged_entries_and_keeps_limits_fatal();
    return 0;
}
