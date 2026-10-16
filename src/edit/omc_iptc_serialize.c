#include "omc/omc_iptc_serialize.h"
#include "omc_iptc_serialize_internal.h"
#include "../core/omc_value_internal.h"

#include <stdlib.h>
#include <string.h>

typedef char omc_iptc_iim_pointer_fits_u64
    [(sizeof(void *) <= sizeof(omc_u64)) ? 1 : -1];

typedef struct omc_iptc_iim_item {
    omc_size entry_ordinal;
    omc_u16 record;
    omc_u16 dataset;
    omc_u32 source_order;
    const omc_u8 *payload;
    omc_size payload_size;
} omc_iptc_iim_item;

static omc_iptc_iim_serialize_res
omc_iptc_iim_result(omc_iptc_iim_serialize_status status)
{
    omc_iptc_iim_serialize_res result;

    memset(&result, 0, sizeof(result));
    result.status = status;
    result.failed_entry = OMC_INVALID_ENTRY_ID;
    return result;
}

void
omc_iptc_iim_serialize_opts_init(omc_iptc_iim_serialize_opts *opts)
{
    if (opts == NULL) {
        return;
    }
    opts->max_datasets = 200000U;
    opts->max_dataset_bytes = 8U * 1024U * 1024U;
    opts->max_output_bytes = 64U * 1024U * 1024U;
}

static int
omc_iptc_iim_options_valid(const omc_iptc_iim_serialize_opts *opts)
{
    if (opts == NULL || opts->max_datasets == 0U
        || opts->max_dataset_bytes == 0U || opts->max_output_bytes == 0U
        || opts->max_output_bytes > (omc_u64)(~(omc_u32)0)
        || opts->max_output_bytes > (omc_u64)(~(omc_size)0)) {
        return 0;
    }
    return 1;
}

static int
omc_iptc_iim_ref_valid(const omc_store *store, omc_byte_ref ref)
{
    if ((omc_size)ref.offset > store->arena.size
        || (omc_size)ref.size > store->arena.size - (omc_size)ref.offset) {
        return 0;
    }
    if (ref.size != 0U && store->arena.data == NULL) {
        return 0;
    }
    return 1;
}

static int
omc_iptc_iim_payload(const omc_store *store, const omc_val *value,
                     const omc_u8 **payload, omc_size *payload_size)
{
    omc_u32 width;
    omc_u64 expected_size;

    if (value->kind != OMC_VAL_BYTES && value->kind != OMC_VAL_TEXT
        && value->kind != OMC_VAL_ARRAY) {
        return 0;
    }
    if (!omc_iptc_iim_ref_valid(store, value->u.ref)) {
        return 0;
    }
    if (value->kind == OMC_VAL_BYTES || value->kind == OMC_VAL_TEXT) {
        if (value->count != value->u.ref.size) {
            return 0;
        }
    } else {
        width = omc_elem_size(value->elem_type);
        if (width == 0U) {
            return 0;
        }
        expected_size = (omc_u64)value->count * (omc_u64)width;
        if (expected_size != (omc_u64)value->u.ref.size) {
            return 0;
        }
    }

    *payload = store->arena.data == NULL
                   ? (const omc_u8 *)NULL
                   : store->arena.data + value->u.ref.offset;
    *payload_size = (omc_size)value->u.ref.size;
    return 1;
}

static int
omc_iptc_iim_entry_item(const omc_store *store, omc_size ordinal,
                        omc_iptc_iim_item *item)
{
    const omc_entry *entry;
    const omc_u8 *payload;
    omc_size payload_size;

    entry = &store->entries[ordinal];
    if ((entry->flags & (OMC_ENTRY_FLAG_TRUNCATED
                         | OMC_ENTRY_FLAG_UNREADABLE)) != 0U
        || entry->key.u.iptc_dataset.record > 255U
        || entry->key.u.iptc_dataset.dataset > 255U
        || !omc_iptc_iim_payload(store, &entry->value, &payload,
                                 &payload_size)) {
        return 0;
    }

    item->entry_ordinal = ordinal;
    item->record = entry->key.u.iptc_dataset.record;
    item->dataset = entry->key.u.iptc_dataset.dataset;
    item->source_order = entry->origin.order_in_block;
    item->payload = payload;
    item->payload_size = payload_size;
    return 1;
}

static int
omc_iptc_iim_item_less(const omc_iptc_iim_item *first,
                       const omc_iptc_iim_item *second)
{
    int first_charset;
    int second_charset;

    first_charset = first->record == 1U && first->dataset == 90U;
    second_charset = second->record == 1U && second->dataset == 90U;
    if (first_charset != second_charset) {
        return first_charset;
    }
    if (first->source_order != second->source_order) {
        return first->source_order < second->source_order;
    }
    if (first->record != second->record) {
        return first->record < second->record;
    }
    if (first->dataset != second->dataset) {
        return first->dataset < second->dataset;
    }
    return first->entry_ordinal < second->entry_ordinal;
}

static void
omc_iptc_iim_sift_down(omc_iptc_iim_item *items, omc_size root,
                       omc_size count)
{
    omc_size child;
    omc_iptc_iim_item swap;

    while (count > 1U && root <= (count - 2U) / 2U) {
        child = root * 2U + 1U;
        if (child + 1U < count
            && omc_iptc_iim_item_less(&items[child], &items[child + 1U])) {
            child += 1U;
        }
        if (!omc_iptc_iim_item_less(&items[root], &items[child])) {
            return;
        }
        swap = items[root];
        items[root] = items[child];
        items[child] = swap;
        root = child;
    }
}

static void
omc_iptc_iim_sort(omc_iptc_iim_item *items, omc_size count)
{
    omc_size index;
    omc_size end;
    omc_iptc_iim_item swap;

    if (count < 2U) {
        return;
    }
    index = count / 2U;
    while (index != 0U) {
        index -= 1U;
        omc_iptc_iim_sift_down(items, index, count);
    }
    end = count;
    while (end > 1U) {
        swap = items[0];
        items[0] = items[end - 1U];
        items[end - 1U] = swap;
        end -= 1U;
        omc_iptc_iim_sift_down(items, 0U, end);
    }
}

static omc_u64
omc_iptc_iim_pointer_value(const void *pointer)
{
    return (omc_u64)pointer;
}

static int
omc_iptc_iim_ranges_overlap(const void *first_data, omc_size first_size,
                            const void *second_data, omc_size second_size)
{
    omc_u64 first_begin;
    omc_u64 second_begin;
    omc_u64 first_end;
    omc_u64 second_end;
    omc_u64 max_address;

    if (first_size == 0U || second_size == 0U || first_data == NULL || second_data == NULL) {
        return 0;
    }
    first_begin = omc_iptc_iim_pointer_value(first_data);
    second_begin = omc_iptc_iim_pointer_value(second_data);
    max_address = ~(omc_u64)0;
    if ((omc_u64)first_size > max_address - first_begin
        || (omc_u64)second_size > max_address - second_begin) {
        return 1;
    }
    first_end = first_begin + (omc_u64)first_size;
    second_end = second_begin + (omc_u64)second_size;
    return first_begin < second_end && second_begin < first_end;
}

static void
omc_iptc_iim_store_u32be(omc_u8 *bytes, omc_u32 value)
{
    bytes[0] = (omc_u8)(value >> 24U);
    bytes[1] = (omc_u8)(value >> 16U);
    bytes[2] = (omc_u8)(value >> 8U);
    bytes[3] = (omc_u8)value;
}

static omc_size
omc_iptc_iim_write_prefix(omc_u8 *output, omc_size output_size,
                          omc_size written, const omc_u8 *source,
                          omc_size source_size)
{
    omc_size available;
    omc_size copied;

    if (written >= output_size || source_size == 0U) {
        return written;
    }
    available = output_size - written;
    copied = source_size < available ? source_size : available;
    memcpy(output + written, source, copied);
    return written + copied;
}

static omc_iptc_iim_serialize_res
omc_iptc_iim_fail(omc_iptc_iim_serialize_status status,
                  omc_size entry_ordinal)
{
    omc_iptc_iim_serialize_res result;

    result = omc_iptc_iim_result(status);
    if (entry_ordinal < (omc_size)OMC_INVALID_ENTRY_ID) {
        result.failed_entry = (omc_entry_id)entry_ordinal;
    }
    return result;
}

omc_iptc_iim_serialize_res
omc_iptc_iim_serialize_internal(
    const omc_store *store, const omc_iptc_iim_serialize_opts *options,
    int skip_unsupported, omc_mut_bytes output)
{
    omc_iptc_iim_serialize_opts defaults;
    const omc_iptc_iim_serialize_opts *opts;
    omc_iptc_iim_serialize_res result;
    const omc_entry *entry;
    omc_iptc_iim_item item;
    omc_iptc_iim_item *items;
    omc_size i;
    omc_size item_count;
    omc_size allocated_count;
    omc_size written;
    omc_size write_limit;
    omc_size entry_storage_size;
    omc_size block_storage_size;
    omc_u32 active_count;
    omc_u32 serialized_count;
    omc_u32 skipped_count;
    omc_u64 total_size;
    omc_u64 dataset_size;
    omc_u8 header[9];
    omc_size header_size;
    omc_size header_written;
    omc_size payload_written;
    omc_u32 payload_length;

    omc_iptc_iim_serialize_opts_init(&defaults);
    opts = options != NULL ? options : &defaults;
    if (!omc_iptc_iim_options_valid(opts)
        || (skip_unsupported != 0 && skip_unsupported != 1)
        || !omc_store_shape_valid(store)
        || (output.size != 0U && output.data == NULL)) {
        return omc_iptc_iim_result(OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    }
    if (store->entry_capacity
            > (omc_size)(~(omc_size)0) / sizeof(*store->entries)
        || store->block_capacity
               > (omc_size)(~(omc_size)0) / sizeof(*store->blocks)) {
        return omc_iptc_iim_result(OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    }
    entry_storage_size = store->entry_capacity * sizeof(*store->entries);
    block_storage_size = store->block_capacity * sizeof(*store->blocks);
    if (omc_iptc_iim_ranges_overlap(output.data, output.size, store,
                                    sizeof(*store))
        || omc_iptc_iim_ranges_overlap(output.data, output.size,
                                       store->arena.data,
                                       store->arena.capacity)
        || omc_iptc_iim_ranges_overlap(output.data, output.size,
                                       store->entries, entry_storage_size)
        || omc_iptc_iim_ranges_overlap(output.data, output.size,
                                       store->blocks, block_storage_size)) {
        return omc_iptc_iim_result(OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS);
    }

    active_count = 0U;
    serialized_count = 0U;
    skipped_count = 0U;
    total_size = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_IPTC_DATASET) {
            continue;
        }
        if (active_count >= opts->max_datasets) {
            return omc_iptc_iim_fail(OMC_IPTC_IIM_SERIALIZE_LIMIT, i);
        }
        active_count += 1U;

        if (!omc_iptc_iim_entry_item(store, i, &item)) {
            if (!skip_unsupported) {
                return omc_iptc_iim_fail(
                    OMC_IPTC_IIM_SERIALIZE_INVALID_METADATA, i);
            }
            skipped_count += 1U;
            continue;
        }
        if ((omc_u64)item.payload_size > (omc_u64)opts->max_dataset_bytes) {
            return omc_iptc_iim_fail(OMC_IPTC_IIM_SERIALIZE_LIMIT, i);
        }

        dataset_size = (omc_u64)item.payload_size
                       + (item.payload_size <= 0x7FFFU ? 5U : 9U);
        if (dataset_size > opts->max_output_bytes - total_size) {
            return omc_iptc_iim_fail(OMC_IPTC_IIM_SERIALIZE_LIMIT, i);
        }
        total_size += dataset_size;
        serialized_count += 1U;
    }

    result = omc_iptc_iim_result(OMC_IPTC_IIM_SERIALIZE_OK);
    result.needed = total_size;
    result.entries_serialized = serialized_count;
    result.entries_skipped = skipped_count;
    if (output.size == 0U || serialized_count == 0U) {
        if (serialized_count != 0U) {
            result.status = OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED;
        }
        return result;
    }
    if ((omc_size)serialized_count
        > ((omc_size)(~(omc_size)0) / sizeof(*items))) {
        return omc_iptc_iim_result(OMC_IPTC_IIM_SERIALIZE_LIMIT);
    }
    allocated_count = (omc_size)serialized_count;
    items = (omc_iptc_iim_item *)malloc(allocated_count * sizeof(*items));
    if (items == NULL) {
        return omc_iptc_iim_result(OMC_IPTC_IIM_SERIALIZE_NO_MEMORY);
    }

    item_count = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_IPTC_DATASET) {
            continue;
        }
        if (!omc_iptc_iim_entry_item(store, i, &item)) {
            if (skip_unsupported) {
                continue;
            }
            free(items);
            return omc_iptc_iim_fail(
                OMC_IPTC_IIM_SERIALIZE_INVALID_METADATA, i);
        }
        items[item_count] = item;
        item_count += 1U;
    }
    if (item_count != allocated_count) {
        free(items);
        return omc_iptc_iim_result(OMC_IPTC_IIM_SERIALIZE_INVALID_METADATA);
    }
    omc_iptc_iim_sort(items, item_count);

    write_limit = output.size;
    if ((omc_u64)write_limit > total_size) {
        write_limit = (omc_size)total_size;
    }
    written = 0U;
    for (i = 0U; i < item_count && written < write_limit; ++i) {
        header[0] = 0x1CU;
        header[1] = (omc_u8)items[i].record;
        header[2] = (omc_u8)items[i].dataset;
        if (items[i].payload_size <= 0x7FFFU) {
            header[3] = (omc_u8)(items[i].payload_size >> 8U);
            header[4] = (omc_u8)items[i].payload_size;
            header_size = 5U;
        } else {
            header[3] = 0x80U;
            header[4] = 0x04U;
            payload_length = (omc_u32)items[i].payload_size;
            omc_iptc_iim_store_u32be(header + 5U, payload_length);
            header_size = 9U;
        }
        header_written = written;
        written = omc_iptc_iim_write_prefix(output.data, write_limit,
                                             written, header, header_size);
        payload_written = written - header_written;
        if (payload_written < header_size) {
            break;
        }
        written = omc_iptc_iim_write_prefix(
            output.data, write_limit, written, items[i].payload,
            items[i].payload_size);
    }
    free(items);

    result.written = (omc_u64)written;
    result.status = (omc_u64)written == total_size
                        ? OMC_IPTC_IIM_SERIALIZE_OK
                        : OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED;
    return result;
}

omc_iptc_iim_serialize_res
omc_serialize_iptc_iim(const omc_store *store, omc_mut_bytes output,
                       const omc_iptc_iim_serialize_opts *opts)
{
    omc_iptc_iim_serialize_res result;

    result = omc_iptc_iim_serialize_internal(store, opts, 0, output);
    if ((result.status == OMC_IPTC_IIM_SERIALIZE_OK
         || result.status == OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED)
        && result.entries_serialized == 0U) {
        result.status = OMC_IPTC_IIM_SERIALIZE_NO_IPTC_DATA;
        result.written = 0U;
        result.needed = 0U;
    }
    return result;
}
