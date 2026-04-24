#include "omc/omc_icc.h"

#include "omc_test_assert.h"
#include <string.h>

static void
write_u16be(omc_u8* out, omc_u32 off, omc_u16 value)
{
    out[off + 0U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 0) & 0xFFU);
}

static void
write_u32be(omc_u8* out, omc_u32 off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)((value >> 24) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 0) & 0xFFU);
}

static void
write_u64be(omc_u8* out, omc_u32 off, omc_u64 value)
{
    out[off + 0U] = (omc_u8)((value >> 56) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 48) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 40) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 32) & 0xFFU);
    out[off + 4U] = (omc_u8)((value >> 24) & 0xFFU);
    out[off + 5U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 6U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 7U] = (omc_u8)((value >> 0) & 0xFFU);
}

static omc_u32
fourcc(char a, char b, char c, char d)
{
    return OMC_FOURCC(a, b, c, d);
}

static void
build_test_icc(omc_u8* out, omc_size size)
{
    omc_u32 i;

    memset(out, 0, size);
    write_u32be(out, 0U, (omc_u32)size);
    write_u32be(out, 4U, fourcc('a', 'p', 'p', 'l'));
    write_u32be(out, 8U, 0x04300000U);
    write_u32be(out, 12U, fourcc('m', 'n', 't', 'r'));
    write_u32be(out, 16U, fourcc('R', 'G', 'B', ' '));
    write_u32be(out, 20U, fourcc('X', 'Y', 'Z', ' '));
    write_u16be(out, 24U, 2026U);
    write_u16be(out, 26U, 1U);
    write_u16be(out, 28U, 28U);
    out[36] = (omc_u8)'a';
    out[37] = (omc_u8)'c';
    out[38] = (omc_u8)'s';
    out[39] = (omc_u8)'p';
    write_u32be(out, 40U, fourcc('M', 'S', 'F', 'T'));
    write_u32be(out, 44U, 1U);
    write_u32be(out, 48U, fourcc('A', 'P', 'P', 'L'));
    write_u32be(out, 52U, fourcc('M', '1', '2', '3'));
    write_u64be(out, 56U, 1U);
    write_u32be(out, 64U, 1U);
    write_u32be(out, 68U, 63189U);
    write_u32be(out, 72U, 65536U);
    write_u32be(out, 76U, 54061U);
    write_u32be(out, 80U, fourcc('o', 'p', 'n', 'm'));
    write_u32be(out, 128U, 1U);
    write_u32be(out, 132U, fourcc('d', 'e', 's', 'c'));
    write_u32be(out, 136U, 144U);
    write_u32be(out, 140U, 16U);
    for (i = 0U; i < 16U; ++i) {
        out[144U + i] = (omc_u8)i;
    }
}

static const omc_entry*
find_icc_header(const omc_store* store, omc_u32 offset)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_ICC_HEADER_FIELD
            && entry->key.u.icc_header_field.offset == offset) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_icc_tag(const omc_store* store, omc_u32 signature)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_ICC_TAG
            && entry->key.u.icc_tag.signature == signature) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static void
test_decode_icc(void)
{
    omc_u8 icc[160];
    omc_store store;
    omc_icc_res meas;
    omc_icc_res dec;
    const omc_entry* e;
    omc_const_bytes view;

    build_test_icc(icc, sizeof(icc));
    omc_store_init(&store);

    meas = omc_icc_meas(icc, sizeof(icc), (const omc_icc_opts*)0);
    dec = omc_icc_dec(icc, sizeof(icc), &store, OMC_INVALID_BLOCK_ID,
                      (const omc_icc_opts*)0);

    assert(meas.status == OMC_ICC_OK);
    assert(dec.status == OMC_ICC_OK);
    assert(dec.entries_decoded == meas.entries_decoded);

    e = find_icc_header(&store, 0U);
    assert(e != (const omc_entry*)0);
    assert(e->value.kind == OMC_VAL_SCALAR);
    assert(e->value.elem_type == OMC_ELEM_U32);
    assert(e->value.u.u64 == sizeof(icc));

    e = find_icc_header(&store, 56U);
    assert(e != (const omc_entry*)0);
    assert(e->value.kind == OMC_VAL_SCALAR);
    assert(e->value.elem_type == OMC_ELEM_U64);
    assert(e->value.u.u64 == 1U);

    e = find_icc_tag(&store, fourcc('d', 'e', 's', 'c'));
    assert(e != (const omc_entry*)0);
    assert(e->value.kind == OMC_VAL_BYTES);
    view = omc_arena_view(&store.arena, e->value.u.ref);
    assert(view.size == 16U);
    assert(view.data[0] == 0U);
    assert(view.data[15] == 15U);

    omc_store_fini(&store);
}

int
main(void)
{
    test_decode_icc();
    return 0;
}
