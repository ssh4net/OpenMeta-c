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

static omc_u32
f32_bits(float value)
{
    omc_u32 bits;

    bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
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
write_u16le_at(omc_u8* out, omc_u32 off, omc_u16 value)
{
    out[off + 0U] = (omc_u8)((value >> 0) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 8) & 0xFFU);
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
make_test_tiff_with_make_model_and_makernote_count(omc_u8* out,
                                                   const char* make,
                                                   const char* model,
                                                   const omc_u8* makernote,
                                                   omc_size makernote_size,
                                                   omc_u32 makernote_count)
{
    omc_size size;
    omc_size make_size;
    omc_size model_size;
    omc_u32 make_off;
    omc_u32 model_off;
    omc_u32 maker_off;

    make_size = strlen(make) + 1U;
    model_size = strlen(model) + 1U;
    make_off = 50U;
    model_off = make_off + (omc_u32)make_size;
    maker_off = model_off + (omc_u32)model_size;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 3U);

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)make_size);
    append_u32le(out, &size, make_off);

    append_u16le(out, &size, 0x0110U);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)model_size);
    append_u32le(out, &size, model_off);

    append_u16le(out, &size, 0x927CU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, makernote_count);
    append_u32le(out, &size, maker_off);

    append_u32le(out, &size, 0U);
    append_text(out, &size, make);
    append_u8(out, &size, 0U);
    append_text(out, &size, model);
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

static omc_size
make_nikon_makernote_with_binary_subdirs(omc_u8* out)
{
    omc_size size;
    omc_u32 off_vr_pos;
    omc_u32 off_dist_pos;
    omc_u32 off_flash_pos;
    omc_u32 off_multi_pos;
    omc_u32 off_afinfo_pos;
    omc_u32 off_file_pos;
    omc_u32 off_retouch_pos;
    omc_u32 off_payload;

    size = 0U;
    append_bytes(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 8U);

    append_u16le(out, &size, 0x0001U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x01020304U);

    append_u16le(out, &size, 0x001FU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 8U);
    off_vr_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x002BU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 5U);
    off_dist_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x00A8U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 10U);
    off_flash_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x00B0U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 16U);
    off_multi_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x00B7U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 29U);
    off_afinfo_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x00B8U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 8U);
    off_file_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x00BBU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 6U);
    off_retouch_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u32le(out, &size, 0U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_vr_pos, off_payload);
    append_text(out, &size, "0101");
    append_u8(out, &size, 1U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_dist_pos, off_payload);
    append_text(out, &size, "0100");
    append_u8(out, &size, 1U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_flash_pos, off_payload);
    append_text(out, &size, "0106");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0xAAU);
    append_u8(out, &size, 0xBBU);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_multi_pos, off_payload);
    append_text(out, &size, "0100");
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 3U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_afinfo_pos, off_payload);
    append_text(out, &size, "0100");
    append_u32le(out, &size, 0U);
    append_u8(out, &size, 0xAAU);
    append_u8(out, &size, 0xBBU);
    append_u8(out, &size, 0xCCU);
    append_u8(out, &size, 0xDDU);
    append_u8(out, &size, 0xEEU);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 1U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_file_pos, off_payload);
    append_text(out, &size, "0100");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u16le(out, &size, 99U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_retouch_pos, off_payload);
    append_text(out, &size, "0100");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0xFFU);

    return size;
}

static omc_size
make_nikon_makernote_with_info_blocks(omc_u8* out)
{
    omc_size size;
    omc_u32 off_pc_pos;
    omc_u32 off_iso_pos;
    omc_u32 off_hdr_pos;
    omc_u32 off_loc_pos;
    omc_u32 off_payload;
    omc_u32 i;

    size = 0U;
    append_bytes(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 6U);

    append_u16le(out, &size, 0x0001U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x01020304U);

    append_u16le(out, &size, 0x0023U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 66U);
    off_pc_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x0024U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 4U);
    append_u32le(out, &size, 0x0201FDE4U);

    append_u16le(out, &size, 0x0025U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 10U);
    off_iso_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x0035U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 8U);
    off_hdr_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x0039U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 17U);
    off_loc_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u32le(out, &size, 0U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_pc_pos, off_payload);
    append_text(out, &size, "0200");
    append_text(out, &size, "NEUTRAL");
    for (i = 0U; i < 13U; ++i) {
        append_u8(out, &size, 0U);
    }
    append_text(out, &size, "STANDARD");
    for (i = 0U; i < 12U; ++i) {
        append_u8(out, &size, 0U);
    }
    for (i = 0U; i < 21U; ++i) {
        append_u8(out, &size, 0U);
    }
    append_u8(out, &size, 15U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_iso_pos, off_payload);
    append_text(out, &size, "0100");
    append_u32le(out, &size, 0U);
    append_u16le(out, &size, 400U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_hdr_pos, off_payload);
    append_text(out, &size, "0100");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 4U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_loc_pos, off_payload);
    append_text(out, &size, "0100");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_text(out, &size, "TOKYO-JP");

    return size;
}

static omc_size
make_canon_custom_functions2_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x0099U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 8U);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);
    append_u16le(out, &size, 32U);
    append_u16le(out, &size, 0U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 20U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x0101U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_canon_afinfo2_makernote(omc_u8* out)
{
    static const omc_s16 x_pos[9] = { 0, -649, 649, -1034, 0,
                                      1034, -649, 649, 0 };
    static const omc_s16 y_pos[9] = { 562, 298, 298, 0, 0,
                                      0, -298, -298, -562 };
    omc_size size;
    omc_u32 i;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x0026U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 48U);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);
    append_u16le(out, &size, 96U);
    append_u16le(out, &size, 2U);
    append_u16le(out, &size, 9U);
    append_u16le(out, &size, 9U);
    append_u16le(out, &size, 3888U);
    append_u16le(out, &size, 2592U);
    append_u16le(out, &size, 3888U);
    append_u16le(out, &size, 2592U);

    for (i = 0U; i < 9U; ++i) {
        append_u16le(out, &size, 97U);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(out, &size, 98U);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(out, &size, (omc_u16)x_pos[i]);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(out, &size, (omc_u16)y_pos[i]);
    }

    append_u16le(out, &size, 4U);
    append_u16le(out, &size, 4U);
    append_u16le(out, &size, 0U);
    append_u16le(out, &size, 0U);
    return size;
}

static omc_size
make_nikon_makernote_with_preview_settings_and_aftune(omc_u8* out)
{
    omc_size size;
    omc_u32 off_preview_pos;
    omc_u32 off_settings_pos;
    omc_u32 rel_off;

    size = 0U;
    append_bytes(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 4U);

    append_u16le(out, &size, 0x0001U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x01020304U);

    append_u16le(out, &size, 0x0011U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    off_preview_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x004EU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 48U);
    off_settings_pos = (omc_u32)size;
    append_u32le(out, &size, 0U);

    append_u16le(out, &size, 0x00B9U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 4U);
    append_u8(out, &size, 1U);
    append_u8(out, &size, 4U);
    append_u8(out, &size, 0xFDU);
    append_u8(out, &size, 5U);

    append_u32le(out, &size, 0U);

    rel_off = (omc_u32)(size - 10U);
    write_u32le_at(out, off_settings_pos, rel_off);
    memset(out + size, 0, 48U);
    write_u32le_at(out, (omc_u32)size + 20U, 3U);
    out[size + 24U + 0U] = 0x01U;
    out[size + 24U + 1U] = 0x00U;
    out[size + 24U + 2U] = 0x00U;
    out[size + 24U + 3U] = 0x04U;
    write_u32le_at(out, (omc_u32)size + 24U + 4U, 6400U);
    out[size + 32U + 0U] = 0x46U;
    out[size + 32U + 1U] = 0x00U;
    out[size + 32U + 2U] = 0x00U;
    out[size + 32U + 3U] = 0x01U;
    write_u32le_at(out, (omc_u32)size + 32U + 4U, 1U);
    out[size + 40U + 0U] = 0x63U;
    out[size + 40U + 1U] = 0x00U;
    out[size + 40U + 2U] = 0x00U;
    out[size + 40U + 3U] = 0x03U;
    write_u32le_at(out, (omc_u32)size + 40U + 4U, 9U);
    size += 48U;

    rel_off = (omc_u32)(size - 10U);
    write_u32le_at(out, off_preview_pos, rel_off);
    append_u16le(out, &size, 4U);

    append_u16le(out, &size, 0x0103U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 1U);
    append_u16le(out, &size, 6U);
    append_u16le(out, &size, 0U);

    append_u16le(out, &size, 0x0201U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x0200U);

    append_u16le(out, &size, 0x0202U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 0x1234U);

    append_u16le(out, &size, 0x0213U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 1U);
    append_u16le(out, &size, 2U);
    append_u16le(out, &size, 0U);

    append_u32le(out, &size, 0U);
    return size;
}

static omc_size
make_canon_filterinfo_makernote(omc_u8* out)
{
    omc_size size;
    omc_u32 filter_size;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x4024U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 12U);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);

    filter_size = (omc_u32)size;
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 4U);
    append_u32le(out, &size, 36U);
    append_u32le(out, &size, 2U);

    append_u32le(out, &size, 0x0402U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 2U);

    append_u32le(out, &size, 0x0403U);
    append_u32le(out, &size, 2U);
    append_u32le(out, &size, 300U);
    append_u32le(out, &size, 700U);

    write_u32le_at(out, filter_size, (omc_u32)(size - filter_size));
    return size;
}

static omc_size
make_canon_timeinfo_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x0035U);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, 4U);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 540U);
    append_u32le(out, &size, 1234U);
    append_u32le(out, &size, 1U);
    return size;
}

static omc_size
make_canon_camera_info_psinfo_makernote(omc_u8* out)
{
    omc_size size;
    omc_u32 cam_off;
    omc_u32 cam_bytes;

    size = 0U;
    cam_bytes = 0x025BU + 0x00DEU;

    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x000DU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, cam_bytes);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);

    cam_off = (omc_u32)size;
    memset(out + size, 0, cam_bytes);
    write_u16le_at(out, cam_off + 0U, 1U);
    write_u16le_at(out, cam_off + 2U, 0x0003U);
    write_u16le_at(out, cam_off + 4U, 3U);
    write_u32le_at(out, cam_off + 6U, 1U);
    write_u16le_at(out, cam_off + 10U, 42U);
    write_u32le_at(out, cam_off + 14U, 0U);
    write_u32le_at(out, cam_off + 0x025BU + 0x0004U, 3U);
    write_u16le_at(out, cam_off + 0x025BU + 0x00D8U, 129U);
    size += cam_bytes;

    return size;
}

static omc_size
make_canon_camera_info_psinfo2_makernote(omc_u8* out)
{
    omc_size size;
    omc_u32 cam_off;
    omc_u32 cam_bytes;

    size = 0U;
    cam_bytes = 0x025BU + 0x00F6U;

    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x000DU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, cam_bytes);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);

    cam_off = (omc_u32)size;
    memset(out + size, 0, cam_bytes);
    write_u16le_at(out, cam_off + 0U, 1U);
    write_u16le_at(out, cam_off + 2U, 0x0003U);
    write_u16le_at(out, cam_off + 4U, 3U);
    write_u32le_at(out, cam_off + 6U, 1U);
    write_u16le_at(out, cam_off + 10U, 42U);
    write_u32le_at(out, cam_off + 14U, 0U);
    write_u32le_at(out, cam_off + 0x025BU + 0x0004U, 3U);
    write_u32le_at(out, cam_off + 0x025BU + 0x0090U, 7U);
    write_u32le_at(out, cam_off + 0x025BU + 0x00E4U, 2U);
    write_u16le_at(out, cam_off + 0x025BU + 0x00F0U, 129U);
    size += cam_bytes;

    return size;
}

static omc_size
make_canon_camera_info_blob_makernote(omc_u8* out, const omc_u8* cam,
                                      omc_size cam_size)
{
    omc_size size;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x000DU);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, (omc_u32)cam_size);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);
    append_bytes(out, &size, cam, cam_size);
    return size;
}

static omc_size
make_canon_camera_info_blob_with_ascii(omc_u8* out, omc_u16 tag,
                                       const char* text, omc_size width)
{
    omc_size size;
    omc_size text_size;

    text_size = strlen(text);
    assert(text_size <= width);
    size = (omc_size)tag + width;
    memset(out, 0, size);
    memcpy(out + tag, text, text_size);
    return size;
}

static omc_size
make_canon_camera_info_blob_with_u16(omc_u8* out, omc_u16 tag, omc_u16 value)
{
    omc_size size;

    size = (omc_size)tag + 2U;
    memset(out, 0, size);
    write_u16le_at(out, tag, value);
    return size;
}

static omc_size
make_canon_camera_info_blob_with_u32(omc_u8* out, omc_u16 tag, omc_u32 value)
{
    omc_size size;

    size = (omc_size)tag + 4U;
    memset(out, 0, size);
    write_u32le_at(out, tag, value);
    return size;
}

static omc_size
make_canon_camera_info_blob_with_ifd_ascii(omc_u8* out, omc_u16 tag,
                                           const char* text, omc_size width)
{
    omc_size size;
    omc_size text_size;

    text_size = strlen(text);
    assert(width > 4U);
    assert(text_size <= width);

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, tag);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)width);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);
    memset(out + size, 0, width);
    memcpy(out + size, text, text_size);
    size += width;
    return size;
}

static omc_size
make_canon_camera_info_u32_table_makernote(omc_u8* out, omc_u32 count,
                                           omc_u16 tag, omc_u32 value)
{
    omc_size size;
    omc_u32 words_off;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x000DU);
    append_u16le(out, &size, 4U);
    append_u32le(out, &size, count);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);

    words_off = (omc_u32)size;
    memset(out + size, 0, count * 4U);
    write_u32le_at(out, words_off + ((omc_u32)tag * 4U), value);
    size += (omc_size)count * 4U;
    return size;
}

static omc_size
make_canon_colordata8_makernote(omc_u8* out)
{
    omc_size size;
    omc_u32 color_off;
    omc_u32 color_count;
    omc_u32 color_bytes;

    size = 0U;
    color_count = 1353U;
    color_bytes = color_count * 2U;

    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x4001U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, color_count);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);

    color_off = (omc_u32)size;
    memset(out + size, 0, color_bytes);
    write_u16le_at(out, color_off + (0x0000U * 2U), 14U);
    write_u16le_at(out, color_off + (0x003FU * 2U), 777U);
    write_u16le_at(out, color_off + (0x0043U * 2U), 6100U);
    write_u16le_at(out, color_off + (0x0107U * 2U), 100U);
    write_u16le_at(out, color_off + (0x0108U * 2U),
                   (omc_u16)(0xFFFFU - 24U));
    write_u16le_at(out, color_off + (0x0109U * 2U), 300U);
    write_u16le_at(out, color_off + (0x010AU * 2U), 5200U);
    write_u16le_at(out, color_off + (0x010BU * 2U),
                   (omc_u16)(0xFFFFU - 9U));
    write_u16le_at(out, color_off + (0x010CU * 2U), 20U);
    write_u16le_at(out, color_off + (0x010DU * 2U),
                   (omc_u16)(0xFFFFU - 29U));
    write_u16le_at(out, color_off + (0x010EU * 2U), 40U);
    size += color_bytes;

    return size;
}

static omc_size
make_canon_colordata_counted_makernote(omc_u8* out, omc_u32 count,
                                       omc_u16 version)
{
    omc_size size;
    omc_u32 color_off;
    omc_u32 color_bytes;

    size = 0U;
    color_bytes = count * 2U;

    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x4001U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, count);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);

    color_off = (omc_u32)size;
    memset(out + size, 0, color_bytes);
    write_u16le_at(out, color_off + (0x0000U * 2U), version);

    if (count > 0x0061U) {
        write_u16le_at(out, color_off + (0x0061U * 2U),
                       (omc_u16)(6200U + version));
    }
    if (count > 0x00E0U) {
        write_u16le_at(out, color_off + (0x00E0U * 2U), 150U);
        write_u16le_at(out, color_off + (0x00E1U * 2U), 250U);
        write_u16le_at(out, color_off + (0x00E2U * 2U), 350U);
        write_u16le_at(out, color_off + (0x00E3U * 2U), 4500U);
    }
    if (count > 0x0042U) {
        write_u16le_at(out, color_off + (0x003FU * 2U),
                       (omc_u16)(1700U + version));
        write_u16le_at(out, color_off + (0x0040U * 2U),
                       (omc_u16)(1800U + version));
        write_u16le_at(out, color_off + (0x0041U * 2U),
                       (omc_u16)(1900U + version));
        write_u16le_at(out, color_off + (0x0042U * 2U),
                       (omc_u16)(2000U + version));
    }
    if (count > 0x010DU) {
        write_u16le_at(out, color_off + (0x010DU * 2U), 410U);
        write_u16le_at(out, color_off + (0x010EU * 2U), 510U);
        write_u16le_at(out, color_off + (0x010FU * 2U), 610U);
        write_u16le_at(out, color_off + (0x0110U * 2U), 5200U);
    }
    if (count > 0x0076U) {
        write_u16le_at(out, color_off + (0x0073U * 2U),
                       (omc_u16)(2700U + version));
        write_u16le_at(out, color_off + (0x0074U * 2U),
                       (omc_u16)(2800U + version));
        write_u16le_at(out, color_off + (0x0075U * 2U),
                       (omc_u16)(2900U + version));
        write_u16le_at(out, color_off + (0x0076U * 2U),
                       (omc_u16)(3000U + version));
    }
    if (count > 0x017BU) {
        write_u16le_at(out, color_off + (0x0178U * 2U), 710U);
        write_u16le_at(out, color_off + (0x0179U * 2U), 810U);
        write_u16le_at(out, color_off + (0x017AU * 2U), 910U);
        write_u16le_at(out, color_off + (0x017BU * 2U), 6100U);
    }

    size += color_bytes;
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
make_test_jpeg_irb_fields(omc_u8* out)
{
    omc_u8 irb[64];
    omc_u8 angle[8];
    omc_size irb_size;
    omc_size angle_size;
    omc_size size;
    omc_u16 seg_len;

    angle_size = 0U;
    append_u32be(angle, &angle_size, 30U);

    irb_size = 0U;
    append_irb_resource(irb, &irb_size, 0x040DU, angle, angle_size);

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);

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
make_ciff_inline_directory(omc_u8* out, const omc_ciff_val_ent* entries,
                           omc_size entry_count)
{
    omc_size size;
    omc_size i;
    omc_u8 tmp[8];

    size = 0U;
    append_u16le(out, &size, (omc_u16)entry_count);

    for (i = 0U; i < entry_count; ++i) {
        append_u16le(out, &size, entries[i].tag);
        memset(tmp, 0, sizeof(tmp));
        if (entries[i].value_size != 0U) {
            memcpy(tmp, entries[i].value,
                   entries[i].value_size < sizeof(tmp)
                       ? entries[i].value_size
                       : sizeof(tmp));
        }
        append_bytes(out, &size, tmp, sizeof(tmp));
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
make_padded_ascii(omc_u8* out, const char* text, omc_size width)
{
    omc_size text_size;

    memset(out, 0, width);
    text_size = strlen(text);
    if (text_size > width) {
        text_size = width;
    }
    memcpy(out, text, text_size);
    return width;
}

static omc_size
make_padded_u16_scalar(omc_u8* out, omc_u16 value)
{
    memset(out, 0, 8U);
    write_u16le_at(out, 0U, value);
    return 8U;
}

static omc_size
make_padded_u32_scalar(omc_u8* out, omc_u32 value)
{
    memset(out, 0, 8U);
    write_u32le_at(out, 0U, value);
    return 8U;
}

static omc_size
make_padded_f32_scalar(omc_u8* out, float value)
{
    return make_padded_u32_scalar(out, f32_bits(value));
}

static omc_size
make_u32_pair(omc_u8* out, omc_u32 first, omc_u32 second)
{
    omc_size size;

    size = 0U;
    append_u32le(out, &size, first);
    append_u32le(out, &size, second);
    return size;
}

static omc_size
make_test_crw_textual_ciff(omc_u8* out)
{
    omc_u8 text0[32];
    omc_u8 text1[32];
    omc_u8 text2[32];
    omc_u8 text3[32];
    omc_u8 dir2804[128];
    omc_u8 dir2807[128];
    omc_u8 dir3004[128];
    omc_u8 dir300a[128];
    omc_u8 root[512];
    omc_size dir2804_size;
    omc_size dir2807_size;
    omc_size dir3004_size;
    omc_size dir300a_size;
    omc_size root_size;
    omc_ciff_val_ent dir2804_entries[1];
    omc_ciff_val_ent dir2807_entries[1];
    omc_ciff_val_ent dir3004_entries[1];
    omc_ciff_val_ent dir300a_entries[1];
    omc_ciff_val_ent root_entries[4];
    omc_size size;

    make_padded_ascii(text0, "High definition camera", 32U);
    make_padded_ascii(text1, "Alice", 32U);
    make_padded_ascii(text2, "Ver 2.10", 32U);
    make_padded_ascii(text3, "IMG_0001.CRW", 32U);

    dir2804_entries[0].tag = 0x0805U;
    dir2804_entries[0].value = text0;
    dir2804_entries[0].value_size = 32U;
    dir2804_size = make_ciff_directory(dir2804, dir2804_entries, 1U);

    dir2807_entries[0].tag = 0x0810U;
    dir2807_entries[0].value = text1;
    dir2807_entries[0].value_size = 32U;
    dir2807_size = make_ciff_directory(dir2807, dir2807_entries, 1U);

    dir3004_entries[0].tag = 0x080CU;
    dir3004_entries[0].value = text2;
    dir3004_entries[0].value_size = 32U;
    dir3004_size = make_ciff_directory(dir3004, dir3004_entries, 1U);

    dir300a_entries[0].tag = 0x0816U;
    dir300a_entries[0].value = text3;
    dir300a_entries[0].value_size = 32U;
    dir300a_size = make_ciff_directory(dir300a, dir300a_entries, 1U);

    root_entries[0].tag = 0x2804U;
    root_entries[0].value = dir2804;
    root_entries[0].value_size = dir2804_size;
    root_entries[1].tag = 0x2807U;
    root_entries[1].value = dir2807;
    root_entries[1].value_size = dir2807_size;
    root_entries[2].tag = 0x3004U;
    root_entries[2].value = dir3004;
    root_entries[2].value_size = dir3004_size;
    root_entries[3].tag = 0x300AU;
    root_entries[3].value = dir300a;
    root_entries[3].value_size = dir300a_size;
    root_size = make_ciff_directory(root, root_entries, 4U);

    size = 0U;
    append_text(out, &size, "II");
    append_u32le(out, &size, 14U);
    append_text(out, &size, "HEAPCCDR");
    append_bytes(out, &size, root, root_size);
    return size;
}

static omc_size
make_test_crw_native_projection(omc_u8* out)
{
    omc_u8 make_model[64];
    omc_u8 subject_distance[8];
    omc_u8 image_format[16];
    omc_u8 exposure_info[16];
    omc_u8 flash_info[16];
    omc_u8 focal_length[8];
    omc_u8 datetime_original[16];
    omc_u8 dimensions_orientation[32];
    omc_u8 dir2807[128];
    omc_u8 dir3002[256];
    omc_u8 dir300a[256];
    omc_u8 dir300b[128];
    omc_u8 root[1024];
    omc_size make_model_size;
    omc_size subject_distance_size;
    omc_size image_format_size;
    omc_size exposure_info_size;
    omc_size flash_info_size;
    omc_size focal_length_size;
    omc_size datetime_original_size;
    omc_size dimensions_orientation_size;
    omc_size dir2807_size;
    omc_size dir3002_size;
    omc_size dir300a_size;
    omc_size dir300b_size;
    omc_size root_size;
    omc_ciff_val_ent dir2807_entries[1];
    omc_ciff_val_ent dir3002_entries[3];
    omc_ciff_val_ent dir300a_entries[3];
    omc_ciff_val_ent dir300b_entries[2];
    omc_ciff_val_ent root_entries[4];
    omc_size size;

    make_model_size = 0U;
    append_text(make_model, &make_model_size, "Canon");
    append_u8(make_model, &make_model_size, 0U);
    append_text(make_model, &make_model_size, "PowerShot Pro70");
    append_u8(make_model, &make_model_size, 0U);

    subject_distance_size = 0U;
    append_u32le(subject_distance, &subject_distance_size, 123U);

    image_format_size = 0U;
    append_u32le(image_format, &image_format_size, 0x00020001U);
    append_u32le(image_format, &image_format_size, f32_bits(10.0f));

    exposure_info_size = 0U;
    append_u32le(exposure_info, &exposure_info_size, f32_bits(0.33333334f));
    append_u32le(exposure_info, &exposure_info_size, f32_bits(6.875f));
    append_u32le(exposure_info, &exposure_info_size, f32_bits(3.0f));

    flash_info_size = make_u32_pair(flash_info, f32_bits(0.0f),
                                    f32_bits(0.0f));

    focal_length_size = 0U;
    append_u16le(focal_length, &focal_length_size, 2U);
    append_u16le(focal_length, &focal_length_size, 473U);
    append_u16le(focal_length, &focal_length_size, 309U);
    append_u16le(focal_length, &focal_length_size, 206U);

    datetime_original_size = 0U;
    append_u32le(datetime_original, &datetime_original_size, 1700000000U);
    append_u32le(datetime_original, &datetime_original_size, 0xFFFFFFFDU);
    append_u32le(datetime_original, &datetime_original_size, 0x0000007BU);

    dimensions_orientation_size = 0U;
    append_u32le(dimensions_orientation, &dimensions_orientation_size, 1536U);
    append_u32le(dimensions_orientation, &dimensions_orientation_size, 1024U);
    append_u32le(dimensions_orientation, &dimensions_orientation_size,
                 f32_bits(1.0f));
    append_u32le(dimensions_orientation, &dimensions_orientation_size, 90U);

    dir2807_entries[0].tag = 0x080AU;
    dir2807_entries[0].value = make_model;
    dir2807_entries[0].value_size = make_model_size;
    dir2807_size = make_ciff_directory(dir2807, dir2807_entries, 1U);

    dir3002_entries[0].tag = 0x1813U;
    dir3002_entries[0].value = flash_info;
    dir3002_entries[0].value_size = flash_info_size;
    dir3002_entries[1].tag = 0x1807U;
    dir3002_entries[1].value = subject_distance;
    dir3002_entries[1].value_size = subject_distance_size;
    dir3002_entries[2].tag = 0x1818U;
    dir3002_entries[2].value = exposure_info;
    dir3002_entries[2].value_size = exposure_info_size;
    dir3002_size = make_ciff_directory(dir3002, dir3002_entries, 3U);

    dir300a_entries[0].tag = 0x1803U;
    dir300a_entries[0].value = image_format;
    dir300a_entries[0].value_size = image_format_size;
    dir300a_entries[1].tag = 0x180EU;
    dir300a_entries[1].value = datetime_original;
    dir300a_entries[1].value_size = datetime_original_size;
    dir300a_entries[2].tag = 0x1810U;
    dir300a_entries[2].value = dimensions_orientation;
    dir300a_entries[2].value_size = dimensions_orientation_size;
    dir300a_size = make_ciff_directory(dir300a, dir300a_entries, 3U);

    dir300b_entries[0].tag = 0x1028U;
    dir300b_entries[0].value = flash_info;
    dir300b_entries[0].value_size = flash_info_size;
    dir300b_entries[1].tag = 0x1029U;
    dir300b_entries[1].value = focal_length;
    dir300b_entries[1].value_size = focal_length_size;
    dir300b_size = make_ciff_directory(dir300b, dir300b_entries, 2U);

    root_entries[0].tag = 0x2807U;
    root_entries[0].value = dir2807;
    root_entries[0].value_size = dir2807_size;
    root_entries[1].tag = 0x3002U;
    root_entries[1].value = dir3002;
    root_entries[1].value_size = dir3002_size;
    root_entries[2].tag = 0x300AU;
    root_entries[2].value = dir300a;
    root_entries[2].value_size = dir300a_size;
    root_entries[3].tag = 0x300BU;
    root_entries[3].value = dir300b;
    root_entries[3].value_size = dir300b_size;
    root_size = make_ciff_directory(root, root_entries, 4U);

    size = 0U;
    append_text(out, &size, "II");
    append_u32le(out, &size, 14U);
    append_text(out, &size, "HEAPCCDR");
    append_bytes(out, &size, root, root_size);
    return size;
}

static omc_size
make_test_crw_semantic_native_scalars(omc_u8* out)
{
    omc_u8 s3002_1010[8];
    omc_u8 s3002_1011[8];
    omc_u8 s3002_1016[8];
    omc_u8 s3002_1807[8];
    omc_u8 s3003_1814[8];
    omc_u8 s3004_101c[8];
    omc_u8 s3004_1834[8];
    omc_u8 s3004_183b[8];
    omc_u8 s300a_100a[8];
    omc_u8 s300a_1804[8];
    omc_u8 s300a_1806[8];
    omc_u8 s300a_1817[8];
    omc_u8 dir3002[128];
    omc_u8 dir3003[64];
    omc_u8 dir3004[128];
    omc_u8 dir300a[128];
    omc_u8 root[640];
    omc_size dir3002_size;
    omc_size dir3003_size;
    omc_size dir3004_size;
    omc_size dir300a_size;
    omc_size root_size;
    omc_ciff_val_ent dir3002_entries[4];
    omc_ciff_val_ent dir3003_entries[1];
    omc_ciff_val_ent dir3004_entries[3];
    omc_ciff_val_ent dir300a_entries[4];
    omc_ciff_val_ent root_entries[4];
    omc_size size;

    make_padded_u16_scalar(s3002_1010, 2U);
    make_padded_u16_scalar(s3002_1011, 1U);
    make_padded_u16_scalar(s3002_1016, 3U);
    make_padded_f32_scalar(s3002_1807, 12.5f);
    make_padded_f32_scalar(s3003_1814, 9.5f);
    make_padded_u16_scalar(s3004_101c, 100U);
    make_padded_u32_scalar(s3004_1834, 0x80000169U);
    make_padded_u32_scalar(s3004_183b, 2U);
    make_padded_u16_scalar(s300a_100a, 7U);
    make_padded_u32_scalar(s300a_1804, 42U);
    make_padded_u32_scalar(s300a_1806, 1000U);
    make_padded_u32_scalar(s300a_1817, 162U);

    dir3002_entries[0].tag = 0x5010U;
    dir3002_entries[0].value = s3002_1010;
    dir3002_entries[0].value_size = 8U;
    dir3002_entries[1].tag = 0x5011U;
    dir3002_entries[1].value = s3002_1011;
    dir3002_entries[1].value_size = 8U;
    dir3002_entries[2].tag = 0x5016U;
    dir3002_entries[2].value = s3002_1016;
    dir3002_entries[2].value_size = 8U;
    dir3002_entries[3].tag = 0x5807U;
    dir3002_entries[3].value = s3002_1807;
    dir3002_entries[3].value_size = 8U;
    dir3002_size = make_ciff_inline_directory(dir3002, dir3002_entries, 4U);

    dir3003_entries[0].tag = 0x5814U;
    dir3003_entries[0].value = s3003_1814;
    dir3003_entries[0].value_size = 8U;
    dir3003_size = make_ciff_inline_directory(dir3003, dir3003_entries, 1U);

    dir3004_entries[0].tag = 0x501CU;
    dir3004_entries[0].value = s3004_101c;
    dir3004_entries[0].value_size = 8U;
    dir3004_entries[1].tag = 0x5834U;
    dir3004_entries[1].value = s3004_1834;
    dir3004_entries[1].value_size = 8U;
    dir3004_entries[2].tag = 0x583BU;
    dir3004_entries[2].value = s3004_183b;
    dir3004_entries[2].value_size = 8U;
    dir3004_size = make_ciff_inline_directory(dir3004, dir3004_entries, 3U);

    dir300a_entries[0].tag = 0x500AU;
    dir300a_entries[0].value = s300a_100a;
    dir300a_entries[0].value_size = 8U;
    dir300a_entries[1].tag = 0x5804U;
    dir300a_entries[1].value = s300a_1804;
    dir300a_entries[1].value_size = 8U;
    dir300a_entries[2].tag = 0x5806U;
    dir300a_entries[2].value = s300a_1806;
    dir300a_entries[2].value_size = 8U;
    dir300a_entries[3].tag = 0x5817U;
    dir300a_entries[3].value = s300a_1817;
    dir300a_entries[3].value_size = 8U;
    dir300a_size = make_ciff_inline_directory(dir300a, dir300a_entries, 4U);

    root_entries[0].tag = 0x3002U;
    root_entries[0].value = dir3002;
    root_entries[0].value_size = dir3002_size;
    root_entries[1].tag = 0x3003U;
    root_entries[1].value = dir3003;
    root_entries[1].value_size = dir3003_size;
    root_entries[2].tag = 0x3004U;
    root_entries[2].value = dir3004;
    root_entries[2].value_size = dir3004_size;
    root_entries[3].tag = 0x300AU;
    root_entries[3].value = dir300a;
    root_entries[3].value_size = dir300a_size;
    root_size = make_ciff_directory(root, root_entries, 4U);

    size = 0U;
    append_text(out, &size, "II");
    append_u32le(out, &size, 14U);
    append_text(out, &size, "HEAPCCDR");
    append_bytes(out, &size, root, root_size);
    return size;
}

static omc_size
make_test_crw_decoder_table(omc_u8* out)
{
    omc_u8 decoder_table[16];
    omc_u8 dir3004[64];
    omc_u8 root[128];
    omc_size decoder_table_size;
    omc_size dir3004_size;
    omc_size root_size;
    omc_ciff_val_ent dir3004_entries[1];
    omc_ciff_val_ent root_entries[1];
    omc_size size;

    decoder_table_size = 0U;
    append_u32le(decoder_table, &decoder_table_size, 7U);
    append_u32le(decoder_table, &decoder_table_size, 0U);
    append_u32le(decoder_table, &decoder_table_size, 4096U);
    append_u32le(decoder_table, &decoder_table_size, 8192U);

    dir3004_entries[0].tag = 0x1835U;
    dir3004_entries[0].value = decoder_table;
    dir3004_entries[0].value_size = decoder_table_size;
    dir3004_size = make_ciff_directory(dir3004, dir3004_entries, 1U);

    root_entries[0].tag = 0x3004U;
    root_entries[0].value = dir3004;
    root_entries[0].value_size = dir3004_size;
    root_size = make_ciff_directory(root, root_entries, 1U);

    size = 0U;
    append_text(out, &size, "II");
    append_u32le(out, &size, 14U);
    append_text(out, &size, "HEAPCCDR");
    append_bytes(out, &size, root, root_size);
    return size;
}

static omc_size
make_test_crw_rawjpginfo_and_whitesample(omc_u8* out)
{
    omc_u8 raw_jpg_info[16];
    omc_u8 white_sample[16];
    omc_u8 dir300b[96];
    omc_u8 root[160];
    omc_size raw_jpg_info_size;
    omc_size white_sample_size;
    omc_size dir300b_size;
    omc_size root_size;
    omc_ciff_val_ent dir300b_entries[2];
    omc_ciff_val_ent root_entries[1];
    omc_size size;

    raw_jpg_info_size = 0U;
    append_u16le(raw_jpg_info, &raw_jpg_info_size, 0U);
    append_u16le(raw_jpg_info, &raw_jpg_info_size, 3U);
    append_u16le(raw_jpg_info, &raw_jpg_info_size, 2U);
    append_u16le(raw_jpg_info, &raw_jpg_info_size, 2048U);
    append_u16le(raw_jpg_info, &raw_jpg_info_size, 1536U);

    white_sample_size = 0U;
    append_u16le(white_sample, &white_sample_size, 0U);
    append_u16le(white_sample, &white_sample_size, 64U);
    append_u16le(white_sample, &white_sample_size, 48U);
    append_u16le(white_sample, &white_sample_size, 4U);
    append_u16le(white_sample, &white_sample_size, 2U);
    append_u16le(white_sample, &white_sample_size, 10U);

    dir300b_entries[0].tag = 0x1030U;
    dir300b_entries[0].value = white_sample;
    dir300b_entries[0].value_size = white_sample_size;
    dir300b_entries[1].tag = 0x10B5U;
    dir300b_entries[1].value = raw_jpg_info;
    dir300b_entries[1].value_size = raw_jpg_info_size;
    dir300b_size = make_ciff_directory(dir300b, dir300b_entries, 2U);

    root_entries[0].tag = 0x300BU;
    root_entries[0].value = dir300b;
    root_entries[0].value_size = dir300b_size;
    root_size = make_ciff_directory(root, root_entries, 1U);

    size = 0U;
    append_text(out, &size, "II");
    append_u32le(out, &size, 14U);
    append_text(out, &size, "HEAPCCDR");
    append_bytes(out, &size, root, root_size);
    return size;
}

static omc_size
make_test_crw_shotinfo(omc_u8* out)
{
    omc_u8 shot_info[32];
    omc_u8 dir300b[96];
    omc_u8 root[160];
    omc_size shot_info_size;
    omc_size dir300b_size;
    omc_size root_size;
    omc_ciff_val_ent dir300b_entries[1];
    omc_ciff_val_ent root_entries[1];
    omc_size size;

    shot_info_size = 0U;
    append_u16le(shot_info, &shot_info_size, 100U);
    append_u16le(shot_info, &shot_info_size, 200U);
    append_u16le(shot_info, &shot_info_size, 300U);
    append_u16le(shot_info, &shot_info_size, 400U);
    append_u16le(shot_info, &shot_info_size, (omc_u16)-64);
    append_u16le(shot_info, &shot_info_size, 3U);
    append_u16le(shot_info, &shot_info_size, 1U);
    append_u16le(shot_info, &shot_info_size, 2U);
    append_u16le(shot_info, &shot_info_size, 9U);
    append_u16le(shot_info, &shot_info_size, 6U);

    dir300b_entries[0].tag = 0x102AU;
    dir300b_entries[0].value = shot_info;
    dir300b_entries[0].value_size = shot_info_size;
    dir300b_size = make_ciff_directory(dir300b, dir300b_entries, 1U);

    root_entries[0].tag = 0x300BU;
    root_entries[0].value = dir300b;
    root_entries[0].value_size = dir300b_size;
    root_size = make_ciff_directory(root, root_entries, 1U);

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
    static const omc_u8 auxc_alpha_subtype[1] = { 0x11U };
    static const omc_u8 auxc_depth_subtype[2] = { 0xAAU, 0xBBU };
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
    append_bytes(auxc_alpha_payload, &auxc_alpha_size, auxc_alpha_subtype,
                 sizeof(auxc_alpha_subtype));

    auxc_depth_size = 0U;
    append_fullbox_header(auxc_depth_payload, &auxc_depth_size, 0U);
    append_text(auxc_depth_payload, &auxc_depth_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_depth_payload, &auxc_depth_size, 0U);
    append_bytes(auxc_depth_payload, &auxc_depth_size, auxc_depth_subtype,
                 sizeof(auxc_depth_subtype));

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
make_test_bmff_aux_subtype_kinds_only(omc_u8* out, omc_u32 major_brand)
{
    static const omc_u8 auxc_text_subtype[] = {
        'p', 'r', 'o', 'f', 'i', 'l', 'e', 0x00U
    };
    static const omc_u8 auxc_u64_subtype[8] = {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U
    };
    static const omc_u8 auxc_uuid_subtype[16] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU
    };
    omc_u8 infe_primary[96];
    omc_u8 infe_aux1[96];
    omc_u8 infe_aux2[96];
    omc_u8 infe_aux3[96];
    omc_u8 iinf_payload[512];
    omc_u8 pitm_payload[16];
    omc_u8 auxc_text_payload[96];
    omc_u8 auxc_u64_payload[96];
    omc_u8 auxc_uuid_payload[112];
    omc_u8 ipco_payload[352];
    omc_u8 ipma_payload[64];
    omc_u8 iprp_payload[448];
    omc_u8 auxl_payload[16];
    omc_u8 iref_payload[64];
    omc_u8 meta_payload[1024];
    omc_u8 moov_box[16];
    omc_u8 ftyp_payload[16];
    omc_size infe_primary_size;
    omc_size infe_aux1_size;
    omc_size infe_aux2_size;
    omc_size infe_aux3_size;
    omc_size iinf_size;
    omc_size pitm_size;
    omc_size auxc_text_size;
    omc_size auxc_u64_size;
    omc_size auxc_uuid_size;
    omc_size ipco_size;
    omc_size ipma_size;
    omc_size iprp_size;
    omc_size auxl_size;
    omc_size iref_size;
    omc_size meta_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    infe_primary_size = 0U;
    append_fullbox_header(infe_primary, &infe_primary_size, 2U);
    append_u16be(infe_primary, &infe_primary_size, 1U);
    append_u16be(infe_primary, &infe_primary_size, 0U);
    append_u32be(infe_primary, &infe_primary_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_primary, &infe_primary_size, "Primary");
    append_u8(infe_primary, &infe_primary_size, 0U);
    append_text(infe_primary, &infe_primary_size, "application/octet-stream");
    append_u8(infe_primary, &infe_primary_size, 0U);
    append_u8(infe_primary, &infe_primary_size, 0U);

    infe_aux1_size = 0U;
    append_fullbox_header(infe_aux1, &infe_aux1_size, 2U);
    append_u16be(infe_aux1, &infe_aux1_size, 2U);
    append_u16be(infe_aux1, &infe_aux1_size, 0U);
    append_u32be(infe_aux1, &infe_aux1_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_aux1, &infe_aux1_size, "AuxText");
    append_u8(infe_aux1, &infe_aux1_size, 0U);
    append_text(infe_aux1, &infe_aux1_size, "application/octet-stream");
    append_u8(infe_aux1, &infe_aux1_size, 0U);
    append_u8(infe_aux1, &infe_aux1_size, 0U);

    infe_aux2_size = 0U;
    append_fullbox_header(infe_aux2, &infe_aux2_size, 2U);
    append_u16be(infe_aux2, &infe_aux2_size, 3U);
    append_u16be(infe_aux2, &infe_aux2_size, 0U);
    append_u32be(infe_aux2, &infe_aux2_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_aux2, &infe_aux2_size, "AuxU64");
    append_u8(infe_aux2, &infe_aux2_size, 0U);
    append_text(infe_aux2, &infe_aux2_size, "application/octet-stream");
    append_u8(infe_aux2, &infe_aux2_size, 0U);
    append_u8(infe_aux2, &infe_aux2_size, 0U);

    infe_aux3_size = 0U;
    append_fullbox_header(infe_aux3, &infe_aux3_size, 2U);
    append_u16be(infe_aux3, &infe_aux3_size, 4U);
    append_u16be(infe_aux3, &infe_aux3_size, 0U);
    append_u32be(infe_aux3, &infe_aux3_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_aux3, &infe_aux3_size, "AuxUuid");
    append_u8(infe_aux3, &infe_aux3_size, 0U);
    append_text(infe_aux3, &infe_aux3_size, "application/octet-stream");
    append_u8(infe_aux3, &infe_aux3_size, 0U);
    append_u8(infe_aux3, &infe_aux3_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 0U);
    append_u16be(iinf_payload, &iinf_size, 4U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_primary, infe_primary_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_aux1, infe_aux1_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_aux2, infe_aux2_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_aux3, infe_aux3_size);

    pitm_size = 0U;
    append_fullbox_header(pitm_payload, &pitm_size, 0U);
    append_u16be(pitm_payload, &pitm_size, 1U);

    auxc_text_size = 0U;
    append_fullbox_header(auxc_text_payload, &auxc_text_size, 0U);
    append_text(auxc_text_payload, &auxc_text_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_text_payload, &auxc_text_size, 0U);
    append_bytes(auxc_text_payload, &auxc_text_size, auxc_text_subtype,
                 sizeof(auxc_text_subtype));

    auxc_u64_size = 0U;
    append_fullbox_header(auxc_u64_payload, &auxc_u64_size, 0U);
    append_text(auxc_u64_payload, &auxc_u64_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_u64_payload, &auxc_u64_size, 0U);
    append_bytes(auxc_u64_payload, &auxc_u64_size, auxc_u64_subtype,
                 sizeof(auxc_u64_subtype));

    auxc_uuid_size = 0U;
    append_fullbox_header(auxc_uuid_payload, &auxc_uuid_size, 0U);
    append_text(auxc_uuid_payload, &auxc_uuid_size,
                "urn:com:apple:photo:2020:aux:matte");
    append_u8(auxc_uuid_payload, &auxc_uuid_size, 0U);
    append_bytes(auxc_uuid_payload, &auxc_uuid_size, auxc_uuid_subtype,
                 sizeof(auxc_uuid_subtype));

    ipco_size = 0U;
    append_bmff_box(ipco_payload, &ipco_size, fourcc('a', 'u', 'x', 'C'),
                    auxc_text_payload, auxc_text_size);
    append_bmff_box(ipco_payload, &ipco_size, fourcc('a', 'u', 'x', 'C'),
                    auxc_u64_payload, auxc_u64_size);
    append_bmff_box(ipco_payload, &ipco_size, fourcc('a', 'u', 'x', 'C'),
                    auxc_uuid_payload, auxc_uuid_size);

    ipma_size = 0U;
    append_fullbox_header(ipma_payload, &ipma_size, 0U);
    append_u32be(ipma_payload, &ipma_size, 4U);
    append_u16be(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 0U);
    append_u16be(ipma_payload, &ipma_size, 2U);
    append_u8(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 1U);
    append_u16be(ipma_payload, &ipma_size, 3U);
    append_u8(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 2U);
    append_u16be(ipma_payload, &ipma_size, 4U);
    append_u8(ipma_payload, &ipma_size, 1U);
    append_u8(ipma_payload, &ipma_size, 3U);

    iprp_size = 0U;
    append_bmff_box(iprp_payload, &iprp_size, fourcc('i', 'p', 'c', 'o'),
                    ipco_payload, ipco_size);
    append_bmff_box(iprp_payload, &iprp_size, fourcc('i', 'p', 'm', 'a'),
                    ipma_payload, ipma_size);

    auxl_size = 0U;
    append_u16be(auxl_payload, &auxl_size, 1U);
    append_u16be(auxl_payload, &auxl_size, 3U);
    append_u16be(auxl_payload, &auxl_size, 2U);
    append_u16be(auxl_payload, &auxl_size, 3U);
    append_u16be(auxl_payload, &auxl_size, 4U);

    iref_size = 0U;
    append_fullbox_header(iref_payload, &iref_size, 0U);
    append_bmff_box(iref_payload, &iref_size, fourcc('a', 'u', 'x', 'l'),
                    auxl_payload, auxl_size);

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
make_test_bmff_pred_only(omc_u8* out, omc_u32 major_brand)
{
    omc_u8 pitm_payload[16];
    omc_u8 pred_payload[16];
    omc_u8 iref_payload[64];
    omc_u8 meta_payload[128];
    omc_u8 ftyp_payload[16];
    omc_size pitm_size;
    omc_size pred_size;
    omc_size iref_size;
    omc_size meta_size;
    omc_size ftyp_size;
    omc_size size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload, &pitm_size, 0U);
    append_u16be(pitm_payload, &pitm_size, 1U);

    pred_size = 0U;
    append_u16be(pred_payload, &pred_size, 9U);
    append_u16be(pred_payload, &pred_size, 2U);
    append_u16be(pred_payload, &pred_size, 10U);
    append_u16be(pred_payload, &pred_size, 11U);

    iref_size = 0U;
    append_fullbox_header(iref_payload, &iref_size, 0U);
    append_bmff_box(iref_payload, &iref_size, fourcc('p', 'r', 'e', 'd'),
                    pred_payload, pred_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('p', 'i', 't', 'm'),
                    pitm_payload, pitm_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'r', 'e', 'f'),
                    iref_payload, iref_size);

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

static omc_u8
sony_encipher_byte(omc_u8 value8)
{
    omc_u32 x;
    omc_u32 x2;
    omc_u32 x3;

    if (value8 >= 249U) {
        return value8;
    }
    x = value8;
    x2 = (x * x) % 249U;
    x3 = (x2 * x) % 249U;
    return (omc_u8)x3;
}

static int
read_u16le_at_raw(const omc_u8* raw, omc_size raw_size, omc_u32 off,
                  omc_u16* out_value)
{
    if (raw == (const omc_u8*)0 || out_value == (omc_u16*)0
        || off > raw_size || (raw_size - off) < 2U) {
        return 0;
    }
    *out_value = (omc_u16)(((omc_u16)raw[off + 0U])
                           | (((omc_u16)raw[off + 1U]) << 8));
    return 1;
}

static int
read_u32le_at_raw(const omc_u8* raw, omc_size raw_size, omc_u32 off,
                  omc_u32* out_value)
{
    if (raw == (const omc_u8*)0 || out_value == (omc_u32*)0
        || off > raw_size || (raw_size - off) < 4U) {
        return 0;
    }
    *out_value = ((omc_u32)raw[off + 0U]) | (((omc_u32)raw[off + 1U]) << 8)
                 | (((omc_u32)raw[off + 2U]) << 16)
                 | (((omc_u32)raw[off + 3U]) << 24);
    return 1;
}

static int
patch_sony_makernote_value_offset_in_tiff(omc_u8* tiff, omc_size tiff_size)
{
    omc_u32 ifd0_off32;
    omc_u16 ifd0_count;
    omc_u32 exif_ifd_off32;
    omc_u16 exif_count;
    omc_u32 maker_note_off32;
    omc_u32 i;

    if (tiff == (omc_u8*)0 || tiff_size < 8U || tiff[0U] != (omc_u8)'I'
        || tiff[1U] != (omc_u8)'I'
        || !read_u32le_at_raw(tiff, tiff_size, 4U, &ifd0_off32)
        || !read_u16le_at_raw(tiff, tiff_size, ifd0_off32, &ifd0_count)) {
        return 0;
    }

    exif_ifd_off32 = 0U;
    maker_note_off32 = 0U;
    for (i = 0U; i < (omc_u32)ifd0_count; ++i) {
        omc_u32 eoff;
        omc_u16 tag16;

        eoff = ifd0_off32 + 2U + (i * 12U);
        if (!read_u16le_at_raw(tiff, tiff_size, eoff, &tag16)) {
            return 0;
        }
        if (tag16 == 0x927CU) {
            if (!read_u32le_at_raw(tiff, tiff_size, eoff + 8U,
                                   &maker_note_off32)) {
                return 0;
            }
            break;
        }
        if (tag16 == 0x8769U) {
            if (!read_u32le_at_raw(tiff, tiff_size, eoff + 8U,
                                   &exif_ifd_off32)) {
                return 0;
            }
        }
    }
    if (maker_note_off32 == 0U && exif_ifd_off32 != 0U) {
        if (!read_u16le_at_raw(tiff, tiff_size, exif_ifd_off32, &exif_count)) {
            return 0;
        }
        for (i = 0U; i < (omc_u32)exif_count; ++i) {
            omc_u32 eoff;
            omc_u16 tag16;

            eoff = exif_ifd_off32 + 2U + (i * 12U);
            if (!read_u16le_at_raw(tiff, tiff_size, eoff, &tag16)) {
                return 0;
            }
            if (tag16 == 0x927CU) {
                if (!read_u32le_at_raw(tiff, tiff_size, eoff + 8U,
                                       &maker_note_off32)) {
                    return 0;
                }
                break;
            }
        }
    }
    if (maker_note_off32 == 0U || maker_note_off32 > tiff_size) {
        return 0;
    }

    if ((tiff_size - maker_note_off32) >= 34U
        && memcmp(tiff + maker_note_off32, "SONY", 4U) == 0) {
        write_u32le_at(tiff, maker_note_off32 + 26U, maker_note_off32 + 34U);
        return 1;
    }
    if ((tiff_size - maker_note_off32) < 18U) {
        return 0;
    }
    write_u32le_at(tiff, maker_note_off32 + 10U, maker_note_off32 + 18U);
    return 1;
}

static omc_size
make_sony_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_bytes(out, &size, "SONY", 4U);
    append_u16le(out, &size, 2U);
    append_u16le(out, &size, 0x0102U);
    append_u16le(out, &size, 3U);
    append_u32le(out, &size, 1U);
    append_u32le(out, &size, 7U);
    append_u16le(out, &size, 0xB020U);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 9U);
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 0U);
    append_bytes(out, &size, "Standard", 8U);
    append_u8(out, &size, 0U);
    return size;
}

static omc_size
make_sony_makernote_ciphered_blob(omc_u8* out, omc_u16 tag,
                                  const omc_u8* plain, omc_size plain_size)
{
    omc_size size;
    omc_size i;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, tag);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, (omc_u32)plain_size);
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 0U);
    for (i = 0U; i < plain_size; ++i) {
        append_u8(out, &size, sony_encipher_byte(plain[i]));
    }
    return size;
}

static omc_size
make_sony_makernote_tag9050b_ciphered(omc_u8* out)
{
    omc_u8 plain[0x90];

    memset(plain, 0, sizeof(plain));
    plain[0x0026U] = 0x01U;
    plain[0x0027U] = 0x00U;
    plain[0x0028U] = 0x02U;
    plain[0x0029U] = 0x00U;
    plain[0x002AU] = 0x03U;
    plain[0x002BU] = 0x00U;
    plain[0x003AU] = 0x11U;
    plain[0x003BU] = 0x22U;
    plain[0x003CU] = 0x33U;
    plain[0x003DU] = 0x44U;
    plain[0x0088U] = 0x01U;
    plain[0x0089U] = 0x02U;
    plain[0x008AU] = 0x03U;
    plain[0x008BU] = 0x04U;
    plain[0x008CU] = 0x05U;
    plain[0x008DU] = 0x06U;
    return make_sony_makernote_ciphered_blob(out, 0x9050U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag2010i_ciphered(omc_u8* out)
{
    omc_u8 plain[0x1800];
    omc_u32 i;

    memset(plain, 0, sizeof(plain));
    plain[0x0217U] = 0x34U;
    plain[0x0218U] = 0x12U;
    plain[0x0252U] = 0x01U;
    plain[0x0253U] = 0x00U;
    plain[0x0254U] = 0x02U;
    plain[0x0255U] = 0x00U;
    plain[0x0256U] = 0x03U;
    plain[0x0257U] = 0x00U;
    plain[0x0320U] = 0x64U;
    plain[0x0321U] = 0x00U;
    for (i = 0U; i < 32U; ++i) {
        plain[0x17D0U + i] = (omc_u8)i;
    }
    return make_sony_makernote_ciphered_blob(out, 0x2010U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag3000_shotinfo(omc_u8* out)
{
    omc_u8 blob[0x44];
    omc_size size;

    memset(blob, 0, sizeof(blob));
    blob[0U] = (omc_u8)'I';
    blob[1U] = (omc_u8)'I';
    write_u16le_at(blob, 0x0002U, 94U);
    memcpy(blob + 0x0006U, "2017:02:08 07:07:08", 19U);
    write_u16le_at(blob, 0x001AU, 5304U);
    write_u16le_at(blob, 0x001CU, 7952U);
    write_u16le_at(blob, 0x0030U, 2U);
    write_u16le_at(blob, 0x0032U, 37U);
    memcpy(blob + 0x0034U, "DC7303320222000", 15U);

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x3000U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, sizeof(blob));
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 0U);
    append_bytes(out, &size, blob, sizeof(blob));
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
find_exif_entry_typed(const omc_store* store, const char* ifd_name,
                      omc_u16 tag, omc_val_kind kind, omc_elem_type elem_type)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag
            || entry->value.kind != kind
            || entry->value.elem_type != elem_type) {
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

static const omc_entry*
find_irb_field(const omc_store* store, omc_u16 resource_id, const char* field)
{
    omc_size i;
    omc_size field_len;

    field_len = strlen(field);
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_PHOTOSHOP_IRB_FIELD
            || entry->key.u.photoshop_irb_field.resource_id != resource_id) {
            continue;
        }
        view = omc_arena_view(&store->arena, entry->key.u.photoshop_irb_field.field);
        if (view.size == field_len && memcmp(view.data, field, field_len) == 0) {
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

static omc_size
count_bmff_field_scalar_value(const omc_store* store, const char* field,
                              omc_u64 value)
{
    omc_size i;
    omc_size count;

    count = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_BMFF_FIELD
            || entry->value.kind != OMC_VAL_SCALAR) {
            continue;
        }
        field_view = omc_arena_view(&store->arena, entry->key.u.bmff_field.field);
        if (field_view.size == strlen(field)
            && memcmp(field_view.data, field, field_view.size) == 0
            && entry->value.u.u64 == value) {
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
test_read_jpeg_irb_fields(void)
{
    omc_u8 file_bytes[128];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[256];
    omc_u32 payload_parts[32];
    omc_read_res res;
    const omc_entry* entry;

    file_size = make_test_jpeg_irb_fields(file_bytes);
    omc_store_init(&store);
    res = omc_read_simple(file_bytes, file_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 32U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.irb.status == OMC_IRB_OK);
    assert(res.irb.resources_decoded == 1U);
    assert(res.irb.entries_decoded == 2U);

    entry = find_irb_entry(&store, 0x040DU);
    assert(entry != (const omc_entry*)0);
    entry = find_irb_field(&store, 0x040DU, "GlobalAngle");
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 30U);

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
test_read_tiff_nikon_binary_makernote(void)
{
    omc_u8 makernote[512];
    omc_u8 tiff[768];
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

    makernote_size = make_nikon_makernote_with_binary_subdirs(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Nikon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_nikon_distortinfo_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 1U);

    entry = find_exif_entry(&store, "mk_nikon_flashinfo0106_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_nikon_flashinfo0106_0", 0x0008U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_nikon_multiexposure_0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_nikon_multiexposure_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_nikon_afinfo2v0100_0", 0x001CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 1U);

    entry = find_exif_entry(&store, "mk_nikon_afinfo2v0100_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_nikon_afinfo2v0100_0", 0x0010U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_nikon_fileinfo_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0U);

    omc_store_fini(&store);
}

static void
test_read_tiff_nikon_info_makernote(void)
{
    omc_u8 makernote[512];
    omc_u8 tiff[768];
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
    omc_const_bytes view;

    makernote_size = make_nikon_makernote_with_info_blocks(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Nikon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_nikon_picturecontrol2_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 7U);
    assert(memcmp(view.data, "NEUTRAL", 7U) == 0);

    entry = find_exif_entry(&store, "mk_nikon_worldtime_0", 0x0000U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == -540);

    omc_store_fini(&store);
}

static void
test_read_tiff_canon_subtables_makernote(void)
{
    omc_u8 makernote[256];
    omc_u8 tiff[512];
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

    makernote_size = make_canon_afinfo2_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_afinfo2_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 9U);

    entry = find_exif_entry(&store, "mk_canon_afinfo2_0", 0x260EU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0U);
    omc_store_fini(&store);

    makernote_size = make_canon_custom_functions2_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canoncustom_functions2_0", 0x0101U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0U);

    omc_store_fini(&store);
}

static void
test_read_tiff_nikon_preview_and_aftune_makernote(void)
{
    omc_u8 makernote[512];
    omc_u8 tiff[768];
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

    makernote_size = make_nikon_makernote_with_preview_settings_and_aftune(
        makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Nikon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_nikonsettings_main_0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 6400U);

    entry = find_exif_entry(&store, "mk_nikon_aftune_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I8);
    assert(entry->value.u.i64 == -3);

    omc_store_fini(&store);
}

static void
test_read_tiff_sony_makernote_and_postpass(void)
{
    omc_u8 makernote[8192];
    omc_u8 tiff[16384];
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
    omc_const_bytes view;

    makernote_size = make_sony_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony0", 0x0102U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 7U);
    entry = find_exif_entry(&store, "mk_sony0", 0xB020U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 8U);
    assert(memcmp(view.data, "Standard", 8U) == 0);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag9050b_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9050b_0", 0x0026U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.count == 3U);
    entry = find_exif_entry(&store, "mk_sony_tag9050b_0", 0x003AU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x44332211U);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag2010i_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag2010i_0", 0x0217U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == 0x1234);
    entry = find_exif_entry(&store, "mk_sony_meterinfo9_0", 0x021CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    assert(entry->value.count == 0x005AU);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 0x005AU);
    assert(view.data[0x36U] == 0x01U);
    assert(view.data[0x37U] == 0x00U);
    assert(view.data[0x38U] == 0x02U);
    assert(view.data[0x39U] == 0x00U);
    assert(view.data[0x3AU] == 0x03U);
    assert(view.data[0x3BU] == 0x00U);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag3000_shotinfo(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_shotinfo_0", 0x0006U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 19U);
    assert(memcmp(view.data, "2017:02:08 07:07:08", 19U) == 0);
    entry = find_exif_entry(&store, "mk_sony_shotinfo_0", 0x001AU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 5304U);
    omc_store_fini(&store);
}

static void
test_read_tiff_canon_filterinfo_and_timeinfo_makernote(void)
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
    omc_const_bytes view;
    omc_u32 vals[2];

    makernote_size = make_canon_filterinfo_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_filterinfo_0", 0x0402U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 2U);

    entry = find_exif_entry(&store, "mk_canon_filterinfo_0", 0x0403U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size >= 8U);
    memcpy(vals, view.data, sizeof(vals));
    assert(vals[0] == 300U);
    assert(vals[1] == 700U);
    omc_store_fini(&store);

    makernote_size = make_canon_timeinfo_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_timeinfo_0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 540U);

    entry = find_exif_entry(&store, "mk_canon_timeinfo_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 1234U);

    omc_store_fini(&store);
}

static void
test_read_tiff_canon_camera_info_and_colordata_makernote(void)
{
    omc_u8 makernote[4096];
    omc_u8 tiff[8192];
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

    makernote_size = make_canon_camera_info_psinfo_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_camerainfo_0", 0x0003U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 42U);

    entry = find_exif_entry(&store, "mk_canon_camerainfo_0", 0x0018U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.count == 4U);

    entry = find_exif_entry(&store, "mk_canon_psinfo_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I32);
    assert(entry->value.u.i64 == 3);

    omc_store_fini(&store);

    makernote_size = make_canon_colordata8_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_colordata8_0", 0x0043U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 6100U);

    entry = find_exif_entry(&store, "mk_canon_colorcalib_0", 0x0000U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 100U);

    entry = find_exif_entry(&store, "mk_canon_colorcalib_0", 0x0001U);
    assert(entry == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_read_tiff_canon_camera_info_model_and_psinfo2_makernote(void)
{
    omc_u8 cam[1024];
    omc_u8 makernote[2048];
    omc_u8 tiff[4096];
    omc_size cam_size;
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
    omc_const_bytes view;

    cam_size = make_canon_camera_info_blob_with_ascii(cam, 0x0107U, "1.2.3",
                                                      6U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS 450D", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_camerainfo450d_0", 0x0107U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "1.2.3", 5U) == 0);
    omc_store_fini(&store);

    cam_size = make_canon_camera_info_blob_with_ifd_ascii(cam, 0x0256U,
                                                          "1.1.2", 6U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_camerainfo6d_0", 0x0256U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "1.1.2", 5U) == 0);
    omc_store_fini(&store);

    makernote_size = make_canon_camera_info_psinfo2_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_psinfo2_0", 0x0090U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I32);
    assert(entry->value.u.i64 == 7);

    entry = find_exif_entry(&store, "mk_canon_psinfo2_0", 0x00F0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 129U);
    omc_store_fini(&store);

    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS 1000D", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;

    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_psinfo_0", 0x0090U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I32);
    assert(entry->value.u.i64 == 7);
    entry = find_exif_entry(&store, "mk_canon_psinfo2_0", 0x0090U);
    assert(entry == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_read_tiff_canon_camera_info_additional_cohorts_makernote(void)
{
    omc_u8 cam[2048];
    omc_u8 makernote[4096];
    omc_u8 tiff[8192];
    omc_size cam_size;
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
    omc_const_bytes view;

    cam_size = make_canon_camera_info_blob_with_ascii(cam, 0x019BU, "1.0.1",
                                                      6U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS 1100D", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(find_exif_entry(&store, "mk_canon_camerainfo_0", 0x019BU)
           == (const omc_entry*)0);
    entry = find_exif_entry(&store, "mk_canon_camerainfo1100d_0", 0x019BU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "1.0.1", 5U) == 0);
    omc_store_fini(&store);

    cam_size = make_canon_camera_info_blob_with_ascii(cam, 0x019BU, "1.0.9",
                                                      6U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS Kiss X70", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(find_exif_entry(&store, "mk_canon_camerainfo650d_0", 0x019BU)
           == (const omc_entry*)0);
    entry = find_exif_entry(&store, "mk_canon_camerainfo600d_0", 0x019BU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "1.0.9", 5U) == 0);
    omc_store_fini(&store);

    cam_size = make_canon_camera_info_blob_with_ascii(cam, 0x01ACU, "1.2.0",
                                                      6U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS 7D", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(find_exif_entry(&store, "mk_canon_camerainfo_0", 0x01ACU)
           == (const omc_entry*)0);
    entry = find_exif_entry(&store, "mk_canon_camerainfo7d_0", 0x01ACU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "1.2.0", 5U) == 0);
    omc_store_fini(&store);

    cam_size = make_canon_camera_info_blob_with_ifd_ascii(cam, 0x01ACU,
                                                          "1.0.7", 6U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    assert(find_exif_entry(&store, "mk_canon_camerainfo_0", 0x01ACU)
           == (const omc_entry*)0);
    entry = find_exif_entry(&store, "mk_canon_camerainfo7d_0", 0x01ACU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "1.0.7", 5U) == 0);
    omc_store_fini(&store);
}

static void
test_read_tiff_canon_colordata_counted_families_makernote(void)
{
    omc_u8 makernote[8192];
    omc_u8 tiff[16384];
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

    makernote_size = make_canon_colordata_counted_makernote(makernote, 1338U,
                                                            7U);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_colorcoefs_0", 0x0022U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 6207U);
    omc_store_fini(&store);

    makernote_size = make_canon_colordata_counted_makernote(makernote, 1312U,
                                                            10U);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_colordata7_0", 0x003FU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 1710U);
    entry = find_exif_entry(&store, "mk_canon_colorcalib_0", 0x0038U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 410U);
    omc_store_fini(&store);

    makernote_size = make_canon_colordata_counted_makernote(makernote, 3778U,
                                                            65U);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_colordata12_0", 0x0073U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 2765U);
    entry = find_exif_entry(&store, "mk_canon_colorcalib_0", 0x0038U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 710U);
    omc_store_fini(&store);
}

static void
test_read_tiff_canon_camera_info_extended_fixed_fields_makernote(void)
{
    omc_u8 cam[2048];
    omc_u8 makernote[2048];
    omc_u8 tiff[4096];
    omc_size cam_size;
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
    omc_const_bytes view;

    cam_size = make_canon_camera_info_blob_with_u16(cam, 0x0048U, 5300U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS-1DS", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_camerainfo1d_0", 0x0048U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 5300U);
    omc_store_fini(&store);

    cam_size = make_canon_camera_info_blob_with_ascii(cam, 0x016BU,
                                                      "1234567890ABCDEF", 16U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS 5DS", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_camerainfounknown_0", 0x016BU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 16U);
    assert(memcmp(view.data, "1234567890ABCDEF", 16U) == 0);
    omc_store_fini(&store);

    cam_size = make_canon_camera_info_blob_with_ascii(cam, 0x05C1U, "1.0.0",
                                                      6U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS R1", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_camerainfounknown_0", 0x05C1U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "1.0.0", 5U) == 0);
    omc_store_fini(&store);

    makernote_size = make_canon_camera_info_u32_table_makernote(
        makernote, 0x0048U, 0x0047U, 19U);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon PowerShot S1 IS", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry_typed(&store, "mk_canon_camerainfounknown32_0",
                                  0x0047U, OMC_VAL_SCALAR, OMC_ELEM_U32);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 19U);
    omc_store_fini(&store);

    memset(cam, 0, sizeof(cam));
    cam_size = 0x0075U + 4U;
    cam[0x0066U] = 7U;
    memcpy(cam + 0x0075U, "0400", 4U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS-1D Mark II", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_camerainfo1dmkii_0", 0x0066U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 7U);
    entry = find_exif_entry(&store, "mk_canon_camerainfo1dmkii_0", 0x0075U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 4U);
    assert(memcmp(view.data, "0400", 4U) == 0);
    omc_store_fini(&store);

    memset(cam, 0, sizeof(cam));
    cam_size = 0x0079U + 4U;
    cam[0x0074U] = 3U;
    memcpy(cam + 0x0079U, "0800", 4U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS-1D Mark II N", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_camerainfo1dmkiin_0", 0x0074U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 3U);
    entry = find_exif_entry(&store, "mk_canon_camerainfo1dmkiin_0", 0x0079U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 4U);
    assert(memcmp(view.data, "0800", 4U) == 0);
    omc_store_fini(&store);

    cam_size = make_canon_camera_info_blob_with_u32(cam, 0x011CU, 0x01020304U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS 5D", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 4U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 8U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_camerainfo5d_0", 0x011CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x01020304U);
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
test_read_crw_textual_ciff(void)
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
    omc_const_bytes view;

    crw_size = make_test_crw_textual_ciff(crw);
    omc_store_init(&store);

    res = omc_read_simple(crw, crw_size, &store, blocks, 8U, ifds, 16U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "ciff_2804_0", 0x0805U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 22U);
    assert(memcmp(view.data, "High definition camera", 22U) == 0);

    entry = find_exif_entry(&store, "ciff_2807_1", 0x0810U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "Alice", 5U) == 0);

    entry = find_exif_entry(&store, "ciff_3004_2", 0x080CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 8U);
    assert(memcmp(view.data, "Ver 2.10", 8U) == 0);

    entry = find_exif_entry(&store, "ciff_300A_3", 0x0816U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 12U);
    assert(memcmp(view.data, "IMG_0001.CRW", 12U) == 0);

    entry = find_exif_entry(&store, "ifd0", 0x010EU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);

    entry = find_exif_entry(&store, "exififd", 0xA430U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);

    omc_store_fini(&store);
}

static void
test_read_crw_native_projection(void)
{
    omc_u8 crw[2048];
    omc_size crw_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[24];
    omc_u8 payload[256];
    omc_u32 payload_parts[8];
    omc_read_res res;
    const omc_entry* entry;
    omc_const_bytes view;

    crw_size = make_test_crw_native_projection(crw);
    omc_store_init(&store);

    res = omc_read_simple(crw, crw_size, &store, blocks, 8U, ifds, 24U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry_typed(&store, "ciff_2807_0_makemodel", 0x0000U,
                                  OMC_VAL_TEXT, OMC_ELEM_U8);
    assert(entry != (const omc_entry*)0);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "Canon", 5U) == 0);

    entry = find_exif_entry(&store, "ciff_2807_0_makemodel", 0x0006U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 15U);
    assert(memcmp(view.data, "PowerShot Pro70", 15U) == 0);

    entry = find_exif_entry_typed(&store, "ciff_300A_2_imageformat",
                                  0x0001U, OMC_VAL_SCALAR,
                                  OMC_ELEM_F32_BITS);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.f32_bits == f32_bits(10.0f));

    entry = find_exif_entry_typed(&store, "ciff_300A_2_timestamp", 0x0001U,
                                  OMC_VAL_SCALAR, OMC_ELEM_I32);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.i64 == -3);

    entry = find_exif_entry_typed(&store, "ciff_300A_2_imageinfo", 0x0003U,
                                  OMC_VAL_SCALAR, OMC_ELEM_I32);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.i64 == 90);

    entry = find_exif_entry_typed(&store, "ciff_3002_1_exposureinfo",
                                  0x0002U, OMC_VAL_SCALAR,
                                  OMC_ELEM_F32_BITS);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.f32_bits == f32_bits(3.0f));

    entry = find_exif_entry_typed(&store, "ciff_3002_1_flashinfo", 0x0001U,
                                  OMC_VAL_SCALAR, OMC_ELEM_F32_BITS);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.f32_bits == f32_bits(0.0f));

    entry = find_exif_entry_typed(&store, "ciff_300B_3_focallength",
                                  0x0003U, OMC_VAL_SCALAR, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 206U);

    omc_store_fini(&store);
}

static void
test_read_crw_semantic_native_scalars(void)
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

    crw_size = make_test_crw_semantic_native_scalars(crw);
    omc_store_init(&store);

    res = omc_read_simple(crw, crw_size, &store, blocks, 8U, ifds, 16U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);

    entry = find_exif_entry_typed(&store, "ciff_3002_0", 0x1010U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 2U);

    entry = find_exif_entry_typed(&store, "ciff_3002_0", 0x1807U,
                                  OMC_VAL_SCALAR, OMC_ELEM_F32_BITS);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.f32_bits == f32_bits(12.5f));

    entry = find_exif_entry_typed(&store, "ciff_3003_1", 0x1814U,
                                  OMC_VAL_SCALAR, OMC_ELEM_F32_BITS);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.f32_bits == f32_bits(9.5f));

    entry = find_exif_entry_typed(&store, "ciff_3004_2", 0x1834U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U32);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 0x80000169U);

    entry = find_exif_entry_typed(&store, "ciff_300A_3", 0x1817U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U32);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 162U);

    omc_store_fini(&store);
}

static void
test_read_crw_native_tables(void)
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

    crw_size = make_test_crw_decoder_table(crw);
    omc_store_init(&store);
    res = omc_read_simple(crw, crw_size, &store, blocks, 8U, ifds, 16U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry_typed(&store, "ciff_3004_0", 0x1835U,
                                  OMC_VAL_ARRAY, OMC_ELEM_U32);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.count == 4U);
    entry = find_exif_entry_typed(&store, "ciff_3004_0_decodertable",
                                  0x0003U, OMC_VAL_SCALAR, OMC_ELEM_U32);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 8192U);
    omc_store_fini(&store);

    crw_size = make_test_crw_rawjpginfo_and_whitesample(crw);
    omc_store_init(&store);
    res = omc_read_simple(crw, crw_size, &store, blocks, 8U, ifds, 16U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry_typed(&store, "ciff_300B_0", 0x10B5U,
                                  OMC_VAL_ARRAY, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.count == 5U);
    entry = find_exif_entry_typed(&store, "ciff_300B_0_rawjpginfo", 0x0004U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 1536U);
    entry = find_exif_entry_typed(&store, "ciff_300B_0_whitesample", 0x0005U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 10U);
    omc_store_fini(&store);

    crw_size = make_test_crw_shotinfo(crw);
    omc_store_init(&store);
    res = omc_read_simple(crw, crw_size, &store, blocks, 8U, ifds, 16U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry_typed(&store, "ciff_300B_0_shotinfo", 0x0005U,
                                  OMC_VAL_SCALAR, OMC_ELEM_I16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.i64 == -64);
    entry = find_exif_entry_typed(&store, "ciff_300B_0_shotinfo", 0x000AU,
                                  OMC_VAL_SCALAR, OMC_ELEM_I16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.i64 == 6);
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
    const omc_entry* iref_item_count;
    const omc_entry* iref_from_unique_count;
    const omc_entry* iref_to_unique_count;
    const omc_entry* auxl_edge_count;
    const omc_entry* dimg_edge_count;
    const omc_entry* thmb_edge_count;
    const omc_entry* auxl_item_count;
    const omc_entry* auxl_graph_edge_count;
    const omc_entry* auxl_graph_from_unique_count;
    const omc_entry* auxl_graph_to_unique_count;
    const omc_entry* dimg_graph_edge_count;
    const omc_entry* thmb_graph_edge_count;
    const omc_entry* primary_auxl_item_id;
    const omc_entry* primary_auxl_count;
    const omc_entry* primary_dimg_item_id;
    const omc_entry* primary_thmb_item_id;
    const omc_entry* primary_alpha_item_id;
    const omc_entry* primary_alpha_count;
    const omc_entry* primary_depth_item_id;
    const omc_entry* primary_depth_count;
    const omc_entry* aux_item_count;
    const omc_entry* aux_alpha_count;
    const omc_entry* aux_depth_count;
    const omc_entry* compat_brands;
    omc_u32 compat_brand_value;
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
    compat_brands = find_bmff_field(&store, "ftyp.compat_brands");
    assert(compat_brands != (const omc_entry*)0);
    assert(compat_brands->value.kind == OMC_VAL_ARRAY);
    assert(compat_brands->value.elem_type == OMC_ELEM_U32);
    assert(compat_brands->value.count == 1U);
    value_view = omc_arena_view(&store.arena, compat_brands->value.u.ref);
    assert(value_view.size == sizeof(omc_u32));
    memcpy(&compat_brand_value, value_view.data, sizeof(compat_brand_value));
    assert(compat_brand_value == fourcc('m', 'i', 'f', '1'));

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
    assert(count_bmff_field(&store, "iref.ref_type_name") == 4U);
    assert(find_bmff_field_text(&store, "iref.ref_type_name", "auxl")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.ref_type_name", "dimg")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.ref_type_name", "thmb")
           != (const omc_entry*)0);
    assert(count_bmff_field(&store, "iref.from_item_id") == 4U);
    assert(count_bmff_field(&store, "iref.to_item_id") == 4U);
    iref_item_count = find_bmff_field(&store, "iref.item_count");
    assert(iref_item_count != (const omc_entry*)0);
    assert(iref_item_count->value.u.u64 == 3U);
    iref_from_unique_count = find_bmff_field(&store,
                                             "iref.from_item_unique_count");
    assert(iref_from_unique_count != (const omc_entry*)0);
    assert(iref_from_unique_count->value.u.u64 == 1U);
    iref_to_unique_count = find_bmff_field(&store,
                                           "iref.to_item_unique_count");
    assert(iref_to_unique_count != (const omc_entry*)0);
    assert(iref_to_unique_count->value.u.u64 == 2U);
    assert(count_bmff_field(&store, "iref.item_id") == 3U);
    assert(count_bmff_field(&store, "iref.item_out_edge_count") == 3U);
    assert(count_bmff_field(&store, "iref.item_in_edge_count") == 3U);

    auxl_edge_count = find_bmff_field(&store, "iref.auxl.edge_count");
    assert(auxl_edge_count != (const omc_entry*)0);
    assert(auxl_edge_count->value.u.u64 == 2U);
    assert(count_bmff_field(&store, "iref.auxl.from_item_id") == 2U);
    assert(count_bmff_field(&store, "iref.auxl.to_item_id") == 2U);
    auxl_item_count = find_bmff_field(&store, "iref.auxl.item_count");
    assert(auxl_item_count != (const omc_entry*)0);
    assert(auxl_item_count->value.u.u64 == 3U);
    auxl_graph_edge_count = find_bmff_field(&store, "iref.graph.auxl.edge_count");
    assert(auxl_graph_edge_count != (const omc_entry*)0);
    assert(auxl_graph_edge_count->value.u.u64 == 2U);
    auxl_graph_from_unique_count = find_bmff_field(
        &store, "iref.graph.auxl.from_item_unique_count");
    assert(auxl_graph_from_unique_count != (const omc_entry*)0);
    assert(auxl_graph_from_unique_count->value.u.u64 == 1U);
    auxl_graph_to_unique_count = find_bmff_field(
        &store, "iref.graph.auxl.to_item_unique_count");
    assert(auxl_graph_to_unique_count != (const omc_entry*)0);
    assert(auxl_graph_to_unique_count->value.u.u64 == 2U);

    dimg_edge_count = find_bmff_field(&store, "iref.dimg.edge_count");
    assert(dimg_edge_count != (const omc_entry*)0);
    assert(dimg_edge_count->value.u.u64 == 1U);
    assert(count_bmff_field(&store, "iref.dimg.from_item_id") == 1U);
    assert(count_bmff_field(&store, "iref.dimg.to_item_id") == 1U);
    dimg_graph_edge_count = find_bmff_field(&store, "iref.graph.dimg.edge_count");
    assert(dimg_graph_edge_count != (const omc_entry*)0);
    assert(dimg_graph_edge_count->value.u.u64 == 1U);

    thmb_edge_count = find_bmff_field(&store, "iref.thmb.edge_count");
    assert(thmb_edge_count != (const omc_entry*)0);
    assert(thmb_edge_count->value.u.u64 == 1U);
    assert(count_bmff_field(&store, "iref.thmb.from_item_id") == 1U);
    assert(count_bmff_field(&store, "iref.thmb.to_item_id") == 1U);
    thmb_graph_edge_count = find_bmff_field(&store, "iref.graph.thmb.edge_count");
    assert(thmb_graph_edge_count != (const omc_entry*)0);
    assert(thmb_graph_edge_count->value.u.u64 == 1U);

    primary_auxl_item_id = find_bmff_field(&store, "primary.auxl_item_id");
    assert(primary_auxl_item_id != (const omc_entry*)0);
    assert(primary_auxl_item_id->value.u.u64 == 1U);
    assert(count_bmff_field(&store, "primary.auxl_item_id") == 2U);
    primary_auxl_count = find_bmff_field(&store, "primary.auxl_count");
    assert(primary_auxl_count != (const omc_entry*)0);
    assert(primary_auxl_count->value.u.u64 == 2U);
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
    primary_alpha_count = find_bmff_field(&store, "primary.alpha_count");
    assert(primary_alpha_count != (const omc_entry*)0);
    assert(primary_alpha_count->value.u.u64 == 1U);

    primary_depth_item_id = find_bmff_field(&store, "primary.depth_item_id");
    assert(primary_depth_item_id != (const omc_entry*)0);
    assert(primary_depth_item_id->value.u.u64 == 3U);
    primary_depth_count = find_bmff_field(&store, "primary.depth_count");
    assert(primary_depth_count != (const omc_entry*)0);
    assert(primary_depth_count->value.u.u64 == 1U);
    assert(count_bmff_field_scalar_value(&store,
                                         "primary.linked_item_role_count",
                                         4U) == 1U);
    assert(count_bmff_field(&store, "primary.linked_item_id") == 4U);
    assert(count_bmff_field_scalar_value(&store, "primary.linked_item_type",
                                         fourcc('E', 'x', 'i', 'f')) == 2U);
    assert(count_bmff_field_scalar_value(&store, "primary.linked_item_type",
                                         fourcc('m', 'i', 'm', 'e')) == 2U);
    assert(find_bmff_field_text(&store, "primary.linked_item_name", "Exif")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "primary.linked_item_name", "C2PA")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "primary.linked_item_role", "alpha")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "primary.linked_item_role", "depth")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "primary.linked_item_role",
                                "derived")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "primary.linked_item_role",
                                "thumbnail")
           != (const omc_entry*)0);

    assert(count_bmff_field(&store, "aux.item_id") == 2U);
    aux_item_count = find_bmff_field(&store, "aux.item_count");
    assert(aux_item_count != (const omc_entry*)0);
    assert(aux_item_count->value.u.u64 == 2U);
    aux_alpha_count = find_bmff_field(&store, "aux.alpha_count");
    assert(aux_alpha_count != (const omc_entry*)0);
    assert(aux_alpha_count->value.u.u64 == 1U);
    aux_depth_count = find_bmff_field(&store, "aux.depth_count");
    assert(aux_depth_count != (const omc_entry*)0);
    assert(aux_depth_count->value.u.u64 == 1U);
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
    assert(find_bmff_field_text(&store, "aux.subtype_hex", "0x11")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_hex", "0xAABB")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_kind", "u8")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_kind", "u16be")
           != (const omc_entry*)0);
    assert(count_bmff_field_scalar_value(&store, "aux.subtype_len", 1U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "aux.subtype_len", 2U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "aux.subtype_u32", 17U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "aux.subtype_u32",
                                         0xAABBU) == 1U);
    assert(find_bmff_field_text(&store, "iref.auxl.semantic", "alpha")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.semantic", "depth")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.type",
                                "urn:mpeg:hevc:2015:auxid:1")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.type",
                                "urn:mpeg:hevc:2015:auxid:2")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_hex", "0x11")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_hex", "0xAABB")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_kind", "u8")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_kind", "u16be")
           != (const omc_entry*)0);
    assert(count_bmff_field_scalar_value(&store, "iref.auxl.subtype_u32",
                                         17U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.auxl.subtype_u32",
                                         0xAABBU) == 1U);

    omc_store_fini(&store);
}

static void
test_read_bmff_aux_subtype_kinds(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;

    file_size = make_test_bmff_aux_subtype_kinds_only(
        file_bytes, fourcc('h', 'e', 'i', 'c'));
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.bmff.status == OMC_BMFF_OK);
    assert(count_bmff_field(&store, "aux.item_id") == 3U);
    assert(count_bmff_field_scalar_value(&store, "aux.item_count", 3U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "aux.depth_count", 1U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "aux.alpha_count", 1U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "aux.matte_count", 1U) == 1U);
    assert(find_bmff_field_text(&store, "aux.subtype_kind", "ascii_z")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_kind", "u64be")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_kind", "uuid")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_text", "profile")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_text",
                                "00010203-0405-0607-0809-0A0B0C0D0E0F")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "aux.subtype_uuid",
                                "00010203-0405-0607-0809-0A0B0C0D0E0F")
           != (const omc_entry*)0);
    assert(count_bmff_field_scalar_value(
               &store, "aux.subtype_u64",
               (((omc_u64)0x11223344U) << 32) | (omc_u64)0x55667788U)
           == 1U);
    assert(find_bmff_field_text(&store, "iref.auxl.semantic", "depth")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.semantic", "alpha")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.semantic", "matte")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_kind", "ascii_z")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_kind", "u64be")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_kind", "uuid")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_text", "profile")
           != (const omc_entry*)0);
    assert(find_bmff_field_text(&store, "iref.auxl.subtype_uuid",
                                "00010203-0405-0607-0809-0A0B0C0D0E0F")
           != (const omc_entry*)0);
    assert(count_bmff_field_scalar_value(
               &store, "iref.auxl.subtype_u64",
               (((omc_u64)0x11223344U) << 32) | (omc_u64)0x55667788U)
           == 1U);

    omc_store_fini(&store);
}

static void
test_read_bmff_pred_edges(void)
{
    omc_u8 file_bytes[512];
    omc_size file_size;
    omc_store store;
    omc_blk_ref blocks[4];
    omc_exif_ifd_ref ifds[4];
    omc_u8 payload[64];
    omc_u32 payload_parts[8];
    omc_read_res res;

    file_size = make_test_bmff_pred_only(file_bytes,
                                         fourcc('h', 'e', 'i', 'c'));
    omc_store_init(&store);

    res = omc_read_simple(file_bytes, file_size, &store, blocks, 4U, ifds, 4U,
                          payload, sizeof(payload), payload_parts, 8U,
                          (const omc_read_opts*)0);

    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.bmff.status == OMC_BMFF_OK);
    assert(count_bmff_field(&store, "iref.ref_type_name") == 2U);
    assert(find_bmff_field_text(&store, "iref.ref_type_name", "pred")
           != (const omc_entry*)0);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.edge_count", 2U)
           == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.graph.pred.edge_count",
                                         2U)
           == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.item_count", 3U)
           == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.item_id", 9U) == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.item_id", 10U)
           == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.item_id", 11U)
           == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.from_item_id", 9U)
           == 2U);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.to_item_id", 10U)
           == 1U);
    assert(count_bmff_field_scalar_value(&store, "iref.pred.to_item_id", 11U)
           == 1U);

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

static omc_size
make_apple_makernote_extended(omc_u8* out)
{
    omc_size size;
    omc_u32 array_off;
    omc_u32 text_off;

    size = 0U;
    append_text(out, &size, "Apple iOS");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 1U);
    append_text(out, &size, "MM");

    array_off = 14U + 2U + (5U * 12U) + 4U;
    text_off = array_off + 6U;

    append_u16be(out, &size, 5U);

    append_u16be(out, &size, 0x0001U);
    append_u16be(out, &size, 4U);
    append_u32be(out, &size, 1U);
    append_u32be(out, &size, 17U);

    append_u16be(out, &size, 0x0004U);
    append_u16be(out, &size, 3U);
    append_u32be(out, &size, 1U);
    append_u16be(out, &size, 2U);
    append_u16be(out, &size, 0U);

    append_u16be(out, &size, 0x0007U);
    append_u16be(out, &size, 3U);
    append_u32be(out, &size, 3U);
    append_u32be(out, &size, array_off);

    append_u16be(out, &size, 0x0008U);
    append_u16be(out, &size, 2U);
    append_u32be(out, &size, 6U);
    append_u32be(out, &size, text_off);

    append_u16be(out, &size, 0x0045U);
    append_u16be(out, &size, 3U);
    append_u32be(out, &size, 1U);
    append_u16be(out, &size, 1U);
    append_u16be(out, &size, 0U);

    append_u32be(out, &size, 0U);
    append_u16be(out, &size, 1U);
    append_u16be(out, &size, 2U);
    append_u16be(out, &size, 3U);
    append_text(out, &size, "HELLO");
    append_u8(out, &size, 0U);
    return size;
}

static omc_size
make_nintendo_makernote(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x1101U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, 0x34U);
    append_u32le(out, &size, 18U);
    append_u32le(out, &size, 0U);

    assert(size == 18U);
    memset(out + size, 0, 0x34U);
    memcpy(out + size + 0U, "3DS1", 4U);
    write_u32le_at(out, 18U + 0x08U, 0x12345678U);
    out[18U + 0x18U] = 0xAAU;
    out[18U + 0x19U] = 0xBBU;
    out[18U + 0x1AU] = 0xCCU;
    out[18U + 0x1BU] = 0xDDU;
    write_u32le_at(out, 18U + 0x28U, 0x3FC00000U);
    write_u16le_at(out, 18U + 0x30U, 5U);
    return size + 0x34U;
}

static omc_size
make_hp_type6_makernote(omc_u8* out)
{
    memset(out, 0, 0x80U);
    out[0U] = (omc_u8)'I';
    out[1U] = (omc_u8)'I';
    out[2U] = (omc_u8)'I';
    out[3U] = (omc_u8)'I';
    out[4U] = 0x06U;
    out[5U] = 0U;

    write_u16le_at(out, 0x000CU, 28U);
    write_u32le_at(out, 0x0010U, 50000U);
    memcpy(out + 0x0014U, "2025:03:16 12:34:56", 19U);
    write_u16le_at(out, 0x0034U, 200U);
    memcpy(out + 0x0058U, "SERIAL NUMBER:HP-12345", 22U);
    return 0x80U;
}

static void
test_read_tiff_small_vendor_makernotes(void)
{
    omc_u8 tiff[2048];
    omc_u8 makernote[512];
    omc_size tiff_size;
    omc_size makernote_size;
    omc_store store;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[1024];
    omc_u32 payload_parts[32];
    omc_read_opts opts;
    omc_read_res res;
    const omc_entry* entry;
    omc_const_bytes view;

    makernote_size = make_apple_makernote_extended(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Apple", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 32U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_apple0", 0x0008U);
    assert(entry != (const omc_entry*)0);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(memcmp(view.data, "HELLO", 5U) == 0);
    entry = find_exif_entry_typed(&store, "mk_apple0", 0x0045U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 1U);
    omc_store_fini(&store);

    makernote_size = make_nintendo_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Nintendo", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 32U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry_typed(&store, "mk_nintendo_camerainfo_0", 0x0030U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 5U);
    omc_store_fini(&store);

    makernote_size = make_hp_type6_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "HP", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_read_opts_init(&opts);
    opts.exif.decode_makernote = 1;
    res = omc_read_simple(tiff, tiff_size, &store, blocks, 8U, ifds, 8U,
                          payload, sizeof(payload), payload_parts, 32U, &opts);
    assert(res.scan.status == OMC_SCAN_OK);
    assert(res.exif.status == OMC_EXIF_OK);
    entry = find_exif_entry_typed(&store, "mk_hp_type6_0", 0x0034U,
                                  OMC_VAL_SCALAR, OMC_ELEM_U16);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 200U);
    omc_store_fini(&store);
}

int
main(void)
{
    test_read_jpeg_all();
    test_read_jpeg_comment();
    test_read_jpeg_irb_fields();
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
    test_read_tiff_nikon_binary_makernote();
    test_read_tiff_nikon_info_makernote();
    test_read_tiff_canon_subtables_makernote();
    test_read_tiff_nikon_preview_and_aftune_makernote();
    test_read_tiff_sony_makernote_and_postpass();
    test_read_tiff_canon_filterinfo_and_timeinfo_makernote();
    test_read_tiff_canon_camera_info_and_colordata_makernote();
    test_read_tiff_canon_colordata_counted_families_makernote();
    test_read_tiff_canon_camera_info_model_and_psinfo2_makernote();
    test_read_tiff_canon_camera_info_additional_cohorts_makernote();
    test_read_tiff_canon_camera_info_extended_fixed_fields_makernote();
    test_read_tiff_small_vendor_makernotes();
    test_read_crw_minimal_ciff();
    test_read_crw_derived_exif();
    test_read_crw_textual_ciff();
    test_read_crw_native_projection();
    test_read_crw_semantic_native_scalars();
    test_read_crw_native_tables();
    test_read_bmff_fields();
    test_read_bmff_aux_subtype_kinds();
    test_read_bmff_pred_edges();
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
