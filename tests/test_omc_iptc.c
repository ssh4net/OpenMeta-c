#include "omc/omc_iptc.h"

#include "omc_test_assert.h"
#include <string.h>

static const omc_entry*
find_iptc_dataset(const omc_store* store, omc_u16 record, omc_u16 dataset,
                  omc_u32 ordinal)
{
    omc_size i;
    omc_u32 seen;

    seen = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_IPTC_DATASET) {
            continue;
        }
        if (entry->key.u.iptc_dataset.record != record
            || entry->key.u.iptc_dataset.dataset != dataset) {
            continue;
        }
        if (seen == ordinal) {
            return entry;
        }
        seen += 1U;
    }

    return (const omc_entry*)0;
}

static void
test_decode_iptc(void)
{
    static const omc_u8 iptc[] = {
        0x1CU, 0x02U, 0x19U, 0x00U, 0x05U, (omc_u8)'h',
        (omc_u8)'e', (omc_u8)'l', (omc_u8)'l', (omc_u8)'o',
        0x1CU, 0x02U, 0x19U, 0x80U, 0x02U, 0x00U,
        0x03U, (omc_u8)'a', (omc_u8)'b', (omc_u8)'c'
    };
    omc_store store;
    omc_iptc_res meas;
    omc_iptc_res dec;
    const omc_entry* entry;
    omc_const_bytes view;

    omc_store_init(&store);

    meas = omc_iptc_meas(iptc, sizeof(iptc), (const omc_iptc_opts*)0);
    dec = omc_iptc_dec(iptc, sizeof(iptc), &store, OMC_INVALID_BLOCK_ID,
                       OMC_ENTRY_FLAG_NONE, (const omc_iptc_opts*)0);

    assert(meas.status == OMC_IPTC_OK);
    assert(dec.status == OMC_IPTC_OK);
    assert(dec.entries_decoded == 2U);
    assert(dec.entries_decoded == meas.entries_decoded);

    entry = find_iptc_dataset(&store, 2U, 25U, 0U);
    assert(entry != (const omc_entry*)0);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "hello", 5U) == 0);

    entry = find_iptc_dataset(&store, 2U, 25U, 1U);
    assert(entry != (const omc_entry*)0);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 3U);
    assert(memcmp(view.data, "abc", 3U) == 0);

    omc_store_fini(&store);
}

static void
test_iptc_limits_and_unsupported(void)
{
    static const omc_u8 bad[] = { 0x00U, 0x01U, 0x02U, 0x03U };
    static const omc_u8 two[] = {
        0x1CU, 0x02U, 0x19U, 0x00U, 0x01U, (omc_u8)'a',
        0x1CU, 0x02U, 0x1AU, 0x00U, 0x01U, (omc_u8)'b'
    };
    omc_store store;
    omc_iptc_opts opts;
    omc_iptc_res res;

    omc_store_init(&store);
    res = omc_iptc_dec(bad, sizeof(bad), &store, OMC_INVALID_BLOCK_ID,
                       OMC_ENTRY_FLAG_NONE, (const omc_iptc_opts*)0);
    assert(res.status == OMC_IPTC_UNSUPPORTED);
    omc_store_fini(&store);

    omc_store_init(&store);
    omc_iptc_opts_init(&opts);
    opts.limits.max_datasets = 1U;
    res = omc_iptc_dec(two, sizeof(two), &store, OMC_INVALID_BLOCK_ID,
                       OMC_ENTRY_FLAG_NONE, &opts);
    assert(res.status == OMC_IPTC_LIMIT);
    omc_store_fini(&store);
}

int
main(void)
{
    test_decode_iptc();
    test_iptc_limits_and_unsupported();
    return 0;
}
