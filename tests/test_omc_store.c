#include "omc/omc_arena.h"
#include "omc/omc_key.h"
#include "omc/omc_store.h"
#include "omc/omc_val.h"

#include "omc_test_assert.h"
#include <string.h>

static void
test_store_roundtrip(void)
{
    omc_store store;
    omc_block_info block;
    omc_block_id block_id;
    omc_entry entry;
    omc_entry_id entry_id;
    omc_byte_ref ifd_ref;
    omc_const_bytes ifd_view;
    const omc_entry* stored_entry;
    omc_status status;
    const char ifd_name[] = "ifd0";

    omc_store_init(&store);

    memset(&block, 0, sizeof(block));
    block.format = OMC_SCAN_FMT_JPEG;
    block.kind = OMC_BLK_EXIF;
    block.id = 3U;

    status = omc_store_add_block(&store, &block, &block_id);
    assert(status == OMC_STATUS_OK);
    assert(block_id == 0U);

    status = omc_arena_append(&store.arena, ifd_name, sizeof(ifd_name) - 1U,
                              &ifd_ref);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, 0x010FU);
    omc_val_make_u32(&entry.value, 42U);
    entry.origin.block = block_id;
    entry.origin.order_in_block = 0U;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 4U;
    entry.origin.wire_count = 1U;
    entry.flags = OMC_ENTRY_FLAG_NONE;

    status = omc_store_add_entry(&store, &entry, &entry_id);
    assert(status == OMC_STATUS_OK);
    assert(entry_id == 0U);

    stored_entry = omc_store_entry(&store, entry_id);
    assert(stored_entry != NULL);
    assert(stored_entry->key.kind == OMC_KEY_EXIF_TAG);
    assert(stored_entry->value.kind == OMC_VAL_SCALAR);
    assert(stored_entry->value.u.u64 == 42U);

    ifd_view = omc_arena_view(&store.arena, stored_entry->key.u.exif_tag.ifd);
    assert(ifd_view.data != NULL);
    assert(ifd_view.size == 4U);
    assert(memcmp(ifd_view.data, "ifd0", 4U) == 0);

    omc_store_fini(&store);
}

static void
test_add_entry_rejects_invalid_sentinel_id(void)
{
    omc_store store;
    omc_entry entry;
    omc_entry dummy_entries[1];
    omc_status status;

    omc_store_init(&store);
    memset(&entry, 0, sizeof(entry));
    memset(dummy_entries, 0, sizeof(dummy_entries));

    omc_key_make_comment(&entry.key);
    omc_val_make_u32(&entry.value, 7U);

    store.entries = dummy_entries;
    store.entry_capacity = (omc_size)OMC_INVALID_ENTRY_ID;
    store.entry_count = (omc_size)OMC_INVALID_ENTRY_ID;

    status = omc_store_add_entry(&store, &entry, (omc_entry_id*)0);
    assert(status == OMC_STATUS_OVERFLOW);
    assert(store.entry_count == (omc_size)OMC_INVALID_ENTRY_ID);

    store.entries = (omc_entry*)0;
    store.entry_capacity = 0U;
    store.entry_count = 0U;
    omc_store_fini(&store);
}

static void
test_add_block_rejects_invalid_sentinel_id(void)
{
    omc_store store;
    omc_block_info block;
    omc_block_info dummy_blocks[1];
    omc_status status;

    omc_store_init(&store);
    memset(&block, 0, sizeof(block));
    memset(dummy_blocks, 0, sizeof(dummy_blocks));

    block.format = OMC_SCAN_FMT_JPEG;
    block.kind = OMC_BLK_EXIF;
    block.id = 1U;

    store.blocks = dummy_blocks;
    store.block_capacity = (omc_size)OMC_INVALID_BLOCK_ID;
    store.block_count = (omc_size)OMC_INVALID_BLOCK_ID;

    status = omc_store_add_block(&store, &block, (omc_block_id*)0);
    assert(status == OMC_STATUS_OVERFLOW);
    assert(store.block_count == (omc_size)OMC_INVALID_BLOCK_ID);

    store.blocks = (omc_block_info*)0;
    store.block_capacity = 0U;
    store.block_count = 0U;
    omc_store_fini(&store);
}

int
main(void)
{
    test_store_roundtrip();
    test_add_entry_rejects_invalid_sentinel_id();
    test_add_block_rejects_invalid_sentinel_id();
    return 0;
}
