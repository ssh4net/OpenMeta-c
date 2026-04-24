#include "omc/omc_edit.h"

#include "omc_test_assert.h"
#include <string.h>

static omc_byte_ref
append_bytes(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, strlen(text), &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static void
expect_text_ref(const omc_arena* arena, omc_byte_ref ref, const char* text)
{
    omc_const_bytes view;

    view = omc_arena_view(arena, ref);
    assert(view.data != NULL);
    assert(view.size == strlen(text));
    assert(memcmp(view.data, text, view.size) == 0);
}

static void
test_commit_appends_new_entry(void)
{
    omc_store base;
    omc_store out;
    omc_edit edit;
    omc_entry entry;
    omc_block_info block;
    omc_status status;
    omc_byte_ref ifd_ref;
    omc_byte_ref wire_name_ref;

    omc_store_init(&base);
    omc_store_init(&out);
    omc_edit_init(&edit);

    memset(&block, 0, sizeof(block));
    block.format = OMC_SCAN_FMT_JPEG;
    block.kind = OMC_BLK_EXIF;
    block.id = 9U;
    status = omc_store_add_block(&base, &block, NULL);
    assert(status == OMC_STATUS_OK);

    ifd_ref = append_bytes(&edit.arena, "ifd0");
    wire_name_ref = append_bytes(&edit.arena, "SHORT");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, 0x010FU);
    omc_val_make_u32(&entry.value, 7U);
    entry.origin.block = 0U;
    entry.origin.order_in_block = 0U;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 3U;
    entry.origin.wire_count = 1U;
    entry.origin.wire_type_name = wire_name_ref;

    status = omc_edit_add_entry(&edit, &entry);
    assert(status == OMC_STATUS_OK);

    status = omc_edit_commit(&base, &edit, 1U, &out);
    assert(status == OMC_STATUS_OK);
    assert(out.block_count == 1U);
    assert(out.entry_count == 1U);
    assert(out.entries[0].value.u.u64 == 7U);
    expect_text_ref(&out.arena, out.entries[0].key.u.exif_tag.ifd, "ifd0");
    expect_text_ref(&out.arena, out.entries[0].origin.wire_type_name, "SHORT");

    omc_edit_fini(&edit);
    omc_store_fini(&out);
    omc_store_fini(&base);
}

static void
test_commit_updates_and_tombstones(void)
{
    omc_store base;
    omc_store out;
    omc_edit edits[2];
    omc_entry entry;
    omc_status status;
    omc_byte_ref ifd_ref;
    omc_byte_ref new_text_ref;

    omc_store_init(&base);
    omc_store_init(&out);
    omc_edit_init(&edits[0]);
    omc_edit_init(&edits[1]);

    ifd_ref = append_bytes(&base.arena, "ifd0");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, 0x0100U);
    omc_val_make_u32(&entry.value, 33U);
    entry.flags = OMC_ENTRY_FLAG_NONE;
    status = omc_store_add_entry(&base, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    status = omc_edit_tombstone(&edits[0], 99U);
    assert(status == OMC_STATUS_OK);

    new_text_ref = append_bytes(&edits[0].arena, "hello");
    omc_val_make_text(&entry.value, new_text_ref, OMC_TEXT_UTF8);
    status = omc_edit_set_value(&edits[0], 0U, &entry.value);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_comment(&entry.key);
    omc_val_make_u16(&entry.value, 15U);
    status = omc_edit_add_entry(&edits[1], &entry);
    assert(status == OMC_STATUS_OK);
    status = omc_edit_tombstone(&edits[1], 1U);
    assert(status == OMC_STATUS_OK);

    status = omc_edit_commit(&base, edits, 2U, &out);
    assert(status == OMC_STATUS_OK);
    assert(out.entry_count == 2U);
    assert(out.entries[0].flags == OMC_ENTRY_FLAG_DIRTY);
    assert(out.entries[0].value.kind == OMC_VAL_TEXT);
    expect_text_ref(&out.arena, out.entries[0].value.u.ref, "hello");
    assert((out.entries[1].flags & OMC_ENTRY_FLAG_DELETED) != 0U);
    assert((out.entries[1].flags & OMC_ENTRY_FLAG_DIRTY) != 0U);

    omc_edit_fini(&edits[1]);
    omc_edit_fini(&edits[0]);
    omc_store_fini(&out);
    omc_store_fini(&base);
}

static void
test_compact_removes_deleted_entries(void)
{
    omc_store base;
    omc_store out;
    omc_entry entry;
    omc_status status;
    omc_byte_ref field_ref;

    omc_store_init(&base);
    omc_store_init(&out);

    field_ref = append_bytes(&base.arena, "version");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_printim_field(&entry.key, field_ref);
    omc_val_make_u32(&entry.value, 1U);
    status = omc_store_add_entry(&base, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_comment(&entry.key);
    omc_val_make_u32(&entry.value, 2U);
    entry.flags = OMC_ENTRY_FLAG_DELETED;
    status = omc_store_add_entry(&base, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    status = omc_store_compact(&base, &out);
    assert(status == OMC_STATUS_OK);
    assert(out.entry_count == 1U);
    assert(out.entries[0].key.kind == OMC_KEY_PRINTIM_FIELD);
    assert(out.entries[0].value.u.u64 == 1U);
    expect_text_ref(&out.arena, out.entries[0].key.u.printim_field.field,
                    "version");

    omc_store_fini(&out);
    omc_store_fini(&base);
}

int
main(void)
{
    test_commit_appends_new_entry();
    test_commit_updates_and_tombstones();
    test_compact_removes_deleted_entries();
    return 0;
}
