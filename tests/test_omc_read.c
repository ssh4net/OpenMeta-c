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
append_zeroes(omc_u8* out, omc_size* io_size, omc_size count)
{
    memset(out + *io_size, 0, count);
    *io_size += count;
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
append_u32be(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
}

static void
append_jpeg_segment(omc_u8* out, omc_size* io_size, omc_u16 marker,
                    const omc_u8* payload, omc_size payload_size)
{
    append_u8(out, io_size, 0xFFU);
    append_u8(out, io_size, (omc_u8)(marker & 0xFFU));
    append_u16be(out, io_size, (omc_u16)(payload_size + 2U));
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
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
write_u32le_at(omc_u8* out, omc_u32 off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)((value >> 0) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 24) & 0xFFU);
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

static void
append_png_chunk(omc_u8* out, omc_size* io_size, const char* type,
                 const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)payload_size);
    append_bytes(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
    append_u32be(out, io_size, 0U);
}

static void
append_webp_chunk(omc_u8* out, omc_size* io_size, const char* type,
                  const omc_u8* payload, omc_size payload_size)
{
    append_bytes(out, io_size, type, 4U);
    append_u32le(out, io_size, (omc_u32)payload_size);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static void
append_gif_sub_blocks(omc_u8* out, omc_size* io_size, const omc_u8* payload,
                      omc_size payload_size)
{
    omc_size off;

    off = 0U;
    while (off < payload_size) {
        omc_size chunk_size;

        chunk_size = payload_size - off;
        if (chunk_size > 255U) {
            chunk_size = 255U;
        }
        append_u8(out, io_size, (omc_u8)chunk_size);
        append_bytes(out, io_size, payload + off, chunk_size);
        off += chunk_size;
    }
    append_u8(out, io_size, 0U);
}

static void
append_fullbox_header(omc_u8* out, omc_size* io_size, omc_u8 version)
{
    append_u8(out, io_size, version);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
}

static void
append_bmff_box(omc_u8* out, omc_size* io_size, omc_u32 type,
                const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)(8U + payload_size));
    append_u32be(out, io_size, type);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static omc_u32
adler32_bytes(const omc_u8* data, omc_size size)
{
    omc_u32 s1;
    omc_u32 s2;
    omc_size i;

    s1 = 1U;
    s2 = 0U;
    for (i = 0U; i < size; ++i) {
        s1 = (s1 + data[i]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    return (s2 << 16) | s1;
}

static omc_size
make_zlib_store_stream(omc_u8* out, const omc_u8* payload, omc_size payload_size)
{
    omc_u16 len;
    omc_u16 nlen;
    omc_u32 adler;
    omc_size size;

    assert(payload_size <= 65535U);

    len = (omc_u16)payload_size;
    nlen = (omc_u16)~len;
    adler = adler32_bytes(payload, payload_size);

    size = 0U;
    append_u8(out, &size, 0x78U);
    append_u8(out, &size, 0x01U);
    append_u8(out, &size, 0x01U);
    append_u8(out, &size, (omc_u8)(len & 0xFFU));
    append_u8(out, &size, (omc_u8)((len >> 8) & 0xFFU));
    append_u8(out, &size, (omc_u8)(nlen & 0xFFU));
    append_u8(out, &size, (omc_u8)((nlen >> 8) & 0xFFU));
    append_bytes(out, &size, payload, payload_size);
    append_u8(out, &size, (omc_u8)((adler >> 24) & 0xFFU));
    append_u8(out, &size, (omc_u8)((adler >> 16) & 0xFFU));
    append_u8(out, &size, (omc_u8)((adler >> 8) & 0xFFU));
    append_u8(out, &size, (omc_u8)((adler >> 0) & 0xFFU));
    return size;
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

static omc_size
make_test_tiff_geotiff(omc_u8* out)
{
    static const omc_u8 geodouble_bits[8] = {
        0x00U, 0x00U, 0x00U, 0x40U, 0xA6U, 0x54U, 0x58U, 0x41U
    };
    omc_size size;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 3U);

    append_u16le(out, &size, 0x87AFU);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 16U);
    append_u32le(out, &size, 50U);

    append_u16le(out, &size, 0x87B0U);
    append_u16le(out, &size, 12U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 82U);

    append_u16le(out, &size, 0x87B1U);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 13U);
    append_u32le(out, &size, 90U);

    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0U);
    append_u16le(out, &size, 3U);

    append_u16le(out, &size, 1024U);
    append_u16le(out, &size, 0U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 2U);

    append_u16le(out, &size, 1026U);
    append_u16le(out, &size, 0x87B1U);
    append_u16le(out, &size, 13U);
    append_u16le(out, &size, 0U);

    append_u16le(out, &size, 2057U);
    append_u16le(out, &size, 0x87B0U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0U);

    append_bytes(out, &size, geodouble_bits, sizeof(geodouble_bits));
    append_text(out, &size, "TestCitation|");
    return size;
}

static omc_size
make_test_tiff_printim(omc_u8* out)
{
    static const omc_u8 printim_magic[8] = {
        'P', 'r', 'i', 'n', 't', 'I', 'M', 0
    };
    omc_size size;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);

    append_u16le(out, &size, 0xC4A5U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 22U);
    append_u32le(out, &size, 26U);

    append_u32le(out, &size, 0U);
    append_bytes(out, &size, printim_magic, sizeof(printim_magic));
    append_text(out, &size, "0300");
    append_u16le(out, &size, 0U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x0001U);
    append_u32le(out, &size, 0x00160016U);
    return size;
}

static omc_size
make_test_tiff_with_makernote_count(omc_u8* out, const omc_u8* makernote,
                                    omc_size makernote_size,
                                    omc_u32 makernote_count)
{
    omc_size size;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x927CU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, makernote_count);
    append_u32le(out, &size, 26U);
    append_u32le(out, &size, 0U);
    append_bytes(out, &size, makernote, makernote_size);
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
    append_text(out, &size, "II");
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
    append_text(out, &size, make);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, makernote, makernote_size);
    return size;
}

static omc_size
make_fuji_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "FUJIFILM", 8U);
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
make_nikon_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_text(out, &size, "II");
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
    append_text(out, &size, "0101");
    append_u8(out, &size, 1U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 2U);
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

static void
append_irb_resource(omc_u8* out, omc_size* io_size, omc_u16 resource_id,
                    const omc_u8* payload, omc_size payload_size)
{
    append_text(out, io_size, "8BIM");
    append_u16be(out, io_size, resource_id);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, (omc_u8)((payload_size >> 24) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((payload_size >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((payload_size >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((payload_size >> 0) & 0xFFU));
    append_bytes(out, io_size, payload, payload_size);
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
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
    omc_u8 irb[64];
    static const omc_u8 iptc[] = { 0x1CU, 0x02U, 0x19U, 0x00U, 0x04U,
                                   (omc_u8)'t', (omc_u8)'e', (omc_u8)'s',
                                   (omc_u8)'t' };
    static const omc_u8 other_irb[] = { 0x01U, 0x02U, 0x03U };
    omc_size tiff_size;
    omc_size irb_size;
    omc_size size;
    omc_u16 seg_len;

    tiff_size = make_test_tiff_le(tiff);
    build_test_icc(icc, sizeof(icc));
    irb_size = 0U;
    append_irb_resource(irb, &irb_size, 0x0404U, iptc, sizeof(iptc));
    append_irb_resource(irb, &irb_size, 0x1234U, other_irb, sizeof(other_irb));

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
    append_u8(out, &size, 0xEDU);
    seg_len = (omc_u16)(2U + 14U + irb_size);
    append_u16be(out, &size, seg_len);
    append_text(out, &size, "Photoshop 3.0");
    append_u8(out, &size, 0U);
    append_bytes(out, &size, irb, irb_size);

    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD9U);
    return size;
}

static omc_size
make_test_jpeg_comment(omc_u8* out)
{
    static const char comment[] = "OpenMeta JPEG comment";
    omc_size size;

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);
    append_jpeg_segment(out, &size, 0xFFFEU, (const omc_u8*)comment,
                        sizeof(comment) - 1U);
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD9U);
    return size;
}

static omc_size
make_test_png_all(omc_u8* out, int compressed_xmp)
{
    static const omc_u8 png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[64];
    omc_u8 icc[160];
    omc_u8 icc_payload[256];
    omc_u8 deflate[512];
    omc_u8 xmp_payload[512];
    omc_size deflate_size;
    omc_size icc_payload_size;
    omc_size tiff_size;
    omc_size xmp_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    xmp_size = 0U;
    append_text(xmp_payload, &xmp_size, "XML:com.adobe.xmp");
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, compressed_xmp ? 1U : 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    if (compressed_xmp) {
        deflate_size = make_zlib_store_stream(deflate, (const omc_u8*)xmp,
                                              sizeof(xmp) - 1U);
        append_bytes(xmp_payload, &xmp_size, deflate, deflate_size);
    } else {
        append_bytes(xmp_payload, &xmp_size, xmp, sizeof(xmp) - 1U);
    }

    size = 0U;
    append_bytes(out, &size, png_sig, sizeof(png_sig));
    append_png_chunk(out, &size, "eXIf", tiff, tiff_size);
    append_png_chunk(out, &size, "iTXt", xmp_payload, xmp_size);
    if (compressed_xmp) {
        build_test_icc(icc, sizeof(icc));
        deflate_size = make_zlib_store_stream(deflate, icc, sizeof(icc));
        icc_payload_size = 0U;
        append_text(icc_payload, &icc_payload_size, "icc");
        append_u8(icc_payload, &icc_payload_size, 0U);
        append_u8(icc_payload, &icc_payload_size, 0U);
        append_bytes(icc_payload, &icc_payload_size, deflate, deflate_size);
        append_png_chunk(out, &size, "iCCP", icc_payload, icc_payload_size);
    }
    append_png_chunk(out, &size, "IEND", (const omc_u8*)0, 0U);
    return size;
}

static omc_size
make_test_png_text_all(omc_u8* out)
{
    static const omc_u8 png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    static const char ztxt_text[] = "Shot A";
    omc_u8 deflate[64];
    omc_u8 text_payload[64];
    omc_u8 itxt_payload[128];
    omc_u8 ztxt_payload[128];
    omc_size deflate_size;
    omc_size text_size;
    omc_size itxt_size;
    omc_size ztxt_size;
    omc_size size;

    text_size = 0U;
    append_text(text_payload, &text_size, "Author");
    append_u8(text_payload, &text_size, 0U);
    append_text(text_payload, &text_size, "Alice");

    itxt_size = 0U;
    append_text(itxt_payload, &itxt_size, "Description");
    append_u8(itxt_payload, &itxt_size, 0U);
    append_u8(itxt_payload, &itxt_size, 0U);
    append_u8(itxt_payload, &itxt_size, 0U);
    append_text(itxt_payload, &itxt_size, "en");
    append_u8(itxt_payload, &itxt_size, 0U);
    append_text(itxt_payload, &itxt_size, "Beschreibung");
    append_u8(itxt_payload, &itxt_size, 0U);
    append_text(itxt_payload, &itxt_size, "OpenMeta PNG");

    deflate_size = make_zlib_store_stream(deflate, (const omc_u8*)ztxt_text,
                                          sizeof(ztxt_text) - 1U);
    ztxt_size = 0U;
    append_text(ztxt_payload, &ztxt_size, "Comment");
    append_u8(ztxt_payload, &ztxt_size, 0U);
    append_u8(ztxt_payload, &ztxt_size, 0U);
    append_bytes(ztxt_payload, &ztxt_size, deflate, deflate_size);

    size = 0U;
    append_bytes(out, &size, png_sig, sizeof(png_sig));
    append_png_chunk(out, &size, "tEXt", text_payload, text_size);
    append_png_chunk(out, &size, "iTXt", itxt_payload, itxt_size);
    append_png_chunk(out, &size, "zTXt", ztxt_payload, ztxt_size);
    append_png_chunk(out, &size, "IEND", (const omc_u8*)0, 0U);
    return size;
}

static omc_size
make_test_webp_all(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[64];
    omc_u8 exif_payload[96];
    omc_u8 icc[160];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    exif_size = 0U;
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);
    build_test_icc(icc, sizeof(icc));

    size = 0U;
    append_text(out, &size, "RIFF");
    append_u32le(out, &size, 0U);
    append_text(out, &size, "WEBP");
    append_webp_chunk(out, &size, "EXIF", exif_payload, exif_size);
    append_webp_chunk(out, &size, "XMP ", (const omc_u8*)xmp,
                      sizeof(xmp) - 1U);
    append_webp_chunk(out, &size, "ICCP", icc, sizeof(icc));
    write_u32le_at(out, 4U, (omc_u32)(size - 8U));
    return size;
}

static omc_size
make_test_gif_all(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    static const char comment[] = "OpenMeta GIF comment";
    omc_u8 icc[160];
    omc_size size;

    build_test_icc(icc, sizeof(icc));

    size = 0U;
    append_text(out, &size, "GIF89a");
    append_bytes(out, &size, "\x01\x00\x01\x00\x00\x00\x00", 7U);

    append_u8(out, &size, 0x21U);
    append_u8(out, &size, 0xFEU);
    append_gif_sub_blocks(out, &size, (const omc_u8*)comment,
                          sizeof(comment) - 1U);

    append_u8(out, &size, 0x21U);
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 11U);
    append_text(out, &size, "XMP DataXMP");
    append_gif_sub_blocks(out, &size, (const omc_u8*)xmp, sizeof(xmp) - 1U);

    append_u8(out, &size, 0x21U);
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 11U);
    append_text(out, &size, "ICCRGBG1012");
    append_gif_sub_blocks(out, &size, icc, sizeof(icc));

    append_u8(out, &size, 0x3BU);
    return size;
}

static omc_size
make_test_raf_all(omc_u8* out)
{
    static const char xmp_sig[] = "http://ns.adobe.com/xap/1.0/\0";
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[64];
    omc_size tiff_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    size = 0U;
    append_text(out, &size, "FUJIFILMCCD-RAW ");
    if (size < 160U) {
        append_zeroes(out, &size, 160U - size);
    }
    append_bytes(out, &size, tiff, tiff_size);
    append_bytes(out, &size, xmp_sig, sizeof(xmp_sig) - 1U);
    append_text(out, &size, xmp);
    return size;
}

static omc_size
make_test_x3f_all(omc_u8* out)
{
    omc_u8 tiff[64];
    omc_size tiff_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    size = 0U;
    append_text(out, &size, "FOVb");
    append_zeroes(out, &size, 60U);
    append_text(out, &size, "Exif");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, tiff, tiff_size);
    return size;
}

typedef struct omc_ciff_val_ent {
    omc_u16 tag;
    const omc_u8* value;
    omc_size value_size;
} omc_ciff_val_ent;

static omc_size
make_ciff_directory(omc_u8* out, const omc_ciff_val_ent* entries,
                    omc_size entry_count)
{
    omc_size size;
    omc_u32 data_off;
    omc_size i;

    size = 0U;
    append_u16le(out, &size, (omc_u16)entry_count);
    data_off = 2U + (omc_u32)(entry_count * 10U);

    for (i = 0U; i < entry_count; ++i) {
        append_u16le(out, &size, entries[i].tag);
        append_u32le(out, &size, (omc_u32)entries[i].value_size);
        append_u32le(out, &size, data_off);
        data_off += (omc_u32)entries[i].value_size;
    }

    for (i = 0U; i < entry_count; ++i) {
        append_bytes(out, &size, entries[i].value, entries[i].value_size);
    }

    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_test_crw_minimal(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_text(out, &size, "II");
    append_u32le(out, &size, 14U);
    append_text(out, &size, "HEAPCCDR");
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x4801U);
    append_text(out, &size, "CIFFTEST");
    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_test_crw_derived(omc_u8* out)
{
    omc_u8 make_model[64];
    omc_size make_model_size;
    omc_u8 subject_distance[8];
    omc_size subject_distance_size;
    omc_u8 datetime_original[8];
    omc_size datetime_original_size;
    omc_u8 dimensions_orientation[32];
    omc_size dimensions_orientation_size;
    omc_u8 dir2807[128];
    omc_u8 dir3002[64];
    omc_u8 dir300a[128];
    omc_u8 root[384];
    omc_size dir2807_size;
    omc_size dir3002_size;
    omc_size dir300a_size;
    omc_size root_size;
    omc_ciff_val_ent dir2807_entries[1];
    omc_ciff_val_ent dir3002_entries[1];
    omc_ciff_val_ent dir300a_entries[2];
    omc_ciff_val_ent root_entries[3];
    omc_size size;

    make_model_size = 0U;
    append_text(make_model, &make_model_size, "Canon");
    append_u8(make_model, &make_model_size, 0U);
    append_text(make_model, &make_model_size, "PowerShot Pro70");
    append_u8(make_model, &make_model_size, 0U);

    subject_distance_size = 0U;
    append_u32le(subject_distance, &subject_distance_size, 123U);

    datetime_original_size = 0U;
    append_u32le(datetime_original, &datetime_original_size, 1700000000U);

    dimensions_orientation_size = 0U;
    append_u32le(dimensions_orientation, &dimensions_orientation_size, 1536U);
    append_u32le(dimensions_orientation, &dimensions_orientation_size, 1024U);
    append_u32le(dimensions_orientation, &dimensions_orientation_size, 0U);
    append_u32le(dimensions_orientation, &dimensions_orientation_size, 90U);

    dir2807_entries[0].tag = 0x080AU;
    dir2807_entries[0].value = make_model;
    dir2807_entries[0].value_size = make_model_size;
    dir2807_size = make_ciff_directory(dir2807, dir2807_entries, 1U);

    dir3002_entries[0].tag = 0x1807U;
    dir3002_entries[0].value = subject_distance;
    dir3002_entries[0].value_size = subject_distance_size;
    dir3002_size = make_ciff_directory(dir3002, dir3002_entries, 1U);

    dir300a_entries[0].tag = 0x180EU;
    dir300a_entries[0].value = datetime_original;
    dir300a_entries[0].value_size = datetime_original_size;
    dir300a_entries[1].tag = 0x1810U;
    dir300a_entries[1].value = dimensions_orientation;
    dir300a_entries[1].value_size = dimensions_orientation_size;
    dir300a_size = make_ciff_directory(dir300a, dir300a_entries, 2U);

    root_entries[0].tag = 0x2807U;
    root_entries[0].value = dir2807;
    root_entries[0].value_size = dir2807_size;
    root_entries[1].tag = 0x3002U;
    root_entries[1].value = dir3002;
    root_entries[1].value_size = dir3002_size;
    root_entries[2].tag = 0x300AU;
    root_entries[2].value = dir300a;
    root_entries[2].value_size = dir300a_size;
    root_size = make_ciff_directory(root, root_entries, 3U);

    size = 0U;
    append_text(out, &size, "II");
    append_u32le(out, &size, 14U);
    append_text(out, &size, "HEAPCCDR");
    append_bytes(out, &size, root, root_size);
    return size;
}

static omc_size
make_test_jpeg_app11_jumbf(omc_u8* out, int header_only_first,
                           int out_of_order)
{
    static const omc_u8 jumb_box[] = {
        0x00U, 0x00U, 0x00U, 0x21U, 'j', 'u', 'm', 'b',
        0x00U, 0x00U, 0x00U, 0x0DU, 'j', 'u', 'm', 'd',
        'c', '2', 'p', 'a', 0x00U,
        0x00U, 0x00U, 0x00U, 0x0CU, 'c', 'b', 'o', 'r',
        0xA1U, 0x61U, 0x61U, 0x01U
    };
    omc_u8 seg1[64];
    omc_u8 seg2[64];
    omc_size seg1_size;
    omc_size seg2_size;
    omc_size split;
    omc_size size;

    split = header_only_first ? 0U : 12U;

    seg1_size = 0U;
    append_text(seg1, &seg1_size, "JP");
    append_u8(seg1, &seg1_size, 0U);
    append_u8(seg1, &seg1_size, 0U);
    append_u32be(seg1, &seg1_size, 1U);
    append_bytes(seg1, &seg1_size, jumb_box, 8U);
    append_bytes(seg1, &seg1_size, jumb_box + 8U, split);

    seg2_size = 0U;
    append_text(seg2, &seg2_size, "JP");
    append_u8(seg2, &seg2_size, 0U);
    append_u8(seg2, &seg2_size, 0U);
    append_u32be(seg2, &seg2_size, 2U);
    append_bytes(seg2, &seg2_size, jumb_box, 8U);
    append_bytes(seg2, &seg2_size, jumb_box + 8U + split,
                 sizeof(jumb_box) - 8U - split);

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);
    if (out_of_order) {
        append_jpeg_segment(out, &size, 0xFFEBU, seg2, seg2_size);
        append_jpeg_segment(out, &size, 0xFFEBU, seg1, seg1_size);
    } else {
        append_jpeg_segment(out, &size, 0xFFEBU, seg1, seg1_size);
        append_jpeg_segment(out, &size, 0xFFEBU, seg2, seg2_size);
    }
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD9U);
    return size;
}

static omc_size
make_test_bmff_all(omc_u8* out, omc_u32 major_brand)
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
    omc_u8 exif_payload[96];
    omc_u8 jumb_box[64];
    omc_u8 idat_payload[512];
    omc_u8 infe_exif[64];
    omc_u8 infe_xmp[96];
    omc_u8 infe_jumb[96];
    omc_u8 iinf_payload[384];
    omc_u8 iloc_payload[192];
    omc_u8 idat_box[544];
    omc_u8 colr_payload[256];
    omc_u8 ipco_payload[288];
    omc_u8 iprp_payload[320];
    omc_u8 meta_payload[1536];
    omc_u8 moov_box[16];
    omc_u8 ftyp_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size jumb_size;
    omc_size idat_size;
    omc_size exif_off;
    omc_size xmp_off;
    omc_size jumb_off;
    omc_size infe_exif_size;
    omc_size infe_xmp_size;
    omc_size infe_jumb_size;
    omc_size iinf_size;
    omc_size iloc_size;
    omc_size idat_box_size;
    omc_size colr_size;
    omc_size ipco_size;
    omc_size iprp_size;
    omc_size meta_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    build_test_icc(icc, sizeof(icc));
    jumb_size = 0U;
    append_u32be(jumb_box, &jumb_size, 0x00000021U);
    append_u32be(jumb_box, &jumb_size, fourcc('j', 'u', 'm', 'b'));
    append_u32be(jumb_box, &jumb_size, 0x0000000DU);
    append_u32be(jumb_box, &jumb_size, fourcc('j', 'u', 'm', 'd'));
    append_text(jumb_box, &jumb_size, "c2pa");
    append_u8(jumb_box, &jumb_size, 0U);
    append_u32be(jumb_box, &jumb_size, 0x0000000CU);
    append_u32be(jumb_box, &jumb_size, fourcc('c', 'b', 'o', 'r'));
    append_u8(jumb_box, &jumb_size, 0xA1U);
    append_u8(jumb_box, &jumb_size, 0x61U);
    append_u8(jumb_box, &jumb_size, 0x61U);
    append_u8(jumb_box, &jumb_size, 0x01U);

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 6U);
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    idat_size = 0U;
    exif_off = idat_size;
    append_bytes(idat_payload, &idat_size, exif_payload, exif_size);
    xmp_off = idat_size;
    append_bytes(idat_payload, &idat_size, xmp, sizeof(xmp) - 1U);
    jumb_off = idat_size;
    append_bytes(idat_payload, &idat_size, jumb_box, jumb_size);

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif, &infe_exif_size, 2U);
    append_u16be(infe_exif, &infe_exif_size, 1U);
    append_u16be(infe_exif, &infe_exif_size, 0U);
    append_u32be(infe_exif, &infe_exif_size, fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif, &infe_exif_size, "Exif");
    append_u8(infe_exif, &infe_exif_size, 0U);

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 0U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/rdf+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    infe_jumb_size = 0U;
    append_fullbox_header(infe_jumb, &infe_jumb_size, 2U);
    append_u16be(infe_jumb, &infe_jumb_size, 3U);
    append_u16be(infe_jumb, &infe_jumb_size, 0U);
    append_u32be(infe_jumb, &infe_jumb_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_jumb, &infe_jumb_size, "C2PA");
    append_u8(infe_jumb, &infe_jumb_size, 0U);
    append_text(infe_jumb, &infe_jumb_size, "application/jumbf");
    append_u8(infe_jumb, &infe_jumb_size, 0U);
    append_u8(infe_jumb, &infe_jumb_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 0U);
    append_u16be(iinf_payload, &iinf_size, 3U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_exif, infe_exif_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_jumb, infe_jumb_size);

    iloc_size = 0U;
    append_fullbox_header(iloc_payload, &iloc_size, 1U);
    append_u8(iloc_payload, &iloc_size, 0x44U);
    append_u8(iloc_payload, &iloc_size, 0x40U);
    append_u16be(iloc_payload, &iloc_size, 3U);

    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_size);

    append_u16be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)xmp_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)(sizeof(xmp) - 1U));

    append_u16be(iloc_payload, &iloc_size, 3U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)jumb_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)jumb_size);

    idat_box_size = 0U;
    append_bmff_box(idat_box, &idat_box_size, fourcc('i', 'd', 'a', 't'),
                    idat_payload, idat_size);

    colr_size = 0U;
    append_u32be(colr_payload, &colr_size, fourcc('p', 'r', 'o', 'f'));
    append_bytes(colr_payload, &colr_size, icc, sizeof(icc));
    ipco_size = 0U;
    append_bmff_box(ipco_payload, &ipco_size, fourcc('c', 'o', 'l', 'r'),
                    colr_payload, colr_size);
    iprp_size = 0U;
    append_bmff_box(iprp_payload, &iprp_size, fourcc('i', 'p', 'c', 'o'),
                    ipco_payload, ipco_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'l', 'o', 'c'),
                    iloc_payload, iloc_size);
    append_bytes(meta_payload, &meta_size, idat_box, idat_box_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'p', 'r', 'p'),
                    iprp_payload, iprp_size);

    moov_size = 0U;
    append_bmff_box(moov_box, &moov_size, fourcc('m', 'o', 'o', 'v'),
                    (const omc_u8*)0, 0U);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bytes(out, &size, moov_box, moov_size);
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static omc_size
make_test_bmff_fields_only(omc_u8* out, omc_u32 major_brand)
{
    omc_u8 infe_exif[64];
    omc_u8 infe_xmp[96];
    omc_u8 infe_jumb[96];
    omc_u8 iinf_payload[384];
    omc_u8 pitm_payload[16];
    omc_u8 ispe_payload[16];
    omc_u8 irot_payload[8];
    omc_u8 imir_payload[8];
    omc_u8 auxc_alpha_payload[64];
    omc_u8 auxc_depth_payload[64];
    omc_u8 ipco_payload[192];
    omc_u8 ipma_payload[48];
    omc_u8 iprp_payload[256];
    omc_u8 auxl_payload[16];
    omc_u8 dimg_payload[16];
    omc_u8 thmb_payload[16];
    omc_u8 iref_payload[128];
    omc_u8 meta_payload[1024];
    omc_u8 moov_box[16];
    omc_u8 ftyp_payload[16];
    omc_size infe_exif_size;
    omc_size infe_xmp_size;
    omc_size infe_jumb_size;
    omc_size iinf_size;
    omc_size pitm_size;
    omc_size ispe_size;
    omc_size irot_size;
    omc_size imir_size;
    omc_size auxc_alpha_size;
    omc_size auxc_depth_size;
    omc_size ipco_size;
    omc_size ipma_size;
    omc_size iprp_size;
    omc_size auxl_size;
    omc_size dimg_size;
    omc_size thmb_size;
    omc_size iref_size;
    omc_size meta_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif, &infe_exif_size, 2U);
    append_u16be(infe_exif, &infe_exif_size, 1U);
    append_u16be(infe_exif, &infe_exif_size, 0U);
    append_u32be(infe_exif, &infe_exif_size, fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif, &infe_exif_size, "Exif");
    append_u8(infe_exif, &infe_exif_size, 0U);

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 7U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/rdf+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    infe_jumb_size = 0U;
    append_fullbox_header(infe_jumb, &infe_jumb_size, 2U);
    append_u16be(infe_jumb, &infe_jumb_size, 3U);
    append_u16be(infe_jumb, &infe_jumb_size, 0U);
    append_u32be(infe_jumb, &infe_jumb_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_jumb, &infe_jumb_size, "C2PA");
    append_u8(infe_jumb, &infe_jumb_size, 0U);
    append_text(infe_jumb, &infe_jumb_size, "application/jumbf");
    append_u8(infe_jumb, &infe_jumb_size, 0U);
    append_u8(infe_jumb, &infe_jumb_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 0U);
    append_u16be(iinf_payload, &iinf_size, 3U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_exif, infe_exif_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_jumb, infe_jumb_size);

    pitm_size = 0U;
    append_fullbox_header(pitm_payload, &pitm_size, 0U);
    append_u16be(pitm_payload, &pitm_size, 2U);

    ispe_size = 0U;
    append_fullbox_header(ispe_payload, &ispe_size, 0U);
    append_u32be(ispe_payload, &ispe_size, 640U);
    append_u32be(ispe_payload, &ispe_size, 480U);

    irot_size = 0U;
    append_u8(irot_payload, &irot_size, 1U);

    imir_size = 0U;
    append_u8(imir_payload, &imir_size, 1U);

    auxc_alpha_size = 0U;
    append_fullbox_header(auxc_alpha_payload, &auxc_alpha_size, 0U);
    append_text(auxc_alpha_payload, &auxc_alpha_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_alpha_payload, &auxc_alpha_size, 0U);

    auxc_depth_size = 0U;
    append_fullbox_header(auxc_depth_payload, &auxc_depth_size, 0U);
    append_text(auxc_depth_payload, &auxc_depth_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_depth_payload, &auxc_depth_size, 0U);

    ipco_size = 0U;
    append_bmff_box(ipco_payload, &ipco_size, fourcc('i', 's', 'p', 'e'),
                    ispe_payload, ispe_size);
    append_bmff_box(ipco_payload, &ipco_size, fourcc('i', 'r', 'o', 't'),
                    irot_payload, irot_size);
    append_bmff_box(ipco_payload, &ipco_size, fourcc('i', 'm', 'i', 'r'),
                    imir_payload, imir_size);
    append_bmff_box(ipco_payload, &ipco_size, fourcc('a', 'u', 'x', 'C'),
                    auxc_alpha_payload, auxc_alpha_size);
    append_bmff_box(ipco_payload, &ipco_size, fourcc('a', 'u', 'x', 'C'),
                    auxc_depth_payload, auxc_depth_size);

    ipma_size = 0U;
    append_fullbox_header(ipma_payload, &ipma_size, 0U);
    append_u32be(ipma_payload, &ipma_size, 3U);
    append_u16be(ipma_payload, &ipma_size, 2U);
    append_u8(ipma_payload, &ipma_size, 3U);
    append_u8(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 2U);
    append_u8(ipma_payload, &ipma_size, 3U);
    append_u16be(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 4U);
    append_u16be(ipma_payload, &ipma_size, 3U);
    append_u8(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 5U);

    iprp_size = 0U;
    append_bmff_box(iprp_payload, &iprp_size, fourcc('i', 'p', 'c', 'o'),
                    ipco_payload, ipco_size);
    append_bmff_box(iprp_payload, &iprp_size, fourcc('i', 'p', 'm', 'a'),
                    ipma_payload, ipma_size);

    auxl_size = 0U;
    append_u16be(auxl_payload, &auxl_size, 2U);
    append_u16be(auxl_payload, &auxl_size, 2U);
    append_u16be(auxl_payload, &auxl_size, 1U);
    append_u16be(auxl_payload, &auxl_size, 3U);

    dimg_size = 0U;
    append_u16be(dimg_payload, &dimg_size, 2U);
    append_u16be(dimg_payload, &dimg_size, 1U);
    append_u16be(dimg_payload, &dimg_size, 3U);

    thmb_size = 0U;
    append_u16be(thmb_payload, &thmb_size, 2U);
    append_u16be(thmb_payload, &thmb_size, 1U);
    append_u16be(thmb_payload, &thmb_size, 1U);

    iref_size = 0U;
    append_fullbox_header(iref_payload, &iref_size, 0U);
    append_bmff_box(iref_payload, &iref_size, fourcc('a', 'u', 'x', 'l'),
                    auxl_payload, auxl_size);
    append_bmff_box(iref_payload, &iref_size, fourcc('d', 'i', 'm', 'g'),
                    dimg_payload, dimg_size);
    append_bmff_box(iref_payload, &iref_size, fourcc('t', 'h', 'm', 'b'),
                    thmb_payload, thmb_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('p', 'i', 't', 'm'),
                    pitm_payload, pitm_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'r', 'e', 'f'),
                    iref_payload, iref_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'p', 'r', 'p'),
                    iprp_payload, iprp_size);

    moov_size = 0U;
    append_bmff_box(moov_box, &moov_size, fourcc('m', 'o', 'o', 'v'),
                    (const omc_u8*)0, 0U);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bytes(out, &size, moov_box, moov_size);
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static omc_size
make_test_cr3_all(omc_u8* out)
{
    static const omc_u8 canon_uuid[16] = {
        0x85U, 0xC0U, 0xB6U, 0x87U, 0x82U, 0x0FU, 0x11U, 0xE0U,
        0x81U, 0x11U, 0xF4U, 0xCEU, 0x46U, 0x2BU, 0x6AU, 0x48U
    };
    omc_u8 tiff[64];
    omc_u8 exif_payload[96];
    omc_u8 cmt_payload[128];
    omc_u8 uuid_payload[192];
    omc_u8 moov_payload[224];
    omc_u8 ftyp_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size cmt_size;
    omc_size uuid_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    exif_size = 0U;
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    cmt_size = 0U;
    append_bmff_box(cmt_payload, &cmt_size, fourcc('C', 'M', 'T', '1'),
                    exif_payload, exif_size);

    uuid_size = 0U;
    append_bytes(uuid_payload, &uuid_size, canon_uuid, 16U);
    append_bytes(uuid_payload, &uuid_size, cmt_payload, cmt_size);

    moov_size = 0U;
    append_bmff_box(moov_payload, &moov_size, fourcc('u', 'u', 'i', 'd'),
                    uuid_payload, uuid_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, fourcc('c', 'r', 'x', ' '));
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('C', 'R', '3', ' '));

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'o', 'o', 'v'),
                    moov_payload, moov_size);
    return size;
}

static omc_size
make_test_bmff_iref_xmp_all(omc_u8* out, omc_u32 major_brand)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 infe_xmp[96];
    omc_u8 iinf_payload[160];
    omc_u8 iloc_payload[256];
    omc_u8 iref_iloc_payload[96];
    omc_u8 iref_payload[128];
    omc_u8 idat_box[384];
    omc_u8 meta_payload[1024];
    omc_u8 ftyp_payload[16];
    omc_size xmp_size;
    omc_size split_at;
    omc_size infe_xmp_size;
    omc_size iinf_size;
    omc_size iloc_size;
    omc_size iref_iloc_size;
    omc_size iref_size;
    omc_size idat_box_size;
    omc_size meta_size;
    omc_size ftyp_size;
    omc_size size;

    xmp_size = sizeof(xmp) - 1U;
    split_at = xmp_size / 2U;

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 1U);
    append_u16be(infe_xmp, &infe_xmp_size, 0U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/xmp+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 2U);
    append_u32be(iinf_payload, &iinf_size, 1U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);

    idat_box_size = 0U;
    append_bmff_box(idat_box, &idat_box_size, fourcc('i', 'd', 'a', 't'),
                    (const omc_u8*)xmp, xmp_size);

    iloc_size = 0U;
    append_fullbox_header(iloc_payload, &iloc_size, 2U);
    append_u8(iloc_payload, &iloc_size, 0x44U);
    append_u8(iloc_payload, &iloc_size, 0x00U);
    append_u32be(iloc_payload, &iloc_size, 3U);

    append_u32be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)split_at);

    append_u32be(iloc_payload, &iloc_size, 3U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)split_at);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)(xmp_size - split_at));

    append_u32be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 2U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)split_at);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)(xmp_size - split_at));

    iref_iloc_size = 0U;
    append_u32be(iref_iloc_payload, &iref_iloc_size, 1U);
    append_u16be(iref_iloc_payload, &iref_iloc_size, 2U);
    append_u32be(iref_iloc_payload, &iref_iloc_size, 2U);
    append_u32be(iref_iloc_payload, &iref_iloc_size, 3U);

    iref_size = 0U;
    append_fullbox_header(iref_payload, &iref_size, 1U);
    append_bmff_box(iref_payload, &iref_size, fourcc('i', 'l', 'o', 'c'),
                    iref_iloc_payload, iref_iloc_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'l', 'o', 'c'),
                    iloc_payload, iloc_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'r', 'e', 'f'),
                    iref_payload, iref_size);
    append_bytes(meta_payload, &meta_size, idat_box, idat_box_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static omc_size
make_test_bmff_external_dref_all(omc_u8* out, omc_u32 major_brand)
{
    static const char xmp[] = "<xmp/>";
    omc_u8 infe_xmp[96];
    omc_u8 iinf_payload[160];
    omc_u8 iloc_payload[96];
    omc_u8 idat_box[64];
    omc_u8 url_payload[8];
    omc_u8 dref_payload[32];
    omc_u8 dinf_payload[48];
    omc_u8 meta_payload[512];
    omc_u8 ftyp_payload[16];
    omc_size infe_xmp_size;
    omc_size iinf_size;
    omc_size iloc_size;
    omc_size idat_box_size;
    omc_size url_size;
    omc_size dref_size;
    omc_size dinf_size;
    omc_size meta_size;
    omc_size ftyp_size;
    omc_size size;

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 1U);
    append_u16be(infe_xmp, &infe_xmp_size, 0U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/xmp+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 2U);
    append_u32be(iinf_payload, &iinf_size, 1U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);

    iloc_size = 0U;
    append_fullbox_header(iloc_payload, &iloc_size, 1U);
    append_u8(iloc_payload, &iloc_size, 0x44U);
    append_u8(iloc_payload, &iloc_size, 0x40U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)(sizeof(xmp) - 1U));

    idat_box_size = 0U;
    append_bmff_box(idat_box, &idat_box_size, fourcc('i', 'd', 'a', 't'),
                    (const omc_u8*)xmp, sizeof(xmp) - 1U);

    url_size = 0U;
    append_fullbox_header(url_payload, &url_size, 0U);

    dref_size = 0U;
    append_fullbox_header(dref_payload, &dref_size, 0U);
    append_u32be(dref_payload, &dref_size, 1U);
    append_bmff_box(dref_payload, &dref_size, fourcc('u', 'r', 'l', ' '),
                    url_payload, url_size);

    dinf_size = 0U;
    append_bmff_box(dinf_payload, &dinf_size, fourcc('d', 'r', 'e', 'f'),
                    dref_payload, dref_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'l', 'o', 'c'),
                    iloc_payload, iloc_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('d', 'i', 'n', 'f'),
                    dinf_payload, dinf_size);
    append_bytes(meta_payload, &meta_size, idat_box, idat_box_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static omc_size
make_test_jp2_all(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[64];
    omc_u8 exif_payload[96];
    omc_u8 icc[160];
    omc_u8 colr_payload[256];
    omc_u8 colr_box[288];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size colr_size;
    omc_size colr_box_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    build_test_icc(icc, sizeof(icc));

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    colr_size = 0U;
    append_u8(colr_payload, &colr_size, 2U);
    append_u8(colr_payload, &colr_size, 0U);
    append_u8(colr_payload, &colr_size, 0U);
    append_bytes(colr_payload, &colr_size, icc, sizeof(icc));
    colr_box_size = 0U;
    append_bmff_box(colr_box, &colr_box_size, fourcc('c', 'o', 'l', 'r'),
                    colr_payload, colr_size);

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('j', 'P', ' ', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('j', 'p', '2', 'h'),
                    colr_box, colr_box_size);
    append_bmff_box(out, &size, fourcc('x', 'm', 'l', ' '),
                    (const omc_u8*)xmp, sizeof(xmp) - 1U);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    return size;
}

static omc_size
make_test_jxl_all(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[64];
    omc_u8 exif_payload[96];
    omc_u8 jumb_payload[32];
    omc_u8 brob_payload[32];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size jumb_size;
    omc_size brob_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    jumb_size = 0U;
    append_u32be(jumb_payload, &jumb_size, 0x0000000DU);
    append_u32be(jumb_payload, &jumb_size, fourcc('j', 'u', 'm', 'd'));
    append_text(jumb_payload, &jumb_size, "c2pa");
    append_u8(jumb_payload, &jumb_size, 0U);
    append_u32be(jumb_payload, &jumb_size, 0x0000000CU);
    append_u32be(jumb_payload, &jumb_size, fourcc('c', 'b', 'o', 'r'));
    append_u8(jumb_payload, &jumb_size, 0xA1U);
    append_u8(jumb_payload, &jumb_size, 0x61U);
    append_u8(jumb_payload, &jumb_size, 0x61U);
    append_u8(jumb_payload, &jumb_size, 0x01U);

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    brob_size = 0U;
    append_u32be(brob_payload, &brob_size, fourcc('x', 'm', 'l', ' '));
    append_text(brob_payload, &brob_size, "zzz");

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('J', 'X', 'L', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    append_bmff_box(out, &size, fourcc('x', 'm', 'l', ' '),
                    (const omc_u8*)xmp, sizeof(xmp) - 1U);
    append_bmff_box(out, &size, fourcc('j', 'u', 'm', 'b'),
                    jumb_payload, jumb_size);
    append_bmff_box(out, &size, fourcc('b', 'r', 'o', 'b'),
                    brob_payload, brob_size);
    return size;
}

static omc_size
make_test_jxl_brob_jumbf(omc_u8* out)
{
    static const omc_u8 brotli_payload[] = {
        0x0BU, 0x0CU, 0x80U, 0x00U, 0x00U, 0x00U, 0x0DU, 0x6AU,
        0x75U, 0x6DU, 0x64U, 0x63U, 0x32U, 0x70U, 0x61U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x0CU, 0x63U, 0x62U, 0x6FU, 0x72U,
        0xA1U, 0x61U, 0x61U, 0x01U, 0x03U
    };
    omc_u8 brob_payload[64];
    omc_size brob_size;
    omc_size size;

    brob_size = 0U;
    append_u32be(brob_payload, &brob_size, fourcc('j', 'u', 'm', 'b'));
    append_bytes(brob_payload, &brob_size, brotli_payload,
                 sizeof(brotli_payload));

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('J', 'X', 'L', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('b', 'r', 'o', 'b'),
                    brob_payload, brob_size);
    return size;
}

static omc_size
make_test_jxl_brob_xmp(omc_u8* out)
{
    static const omc_u8 brotli_payload[] = {
        0x1BU, 0xD0U, 0x00U, 0x00U, 0x64U, 0x68U, 0x5EU, 0xA9U,
        0xEEU, 0x9DU, 0x8EU, 0xF1U, 0xA5U, 0x06U, 0x85U, 0x72U,
        0x4CU, 0xC2U, 0x36U, 0x9DU, 0x1CU, 0xE1U, 0xC1U, 0xE8U,
        0x72U, 0xD0U, 0x20U, 0xD2U, 0x6FU, 0x11U, 0xA5U, 0x10U,
        0x86U, 0x89U, 0xE7U, 0x29U, 0x2BU, 0xCDU, 0x60U, 0xD3U,
        0x94U, 0xFFU, 0xB9U, 0x1DU, 0xD6U, 0x83U, 0x0DU, 0x56U,
        0x15U, 0x76U, 0x2BU, 0xEAU, 0x66U, 0xD8U, 0xADU, 0x98U,
        0xF4U, 0x5CU, 0xADU, 0x5DU, 0x5EU, 0x5BU, 0x55U, 0x18U,
        0x5AU, 0xEDU, 0xB7U, 0x49U, 0xEAU, 0xF5U, 0xF7U, 0x7AU,
        0x47U, 0xB5U, 0x24U, 0xF5U, 0xAEU, 0x06U, 0xD5U, 0x90U,
        0x30U, 0xC2U, 0x14U, 0x51U, 0x0AU, 0x92U, 0xD4U, 0x20U,
        0x8FU, 0xAEU, 0xB0U, 0x01U, 0xB8U, 0x3CU, 0x23U, 0xF1U,
        0x80U, 0x24U, 0xC3U, 0x60U, 0x43U, 0xB8U, 0x0CU, 0xA5U,
        0xEDU, 0x11U, 0xFFU, 0xB3U, 0x81U, 0x05U, 0x44U, 0x20U,
        0x46U, 0xABU, 0x6EU, 0xB0U, 0x61U, 0x7DU, 0x49U, 0x8AU,
        0x15U, 0x9FU, 0xFEU, 0xDEU, 0x9BU, 0xDDU, 0xEAU, 0x15U,
        0x94U, 0x7BU, 0xA8U, 0xC2U, 0x56U, 0x68U, 0xBFU, 0x45U,
        0xDEU, 0x72U, 0xBFU, 0x45U, 0x49U, 0xB4U, 0x07U
    };
    omc_u8 brob_payload[192];
    omc_size brob_size;
    omc_size size;

    brob_size = 0U;
    append_u32be(brob_payload, &brob_size, fourcc('x', 'm', 'l', ' '));
    append_bytes(brob_payload, &brob_size, brotli_payload,
                 sizeof(brotli_payload));

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('J', 'X', 'L', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('b', 'r', 'o', 'b'),
                    brob_payload, brob_size);
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

static omc_size
count_xmp_entries(const omc_store* store, const char* schema_ns,
                  const char* property_path)
{
    omc_size i;
    omc_size count;

    count = 0U;
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
            count += 1U;
        }
    }
    return count;
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
find_iptc_dataset(const omc_store* store, omc_u16 record, omc_u16 dataset)
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

static const omc_block_info*
find_block_by_kind(const omc_store* store, omc_blk_kind kind)
{
    omc_size i;

    for (i = 0U; i < store->block_count; ++i) {
        const omc_block_info* block;

        block = omc_store_block(store, (omc_block_id)i);
        if (block != (const omc_block_info*)0 && block->kind == kind) {
            return block;
        }
    }
    return (const omc_block_info*)0;
}

static const omc_entry*
find_jumbf_field(const omc_store* store, const char* field)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_JUMBF_FIELD) {
            continue;
        }
        field_view = omc_arena_view(&store->arena, entry->key.u.jumbf_field.field);
        if (field_view.size == strlen(field)
            && memcmp(field_view.data, field, field_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_jumbf_cbor_key(const omc_store* store, const char* key_text)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key_view = omc_arena_view(&store->arena, entry->key.u.jumbf_cbor_key.key);
        if (key_view.size == strlen(key_text)
            && memcmp(key_view.data, key_text, key_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_comment_entry(const omc_store* store)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_COMMENT) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_png_text_entry(const omc_store* store, const char* keyword,
                    const char* field)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes keyword_view;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_PNG_TEXT) {
            continue;
        }
        keyword_view = omc_arena_view(&store->arena,
                                      entry->key.u.png_text.keyword);
        field_view = omc_arena_view(&store->arena,
                                    entry->key.u.png_text.field);
        if (keyword_view.size == strlen(keyword)
            && field_view.size == strlen(field)
            && memcmp(keyword_view.data, keyword, keyword_view.size) == 0
            && memcmp(field_view.data, field, field_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_geotiff_key(const omc_store* store, omc_u16 key_id)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_GEOTIFF_KEY
            && entry->key.u.geotiff_key.key_id == key_id) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_printim_field(const omc_store* store, const char* field)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_PRINTIM_FIELD) {
            continue;
        }
        field_view = omc_arena_view(&store->arena,
                                    entry->key.u.printim_field.field);
        if (field_view.size == strlen(field)
            && memcmp(field_view.data, field, field_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static omc_size
count_bmff_field(const omc_store* store, const char* field)
{
    omc_size i;
    omc_size count;

    count = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_BMFF_FIELD) {
            continue;
        }
        field_view = omc_arena_view(&store->arena, entry->key.u.bmff_field.field);
        if (field_view.size == strlen(field)
            && memcmp(field_view.data, field, field_view.size) == 0) {
            count += 1U;
        }
    }
    return count;
}

static const omc_entry*
find_bmff_field_text(const omc_store* store, const char* field,
                     const char* value)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;
        omc_const_bytes value_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_BMFF_FIELD
            || entry->value.kind != OMC_VAL_TEXT) {
            continue;
        }
        field_view = omc_arena_view(&store->arena, entry->key.u.bmff_field.field);
        if (field_view.size != strlen(field)
            || memcmp(field_view.data, field, field_view.size) != 0) {
            continue;
        }
        value_view = omc_arena_view(&store->arena, entry->value.u.ref);
        if (value_view.size == strlen(value)
            && memcmp(value_view.data, value, value_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_bmff_field(const omc_store* store, const char* field)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_BMFF_FIELD) {
            continue;
        }
        field_view = omc_arena_view(&store->arena, entry->key.u.bmff_field.field);
        if (field_view.size == strlen(field)
            && memcmp(field_view.data, field, field_view.size) == 0) {
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
    const omc_entry* iptc_headline;
    const omc_entry* irb_iptc;
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
    assert(res.irb.status == OMC_IRB_OK);
    assert(res.iptc.status == OMC_IPTC_OK);
    assert(store.block_count == 4U);

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

    irb_iptc = find_irb_entry(&store, 0x0404U);
    assert(irb_iptc != (const omc_entry*)0);
    block = omc_store_block(&store, irb_iptc->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->kind == OMC_BLK_PS_IRB);

    iptc_headline = find_iptc_dataset(&store, 2U, 25U);
    assert(iptc_headline != (const omc_entry*)0);
    assert((iptc_headline->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    block = omc_store_block(&store, iptc_headline->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->kind == OMC_BLK_PS_IRB);

    omc_store_fini(&store);
}

static void
test_read_jpeg_comment(void)
{
    omc_u8 jpeg[128];
    omc_size jpeg_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* comment;
    omc_const_bytes value_view;
    const omc_block_info* block;

    jpeg_size = make_test_jpeg_comment(jpeg);
    omc_store_init(&store);

    res = omc_read_simple(jpeg, jpeg_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(store.block_count == 1U);
    comment = find_comment_entry(&store);
    assert(comment != (const omc_entry*)0);
    assert(comment->value.kind == OMC_VAL_TEXT);
    assert(comment->value.text_encoding == OMC_TEXT_ASCII);
    value_view = omc_arena_view(&store.arena, comment->value.u.ref);
    assert(value_view.size == 21U);
    assert(memcmp(value_view.data, "OpenMeta JPEG comment", value_view.size)
           == 0);
    block = omc_store_block(&store, comment->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->kind == OMC_BLK_COMMENT);

    omc_store_fini(&store);
}

static void
test_read_jpeg_app11_jumbf_split(void)
{
    omc_u8 jpeg[512];
    omc_size jpeg_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[256];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_block_info* jumbf_block;
    const omc_entry* marker;
    const omc_entry* cbor_value;

    jpeg_size = make_test_jpeg_app11_jumbf(jpeg, 1, 1);
    omc_store_init(&store);

    res = omc_read_simple(jpeg, jpeg_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.jumbf.status == OMC_JUMBF_OK);
    assert(store.block_count == 2U);

    jumbf_block = find_block_by_kind(&store, OMC_BLK_JUMBF);
    assert(jumbf_block != (const omc_block_info*)0);
    assert(jumbf_block->format == OMC_SCAN_FMT_JPEG);
    assert(jumbf_block->chunking == OMC_BLK_CHUNK_JPEG_APP11_SEQ);
    assert(jumbf_block->part_count == 2U);

    marker = find_jumbf_field(&store, "c2pa.detected");
    assert(marker != (const omc_entry*)0);
    cbor_value = find_jumbf_cbor_key(&store, "box.0.1.cbor.a");
    assert(cbor_value != (const omc_entry*)0);

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

static void
test_read_png_all(void)
{
    omc_u8 png[1024];
    omc_size png_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* xmp_tool;
    const omc_block_info* block;

    png_size = make_test_png_all(png, 0);
    omc_store_init(&store);

    res = omc_read_simple(png, png_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(store.block_count == 2U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_PNG);
    assert(block->kind == OMC_BLK_EXIF);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_PNG);
    assert(block->kind == OMC_BLK_XMP);

    omc_store_fini(&store);
}

static void
test_read_png_text(void)
{
    omc_u8 png[512];
    omc_size png_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* author;
    const omc_entry* language;
    const omc_entry* translated;
    const omc_entry* comment;
    omc_const_bytes value_view;

    png_size = make_test_png_text_all(png);
    omc_store_init(&store);

    res = omc_read_simple(png, png_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(store.block_count == 3U);

    author = find_png_text_entry(&store, "Author", "text");
    assert(author != (const omc_entry*)0);
    value_view = omc_arena_view(&store.arena, author->value.u.ref);
    assert(value_view.size == 5U);
    assert(memcmp(value_view.data, "Alice", value_view.size) == 0);
    assert(author->value.text_encoding == OMC_TEXT_UNKNOWN);

    language = find_png_text_entry(&store, "Description", "language");
    assert(language != (const omc_entry*)0);
    value_view = omc_arena_view(&store.arena, language->value.u.ref);
    assert(value_view.size == 2U);
    assert(memcmp(value_view.data, "en", value_view.size) == 0);
    assert(language->value.text_encoding == OMC_TEXT_ASCII);

    translated = find_png_text_entry(&store, "Description",
                                     "translated_keyword");
    assert(translated != (const omc_entry*)0);
    value_view = omc_arena_view(&store.arena, translated->value.u.ref);
    assert(value_view.size == 12U);
    assert(memcmp(value_view.data, "Beschreibung", value_view.size) == 0);
    assert(translated->value.text_encoding == OMC_TEXT_UTF8);

    comment = find_png_text_entry(&store, "Comment", "text");
#if OMC_HAVE_ZLIB
    assert(res.pay.status == OMC_PAY_OK);
    assert(comment != (const omc_entry*)0);
    value_view = omc_arena_view(&store.arena, comment->value.u.ref);
    assert(value_view.size == 6U);
    assert(memcmp(value_view.data, "Shot A", value_view.size) == 0);
#else
    assert(res.pay.status == OMC_PAY_UNSUPPORTED);
    assert(comment == (const omc_entry*)0);
#endif

    omc_store_fini(&store);
}

static void
test_read_png_xmp_compressed(void)
{
    omc_u8 png[1024];
    omc_size png_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* xmp_tool;
    const omc_entry* icc_size;

    png_size = make_test_png_all(png, 1);
    omc_store_init(&store);

    res = omc_read_simple(png, png_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    icc_size = find_icc_header(&store, 0U);

#if OMC_HAVE_ZLIB
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.icc.status == OMC_ICC_OK);
    assert(xmp_tool != (const omc_entry*)0);
    assert(icc_size != (const omc_entry*)0);
#else
    assert(res.pay.status == OMC_PAY_UNSUPPORTED);
    assert(xmp_tool == (const omc_entry*)0);
    assert(icc_size == (const omc_entry*)0);
#endif

    omc_store_fini(&store);
}

static void
test_read_webp_all(void)
{
    omc_u8 webp[1024];
    omc_size webp_size;
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

    webp_size = make_test_webp_all(webp);
    omc_store_init(&store);

    res = omc_read_simple(webp, webp_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.icc.status == OMC_ICC_OK);
    assert(store.block_count == 3U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_WEBP);
    assert(block->kind == OMC_BLK_EXIF);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_WEBP);
    assert(block->kind == OMC_BLK_XMP);

    icc_size = find_icc_header(&store, 0U);
    assert(icc_size != (const omc_entry*)0);
    block = omc_store_block(&store, icc_size->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_WEBP);
    assert(block->kind == OMC_BLK_ICC);

    omc_store_fini(&store);
}

static void
test_read_gif_all(void)
{
    omc_u8 gif[1024];
    omc_size gif_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[512];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* comment;
    const omc_entry* xmp_tool;
    const omc_entry* icc_size;
    const omc_block_info* block;
    omc_const_bytes value_view;

    gif_size = make_test_gif_all(gif);
    omc_store_init(&store);

    res = omc_read_simple(gif, gif_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.icc.status == OMC_ICC_OK);
    assert(store.block_count == 3U);

    comment = find_comment_entry(&store);
    assert(comment != (const omc_entry*)0);
    assert(comment->value.kind == OMC_VAL_TEXT);
    assert(comment->value.text_encoding == OMC_TEXT_ASCII);
    value_view = omc_arena_view(&store.arena, comment->value.u.ref);
    assert(value_view.size == 20U);
    assert(memcmp(value_view.data, "OpenMeta GIF comment", value_view.size)
           == 0);
    block = omc_store_block(&store, comment->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_GIF);
    assert(block->kind == OMC_BLK_COMMENT);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_GIF);
    assert(block->kind == OMC_BLK_XMP);

    icc_size = find_icc_header(&store, 0U);
    assert(icc_size != (const omc_entry*)0);
    block = omc_store_block(&store, icc_size->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_GIF);
    assert(block->kind == OMC_BLK_ICC);

    omc_store_fini(&store);
}

static void
test_read_raf_all(void)
{
    omc_u8 raf[1024];
    omc_size raf_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[512];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* xmp_tool;
    const omc_block_info* block;

    raf_size = make_test_raf_all(raf);
    omc_store_init(&store);

    res = omc_read_simple(raf, raf_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(store.block_count == 2U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_RAF);
    assert(block->kind == OMC_BLK_EXIF);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_RAF);
    assert(block->kind == OMC_BLK_XMP);

    omc_store_fini(&store);
}

static void
test_read_x3f_exif(void)
{
    omc_u8 x3f[512];
    omc_size x3f_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_block_info* block;

    x3f_size = make_test_x3f_all(x3f);
    omc_store_init(&store);

    res = omc_read_simple(x3f, x3f_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(store.block_count == 1U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_X3F);
    assert(block->kind == OMC_BLK_EXIF);

    omc_store_fini(&store);
}

static void
test_read_tiff_geotiff(void)
{
    omc_u8 tiff[256];
    omc_size tiff_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* model_type;
    const omc_entry* citation;
    const omc_entry* semi_major;
    const omc_block_info* block;
    omc_const_bytes value_view;

    tiff_size = make_test_tiff_geotiff(tiff);
    omc_store_init(&store);

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(store.block_count == 1U);

    model_type = find_geotiff_key(&store, 1024U);
    assert(model_type != (const omc_entry*)0);
    assert((model_type->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    assert(model_type->value.kind == OMC_VAL_SCALAR);
    assert(model_type->value.elem_type == OMC_ELEM_U16);
    assert(model_type->value.u.u64 == 2U);

    citation = find_geotiff_key(&store, 1026U);
    assert(citation != (const omc_entry*)0);
    assert(citation->value.kind == OMC_VAL_TEXT);
    assert(citation->value.text_encoding == OMC_TEXT_ASCII);
    value_view = omc_arena_view(&store.arena, citation->value.u.ref);
    assert(value_view.size == 12U);
    assert(memcmp(value_view.data, "TestCitation", value_view.size) == 0);

    semi_major = find_geotiff_key(&store, 2057U);
    assert(semi_major != (const omc_entry*)0);
    assert(semi_major->value.kind == OMC_VAL_SCALAR);
    assert(semi_major->value.elem_type == OMC_ELEM_F64_BITS);
    assert(semi_major->value.u.f64_bits
           == ((((omc_u64)0x415854A6U) << 32) | (omc_u64)0x40000000U));

    block = omc_store_block(&store, semi_major->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_TIFF);
    assert(block->kind == OMC_BLK_EXIF);

    omc_store_fini(&store);
}

static void
test_read_tiff_printim(void)
{
    omc_u8 tiff[128];
    omc_size tiff_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* version;
    const omc_entry* field_0001;
    const omc_block_info* block;
    omc_const_bytes value_view;

    tiff_size = make_test_tiff_printim(tiff);
    omc_store_init(&store);

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(store.block_count == 1U);

    version = find_printim_field(&store, "version");
    assert(version != (const omc_entry*)0);
    assert((version->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    assert(version->value.kind == OMC_VAL_TEXT);
    assert(version->value.text_encoding == OMC_TEXT_ASCII);
    value_view = omc_arena_view(&store.arena, version->value.u.ref);
    assert(value_view.size == 4U);
    assert(memcmp(value_view.data, "0300", value_view.size) == 0);

    field_0001 = find_printim_field(&store, "0x0001");
    assert(field_0001 != (const omc_entry*)0);
    assert(field_0001->value.kind == OMC_VAL_SCALAR);
    assert(field_0001->value.elem_type == OMC_ELEM_U32);
    assert(field_0001->value.u.u64 == 0x00160016U);

    block = omc_store_block(&store, field_0001->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_TIFF);
    assert(block->kind == OMC_BLK_EXIF);

    omc_store_fini(&store);
}

static void
test_read_tiff_fuji_makernote(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_opts opts;
    omc_read_res res;
    const omc_entry* entry;

    makernote_size = make_fuji_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_fuji0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0x42U);

    omc_store_fini(&store);
}

static void
test_read_tiff_canon_makernote(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_opts opts;
    omc_read_res res;
    const omc_entry* entry;

    makernote_size = make_canon_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x12345678U);

    omc_store_fini(&store);
}

static void
test_read_tiff_nikon_makernote(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_opts opts;
    omc_read_res res;
    const omc_entry* root;
    const omc_entry* vr_mode;

    makernote_size = make_nikon_makernote(makernote);
    tiff_size = make_test_tiff_with_makernote(tiff, makernote, makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    root = find_exif_entry(&store, "mk_nikon0", 0x0001U);
    assert(root != (const omc_entry*)0);
    assert(root->value.kind == OMC_VAL_SCALAR);
    assert(root->value.elem_type == OMC_ELEM_U32);
    assert(root->value.u.u64 == 0x01020304U);
    vr_mode = find_exif_entry(&store, "mk_nikon_vrinfo_0", 0x0006U);
    assert(vr_mode != (const omc_entry*)0);
    assert(vr_mode->value.kind == OMC_VAL_SCALAR);
    assert(vr_mode->value.elem_type == OMC_ELEM_U8);
    assert(vr_mode->value.u.u64 == 2U);

    omc_store_fini(&store);
}

static void
test_read_crw_minimal_ciff(void)
{
    omc_u8 crw[256];
    omc_size crw_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* entry;
    const omc_block_info* block;
    omc_const_bytes value_view;

    crw_size = make_test_crw_minimal(crw);
    omc_store_init(&store);

    res = omc_read_simple(crw, crw_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "ciff_root", 0x0801U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(entry->value.text_encoding == OMC_TEXT_ASCII);
    value_view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(value_view.size == 8U);
    assert(memcmp(value_view.data, "CIFFTEST", value_view.size) == 0);

    block = omc_store_block(&store, entry->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_CRW);
    assert(block->kind == OMC_BLK_CIFF);

    omc_store_fini(&store);
}

static void
test_read_crw_derived_exif(void)
{
    omc_u8 crw[1024];
    omc_size crw_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[16];
    omc_u8 payload[128];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* entry;

    crw_size = make_test_crw_derived(crw);
    omc_store_init(&store);

    res = omc_read_simple(crw, crw_size, &store, blocks, 8U, ifds, 16U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(entry != (const omc_entry*)0);
    entry = find_exif_entry(&store, "ifd0", 0x0110U);
    assert(entry != (const omc_entry*)0);
    entry = find_exif_entry(&store, "ifd0", 0x0112U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.u.u64 == 6U);

    entry = find_exif_entry(&store, "exififd", 0x9003U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);

    entry = find_exif_entry(&store, "exififd", 0x9206U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.u.u64 == 123U);

    entry = find_exif_entry(&store, "exififd", 0xA002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 1536U);

    entry = find_exif_entry(&store, "exififd", 0xA003U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 1024U);

    omc_store_fini(&store);
}

static void
test_read_bmff_heif_all(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* xmp_tool;
    const omc_entry* icc_size;
    const omc_entry* c2pa_marker;
    const omc_entry* cbor_value;
    const omc_block_info* block;
    const omc_block_info* jumbf_block;

    file_size = make_test_bmff_all(file_bytes, fourcc('h', 'e', 'i', 'c'));
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.icc.status == OMC_ICC_OK);
    assert(res.jumbf.status == OMC_JUMBF_OK);
    assert(res.bmff.status == OMC_BMFF_OK);
    assert(store.block_count == 5U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_HEIF);
    assert(block->kind == OMC_BLK_EXIF);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_HEIF);
    assert(block->kind == OMC_BLK_XMP);

    icc_size = find_icc_header(&store, 0U);
    assert(icc_size != (const omc_entry*)0);
    block = omc_store_block(&store, icc_size->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_HEIF);
    assert(block->kind == OMC_BLK_ICC);

    jumbf_block = find_block_by_kind(&store, OMC_BLK_JUMBF);
    assert(jumbf_block != (const omc_block_info*)0);
    assert(jumbf_block->format == OMC_SCAN_FMT_HEIF);

    c2pa_marker = find_jumbf_field(&store, "c2pa.detected");
    assert(c2pa_marker != (const omc_entry*)0);
    cbor_value = find_jumbf_cbor_key(&store, "box.0.1.cbor.a");
    assert(cbor_value != (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_read_bmff_avif_all(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* c2pa_marker;
    const omc_block_info* block;

    file_size = make_test_bmff_all(file_bytes, fourcc('a', 'v', 'i', 'f'));
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.icc.status == OMC_ICC_OK);
    assert(res.jumbf.status == OMC_JUMBF_OK);
    assert(res.bmff.status == OMC_BMFF_OK);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_AVIF);
    assert(block->kind == OMC_BLK_EXIF);

    c2pa_marker = find_jumbf_field(&store, "c2pa.detected");
    assert(c2pa_marker != (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_read_cr3_exif(void)
{
    omc_u8 file_bytes[512];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[128];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_block_info* block;

    file_size = make_test_cr3_all(file_bytes);
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.bmff.status == OMC_BMFF_OK);
    assert(store.block_count == 2U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_CR3);
    assert(block->kind == OMC_BLK_EXIF);
    assert(block->id == fourcc('C', 'M', 'T', '1'));

    omc_store_fini(&store);
}

static void
test_read_bmff_iref_xmp_split(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[512];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* xmp_tool;
    const omc_block_info* block;

    file_size = make_test_bmff_iref_xmp_all(file_bytes,
                                            fourcc('h', 'e', 'i', 'c'));
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.bmff.status == OMC_BMFF_OK);
    assert(store.block_count == 3U);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    assert(count_xmp_entries(&store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool") == 1U);

    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_HEIF);
    assert(block->kind == OMC_BLK_XMP);
    assert(block->group == 1U);
    assert(block->part_count == 2U);
    assert(block->part_index == 0U);

    omc_store_fini(&store);
}

static void
test_read_bmff_external_dref_is_skipped(void)
{
    omc_u8 file_bytes[768];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[128];
    omc_u32 payload_parts[8];
    omc_read_res res;

    file_size = make_test_bmff_external_dref_all(
        file_bytes, fourcc('h', 'e', 'i', 'c'));
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.bmff.status == OMC_BMFF_OK);
    assert(store.block_count == 1U);
    assert(find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool") == (const omc_entry*)0);
    assert(find_bmff_field(&store, "ftyp.major_brand") != (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_read_bmff_fields(void)
{
    omc_u8 file_bytes[1536];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* item_info_count;
    const omc_entry* primary_item_id;
    const omc_entry* primary_name;
    const omc_entry* primary_type;
    const omc_entry* primary_width;
    const omc_entry* primary_height;
    const omc_entry* primary_rotation;
    const omc_entry* primary_mirror;
    const omc_entry* iref_edge_count;
    const omc_entry* auxl_edge_count;
    const omc_entry* dimg_edge_count;
    const omc_entry* thmb_edge_count;
    const omc_entry* primary_auxl_item_id;
    const omc_entry* primary_dimg_item_id;
    const omc_entry* primary_thmb_item_id;
    const omc_entry* primary_alpha_item_id;
    const omc_entry* primary_depth_item_id;
    omc_const_bytes value_view;

    file_size = make_test_bmff_fields_only(file_bytes,
                                           fourcc('h', 'e', 'i', 'c'));
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.bmff.status == OMC_BMFF_OK);
    assert(store.block_count == 1U);
    assert(count_bmff_field(&store, "item.id") == 3U);

    item_info_count = find_bmff_field(&store, "item.info_count");
    assert(item_info_count != (const omc_entry*)0);
    assert(item_info_count->value.kind == OMC_VAL_SCALAR);
    assert(item_info_count->value.u.u64 == 3U);

    primary_item_id = find_bmff_field(&store, "meta.primary_item_id");
    assert(primary_item_id != (const omc_entry*)0);
    assert(primary_item_id->value.u.u64 == 2U);

    primary_name = find_bmff_field(&store, "primary.item_name");
    assert(primary_name != (const omc_entry*)0);
    value_view = omc_arena_view(&store.arena, primary_name->value.u.ref);
    assert(value_view.size == 3U);
    assert(memcmp(value_view.data, "XMP", value_view.size) == 0);

    primary_type = find_bmff_field(&store, "primary.item_type");
    assert(primary_type != (const omc_entry*)0);
    assert(primary_type->value.u.u64 == (omc_u64)fourcc('m', 'i', 'm', 'e'));

    primary_width = find_bmff_field(&store, "primary.width");
    assert(primary_width != (const omc_entry*)0);
    assert(primary_width->value.u.u64 == 640U);

    primary_height = find_bmff_field(&store, "primary.height");
    assert(primary_height != (const omc_entry*)0);
    assert(primary_height->value.u.u64 == 480U);

    primary_rotation = find_bmff_field(&store, "primary.rotation_degrees");
    assert(primary_rotation != (const omc_entry*)0);
    assert(primary_rotation->value.u.u64 == 90U);

    primary_mirror = find_bmff_field(&store, "primary.mirror");
    assert(primary_mirror != (const omc_entry*)0);
    assert(primary_mirror->value.u.u64 == 1U);

    iref_edge_count = find_bmff_field(&store, "iref.edge_count");
    assert(iref_edge_count != (const omc_entry*)0);
    assert(iref_edge_count->value.u.u64 == 4U);
    assert(count_bmff_field(&store, "iref.ref_type") == 4U);
    assert(count_bmff_field(&store, "iref.from_item_id") == 4U);
    assert(count_bmff_field(&store, "iref.to_item_id") == 4U);

    auxl_edge_count = find_bmff_field(&store, "iref.auxl.edge_count");
    assert(auxl_edge_count != (const omc_entry*)0);
    assert(auxl_edge_count->value.u.u64 == 2U);
    assert(count_bmff_field(&store, "iref.auxl.from_item_id") == 2U);
    assert(count_bmff_field(&store, "iref.auxl.to_item_id") == 2U);

    dimg_edge_count = find_bmff_field(&store, "iref.dimg.edge_count");
    assert(dimg_edge_count != (const omc_entry*)0);
    assert(dimg_edge_count->value.u.u64 == 1U);
    assert(count_bmff_field(&store, "iref.dimg.from_item_id") == 1U);
    assert(count_bmff_field(&store, "iref.dimg.to_item_id") == 1U);

    thmb_edge_count = find_bmff_field(&store, "iref.thmb.edge_count");
    assert(thmb_edge_count != (const omc_entry*)0);
    assert(thmb_edge_count->value.u.u64 == 1U);
    assert(count_bmff_field(&store, "iref.thmb.from_item_id") == 1U);
    assert(count_bmff_field(&store, "iref.thmb.to_item_id") == 1U);

    primary_auxl_item_id = find_bmff_field(&store, "primary.auxl_item_id");
    assert(primary_auxl_item_id != (const omc_entry*)0);
    assert(primary_auxl_item_id->value.u.u64 == 1U);
    assert(count_bmff_field(&store, "primary.auxl_item_id") == 2U);
    assert(find_bmff_field_text(&store, "primary.auxl_semantic", "alpha")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "primary.auxl_semantic", "depth")
           != (const omc_entry*)0);

    primary_dimg_item_id = find_bmff_field(&store, "primary.dimg_item_id");
    assert(primary_dimg_item_id != (const omc_entry*)0);
    assert(primary_dimg_item_id->value.u.u64 == 3U);

    primary_thmb_item_id = find_bmff_field(&store, "primary.thmb_item_id");
    assert(primary_thmb_item_id != (const omc_entry*)0);
    assert(primary_thmb_item_id->value.u.u64 == 1U);

    primary_alpha_item_id = find_bmff_field(&store, "primary.alpha_item_id");
    assert(primary_alpha_item_id != (const omc_entry*)0);
    assert(primary_alpha_item_id->value.u.u64 == 1U);

    primary_depth_item_id = find_bmff_field(&store, "primary.depth_item_id");
    assert(primary_depth_item_id != (const omc_entry*)0);
    assert(primary_depth_item_id->value.u.u64 == 3U);

    assert(count_bmff_field(&store, "aux.item_id") == 2U);
    assert(find_bmff_field_text(&store, "aux.semantic", "alpha")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.semantic", "depth")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.type",
                                "urn:mpeg:hevc:2015:auxid:1")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.type",
                                "urn:mpeg:hevc:2015:auxid:2")
           != (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_read_jp2_all(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[512];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* xmp_tool;
    const omc_entry* icc_size;
    const omc_block_info* block;

    file_size = make_test_jp2_all(file_bytes);
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.icc.status == OMC_ICC_OK);
    assert(store.block_count == 3U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_JP2);
    assert(block->kind == OMC_BLK_EXIF);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_JP2);
    assert(block->kind == OMC_BLK_XMP);

    icc_size = find_icc_header(&store, 0U);
    assert(icc_size != (const omc_entry*)0);
    block = omc_store_block(&store, icc_size->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_JP2);
    assert(block->kind == OMC_BLK_ICC);

    omc_store_fini(&store);
}

static void
test_read_jxl_all(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[512];
    omc_u32 payload_parts[16];
    omc_read_res res;
    const omc_entry* exif_make;
    const omc_entry* xmp_tool;
    const omc_entry* c2pa_marker;
    const omc_entry* cbor_value;
    const omc_block_info* block;
    const omc_block_info* jumbf_block;

    file_size = make_test_jxl_all(file_bytes);
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 16U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(res.jumbf.status == OMC_JUMBF_OK);
    assert(store.block_count == 4U);

    exif_make = find_exif_entry(&store, "ifd0", 0x010FU);
    assert(exif_make != (const omc_entry*)0);
    block = omc_store_block(&store, exif_make->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_JXL);
    assert(block->kind == OMC_BLK_EXIF);

    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");
    assert(xmp_tool != (const omc_entry*)0);
    block = omc_store_block(&store, xmp_tool->origin.block);
    assert(block != (const omc_block_info*)0);
    assert(block->format == OMC_SCAN_FMT_JXL);
    assert(block->kind == OMC_BLK_XMP);

    jumbf_block = find_block_by_kind(&store, OMC_BLK_JUMBF);
    assert(jumbf_block != (const omc_block_info*)0);
    assert(jumbf_block->format == OMC_SCAN_FMT_JXL);

    c2pa_marker = find_jumbf_field(&store, "c2pa.detected");
    assert(c2pa_marker != (const omc_entry*)0);
    cbor_value = find_jumbf_cbor_key(&store, "box.1.cbor.a");
    assert(cbor_value != (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_read_jxl_brob_jumbf(void)
{
    omc_u8 file_bytes[256];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[256];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* marker;
    const omc_entry* cbor_value;

    file_size = make_test_jxl_brob_jumbf(file_bytes);
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
#if OMC_HAVE_BROTLI
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.jumbf.status == OMC_JUMBF_OK);
    marker = find_jumbf_field(&store, "c2pa.detected");
    cbor_value = find_jumbf_cbor_key(&store, "box.1.cbor.a");
    assert(marker != (const omc_entry*)0);
    assert(cbor_value != (const omc_entry*)0);
#else
    assert(res.pay.status == OMC_PAY_UNSUPPORTED);
    assert(res.jumbf.entries_decoded == 0U);
#endif

    omc_store_fini(&store);
}

static void
test_read_jxl_brob_xmp(void)
{
    omc_u8 file_bytes[512];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[512];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* xmp_tool;

    file_size = make_test_jxl_brob_xmp(file_bytes);
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    xmp_tool = find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                              "CreatorTool");

#if OMC_HAVE_BROTLI
    assert(res.pay.status == OMC_PAY_OK);
    assert(res.xmp.status == OMC_XMP_OK);
    assert(xmp_tool != (const omc_entry*)0);
#else
    assert(res.pay.status == OMC_PAY_UNSUPPORTED);
    assert(xmp_tool == (const omc_entry*)0);
#endif

    omc_store_fini(&store);
}

int
main(void)
{
    test_read_jpeg_all();
    test_read_jpeg_comment();
    test_read_jpeg_app11_jumbf_split();
    test_read_standalone_xmp();
    test_read_png_all();
    test_read_png_text();
    test_read_png_xmp_compressed();
    test_read_webp_all();
    test_read_gif_all();
    test_read_raf_all();
    test_read_x3f_exif();
    test_read_tiff_geotiff();
    test_read_tiff_printim();
    test_read_tiff_fuji_makernote();
    test_read_tiff_canon_makernote();
    test_read_tiff_nikon_makernote();
    test_read_crw_minimal_ciff();
    test_read_crw_derived_exif();
    test_read_bmff_fields();
    test_read_bmff_heif_all();
    test_read_bmff_avif_all();
    test_read_cr3_exif();
    test_read_bmff_iref_xmp_split();
    test_read_bmff_external_dref_is_skipped();
    test_read_jp2_all();
    test_read_jxl_all();
    test_read_jxl_brob_jumbf();
    test_read_jxl_brob_xmp();
    return 0;
}
