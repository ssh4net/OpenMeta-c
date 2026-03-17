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
append_raw(omc_u8* out, omc_size* io_size, const void* src, omc_size n)
{
    memcpy(out + *io_size, src, n);
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

static omc_size
make_test_tiff_with_makernote_count(omc_u8* out, const omc_u8* makernote,
                                    omc_size makernote_size,
                                    omc_u32 makernote_count)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x927CU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, makernote_count);
    append_u32le(out, &size, 26U);
    append_u32le(out, &size, 0U);
    append_raw(out, &size, makernote, makernote_size);
    return size;
}

static omc_size
make_test_tiff_with_makernote(omc_u8* out, const omc_u8* makernote,
                              omc_size makernote_size)
{
    return make_test_tiff_with_makernote_count(out, makernote, makernote_size,
                                               (omc_u32)makernote_size);
}

static omc_size
make_test_tiff_with_make_and_makernote_count(omc_u8* out, const char* make,
                                             const omc_u8* makernote,
                                             omc_size makernote_size,
                                             omc_u32 makernote_count)
{
    omc_size size;
    omc_size make_size;
    omc_u32 make_off;
    omc_u32 maker_off;

    make_size = strlen(make) + 1U;
    make_off = 38U;
    maker_off = make_off + (omc_u32)make_size;

    size = 0U;
    append_bytes(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 2U);

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)make_size);
    append_u32le(out, &size, make_off);

    append_u16le(out, &size, 0x927CU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, makernote_count);
    append_u32le(out, &size, maker_off);

    append_u32le(out, &size, 0U);
    append_bytes(out, &size, make);
    append_u8(out, &size, 0U);
    append_raw(out, &size, makernote, makernote_size);
    return size;
}

static omc_size
make_fuji_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_raw(out, &size, "FUJIFILM", 8U);
    append_u32le(out, &size, 12U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x0001U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x00000042U);
    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_fuji_makernote_extended(omc_u8* out)
{
    omc_size size;
    omc_u32 version_off;
    omc_u32 focus_off;

    size = 0U;
    append_raw(out, &size, "FUJIFILM", 8U);
    append_u32le(out, &size, 12U);

    append_u16le(out, &size, 4U);
    version_off = 12U + 2U + (4U * 12U) + 4U;
    focus_off = version_off + 5U;

    append_u16le(out, &size, 0x0000U);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 5U);
    append_u32le(out, &size, version_off);

    append_u16le(out, &size, 0x1000U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 1U);
    append_u16le(out, &size, 2U);
    append_u16le(out, &size, 0U);

    append_u16le(out, &size, 0x1023U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 3U);
    append_u32le(out, &size, focus_off);

    append_u16le(out, &size, 0x1438U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 321U);

    append_u32le(out, &size, 0U);
    append_bytes(out, &size, "0130");
    append_u8(out, &size, 0U);
    append_u16le(out, &size, 100U);
    append_u16le(out, &size, 200U);
    append_u16le(out, &size, 300U);
    return size;
}

static omc_size
make_fuji_ge2_makernote(omc_u8* out)
{
    static const omc_u8 ge2_magic[10] = { 'G', 'E', 0x0CU, 0U, 0U,
                                          0U, 0x16U, 0U, 0U, 0U };
    omc_size entry0;

    memset(out, 0, 318U);
    memcpy(out, ge2_magic, sizeof(ge2_magic));

    entry0 = 14U;
    out[entry0 + 0U] = 0x01U;
    out[entry0 + 1U] = 0x00U;
    out[entry0 + 2U] = 0x03U;
    out[entry0 + 3U] = 0x00U;
    out[entry0 + 4U] = 0x01U;
    out[entry0 + 5U] = 0x00U;
    out[entry0 + 6U] = 0x00U;
    out[entry0 + 7U] = 0x00U;
    out[entry0 + 8U] = 0x42U;
    out[entry0 + 9U] = 0x00U;
    out[entry0 + 10U] = 0x00U;
    out[entry0 + 11U] = 0x00U;
    return 318U;
}

static omc_size
make_fuji_ge2_makernote_extended(omc_u8* out)
{
    omc_size size;
    omc_size entry1;

    size = make_fuji_ge2_makernote(out);
    entry1 = 26U;
    out[entry1 + 0U] = 0x04U;
    out[entry1 + 1U] = 0x13U;
    out[entry1 + 2U] = 0x04U;
    out[entry1 + 3U] = 0x00U;
    out[entry1 + 4U] = 0x01U;
    out[entry1 + 5U] = 0x00U;
    out[entry1 + 6U] = 0x00U;
    out[entry1 + 7U] = 0x00U;
    out[entry1 + 8U] = 0x44U;
    out[entry1 + 9U] = 0x33U;
    out[entry1 + 10U] = 0x22U;
    out[entry1 + 11U] = 0x11U;
    return size;
}

static omc_size
make_canon_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x0001U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x12345678U);
    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_canon_camera_settings_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x0001U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 3U);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);
    append_u16le(out, &size, 0U);
    append_u16le(out, &size, 11U);
    append_u16le(out, &size, 22U);
    return size;
}

static omc_size
make_nikon_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_raw(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 2U);
    append_u16le(out, &size, 0x0001U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x01020304U);
    append_u16le(out, &size, 0x001FU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 8U);
    append_u32le(out, &size, 38U);
    append_u32le(out, &size, 0U);
    append_bytes(out, &size, "0101");
    append_u8(out, &size, 1U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
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
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);

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

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
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
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
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
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
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
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_MALFORMED);
    assert(store.entry_count == 0U);
    omc_store_fini(&store);

    tiff_size = make_test_tiff_le(tiff);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.include_pointer_tags = 1;
    opts.limits.max_total_entries = 2U;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_LIMIT);
    assert(res.limit_reason == OMC_EXIF_LIM_MAX_ENTRIES_TOTAL);
    omc_store_fini(&store);
}

static void
test_fuji_makernote_signature(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_fuji_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_fuji0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0x42U);

    omc_store_fini(&store);
}

static void
test_fuji_makernote_extended(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* version;
    const omc_entry* focus;
    const omc_entry* scalar;
    omc_const_bytes view;

    makernote_size = make_fuji_makernote_extended(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    version = find_exif_entry(&store, "mk_fuji0", 0x0000U);
    assert(version != (const omc_entry*)0);
    assert(version->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, version->value.u.ref);
    assert(view.size == 4U);
    assert(memcmp(view.data, "0130", 4U) == 0);

    focus = find_exif_entry(&store, "mk_fuji0", 0x1023U);
    assert(focus != (const omc_entry*)0);
    assert(focus->value.kind == OMC_VAL_ARRAY);
    assert(focus->value.elem_type == OMC_ELEM_U16);
    assert(focus->value.count == 3U);
    view = omc_arena_view(&store.arena, focus->value.u.ref);
    assert(view.size == 6U);

    scalar = find_exif_entry(&store, "mk_fuji0", 0x1438U);
    assert(scalar != (const omc_entry*)0);
    assert(scalar->value.kind == OMC_VAL_SCALAR);
    assert(scalar->value.elem_type == OMC_ELEM_U32);
    assert(scalar->value.u.u64 == 321U);

    omc_store_fini(&store);
}

static void
test_fuji_ge2_makernote(void)
{
    omc_u8 makernote[384];
    omc_u8 tiff[512];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;
    const omc_entry* entry2;

    makernote_size = make_fuji_ge2_makernote_extended(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_fuji0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0x42U);

    entry2 = find_exif_entry(&store, "mk_fuji0", 0x1304U);
    assert(entry2 != (const omc_entry*)0);
    assert(entry2->value.kind == OMC_VAL_SCALAR);
    assert(entry2->value.elem_type == OMC_ELEM_U32);
    assert(entry2->value.u.u64 == 0x11223344U);

    omc_store_fini(&store);
}

static void
test_canon_makernote_root(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x12345678U);

    omc_store_fini(&store);
}

static void
test_canon_camera_settings_makernote(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_camera_settings_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_camerasettings_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert((entry->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 22U);

    omc_store_fini(&store);
}

static void
test_nikon_makernote_root_and_vrinfo(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* root;
    const omc_entry* vr_mode;

    makernote_size = make_nikon_makernote(makernote);
    tiff_size = make_test_tiff_with_makernote(tiff, makernote, makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    root = find_exif_entry(&store, "mk_nikon0", 0x0001U);
    assert(root != (const omc_entry*)0);
    assert(root->value.kind == OMC_VAL_SCALAR);
    assert(root->value.elem_type == OMC_ELEM_U32);
    assert(root->value.u.u64 == 0x01020304U);

    vr_mode = find_exif_entry(&store, "mk_nikon_vrinfo_0", 0x0006U);
    assert(vr_mode != (const omc_entry*)0);
    assert((vr_mode->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    assert(vr_mode->value.kind == OMC_VAL_SCALAR);
    assert(vr_mode->value.elem_type == OMC_ELEM_U8);
    assert(vr_mode->value.u.u64 == 2U);

    omc_store_fini(&store);
}

int
main(void)
{
    test_decode_le_and_measure();
    test_decode_be();
    test_utf8_and_ascii_bytes();
    test_bad_offset_and_limit();
    test_fuji_makernote_signature();
    test_fuji_makernote_extended();
    test_fuji_ge2_makernote();
    test_canon_makernote_root();
    test_canon_camera_settings_makernote();
    test_nikon_makernote_root_and_vrinfo();
    return 0;
}
