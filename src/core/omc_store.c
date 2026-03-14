#include "omc/omc_store.h"

#include <stdlib.h>
#include <string.h>

static omc_status
omc_store_reserve_entries_internal(omc_store* store, omc_size capacity)
{
    void* new_mem;

    if (capacity <= store->entry_capacity) {
        return OMC_STATUS_OK;
    }

    new_mem = realloc(store->entries, capacity * sizeof(*store->entries));
    if (new_mem == NULL) {
        return OMC_STATUS_NO_MEMORY;
    }

    store->entries = (omc_entry*)new_mem;
    store->entry_capacity = capacity;
    return OMC_STATUS_OK;
}

static omc_status
omc_store_reserve_blocks_internal(omc_store* store, omc_size capacity)
{
    void* new_mem;

    if (capacity <= store->block_capacity) {
        return OMC_STATUS_OK;
    }

    new_mem = realloc(store->blocks, capacity * sizeof(*store->blocks));
    if (new_mem == NULL) {
        return OMC_STATUS_NO_MEMORY;
    }

    store->blocks = (omc_block_info*)new_mem;
    store->block_capacity = capacity;
    return OMC_STATUS_OK;
}

static omc_size
omc_store_next_capacity(omc_size current, omc_size needed)
{
    omc_size next;

    if (current == 0U) {
        next = 16U;
    } else {
        next = current;
    }

    while (next < needed) {
        if (next > ((omc_size)(~(omc_size)0) / 2U)) {
            next = needed;
            break;
        }
        next *= 2U;
    }

    return next;
}

void
omc_store_init(omc_store* store)
{
    if (store == NULL) {
        return;
    }

    memset(store, 0, sizeof(*store));
    omc_arena_init(&store->arena);
}

void
omc_store_reset(omc_store* store)
{
    if (store == NULL) {
        return;
    }

    store->entry_count = 0U;
    store->block_count = 0U;
    omc_arena_reset(&store->arena);
}

void
omc_store_fini(omc_store* store)
{
    if (store == NULL) {
        return;
    }

    free(store->entries);
    free(store->blocks);
    store->entries = NULL;
    store->blocks = NULL;
    store->entry_count = 0U;
    store->entry_capacity = 0U;
    store->block_count = 0U;
    store->block_capacity = 0U;
    omc_arena_fini(&store->arena);
}

omc_status
omc_store_reserve_entries(omc_store* store, omc_size capacity)
{
    if (store == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (capacity > ((omc_size)(~(omc_size)0) / sizeof(*store->entries))) {
        return OMC_STATUS_OVERFLOW;
    }
    return omc_store_reserve_entries_internal(store, capacity);
}

omc_status
omc_store_reserve_blocks(omc_store* store, omc_size capacity)
{
    if (store == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (capacity > ((omc_size)(~(omc_size)0) / sizeof(*store->blocks))) {
        return OMC_STATUS_OVERFLOW;
    }
    return omc_store_reserve_blocks_internal(store, capacity);
}

omc_status
omc_store_add_block(omc_store* store, const omc_block_info* info,
                    omc_block_id* out_id)
{
    omc_size needed;
    omc_size capacity;
    omc_status status;

    if (store == NULL || info == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (store->block_count > (omc_size)(~(omc_block_id)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    needed = store->block_count + 1U;
    if (needed > store->block_capacity) {
        capacity = omc_store_next_capacity(store->block_capacity, needed);
        status = omc_store_reserve_blocks(store, capacity);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    store->blocks[store->block_count] = *info;
    if (out_id != NULL) {
        *out_id = (omc_block_id)store->block_count;
    }
    store->block_count = needed;
    return OMC_STATUS_OK;
}

omc_status
omc_store_add_entry(omc_store* store, const omc_entry* entry,
                    omc_entry_id* out_id)
{
    omc_size needed;
    omc_size capacity;
    omc_status status;

    if (store == NULL || entry == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (store->entry_count > (omc_size)(~(omc_entry_id)0)) {
        return OMC_STATUS_OVERFLOW;
    }

    needed = store->entry_count + 1U;
    if (needed > store->entry_capacity) {
        capacity = omc_store_next_capacity(store->entry_capacity, needed);
        status = omc_store_reserve_entries(store, capacity);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    store->entries[store->entry_count] = *entry;
    if (out_id != NULL) {
        *out_id = (omc_entry_id)store->entry_count;
    }
    store->entry_count = needed;
    return OMC_STATUS_OK;
}

const omc_entry*
omc_store_entry(const omc_store* store, omc_entry_id id)
{
    if (store == NULL) {
        return NULL;
    }
    if ((omc_size)id >= store->entry_count) {
        return NULL;
    }

    return &store->entries[id];
}

const omc_block_info*
omc_store_block(const omc_store* store, omc_block_id id)
{
    if (store == NULL) {
        return NULL;
    }
    if ((omc_size)id >= store->block_count) {
        return NULL;
    }

    return &store->blocks[id];
}
