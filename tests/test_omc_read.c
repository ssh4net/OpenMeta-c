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

static omc_size
make_test_tiff_le(omc_u8* out)
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
    append_u32le(out, &size, 26U);
    append_u32le(out, &size, 0U);
    append_bytes(out, &size, "Canon");
    append_u8(out, &size, 0U);

    return size;
}

static omc_size
make_test_jpeg_with_exif(omc_u8* out)
{
    omc_u8 tiff[64];
    omc_size tiff_size;
    omc_size size;
    omc_u16 seg_len;

    tiff_size = make_test_tiff_le(tiff);

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);

    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xE1U);
    seg_len = (omc_u16)(2U + 6U + tiff_size);
    append_u8(out, &size, (omc_u8)((seg_len >> 8) & 0xFFU));
    append_u8(out, &size, (omc_u8)((seg_len >> 0) & 0xFFU));
    append_bytes(out, &size, "Exif");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    memcpy(out + size, tiff, tiff_size);
    size += tiff_size;

    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD9U);
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
test_read_jpeg_exif(void)
{
    omc_u8 jpeg[128];
    omc_size jpeg_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[128];
    omc_u32 payload_parts[16];
    omc_read_opts opts;
    omc_read_res res;
    const omc_entry* make;
    omc_const_bytes view;

    jpeg_size = make_test_jpeg_with_exif(jpeg);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.include_pointer_tags = 1;

    res = omc_read_simple(jpeg, jpeg_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.entries_added == 1U);

    make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(make != (const omc_entry*)0);
    assert(make->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, make->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "Canon", 5U) == 0);

    omc_store_fini(&store);
}

static void
test_read_tiff_direct(void)
{
    omc_u8 tiff[64];
    omc_size tiff_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* make;

    tiff_size = make_test_tiff_le(tiff);
    omc_store_init(&store);

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(make != (const omc_entry*)0);

    omc_store_fini(&store);
}

int
main(void)
{
    test_read_jpeg_exif();
    test_read_tiff_direct();
    return 0;
}
