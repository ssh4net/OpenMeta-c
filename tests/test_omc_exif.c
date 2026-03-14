#include "omc/omc_exif.h"
#include "omc/omc_read.h"

#include <assert.h>
#include <string.h>

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_bytes(omc_u8* out, omc_size* io_size, const char* text)
{
    omc_size n;

    n = strlen(text);
    memcpy(out + *io_size, text, n);
    *io_size += n;
}

static void
append_u16le(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
}

static void
append_u32le(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
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

static omc_size
make_test_tiff_le(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);

    append_u16le(out, &size, 2U);

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 6U);
    append_u32le(out, &size, 38U);

    append_u16le(out, &size, 0x8769U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 44U);

    append_u32le(out, &size, 0U);

    append_bytes(out, &size, "Canon");
    append_u8(out, &size, 0U);

    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x9003U);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 20U);
    append_u32le(out, &size, 62U);
    append_u32le(out, &size, 0U);

    append_bytes(out, &size, "2024:01:01 00:00:00");
    append_u8(out, &size, 0U);

    return size;
}

static omc_size
make_test_tiff_be(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "MM");
    append_u16be(out, &size, 42U);
    append_u32be(out, &size, 8U);

    append_u16be(out, &size, 2U);

    append_u16be(out, &size, 0x010FU);
    append_u16be(out, &size, 2U);
    append_u32be(out, &size, 6U);
    append_u32be(out, &size, 38U);

    append_u16be(out, &size, 0x8769U);
    append_u16be(out, &size, 4U);
    append_u32be(out, &size, 1U);
    append_u32be(out, &size, 44U);

    append_u32be(out, &size, 0U);

    append_bytes(out, &size, "Canon");
    append_u8(out, &size, 0U);

    append_u16be(out, &size, 1U);
    append_u16be(out, &size, 0x9003U);
    append_u16be(out, &size, 2U);
    append_u32be(out, &size, 20U);
    append_u32be(out, &size, 62U);
    append_u32be(out, &size, 0U);

    append_bytes(out, &size, "2024:01:01 00:00:00");
    append_u8(out, &size, 0U);

    return size;
}

static omc_size
make_utf8_inline_tiff(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x010EU);
    append_u16le(out, &size, 129U);
    append_u32le(out, &size, 3U);
    append_u8(out, &size, 'H');
    append_u8(out, &size, 'i');
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_ascii_nul_tiff(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x010EU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 4U);
    append_u8(out, &size, 'A');
    append_u8(out, &size, 0U);
    append_u8(out, &size, 'B');
    append_u8(out, &size, 0U);
    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_bad_offset_tiff(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 6U);
    append_u32le(out, &size, 0x1000U);
    append_u32le(out, &size, 0U);
    return size;
}

static const omc_entry*
find_exif_entry(const omc_store* store, const char* ifd_name, omc_u16 tag)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ifd;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        if (entry->key.u.exif_tag.tag != tag) {
            continue;
        }
        ifd = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd.size == strlen(ifd_name)
            && memcmp(ifd.data, ifd_name, ifd.size) == 0) {
            return entry;
        }
    }

    return (const omc_entry*)0;
}

static void
test_decode_le_and_measure(void)
{
    omc_u8 tiff[128];
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    omc_exif_res meas;
    const omc_entry* make;
    const omc_entry* ptr;
    const omc_entry* dt;
    omc_const_bytes view;

    tiff_size = make_test_tiff_le(tiff);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.include_pointer_tags = 1;

    meas = omc_exif_meas(tiff, tiff_size, &opts);
    res = omc_exif_dec(tiff, tiff_size, &store, ifds, 8U, &opts);

    assert(meas.status == OMC_EXIF_OK);
    assert(meas.ifds_needed == 2U);
    assert(meas.entries_decoded == 3U);
    assert(res.status == OMC_EXIF_OK);
    assert(res.ifds_written == 2U);
    assert(res.entries_decoded == 3U);

    make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(make != (const omc_entry*)0);
    assert(make->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, make->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "Canon", 5U) == 0);

    ptr = find_exif_entry(&store, "ifd0", 0x8769U);
    assert(ptr != (const omc_entry*)0);
    assert(ptr->value.kind == OMC_VAL_SCALAR);
    assert(ptr->value.elem_type == OMC_ELEM_U32);
    assert(ptr->value.u.u64 == 44U);

    dt = find_exif_entry(&store, "exififd", 0x9003U);
    assert(dt != (const omc_entry*)0);
    assert(dt->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, dt->value.u.ref);
    assert(view.size == 19U);
    assert(memcmp(view.data, "2024:01:01 00:00:00", 19U) == 0);

    omc_store_fini(&store);
}

static void
test_decode_be(void)
{
    omc_u8 tiff[128];
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* make;
    const omc_entry* dt;
    omc_const_bytes view;

    tiff_size = make_test_tiff_be(tiff);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.include_pointer_tags = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, ifds, 8U, &opts);
    assert(res.status == OMC_EXIF_OK);
    assert(res.entries_decoded == 3U);

    make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(make != (const omc_entry*)0);
    view = omc_arena_view(&store.arena, make->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "Canon", 5U) == 0);

    dt = find_exif_entry(&store, "exififd", 0x9003U);
    assert(dt != (const omc_entry*)0);
    view = omc_arena_view(&store.arena, dt->value.u.ref);
    assert(view.size == 19U);
    assert(memcmp(view.data, "2024:01:01 00:00:00", 19U) == 0);

    omc_store_fini(&store);
}

static void
test_utf8_and_ascii_bytes(void)
{
    omc_u8 tiff[64];
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* e;
    omc_const_bytes view;

    omc_store_init(&store);
    omc_exif_opts_init(&opts);

    tiff_size = make_utf8_inline_tiff(tiff);
    res = omc_exif_dec(tiff, tiff_size, &store, (omc_exif_ifd_ref*)0, 0U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    e = find_exif_entry(&store, "ifd0", 0x010EU);
    assert(e != (const omc_entry*)0);
    assert(e->value.kind == OMC_VAL_TEXT);
    assert(e->value.text_encoding == OMC_TEXT_UTF8);
    view = omc_arena_view(&store.arena, e->value.u.ref);
    assert(view.size == 2U);
    assert(memcmp(view.data, "Hi", 2U) == 0);
    omc_store_fini(&store);

    omc_store_init(&store);
    tiff_size = make_ascii_nul_tiff(tiff);
    res = omc_exif_dec(tiff, tiff_size, &store, (omc_exif_ifd_ref*)0, 0U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    e = find_exif_entry(&store, "ifd0", 0x010EU);
    assert(e != (const omc_entry*)0);
    assert(e->value.kind == OMC_VAL_BYTES);
    view = omc_arena_view(&store.arena, e->value.u.ref);
    assert(view.size == 4U);
    assert(view.data[0] == 'A');
    assert(view.data[1] == 0U);
    assert(view.data[2] == 'B');
    assert(view.data[3] == 0U);
    omc_store_fini(&store);
}

static void
test_bad_offset_and_limit(void)
{
    omc_u8 tiff[128];
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;

    tiff_size = make_bad_offset_tiff(tiff);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    res = omc_exif_dec(tiff, tiff_size, &store, (omc_exif_ifd_ref*)0, 0U,
                       &opts);
    assert(res.status == OMC_EXIF_MALFORMED);
    assert(store.entry_count == 0U);
    omc_store_fini(&store);

    tiff_size = make_test_tiff_le(tiff);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.include_pointer_tags = 1;
    opts.limits.max_total_entries = 2U;
    res = omc_exif_dec(tiff, tiff_size, &store, (omc_exif_ifd_ref*)0, 0U,
                       &opts);
    assert(res.status == OMC_EXIF_LIMIT);
    assert(res.limit_reason == OMC_EXIF_LIM_MAX_ENTRIES_TOTAL);
    omc_store_fini(&store);
}

int
main(void)
{
    test_decode_le_and_measure();
    test_decode_be();
    test_utf8_and_ascii_bytes();
    test_bad_offset_and_limit();
    return 0;
}
