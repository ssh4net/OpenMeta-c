#include "omc/omc_arena.h"
#include "omc/omc_key.h"
#include "omc/omc_store.h"
#include "omc/omc_val.h"

#include <assert.h>
#include <string.h>

int
main(void)
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

    block.format = 1U;
    block.container = 2U;
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
    return 0;
}
