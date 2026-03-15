#include "omc/omc_irb.h"

#include <assert.h>
#include <string.h>

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_u16be(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
}

static void
append_u32be(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
}

static void
append_text(omc_u8* out, omc_size* io_size, const char* text)
{
    omc_size n;

    n = strlen(text);
    memcpy(out + *io_size, text, n);
    *io_size += n;
}

static void
append_bytes(omc_u8* out, omc_size* io_size, const omc_u8* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
append_irb_resource(omc_u8* out, omc_size* io_size, omc_u16 resource_id,
                    const omc_u8* payload, omc_size payload_size)
{
    append_text(out, io_size, "8BIM");
    append_u16be(out, io_size, resource_id);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u32be(out, io_size, (omc_u32)payload_size);
    append_bytes(out, io_size, payload, payload_size);
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static const omc_entry*
find_irb_entry(const omc_store* store, omc_u16 resource_id)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_PHOTOSHOP_IRB
            && entry->key.u.photoshop_irb.resource_id == resource_id) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_iptc_entry(const omc_store* store, omc_u16 record, omc_u16 dataset)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_IPTC_DATASET
            && entry->key.u.iptc_dataset.record == record
            && entry->key.u.iptc_dataset.dataset == dataset) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static void
test_decode_irb_with_iptc(void)
{
    omc_u8 irb[64];
    static const omc_u8 iptc[] = { 0x1CU, 0x02U, 0x19U, 0x00U, 0x04U,
                                   (omc_u8)'t', (omc_u8)'e', (omc_u8)'s',
                                   (omc_u8)'t' };
    static const omc_u8 other[] = { 0x01U, 0x02U, 0x03U };
    omc_size irb_size;
    omc_store store;
    omc_irb_res meas;
    omc_irb_res dec;
    const omc_entry* entry;

    irb_size = 0U;
    append_irb_resource(irb, &irb_size, 0x0404U, iptc, sizeof(iptc));
    append_irb_resource(irb, &irb_size, 0x1234U, other, sizeof(other));

    omc_store_init(&store);
    meas = omc_irb_meas(irb, irb_size, (const omc_irb_opts*)0);
    dec = omc_irb_dec(irb, irb_size, &store, OMC_INVALID_BLOCK_ID,
                      (const omc_irb_opts*)0);

    assert(meas.status == OMC_IRB_OK);
    assert(dec.status == OMC_IRB_OK);
    assert(dec.resources_decoded == 2U);
    assert(dec.entries_decoded == meas.entries_decoded);
    assert(dec.iptc_entries_decoded == 1U);

    entry = find_irb_entry(&store, 0x0404U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);

    entry = find_irb_entry(&store, 0x1234U);
    assert(entry != (const omc_entry*)0);

    entry = find_iptc_entry(&store, 2U, 25U);
    assert(entry != (const omc_entry*)0);
    assert((entry->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);

    omc_store_fini(&store);
}

static void
test_irb_padding(void)
{
    omc_u8 irb[32];
    static const omc_u8 payload[] = { 0x01U, 0x02U, 0x03U };
    omc_size irb_size;
    omc_store store;
    omc_irb_res dec;

    irb_size = 0U;
    append_irb_resource(irb, &irb_size, 0x1234U, payload, sizeof(payload));
    append_u8(irb, &irb_size, 0U);
    append_u8(irb, &irb_size, 0U);

    omc_store_init(&store);
    dec = omc_irb_dec(irb, irb_size, &store, OMC_INVALID_BLOCK_ID,
                      (const omc_irb_opts*)0);
    assert(dec.status == OMC_IRB_OK);
    assert(dec.resources_decoded == 1U);
    omc_store_fini(&store);
}

int
main(void)
{
    test_decode_irb_with_iptc();
    test_irb_padding();
    return 0;
}
