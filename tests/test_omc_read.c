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
append_bytes(omc_u8* out, omc_size* io_size, const void* src, omc_size n)
{
    memcpy(out + *io_size, src, n);
    *io_size += n;
}

static void
append_text(omc_u8* out, omc_size* io_size, const char* text)
{
    append_bytes(out, io_size, text, strlen(text));
}

static void
append_u16be(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
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
write_u16be_at(omc_u8* out, omc_u32 off, omc_u16 value)
{
    out[off + 0U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 0) & 0xFFU);
}

static void
write_u32be_at(omc_u8* out, omc_u32 off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)((value >> 24) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 0) & 0xFFU);
}

static void
write_u64be_at(omc_u8* out, omc_u32 off, omc_u64 value)
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

static omc_size
make_test_tiff_le(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 6U);
    append_u32le(out, &size, 26U);
    append_u32le(out, &size, 0U);
    append_text(out, &size, "Canon");
    append_u8(out, &size, 0U);
    return size;
}

static void
build_test_icc(omc_u8* out, omc_size size)
{
    omc_u32 i;

    memset(out, 0, size);
    write_u32be_at(out, 0U, (omc_u32)size);
    write_u32be_at(out, 4U, fourcc('a', 'p', 'p', 'l'));
    write_u32be_at(out, 8U, 0x04300000U);
    write_u32be_at(out, 12U, fourcc('m', 'n', 't', 'r'));
    write_u32be_at(out, 16U, fourcc('R', 'G', 'B', ' '));
    write_u32be_at(out, 20U, fourcc('X', 'Y', 'Z', ' '));
    write_u16be_at(out, 24U, 2026U);
    write_u16be_at(out, 26U, 1U);
    write_u16be_at(out, 28U, 28U);
    out[36] = (omc_u8)'a';
    out[37] = (omc_u8)'c';
    out[38] = (omc_u8)'s';
    out[39] = (omc_u8)'p';
    write_u32be_at(out, 40U, fourcc('M', 'S', 'F', 'T'));
    write_u32be_at(out, 44U, 1U);
    write_u32be_at(out, 48U, fourcc('A', 'P', 'P', 'L'));
    write_u32be_at(out, 52U, fourcc('M', '1', '2', '3'));
    write_u64be_at(out, 56U, 1U);
    write_u32be_at(out, 64U, 1U);
    write_u32be_at(out, 68U, 63189U);
    write_u32be_at(out, 72U, 65536U);
    write_u32be_at(out, 76U, 54061U);
    write_u32be_at(out, 80U, fourcc('o', 'p', 'n', 'm'));
    write_u32be_at(out, 128U, 1U);
    write_u32be_at(out, 132U, fourcc('d', 'e', 's', 'c'));
    write_u32be_at(out, 136U, 144U);
    write_u32be_at(out, 140U, 16U);
    for (i = 0U; i < 16U; ++i) {
        out[144U + i] = (omc_u8)i;
    }
}

static omc_size
make_test_jpeg_all(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[64];
    omc_u8 icc[160];
    omc_size tiff_size;
    omc_size size;
    omc_u16 seg_len;

    tiff_size = make_test_tiff_le(tiff);
    build_test_icc(icc, sizeof(icc));

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);

    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xE1U);
    seg_len = (omc_u16)(2U + 6U + tiff_size);
    append_u16be(out, &size, seg_len);
    append_text(out, &size, "Exif");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, tiff, tiff_size);

    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xE1U);
    seg_len = (omc_u16)(2U + 29U + (sizeof(xmp) - 1U));
    append_u16be(out, &size, seg_len);
    append_text(out, &size, "http://ns.adobe.com/xap/1.0/");
    append_u8(out, &size, 0U);
    append_bytes(out, &size, xmp, sizeof(xmp) - 1U);

    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xE2U);
    seg_len = (omc_u16)(2U + 14U + sizeof(icc));
    append_u16be(out, &size, seg_len);
    append_text(out, &size, "ICC_PROFILE");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 1U);
    append_u8(out, &size, 1U);
    append_bytes(out, &size, icc, sizeof(icc));

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
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag) {
            continue;
        }
        ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_view.size == strlen(ifd_name)
            && memcmp(ifd_view.data, ifd_name, ifd_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_xmp_entry(const omc_store* store, const char* schema_ns,
               const char* property_path)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ns_view;
        omc_const_bytes path_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        ns_view = omc_arena_view(&store->arena,
                                 entry->key.u.xmp_property.schema_ns);
        path_view = omc_arena_view(&store->arena,
                                   entry->key.u.xmp_property.property_path);
        if (ns_view.size == strlen(schema_ns)
            && path_view.size == strlen(property_path)
            && memcmp(ns_view.data, schema_ns, ns_view.size) == 0
            && memcmp(path_view.data, property_path, path_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
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

static void
test_read_jpeg_all(void)
{
    omc_u8 jpeg[1024];
    omc_size jpeg_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* xmp_tool;
    const omc_entry* icc_size;
    const omc_block_info* block;

    jpeg_size = make_test_jpeg_all(jpeg);
    omc_store_init(&store);

    res = omc_read_simple(jpeg, jpeg_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.icc.status == OMC_ICC_OK);
    assert(store.block_count == 3U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    assert(exif_make->origin.block != OMC_INVALID_BLOCK_ID);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->kind == OMC_BLK_EXIF);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->kind == OMC_BLK_XMP);

    icc_size = find_icc_header(&store, 0U);
    assert(icc_size != (const omc_entry*)0);
    block = omc_store_block(&store, icc_size->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->kind == OMC_BLK_ICC);

    omc_store_fini(&store);
}

static void
test_read_standalone_xmp(void)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* xmp_tool;

    omc_store_init(&store);
    res = omc_read_simple((const omc_u8*)xmp, sizeof(xmp) - 1U, &store, blocks,
                          4U, ifds, 4U, payload, sizeof(payload),
                          payload_parts, 8U, (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_UNSUPPORTED);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(store.block_count == 1U);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    assert(omc_store_block(&store, xmp_tool->origin.block)->kind == OMC_BLK_XMP);

    omc_store_fini(&store);
}

int
main(void)
{
    test_read_jpeg_all();
    test_read_standalone_xmp();
    return 0;
}
