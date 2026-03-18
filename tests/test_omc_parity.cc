#include <openmeta/meta_key.h>
#include <openmeta/meta_store.h>
#include <openmeta/meta_value.h>
#include <openmeta/simple_meta.h>

extern "C" {
#include "omc/omc_arena.h"
#include "omc/omc_key.h"
#include "omc/omc_read.h"
#include "omc/omc_store.h"
#include "omc/omc_val.h"
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using ByteVec = std::vector<unsigned char>;

static void
append_u8(unsigned char* out, std::size_t* io_size, unsigned char value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_bytes(unsigned char* out, std::size_t* io_size, const void* src,
             std::size_t size)
{
    std::memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
append_text(unsigned char* out, std::size_t* io_size, const char* text)
{
    append_bytes(out, io_size, text, std::strlen(text));
}

static void
append_u16be(unsigned char* out, std::size_t* io_size, std::uint16_t value)
{
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
}

static void
append_u16le(unsigned char* out, std::size_t* io_size, std::uint16_t value)
{
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
}

static void
append_u32le(unsigned char* out, std::size_t* io_size, std::uint32_t value)
{
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 24) & 0xFFU));
}

static void
append_u32be(unsigned char* out, std::size_t* io_size, std::uint32_t value)
{
    append_u8(out, io_size, (unsigned char)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
}

static void
write_u16be_at(unsigned char* out, std::uint32_t off, std::uint16_t value)
{
    out[off + 0U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 1U] = (unsigned char)(value & 0xFFU);
}

static void
write_u16le_at(unsigned char* out, std::uint32_t off, std::uint16_t value)
{
    out[off + 0U] = (unsigned char)(value & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 8) & 0xFFU);
}

static void
write_u32le_at(unsigned char* out, std::uint32_t off, std::uint32_t value)
{
    out[off + 0U] = (unsigned char)(value & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 2U] = (unsigned char)((value >> 16) & 0xFFU);
    out[off + 3U] = (unsigned char)((value >> 24) & 0xFFU);
}

static void
write_u32be_at(unsigned char* out, std::uint32_t off, std::uint32_t value)
{
    out[off + 0U] = (unsigned char)((value >> 24) & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 16) & 0xFFU);
    out[off + 2U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 3U] = (unsigned char)(value & 0xFFU);
}

static void
write_u64be_at(unsigned char* out, std::uint32_t off, std::uint64_t value)
{
    out[off + 0U] = (unsigned char)((value >> 56) & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 48) & 0xFFU);
    out[off + 2U] = (unsigned char)((value >> 40) & 0xFFU);
    out[off + 3U] = (unsigned char)((value >> 32) & 0xFFU);
    out[off + 4U] = (unsigned char)((value >> 24) & 0xFFU);
    out[off + 5U] = (unsigned char)((value >> 16) & 0xFFU);
    out[off + 6U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 7U] = (unsigned char)(value & 0xFFU);
}

static void
append_jpeg_segment(unsigned char* out, std::size_t* io_size,
                    std::uint16_t marker, const unsigned char* payload,
                    std::size_t payload_size)
{
    append_u8(out, io_size, 0xFFU);
    append_u8(out, io_size, (unsigned char)(marker & 0xFFU));
    append_u16be(out, io_size, (std::uint16_t)(payload_size + 2U));
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static void
append_png_chunk(unsigned char* out, std::size_t* io_size, const char* type,
                 const unsigned char* payload, std::size_t payload_size)
{
    append_u32be(out, io_size, (std::uint32_t)payload_size);
    append_bytes(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
    append_u32be(out, io_size, 0U);
}

static void
append_fullbox_header(unsigned char* out, std::size_t* io_size,
                      unsigned char version)
{
    append_u8(out, io_size, version);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
}

static void
append_bmff_box(unsigned char* out, std::size_t* io_size, std::uint32_t type,
                const unsigned char* payload, std::size_t payload_size)
{
    append_u32be(out, io_size, (std::uint32_t)(8U + payload_size));
    append_u32be(out, io_size, type);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static std::uint32_t
make_fourcc(char a, char b, char c, char d)
{
    return (((std::uint32_t)(unsigned char)a) << 24)
           | (((std::uint32_t)(unsigned char)b) << 16)
           | (((std::uint32_t)(unsigned char)c) << 8)
           | (((std::uint32_t)(unsigned char)d) << 0);
}

static ByteVec
build_jpeg_comment_fixture()
{
    std::array<unsigned char, 128> file {};
    static const char comment[] = "OpenMeta JPEG comment";
    std::size_t size = 0U;

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD8U);
    append_jpeg_segment(file.data(), &size, 0xFFFEU,
                        (const unsigned char*)comment, sizeof(comment) - 1U);
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD9U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static std::size_t
build_tiff_le_blob(unsigned char* out)
{
    std::size_t size = 0U;

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
build_test_icc_blob(unsigned char* out, std::size_t size)
{
    std::uint32_t i;

    std::memset(out, 0, size);
    write_u32be_at(out, 0U, (std::uint32_t)size);
    write_u32be_at(out, 4U, make_fourcc('a', 'p', 'p', 'l'));
    write_u32be_at(out, 8U, 0x04300000U);
    write_u32be_at(out, 12U, make_fourcc('m', 'n', 't', 'r'));
    write_u32be_at(out, 16U, make_fourcc('R', 'G', 'B', ' '));
    write_u32be_at(out, 20U, make_fourcc('X', 'Y', 'Z', ' '));
    write_u16be_at(out, 24U, 2026U);
    write_u16be_at(out, 26U, 1U);
    write_u16be_at(out, 28U, 28U);
    out[36] = (unsigned char)'a';
    out[37] = (unsigned char)'c';
    out[38] = (unsigned char)'s';
    out[39] = (unsigned char)'p';
    write_u32be_at(out, 40U, make_fourcc('M', 'S', 'F', 'T'));
    write_u32be_at(out, 44U, 1U);
    write_u32be_at(out, 48U, make_fourcc('A', 'P', 'P', 'L'));
    write_u32be_at(out, 52U, make_fourcc('M', '1', '2', '3'));
    write_u64be_at(out, 56U, 1U);
    write_u32be_at(out, 64U, 1U);
    write_u32be_at(out, 68U, 63189U);
    write_u32be_at(out, 72U, 65536U);
    write_u32be_at(out, 76U, 54061U);
    write_u32be_at(out, 80U, make_fourcc('o', 'p', 'n', 'm'));
    write_u32be_at(out, 128U, 1U);
    write_u32be_at(out, 132U, make_fourcc('d', 'e', 's', 'c'));
    write_u32be_at(out, 136U, 144U);
    write_u32be_at(out, 140U, 16U);
    for (i = 0U; i < 16U; ++i) {
        out[144U + i] = (unsigned char)i;
    }
}

static void
append_irb_resource_blob(unsigned char* out, std::size_t* io_size,
                         std::uint16_t resource_id,
                         const unsigned char* payload,
                         std::size_t payload_size)
{
    append_text(out, io_size, "8BIM");
    append_u16be(out, io_size, resource_id);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, (unsigned char)((payload_size >> 24) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((payload_size >> 16) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((payload_size >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)(payload_size & 0xFFU));
    append_bytes(out, io_size, payload, payload_size);
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static ByteVec
build_jpeg_all_fixture()
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    static const unsigned char iptc[] = {
        0x1CU, 0x02U, 0x19U, 0x00U, 0x04U,
        (unsigned char)'t', (unsigned char)'e', (unsigned char)'s',
        (unsigned char)'t'
    };
    static const unsigned char other_irb[] = { 0x01U, 0x02U, 0x03U };
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 64> tiff {};
    std::array<unsigned char, 160> icc {};
    std::array<unsigned char, 64> irb {};
    std::size_t tiff_size;
    std::size_t irb_size;
    std::size_t size;
    std::uint16_t seg_len;

    tiff_size = build_tiff_le_blob(tiff.data());
    build_test_icc_blob(icc.data(), icc.size());
    irb_size = 0U;
    append_irb_resource_blob(irb.data(), &irb_size, 0x0404U, iptc,
                             sizeof(iptc));
    append_irb_resource_blob(irb.data(), &irb_size, 0x1234U, other_irb,
                             sizeof(other_irb));

    size = 0U;
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD8U);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xE1U);
    seg_len = (std::uint16_t)(2U + 6U + tiff_size);
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "Exif");
    append_u8(file.data(), &size, 0U);
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, tiff.data(), tiff_size);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xE1U);
    seg_len = (std::uint16_t)(2U + 29U + (sizeof(xmp) - 1U));
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "http://ns.adobe.com/xap/1.0/");
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, xmp, sizeof(xmp) - 1U);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xE2U);
    seg_len = (std::uint16_t)(2U + 14U + icc.size());
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "ICC_PROFILE");
    append_u8(file.data(), &size, 0U);
    append_u8(file.data(), &size, 1U);
    append_u8(file.data(), &size, 1U);
    append_bytes(file.data(), &size, icc.data(), icc.size());

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xEDU);
    seg_len = (std::uint16_t)(2U + 14U + irb_size);
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "Photoshop 3.0");
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, irb.data(), irb_size);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD9U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_png_text_fixture()
{
    static const unsigned char png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 96> text_payload {};
    std::array<unsigned char, 160> itxt_payload {};
    std::size_t text_size;
    std::size_t itxt_size;
    std::size_t size;

    text_size = 0U;
    append_text(text_payload.data(), &text_size, "Author");
    append_u8(text_payload.data(), &text_size, 0U);
    append_text(text_payload.data(), &text_size, "Alice");

    itxt_size = 0U;
    append_text(itxt_payload.data(), &itxt_size, "Description");
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_text(itxt_payload.data(), &itxt_size, "en");
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_text(itxt_payload.data(), &itxt_size, "Beschreibung");
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_text(itxt_payload.data(), &itxt_size, "OpenMeta PNG");

    size = 0U;
    append_bytes(file.data(), &size, png_sig, sizeof(png_sig));
    append_png_chunk(file.data(), &size, "tEXt", text_payload.data(),
                     text_size);
    append_png_chunk(file.data(), &size, "iTXt", itxt_payload.data(),
                     itxt_size);
    append_png_chunk(file.data(), &size, "IEND", (const unsigned char*)0, 0U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_tiff_fuji_makernote_fixture()
{
    std::array<unsigned char, 128> file {};
    std::array<unsigned char, 64> makernote {};
    std::size_t maker_size;
    std::size_t size;

    maker_size = 0U;
    append_bytes(makernote.data(), &maker_size, "FUJIFILM", 8U);
    append_u32le(makernote.data(), &maker_size, 12U);
    append_u16le(makernote.data(), &maker_size, 1U);
    append_u16le(makernote.data(), &maker_size, 0x0001U);
    append_u16le(makernote.data(), &maker_size, 3U);
    append_u32le(makernote.data(), &maker_size, 1U);
    append_u32le(makernote.data(), &maker_size, 0x00000042U);
    append_u32le(makernote.data(), &maker_size, 0U);

    size = 0U;
    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0x927CU);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, (std::uint32_t)maker_size);
    append_u32le(file.data(), &size, 26U);
    append_u32le(file.data(), &size, 0U);
    append_bytes(file.data(), &size, makernote.data(), maker_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_tiff_with_make_makernote_fixture(const char* make,
                                       const unsigned char* makernote,
                                       std::size_t makernote_size)
{
    ByteVec file(64U + std::strlen(make) + 1U + makernote_size);
    std::size_t size;
    std::size_t make_size;
    std::uint32_t make_off;
    std::uint32_t maker_off;

    make_size = std::strlen(make) + 1U;
    make_off = 38U;
    maker_off = make_off + (std::uint32_t)make_size;

    size = 0U;
    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 2U);

    append_u16le(file.data(), &size, 0x010FU);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)make_size);
    append_u32le(file.data(), &size, make_off);

    append_u16le(file.data(), &size, 0x927CU);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, (std::uint32_t)makernote_size);
    append_u32le(file.data(), &size, maker_off);

    append_u32le(file.data(), &size, 0U);
    append_text(file.data(), &size, make);
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, makernote, makernote_size);

    file.resize(size);
    return file;
}

static ByteVec
build_tiff_with_make_model_makernote_fixture(const char* make,
                                             const char* model,
                                             const unsigned char* makernote,
                                             std::size_t makernote_size)
{
    ByteVec file(80U + std::strlen(make) + std::strlen(model) + 2U
                 + makernote_size);
    std::size_t size;
    std::size_t make_size;
    std::size_t model_size;
    std::uint32_t make_off;
    std::uint32_t model_off;
    std::uint32_t maker_off;

    make_size = std::strlen(make) + 1U;
    model_size = std::strlen(model) + 1U;
    make_off = 50U;
    model_off = make_off + (std::uint32_t)make_size;
    maker_off = model_off + (std::uint32_t)model_size;

    size = 0U;
    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 3U);

    append_u16le(file.data(), &size, 0x010FU);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)make_size);
    append_u32le(file.data(), &size, make_off);

    append_u16le(file.data(), &size, 0x0110U);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)model_size);
    append_u32le(file.data(), &size, model_off);

    append_u16le(file.data(), &size, 0x927CU);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, (std::uint32_t)makernote_size);
    append_u32le(file.data(), &size, maker_off);

    append_u32le(file.data(), &size, 0U);
    append_text(file.data(), &size, make);
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, model);
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, makernote, makernote_size);

    file.resize(size);
    return file;
}

static unsigned char
sony_encipher_byte(unsigned char value)
{
    std::uint32_t x;
    std::uint32_t x2;
    std::uint32_t x3;

    if (value >= 249U) {
        return value;
    }
    x = value;
    x2 = (x * x) % 249U;
    x3 = (x2 * x) % 249U;
    return (unsigned char)x3;
}

static bool
read_u16le_at_raw(const unsigned char* raw, std::size_t raw_size,
                  std::uint32_t off, std::uint16_t* out_value)
{
    if (raw == nullptr || out_value == nullptr || off > raw_size
        || (raw_size - off) < 2U) {
        return false;
    }
    *out_value = (std::uint16_t)(((std::uint16_t)raw[off + 0U])
                                 | (((std::uint16_t)raw[off + 1U]) << 8));
    return true;
}

static bool
read_u32le_at_raw(const unsigned char* raw, std::size_t raw_size,
                  std::uint32_t off, std::uint32_t* out_value)
{
    if (raw == nullptr || out_value == nullptr || off > raw_size
        || (raw_size - off) < 4U) {
        return false;
    }
    *out_value = ((std::uint32_t)raw[off + 0U])
                 | (((std::uint32_t)raw[off + 1U]) << 8)
                 | (((std::uint32_t)raw[off + 2U]) << 16)
                 | (((std::uint32_t)raw[off + 3U]) << 24);
    return true;
}

static bool
patch_sony_makernote_value_offset_in_tiff(ByteVec* tiff)
{
    std::uint32_t ifd0_off32;
    std::uint16_t ifd0_count;
    std::uint32_t exif_ifd_off32;
    std::uint16_t exif_count;
    std::uint32_t maker_note_off32;
    std::uint32_t i;

    if (tiff == nullptr || tiff->size() < 8U || (*tiff)[0U] != (unsigned char)'I'
        || (*tiff)[1U] != (unsigned char)'I'
        || !read_u32le_at_raw(tiff->data(), tiff->size(), 4U, &ifd0_off32)
        || !read_u16le_at_raw(tiff->data(), tiff->size(), ifd0_off32,
                              &ifd0_count)) {
        return false;
    }

    exif_ifd_off32 = 0U;
    maker_note_off32 = 0U;
    for (i = 0U; i < (std::uint32_t)ifd0_count; ++i) {
        std::uint32_t eoff;
        std::uint16_t tag16;

        eoff = ifd0_off32 + 2U + (i * 12U);
        if (!read_u16le_at_raw(tiff->data(), tiff->size(), eoff, &tag16)) {
            return false;
        }
        if (tag16 == 0x927CU) {
            if (!read_u32le_at_raw(tiff->data(), tiff->size(), eoff + 8U,
                                   &maker_note_off32)) {
                return false;
            }
            break;
        }
        if (tag16 == 0x8769U) {
            if (!read_u32le_at_raw(tiff->data(), tiff->size(), eoff + 8U,
                                   &exif_ifd_off32)) {
                return false;
            }
        }
    }
    if (maker_note_off32 == 0U && exif_ifd_off32 != 0U) {
        if (!read_u16le_at_raw(tiff->data(), tiff->size(), exif_ifd_off32,
                               &exif_count)) {
            return false;
        }
        for (i = 0U; i < (std::uint32_t)exif_count; ++i) {
            std::uint32_t eoff;
            std::uint16_t tag16;

            eoff = exif_ifd_off32 + 2U + (i * 12U);
            if (!read_u16le_at_raw(tiff->data(), tiff->size(), eoff, &tag16)) {
                return false;
            }
            if (tag16 == 0x927CU) {
                if (!read_u32le_at_raw(tiff->data(), tiff->size(), eoff + 8U,
                                       &maker_note_off32)) {
                    return false;
                }
                break;
            }
        }
    }
    if (maker_note_off32 == 0U || maker_note_off32 > tiff->size()) {
        return false;
    }

    if ((tiff->size() - maker_note_off32) >= 34U
        && std::memcmp(tiff->data() + maker_note_off32, "SONY", 4U) == 0) {
        write_u32le_at(tiff->data(), maker_note_off32 + 26U,
                       maker_note_off32 + 34U);
        return true;
    }
    if ((tiff->size() - maker_note_off32) < 18U) {
        return false;
    }
    write_u32le_at(tiff->data(), maker_note_off32 + 10U,
                   maker_note_off32 + 18U);
    return true;
}

static ByteVec
build_tiff_sony_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    ByteVec tiff;
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "SONY", 4U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0x0102U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 7U);
    append_u16le(makernote.data(), &size, 0xB020U);
    append_u16le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 9U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, "Standard", 8U);
    append_u8(makernote.data(), &size, 0U);

    tiff = build_tiff_with_make_makernote_fixture("Sony", makernote.data(),
                                                  size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_sony_ciphered_fixture(std::uint16_t tag, const unsigned char* plain,
                                 std::size_t plain_size)
{
    std::array<unsigned char, 8192> makernote {};
    ByteVec tiff;
    std::size_t size;
    std::size_t i;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, tag);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)plain_size);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    for (i = 0U; i < plain_size; ++i) {
        append_u8(makernote.data(), &size, sony_encipher_byte(plain[i]));
    }

    tiff = build_tiff_with_make_makernote_fixture("Sony", makernote.data(),
                                                  size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_sony_ciphered_model_fixture(const char* model, std::uint16_t tag,
                                       const unsigned char* plain,
                                       std::size_t plain_size)
{
    std::array<unsigned char, 8192> makernote {};
    ByteVec tiff;
    std::size_t size;
    std::size_t i;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, tag);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)plain_size);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    for (i = 0U; i < plain_size; ++i) {
        append_u8(makernote.data(), &size, sony_encipher_byte(plain[i]));
    }

    tiff = build_tiff_with_make_model_makernote_fixture("Sony", model,
                                                        makernote.data(), size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_sony_tag9050b_fixture()
{
    std::array<unsigned char, 0x90> plain {};

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

    return build_tiff_sony_ciphered_fixture(0x9050U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag2010i_fixture()
{
    std::array<unsigned char, 0x1800> plain {};
    std::uint32_t i;

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
        plain[0x17D0U + i] = (unsigned char)i;
    }

    return build_tiff_sony_ciphered_fixture(0x2010U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag202a_fixture()
{
    std::array<unsigned char, 2> plain {};

    plain[0x0001U] = 7U;
    return build_tiff_sony_ciphered_fixture(0x202AU, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag9405b_fixture()
{
    std::array<unsigned char, 0x84> plain {};
    std::uint32_t i;

    plain[0x0004U] = 0x34U;
    plain[0x0005U] = 0x12U;
    plain[0x0010U] = 0x01U;
    plain[0x0014U] = 0xFAU;
    plain[0x0024U] = 0x78U;
    plain[0x0025U] = 0x56U;
    plain[0x0026U] = 0x34U;
    plain[0x0027U] = 0x12U;
    plain[0x005EU] = 9U;
    plain[0x0060U] = 0xBCU;
    plain[0x0061U] = 0x0AU;
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x0064U + (i * 2U),
                       (std::uint16_t)(i - 8U));
    }

    return build_tiff_sony_ciphered_fixture(0x9405U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag9416_fixture()
{
    std::array<unsigned char, 0x96> plain {};
    std::uint32_t i;

    plain[0x0000U] = 0x10U;
    plain[0x0004U] = 0x39U;
    plain[0x0005U] = 0x05U;
    plain[0x000CU] = 0x05U;
    plain[0x0010U] = 0x08U;
    plain[0x001DU] = 0xEFU;
    plain[0x001EU] = 0xCDU;
    plain[0x001FU] = 0xABU;
    plain[0x0020U] = 0x89U;
    plain[0x002BU] = 7U;
    plain[0x0048U] = 3U;
    plain[0x004BU] = 0x80U;
    plain[0x004CU] = 0x07U;
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x004FU + (i * 2U),
                       (std::uint16_t)(20U + i));
    }

    return build_tiff_sony_ciphered_fixture(0x9416U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag9050a_fixture()
{
    std::array<unsigned char, 0x01C1> plain {};

    plain[0x0000U] = 1U;
    plain[0x0001U] = 2U;
    write_u16le_at(plain.data(), 0x0020U, 10U);
    write_u16le_at(plain.data(), 0x0022U, 20U);
    write_u16le_at(plain.data(), 0x0024U, 30U);
    plain[0x0031U] = 0x31U;
    write_u32le_at(plain.data(), 0x0032U, 0x44332211U);
    write_u16le_at(plain.data(), 0x003AU, 0x1234U);
    write_u16le_at(plain.data(), 0x003CU, 0x5678U);
    plain[0x003FU] = 0x3FU;
    plain[0x0067U] = 0x67U;
    plain[0x0105U] = 0x15U;
    plain[0x0106U] = 0x16U;
    write_u16le_at(plain.data(), 0x0107U, 0x0107U);
    write_u16le_at(plain.data(), 0x0109U, 0x0109U);
    plain[0x010BU] = 0x1BU;
    plain[0x0114U] = 0x24U;
    write_u32le_at(plain.data(), 0x01AAU, 0x89ABCDEFU);
    write_u32le_at(plain.data(), 0x01BDU, 0x10203040U);

    return build_tiff_sony_ciphered_model_fixture("Lunar", 0x9050U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9050c_fixture()
{
    std::array<unsigned char, 0x90> plain {};

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

    return build_tiff_sony_ciphered_model_fixture("ILCE-7M4", 0x9050U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag2010b_fixture()
{
    std::array<unsigned char, 0x1A50> plain {};
    std::uint32_t i;

    write_u32le_at(plain.data(), 0x0000U, 0x11223344U);
    write_u32le_at(plain.data(), 0x0004U, 0x55667788U);
    write_u32le_at(plain.data(), 0x0008U, 0x99AABBCCU);
    plain[0x0324U] = 0x24U;
    plain[0x1128U] = 0x28U;
    plain[0x112CU] = 0x2CU;
    write_u16le_at(plain.data(), 0x113EU, 0x113EU);
    write_u16le_at(plain.data(), 0x1140U, 0x1140U);
    write_u16le_at(plain.data(), 0x1218U, 0x1218U);
    write_u16le_at(plain.data(), 0x114CU, (std::uint16_t)-7);
    write_u16le_at(plain.data(), 0x1180U, 1U);
    write_u16le_at(plain.data(), 0x1182U, 2U);
    write_u16le_at(plain.data(), 0x1184U, 3U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x1A23U + (i * 2U),
                       (std::uint16_t)(200U + i));
    }
    for (i = 0U; i < 0x798U; ++i) {
        plain[0x04B4U + i] = (unsigned char)i;
    }

    return build_tiff_sony_ciphered_model_fixture("Lunar", 0x2010U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag2010e_fixture()
{
    std::array<unsigned char, 0x1A90> plain {};
    std::uint32_t i;

    write_u32le_at(plain.data(), 0x0000U, 0xA1B2C3D4U);
    write_u32le_at(plain.data(), 0x0004U, 0x10213243U);
    write_u32le_at(plain.data(), 0x0008U, 0x55667788U);
    plain[0x021CU] = 0x21U;
    plain[0x0328U] = 0x32U;
    plain[0x115CU] = 0x5CU;
    plain[0x1160U] = 0x60U;
    write_u16le_at(plain.data(), 0x1172U, 0x1172U);
    write_u16le_at(plain.data(), 0x1174U, 0x1174U);
    write_u16le_at(plain.data(), 0x1254U, 0x1254U);
    write_u16le_at(plain.data(), 0x1180U, (std::uint16_t)-9);
    write_u16le_at(plain.data(), 0x11B4U, 4U);
    write_u16le_at(plain.data(), 0x11B6U, 5U);
    write_u16le_at(plain.data(), 0x11B8U, 6U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x1870U + (i * 2U),
                       (std::uint16_t)(300U + i));
    }
    plain[0x1891U] = 0x91U;
    plain[0x1892U] = 0x92U;
    write_u16le_at(plain.data(), 0x1893U, 0x1893U);
    write_u16le_at(plain.data(), 0x1896U, 0x1896U);
    plain[0x192CU] = 0x2CU;
    plain[0x1A88U] = 0x88U;
    for (i = 0U; i < 0x798U; ++i) {
        plain[0x04B8U + i] = (unsigned char)(0x80U + i);
    }

    return build_tiff_sony_ciphered_model_fixture("Stellar", 0x2010U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9400a_fixture()
{
    std::array<unsigned char, 0x53> plain {};

    plain[0x0000U] = 0x07U;
    plain[0x0008U] = 0x04U;
    plain[0x0009U] = 0x03U;
    plain[0x000AU] = 0x02U;
    plain[0x000BU] = 0x01U;
    plain[0x000CU] = 0x08U;
    plain[0x000DU] = 0x07U;
    plain[0x000EU] = 0x06U;
    plain[0x000FU] = 0x05U;
    plain[0x0010U] = 2U;
    plain[0x0012U] = 1U;
    plain[0x001AU] = 0x44U;
    plain[0x001BU] = 0x33U;
    plain[0x001CU] = 0x22U;
    plain[0x001DU] = 0x11U;
    plain[0x0022U] = 10U;
    plain[0x0028U] = 3U;
    plain[0x0029U] = 8U;
    plain[0x0044U] = 0xB8U;
    plain[0x0045U] = 0x0BU;
    plain[0x0052U] = 23U;

    return build_tiff_sony_ciphered_model_fixture("SLT-A99", 0x9400U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9404b_fixture()
{
    std::array<unsigned char, 0x20> plain {};

    plain[0x000CU] = 12U;
    plain[0x000EU] = 14U;
    write_u16le_at(plain.data(), 0x001EU, 0x2345U);

    return build_tiff_sony_ciphered_model_fixture("Lunar", 0x9404U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9405a_fixture()
{
    std::array<unsigned char, 0x06EA> plain {};
    std::uint32_t i;

    plain[0x0600U] = 1U;
    plain[0x0601U] = 2U;
    plain[0x0603U] = 3U;
    plain[0x0604U] = 4U;
    write_u16le_at(plain.data(), 0x0605U, 0x0605U);
    write_u16le_at(plain.data(), 0x0608U, 0x0608U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x064AU + (i * 2U),
                       (std::uint16_t)(30U + i));
    }
    for (i = 0U; i < 32U; ++i) {
        write_u16le_at(plain.data(), 0x066AU + (i * 2U),
                       (std::uint16_t)(i - 20U));
    }
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x06CAU + (i * 2U),
                       (std::uint16_t)(100U + i));
    }

    return build_tiff_sony_ciphered_model_fixture("SLT-A99", 0x9405U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag940e_fixture()
{
    std::array<unsigned char, 0x1A14> plain {};
    std::uint32_t i;

    plain[0x1A06U] = 2U;
    plain[0x1A07U] = 3U;
    for (i = 0U; i < 12U; ++i) {
        plain[0x1A08U + i] = (unsigned char)(0xA0U + i);
    }

    return build_tiff_sony_ciphered_model_fixture("ILCE-7M3", 0x940EU,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag940e_afinfo_fixture()
{
    std::array<unsigned char, 0x0180> plain {};
    std::uint32_t i;

    plain[0x0002U] = 2U;
    plain[0x0004U] = 5U;
    plain[0x0007U] = 6U;
    plain[0x0008U] = 7U;
    plain[0x0009U] = 8U;
    plain[0x000AU] = 9U;
    plain[0x000BU] = 10U;
    for (i = 0U; i < 30U; ++i) {
        write_u16le_at(plain.data(), 0x0011U + (i * 2U),
                       (std::uint16_t)(100U + i));
    }
    plain[0x016EU] = 0x78U;
    plain[0x016FU] = 0x56U;
    plain[0x0170U] = 0x34U;
    plain[0x0171U] = 0x12U;
    plain[0x017DU] = 0xFEU;
    plain[0x017EU] = 4U;

    return build_tiff_sony_ciphered_model_fixture("SLT-A99", 0x940EU,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_shotinfo_fixture()
{
    std::array<unsigned char, 0x44> blob {};
    std::array<unsigned char, 128> makernote {};
    ByteVec tiff;
    std::size_t size;

    blob[0U] = (unsigned char)'I';
    blob[1U] = (unsigned char)'I';
    write_u16le_at(blob.data(), 0x0002U, 94U);
    std::memcpy(blob.data() + 0x0006U, "2017:02:08 07:07:08", 19U);
    write_u16le_at(blob.data(), 0x001AU, 5304U);
    write_u16le_at(blob.data(), 0x001CU, 7952U);
    write_u16le_at(blob.data(), 0x0030U, 2U);
    write_u16le_at(blob.data(), 0x0032U, 37U);
    std::memcpy(blob.data() + 0x0034U, "DC7303320222000", 15U);

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x3000U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)blob.size());
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, blob.data(), blob.size());

    tiff = build_tiff_with_make_makernote_fixture("Sony", makernote.data(),
                                                  size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_flir_makernote_fixture()
{
    unsigned char makernote[32];
    std::size_t size = 0U;

    append_u16le(makernote, &size, 1U);
    append_u16le(makernote, &size, 0x0001U);
    append_u16le(makernote, &size, 4U);
    append_u32le(makernote, &size, 1U);
    append_u32le(makernote, &size, 99U);
    append_u32le(makernote, &size, 0U);

    return build_tiff_with_make_makernote_fixture("FLIR", makernote, size);
}

static ByteVec
build_tiff_nintendo_makernote_fixture()
{
    unsigned char makernote[96];
    std::size_t size = 0U;

    append_u16le(makernote, &size, 1U);
    append_u16le(makernote, &size, 0x1101U);
    append_u16le(makernote, &size, 7U);
    append_u32le(makernote, &size, 0x34U);
    append_u32le(makernote, &size, 18U);
    append_u32le(makernote, &size, 0U);

    std::memset(makernote + size, 0, 0x34U);
    std::memcpy(makernote + size + 0U, "3DS1", 4U);
    write_u32le_at(makernote, 18U + 0x08U, 0x12345678U);
    makernote[18U + 0x18U] = 0xAAU;
    makernote[18U + 0x19U] = 0xBBU;
    makernote[18U + 0x1AU] = 0xCCU;
    makernote[18U + 0x1BU] = 0xDDU;
    write_u32le_at(makernote, 18U + 0x28U, 0x3FC00000U);
    write_u16le_at(makernote, 18U + 0x30U, 5U);
    size += 0x34U;

    return build_tiff_with_make_makernote_fixture("Nintendo", makernote,
                                                  size);
}

static ByteVec
build_tiff_hp_type6_makernote_fixture()
{
    unsigned char makernote[128];

    std::memset(makernote, 0, sizeof(makernote));
    makernote[0U] = (unsigned char)'I';
    makernote[1U] = (unsigned char)'I';
    makernote[2U] = (unsigned char)'I';
    makernote[3U] = (unsigned char)'I';
    makernote[4U] = 0x06U;
    makernote[5U] = 0U;

    write_u16le_at(makernote, 0x000CU, 28U);
    write_u32le_at(makernote, 0x0010U, 50000U);
    std::memcpy(makernote + 0x0014U, "2025:03:16 12:34:56", 19U);
    write_u16le_at(makernote, 0x0034U, 200U);
    std::memcpy(makernote + 0x0058U, "SERIAL NUMBER:HP-12345", 22U);

    return build_tiff_with_make_makernote_fixture("HP", makernote,
                                                  sizeof(makernote));
}

static ByteVec
build_tiff_nikon_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);
    append_u16le(makernote.data(), &size, 0x001FU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    append_u32le(makernote.data(), &size, 38U);
    append_u32le(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "0101");
    append_u8(makernote.data(), &size, 1U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Nikon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_binary_makernote_fixture()
{
    std::array<unsigned char, 512> makernote {};
    std::size_t size;
    std::uint32_t off_vr_pos;
    std::uint32_t off_dist_pos;
    std::uint32_t off_flash_pos;
    std::uint32_t off_multi_pos;
    std::uint32_t off_afinfo_pos;
    std::uint32_t off_file_pos;
    std::uint32_t off_retouch_pos;
    std::uint32_t off_payload;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 8U);

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);

    append_u16le(makernote.data(), &size, 0x001FU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    off_vr_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x002BU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 5U);
    off_dist_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00A8U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 10U);
    off_flash_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B0U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 16U);
    off_multi_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B7U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 29U);
    off_afinfo_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B8U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 10U);
    off_file_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00BBU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 6U);
    off_retouch_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_vr_pos, off_payload);
    append_text(makernote.data(), &size, "0101");
    append_u8(makernote.data(), &size, 1U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_dist_pos, off_payload);
    append_text(makernote.data(), &size, "0100");
    append_u8(makernote.data(), &size, 1U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_flash_pos, off_payload);
    append_text(makernote.data(), &size, "0106");
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0xAAU);
    append_u8(makernote.data(), &size, 0xBBU);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_multi_pos, off_payload);
    append_text(makernote.data(), &size, "0100");
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 3U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_afinfo_pos, off_payload);
    append_text(makernote.data(), &size, "0100");
    append_u32le(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0xAAU);
    append_u8(makernote.data(), &size, 0xBBU);
    append_u8(makernote.data(), &size, 0xCCU);
    append_u8(makernote.data(), &size, 0xDDU);
    append_u8(makernote.data(), &size, 0xEEU);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 1U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_file_pos, off_payload);
    append_text(makernote.data(), &size, "0100");
    append_u16le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 99U);
    append_u16le(makernote.data(), &size, 7U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_retouch_pos, off_payload);
    append_text(makernote.data(), &size, "0100");
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0xFFU);

    return build_tiff_with_make_makernote_fixture("Nikon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_makernote_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x12345678U);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_custom_functions2_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0099U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0x01010000U);
    append_u32le(makernote.data(), &size, 0x01020003U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_afinfo2_makernote_fixture()
{
    static const std::int16_t x_pos[9] = {
        0, -649, 649, -1034, 0, 1034, -649, 649, 0
    };
    static const std::int16_t y_pos[9] = {
        562, 298, 298, 0, 0, 0, -298, -298, -562
    };
    std::array<unsigned char, 256> makernote {};
    std::size_t size;
    std::size_t i;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0026U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 48U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 96U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 9U);
    append_u16le(makernote.data(), &size, 9U);
    append_u16le(makernote.data(), &size, 3888U);
    append_u16le(makernote.data(), &size, 2592U);
    append_u16le(makernote.data(), &size, 3888U);
    append_u16le(makernote.data(), &size, 2592U);

    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, 97U);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, 98U);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, (std::uint16_t)x_pos[i]);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, (std::uint16_t)y_pos[i]);
    }

    append_u16le(makernote.data(), &size, 4U);
    append_u16le(makernote.data(), &size, 4U);
    append_u16le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_preview_settings_aftune_fixture()
{
    std::array<unsigned char, 512> makernote {};
    std::size_t size;
    std::uint32_t off_preview_pos;
    std::uint32_t off_settings_pos;
    std::uint32_t rel_off;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 4U);

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);

    append_u16le(makernote.data(), &size, 0x0011U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    off_preview_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x004EU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 48U);
    off_settings_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B9U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u8(makernote.data(), &size, 1U);
    append_u8(makernote.data(), &size, 4U);
    append_u8(makernote.data(), &size, 0xFDU);
    append_u8(makernote.data(), &size, 5U);

    append_u32le(makernote.data(), &size, 0U);

    rel_off = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_settings_pos, rel_off);
    std::memset(makernote.data() + size, 0, 48U);
    write_u32le_at(makernote.data(), (std::uint32_t)size + 20U, 3U);
    makernote[size + 24U + 0U] = 0x01U;
    makernote[size + 24U + 1U] = 0x00U;
    makernote[size + 24U + 2U] = 0x00U;
    makernote[size + 24U + 3U] = 0x04U;
    write_u32le_at(makernote.data(), (std::uint32_t)size + 24U + 4U, 6400U);
    makernote[size + 32U + 0U] = 0x46U;
    makernote[size + 32U + 1U] = 0x00U;
    makernote[size + 32U + 2U] = 0x00U;
    makernote[size + 32U + 3U] = 0x01U;
    write_u32le_at(makernote.data(), (std::uint32_t)size + 32U + 4U, 1U);
    makernote[size + 40U + 0U] = 0x63U;
    makernote[size + 40U + 1U] = 0x00U;
    makernote[size + 40U + 2U] = 0x00U;
    makernote[size + 40U + 3U] = 0x03U;
    write_u32le_at(makernote.data(), (std::uint32_t)size + 40U + 4U, 9U);
    size += 48U;

    rel_off = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_preview_pos, rel_off);
    append_u16le(makernote.data(), &size, 4U);

    append_u16le(makernote.data(), &size, 0x0103U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 6U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0201U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x0200U);

    append_u16le(makernote.data(), &size, 0x0202U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x1234U);

    append_u16le(makernote.data(), &size, 0x0213U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Nikon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_info_blocks_fixture()
{
    std::array<unsigned char, 512> makernote {};
    std::size_t size;
    std::uint32_t off_pc_pos;
    std::uint32_t off_iso_pos;
    std::uint32_t off_hdr_pos;
    std::uint32_t off_loc_pos;
    std::uint32_t off_payload;
    std::size_t i;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 6U);

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);

    append_u16le(makernote.data(), &size, 0x0023U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 66U);
    off_pc_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0024U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 0x0201FDE4U);

    append_u16le(makernote.data(), &size, 0x0025U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 10U);
    off_iso_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0035U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    off_hdr_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0039U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 17U);
    off_loc_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_pc_pos, off_payload);
    append_bytes(makernote.data(), &size, "0200", 4U);
    append_bytes(makernote.data(), &size, "NEUTRAL", 7U);
    for (i = 0U; i < 13U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    append_bytes(makernote.data(), &size, "STANDARD", 8U);
    for (i = 0U; i < 12U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    for (i = 0U; i < 21U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    append_u8(makernote.data(), &size, 15U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_iso_pos, off_payload);
    append_bytes(makernote.data(), &size, "0100", 4U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 400U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_hdr_pos, off_payload);
    append_bytes(makernote.data(), &size, "0100", 4U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 4U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_loc_pos, off_payload);
    append_bytes(makernote.data(), &size, "0100", 4U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, "TOKYO-JP", 8U);

    return build_tiff_with_make_makernote_fixture("Nikon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_filterinfo_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;
    std::uint32_t filter_size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x4024U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 12U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    filter_size = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 36U);
    append_u32le(makernote.data(), &size, 2U);

    append_u32le(makernote.data(), &size, 0x0402U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 2U);

    append_u32le(makernote.data(), &size, 0x0403U);
    append_u32le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 300U);
    append_u32le(makernote.data(), &size, 700U);

    write_u32le_at(makernote.data(), filter_size,
                   (std::uint32_t)(size - filter_size));
    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_timeinfo_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0035U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 540U);
    append_u32le(makernote.data(), &size, 1234U);
    append_u32le(makernote.data(), &size, 1U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_camera_info_psinfo_makernote_fixture()
{
    std::array<unsigned char, 2048> makernote {};
    std::size_t size;
    std::uint32_t cam_off;
    std::uint32_t cam_bytes;

    size      = 0U;
    cam_bytes = 0x025BU + 0x00DEU;

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x000DU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, cam_bytes);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    cam_off = (std::uint32_t)size;
    std::memset(makernote.data() + size, 0, cam_bytes);
    write_u16le_at(makernote.data(), cam_off + 0U, 1U);
    write_u16le_at(makernote.data(), cam_off + 2U, 0x0003U);
    write_u16le_at(makernote.data(), cam_off + 4U, 3U);
    write_u32le_at(makernote.data(), cam_off + 6U, 1U);
    write_u16le_at(makernote.data(), cam_off + 10U, 42U);
    write_u32le_at(makernote.data(), cam_off + 14U, 0U);
    write_u32le_at(makernote.data(), cam_off + 0x025BU + 0x0004U, 3U);
    write_u16le_at(makernote.data(), cam_off + 0x025BU + 0x00D8U, 129U);
    size += cam_bytes;

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_colordata8_makernote_fixture()
{
    std::array<unsigned char, 4096> makernote {};
    std::size_t size;
    std::uint32_t color_off;
    std::uint32_t color_count;
    std::uint32_t color_bytes;

    size        = 0U;
    color_count = 1353U;
    color_bytes = color_count * 2U;

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x4001U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, color_count);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    color_off = (std::uint32_t)size;
    std::memset(makernote.data() + size, 0, color_bytes);
    write_u16le_at(makernote.data(), color_off + (0x0000U * 2U), 14U);
    write_u16le_at(makernote.data(), color_off + (0x003FU * 2U), 777U);
    write_u16le_at(makernote.data(), color_off + (0x0043U * 2U), 6100U);
    write_u16le_at(makernote.data(), color_off + (0x0107U * 2U), 100U);
    write_u16le_at(makernote.data(), color_off + (0x0108U * 2U),
                   (std::uint16_t)(0xFFFFU - 24U));
    write_u16le_at(makernote.data(), color_off + (0x0109U * 2U), 300U);
    write_u16le_at(makernote.data(), color_off + (0x010AU * 2U), 5200U);
    write_u16le_at(makernote.data(), color_off + (0x010BU * 2U),
                   (std::uint16_t)(0xFFFFU - 9U));
    write_u16le_at(makernote.data(), color_off + (0x010CU * 2U), 20U);
    write_u16le_at(makernote.data(), color_off + (0x010DU * 2U),
                   (std::uint16_t)(0xFFFFU - 29U));
    write_u16le_at(makernote.data(), color_off + (0x010EU * 2U), 40U);
    size += color_bytes;

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_geotiff_fixture()
{
    static const unsigned char geodouble_bits[8] = {
        0x00U, 0x00U, 0x00U, 0x40U, 0xA6U, 0x54U, 0x58U, 0x41U
    };
    std::array<unsigned char, 160> file {};
    std::size_t size = 0U;

    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 3U);

    append_u16le(file.data(), &size, 0x87AFU);
    append_u16le(file.data(), &size, 3U);
    append_u32le(file.data(), &size, 16U);
    append_u32le(file.data(), &size, 50U);

    append_u16le(file.data(), &size, 0x87B0U);
    append_u16le(file.data(), &size, 12U);
    append_u32le(file.data(), &size, 1U);
    append_u32le(file.data(), &size, 82U);

    append_u16le(file.data(), &size, 0x87B1U);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, 13U);
    append_u32le(file.data(), &size, 90U);

    append_u32le(file.data(), &size, 0U);

    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0U);
    append_u16le(file.data(), &size, 3U);

    append_u16le(file.data(), &size, 1024U);
    append_u16le(file.data(), &size, 0U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 2U);

    append_u16le(file.data(), &size, 1026U);
    append_u16le(file.data(), &size, 0x87B1U);
    append_u16le(file.data(), &size, 13U);
    append_u16le(file.data(), &size, 0U);

    append_u16le(file.data(), &size, 2057U);
    append_u16le(file.data(), &size, 0x87B0U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0U);

    append_bytes(file.data(), &size, geodouble_bits, sizeof(geodouble_bits));
    append_text(file.data(), &size, "TestCitation|");

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_tiff_printim_fixture()
{
    static const unsigned char printim_magic[8] = {
        'P', 'r', 'i', 'n', 't', 'I', 'M', 0
    };
    std::array<unsigned char, 96> file {};
    std::size_t size = 0U;

    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 1U);

    append_u16le(file.data(), &size, 0xC4A5U);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, 22U);
    append_u32le(file.data(), &size, 26U);

    append_u32le(file.data(), &size, 0U);
    append_bytes(file.data(), &size, printim_magic, sizeof(printim_magic));
    append_text(file.data(), &size, "0300");
    append_u16le(file.data(), &size, 0U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0x0001U);
    append_u32le(file.data(), &size, 0x00160016U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_fields_fixture()
{
    std::array<unsigned char, 2048> file {};
    std::array<unsigned char, 64> infe_exif {};
    std::array<unsigned char, 96> infe_xmp {};
    std::array<unsigned char, 96> infe_jumb {};
    std::array<unsigned char, 384> iinf_payload {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 16> ispe_payload {};
    std::array<unsigned char, 8> irot_payload {};
    std::array<unsigned char, 8> imir_payload {};
    std::array<unsigned char, 64> auxc_alpha_payload {};
    std::array<unsigned char, 64> auxc_depth_payload {};
    std::array<unsigned char, 192> ipco_payload {};
    std::array<unsigned char, 48> ipma_payload {};
    std::array<unsigned char, 256> iprp_payload {};
    std::array<unsigned char, 16> auxl_payload {};
    std::array<unsigned char, 16> dimg_payload {};
    std::array<unsigned char, 16> thmb_payload {};
    std::array<unsigned char, 128> iref_payload {};
    std::array<unsigned char, 1024> meta_payload {};
    std::array<unsigned char, 16> moov_box {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t infe_exif_size;
    std::size_t infe_xmp_size;
    std::size_t infe_jumb_size;
    std::size_t iinf_size;
    std::size_t pitm_size;
    std::size_t ispe_size;
    std::size_t irot_size;
    std::size_t imir_size;
    std::size_t auxc_alpha_size;
    std::size_t auxc_depth_size;
    std::size_t ipco_size;
    std::size_t ipma_size;
    std::size_t iprp_size;
    std::size_t auxl_size;
    std::size_t dimg_size;
    std::size_t thmb_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t moov_size;
    std::size_t ftyp_size;
    std::size_t size;

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif.data(), &infe_exif_size, 2U);
    append_u16be(infe_exif.data(), &infe_exif_size, 1U);
    append_u16be(infe_exif.data(), &infe_exif_size, 0U);
    append_u32be(infe_exif.data(), &infe_exif_size,
                 make_fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif.data(), &infe_exif_size, "Exif");
    append_u8(infe_exif.data(), &infe_exif_size, 0U);

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp.data(), &infe_xmp_size, 2U);
    append_u16be(infe_xmp.data(), &infe_xmp_size, 2U);
    append_u16be(infe_xmp.data(), &infe_xmp_size, 7U);
    append_u32be(infe_xmp.data(), &infe_xmp_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp.data(), &infe_xmp_size, "XMP");
    append_u8(infe_xmp.data(), &infe_xmp_size, 0U);
    append_text(infe_xmp.data(), &infe_xmp_size, "application/rdf+xml");
    append_u8(infe_xmp.data(), &infe_xmp_size, 0U);
    append_u8(infe_xmp.data(), &infe_xmp_size, 0U);

    infe_jumb_size = 0U;
    append_fullbox_header(infe_jumb.data(), &infe_jumb_size, 2U);
    append_u16be(infe_jumb.data(), &infe_jumb_size, 3U);
    append_u16be(infe_jumb.data(), &infe_jumb_size, 0U);
    append_u32be(infe_jumb.data(), &infe_jumb_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_jumb.data(), &infe_jumb_size, "C2PA");
    append_u8(infe_jumb.data(), &infe_jumb_size, 0U);
    append_text(infe_jumb.data(), &infe_jumb_size, "application/jumbf");
    append_u8(infe_jumb.data(), &infe_jumb_size, 0U);
    append_u8(infe_jumb.data(), &infe_jumb_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload.data(), &iinf_size, 0U);
    append_u16be(iinf_payload.data(), &iinf_size, 3U);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_exif.data(),
                    infe_exif_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_xmp.data(),
                    infe_xmp_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_jumb.data(),
                    infe_jumb_size);

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 2U);

    ispe_size = 0U;
    append_fullbox_header(ispe_payload.data(), &ispe_size, 0U);
    append_u32be(ispe_payload.data(), &ispe_size, 640U);
    append_u32be(ispe_payload.data(), &ispe_size, 480U);

    irot_size = 0U;
    append_u8(irot_payload.data(), &irot_size, 1U);

    imir_size = 0U;
    append_u8(imir_payload.data(), &imir_size, 1U);

    auxc_alpha_size = 0U;
    append_fullbox_header(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);
    append_text(auxc_alpha_payload.data(), &auxc_alpha_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);

    auxc_depth_size = 0U;
    append_fullbox_header(auxc_depth_payload.data(), &auxc_depth_size, 0U);
    append_text(auxc_depth_payload.data(), &auxc_depth_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_depth_payload.data(), &auxc_depth_size, 0U);

    ipco_size = 0U;
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('i', 's', 'p', 'e'), ispe_payload.data(),
                    ispe_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('i', 'r', 'o', 't'), irot_payload.data(),
                    irot_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('i', 'm', 'i', 'r'), imir_payload.data(),
                    imir_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_alpha_payload.data(), auxc_alpha_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_depth_payload.data(), auxc_depth_size);

    ipma_size = 0U;
    append_fullbox_header(ipma_payload.data(), &ipma_size, 0U);
    append_u32be(ipma_payload.data(), &ipma_size, 3U);
    append_u16be(ipma_payload.data(), &ipma_size, 2U);
    append_u8(ipma_payload.data(), &ipma_size, 3U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 2U);
    append_u8(ipma_payload.data(), &ipma_size, 3U);
    append_u16be(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 4U);
    append_u16be(ipma_payload.data(), &ipma_size, 3U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 5U);

    iprp_size = 0U;
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'c', 'o'), ipco_payload.data(),
                    ipco_size);
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'm', 'a'), ipma_payload.data(),
                    ipma_size);

    auxl_size = 0U;
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 1U);
    append_u16be(auxl_payload.data(), &auxl_size, 3U);

    dimg_size = 0U;
    append_u16be(dimg_payload.data(), &dimg_size, 2U);
    append_u16be(dimg_payload.data(), &dimg_size, 1U);
    append_u16be(dimg_payload.data(), &dimg_size, 3U);

    thmb_size = 0U;
    append_u16be(thmb_payload.data(), &thmb_size, 2U);
    append_u16be(thmb_payload.data(), &thmb_size, 1U);
    append_u16be(thmb_payload.data(), &thmb_size, 1U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_payload.data(),
                    auxl_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('d', 'i', 'm', 'g'), dimg_payload.data(),
                    dimg_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('t', 'h', 'm', 'b'), thmb_payload.data(),
                    thmb_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'), iinf_payload.data(),
                    iinf_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'p', 'r', 'p'), iprp_payload.data(),
                    iprp_size);

    moov_size = 0U;
    append_bmff_box(moov_box.data(), &moov_size, make_fourcc('m', 'o', 'o', 'v'),
                    (const unsigned char*)0, 0U);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bytes(file.data(), &size, moov_box.data(), moov_size);
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static std::span<const std::byte>
as_byte_span(const ByteVec& bytes)
{
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
}

static std::string
escape_bytes(const unsigned char* data, std::size_t size)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    std::size_t i;

    out.reserve(size * 4U);
    for (i = 0U; i < size; ++i) {
        unsigned char ch = data[i];
        if (ch >= 0x20U && ch <= 0x7EU && ch != '\\' && ch != '|') {
            out.push_back((char)ch);
        } else {
            out.push_back('\\');
            out.push_back('x');
            out.push_back(hex[(ch >> 4) & 0x0FU]);
            out.push_back(hex[ch & 0x0FU]);
        }
    }
    return out;
}

static std::string
escape_bytes(std::span<const std::byte> bytes)
{
    std::string out;
    std::size_t i;

    out.reserve(bytes.size() * 4U);
    for (i = 0U; i < bytes.size(); ++i) {
        unsigned char ch = std::to_integer<unsigned char>(bytes[i]);
        out += escape_bytes(&ch, 1U);
    }
    return out;
}

static std::string
hex_bytes(const unsigned char* data, std::size_t size)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    std::size_t i;

    out.reserve(size * 2U);
    for (i = 0U; i < size; ++i) {
        out.push_back(hex[(data[i] >> 4) & 0x0FU]);
        out.push_back(hex[data[i] & 0x0FU]);
    }
    return out;
}

static std::string
hex_bytes(std::span<const std::byte> bytes)
{
    std::string out;
    std::size_t i;

    out.reserve(bytes.size() * 2U);
    for (i = 0U; i < bytes.size(); ++i) {
        unsigned char ch = std::to_integer<unsigned char>(bytes[i]);
        out += hex_bytes(&ch, 1U);
    }
    return out;
}

static std::string
omc_ref_text(const omc_store* store, omc_byte_ref ref)
{
    omc_const_bytes view = omc_arena_view(&store->arena, ref);
    return escape_bytes((const unsigned char*)view.data, (std::size_t)view.size);
}

static std::string
omc_ref_hex(const omc_store* store, omc_byte_ref ref)
{
    omc_const_bytes view = omc_arena_view(&store->arena, ref);
    return hex_bytes((const unsigned char*)view.data, (std::size_t)view.size);
}

static std::string
cpp_span_text(const openmeta::MetaStore& store, openmeta::ByteSpan ref)
{
    return escape_bytes(store.arena().span(ref));
}

static std::string
cpp_span_hex(const openmeta::MetaStore& store, openmeta::ByteSpan ref)
{
    return hex_bytes(store.arena().span(ref));
}

static const char*
omc_key_kind_name(omc_key_kind kind)
{
    switch (kind) {
        case OMC_KEY_EXIF_TAG: return "ExifTag";
        case OMC_KEY_COMMENT: return "Comment";
        case OMC_KEY_EXR_ATTR: return "ExrAttribute";
        case OMC_KEY_IPTC_DATASET: return "IptcDataset";
        case OMC_KEY_XMP_PROPERTY: return "XmpProperty";
        case OMC_KEY_ICC_HEADER_FIELD: return "IccHeaderField";
        case OMC_KEY_ICC_TAG: return "IccTag";
        case OMC_KEY_PHOTOSHOP_IRB: return "PhotoshopIrb";
        case OMC_KEY_GEOTIFF_KEY: return "GeotiffKey";
        case OMC_KEY_PRINTIM_FIELD: return "PrintImField";
        case OMC_KEY_BMFF_FIELD: return "BmffField";
        case OMC_KEY_JUMBF_FIELD: return "JumbfField";
        case OMC_KEY_JUMBF_CBOR_KEY: return "JumbfCborKey";
        case OMC_KEY_PNG_TEXT: return "PngText";
        default: return "UnknownKey";
    }
}

static const char*
cpp_key_kind_name(openmeta::MetaKeyKind kind)
{
    switch (kind) {
        case openmeta::MetaKeyKind::ExifTag: return "ExifTag";
        case openmeta::MetaKeyKind::Comment: return "Comment";
        case openmeta::MetaKeyKind::ExrAttribute: return "ExrAttribute";
        case openmeta::MetaKeyKind::IptcDataset: return "IptcDataset";
        case openmeta::MetaKeyKind::XmpProperty: return "XmpProperty";
        case openmeta::MetaKeyKind::IccHeaderField: return "IccHeaderField";
        case openmeta::MetaKeyKind::IccTag: return "IccTag";
        case openmeta::MetaKeyKind::PhotoshopIrb: return "PhotoshopIrb";
        case openmeta::MetaKeyKind::GeotiffKey: return "GeotiffKey";
        case openmeta::MetaKeyKind::PrintImField: return "PrintImField";
        case openmeta::MetaKeyKind::BmffField: return "BmffField";
        case openmeta::MetaKeyKind::JumbfField: return "JumbfField";
        case openmeta::MetaKeyKind::JumbfCborKey: return "JumbfCborKey";
        case openmeta::MetaKeyKind::PngText: return "PngText";
        default: return "UnknownKey";
    }
}

static const char*
omc_val_kind_name(omc_val_kind kind)
{
    switch (kind) {
        case OMC_VAL_EMPTY: return "Empty";
        case OMC_VAL_SCALAR: return "Scalar";
        case OMC_VAL_ARRAY: return "Array";
        case OMC_VAL_BYTES: return "Bytes";
        case OMC_VAL_TEXT: return "Text";
        default: return "UnknownVal";
    }
}

static const char*
cpp_val_kind_name(openmeta::MetaValueKind kind)
{
    switch (kind) {
        case openmeta::MetaValueKind::Empty: return "Empty";
        case openmeta::MetaValueKind::Scalar: return "Scalar";
        case openmeta::MetaValueKind::Array: return "Array";
        case openmeta::MetaValueKind::Bytes: return "Bytes";
        case openmeta::MetaValueKind::Text: return "Text";
        default: return "UnknownVal";
    }
}

static const char*
omc_elem_name(omc_elem_type elem)
{
    switch (elem) {
        case OMC_ELEM_U8: return "U8";
        case OMC_ELEM_I8: return "I8";
        case OMC_ELEM_U16: return "U16";
        case OMC_ELEM_I16: return "I16";
        case OMC_ELEM_U32: return "U32";
        case OMC_ELEM_I32: return "I32";
        case OMC_ELEM_U64: return "U64";
        case OMC_ELEM_I64: return "I64";
        case OMC_ELEM_F32_BITS: return "F32Bits";
        case OMC_ELEM_F64_BITS: return "F64Bits";
        case OMC_ELEM_URATIONAL: return "URational";
        case OMC_ELEM_SRATIONAL: return "SRational";
        default: return "UnknownElem";
    }
}

static const char*
cpp_elem_name(openmeta::MetaElementType elem)
{
    switch (elem) {
        case openmeta::MetaElementType::U8: return "U8";
        case openmeta::MetaElementType::I8: return "I8";
        case openmeta::MetaElementType::U16: return "U16";
        case openmeta::MetaElementType::I16: return "I16";
        case openmeta::MetaElementType::U32: return "U32";
        case openmeta::MetaElementType::I32: return "I32";
        case openmeta::MetaElementType::U64: return "U64";
        case openmeta::MetaElementType::I64: return "I64";
        case openmeta::MetaElementType::F32: return "F32Bits";
        case openmeta::MetaElementType::F64: return "F64Bits";
        case openmeta::MetaElementType::URational: return "URational";
        case openmeta::MetaElementType::SRational: return "SRational";
        default: return "UnknownElem";
    }
}

static const char*
omc_text_enc_name(omc_text_encoding enc)
{
    switch (enc) {
        case OMC_TEXT_UNKNOWN: return "Unknown";
        case OMC_TEXT_ASCII: return "Ascii";
        case OMC_TEXT_UTF8: return "Utf8";
        case OMC_TEXT_UTF16LE: return "Utf16LE";
        case OMC_TEXT_UTF16BE: return "Utf16BE";
        default: return "UnknownEnc";
    }
}

static const char*
cpp_text_enc_name(openmeta::TextEncoding enc)
{
    switch (enc) {
        case openmeta::TextEncoding::Unknown: return "Unknown";
        case openmeta::TextEncoding::Ascii: return "Ascii";
        case openmeta::TextEncoding::Utf8: return "Utf8";
        case openmeta::TextEncoding::Utf16LE: return "Utf16LE";
        case openmeta::TextEncoding::Utf16BE: return "Utf16BE";
        default: return "UnknownEnc";
    }
}

static std::string
canonical_omc_key(const omc_store* store, const omc_key* key)
{
    std::string out;

    out = omc_key_kind_name(key->kind);
    switch (key->kind) {
        case OMC_KEY_EXIF_TAG:
            out += "|ifd=";
            out += omc_ref_text(store, key->u.exif_tag.ifd);
            out += "|tag=";
            out += std::to_string((unsigned int)key->u.exif_tag.tag);
            break;
        case OMC_KEY_COMMENT:
            break;
        case OMC_KEY_EXR_ATTR:
            out += "|part=";
            out += std::to_string((unsigned int)key->u.exr_attr.part_index);
            out += "|name=";
            out += omc_ref_text(store, key->u.exr_attr.name);
            break;
        case OMC_KEY_IPTC_DATASET:
            out += "|record=";
            out += std::to_string((unsigned int)key->u.iptc_dataset.record);
            out += "|dataset=";
            out += std::to_string((unsigned int)key->u.iptc_dataset.dataset);
            break;
        case OMC_KEY_XMP_PROPERTY:
            out += "|schema=";
            out += omc_ref_text(store, key->u.xmp_property.schema_ns);
            out += "|path=";
            out += omc_ref_text(store, key->u.xmp_property.property_path);
            break;
        case OMC_KEY_ICC_HEADER_FIELD:
            out += "|offset=";
            out += std::to_string((unsigned long long)
                                  key->u.icc_header_field.offset);
            break;
        case OMC_KEY_ICC_TAG:
            out += "|sig=";
            out += std::to_string((unsigned long long)key->u.icc_tag.signature);
            break;
        case OMC_KEY_PHOTOSHOP_IRB:
            out += "|id=";
            out += std::to_string((unsigned int)
                                  key->u.photoshop_irb.resource_id);
            break;
        case OMC_KEY_GEOTIFF_KEY:
            out += "|id=";
            out += std::to_string((unsigned int)key->u.geotiff_key.key_id);
            break;
        case OMC_KEY_PRINTIM_FIELD:
            out += "|field=";
            out += omc_ref_text(store, key->u.printim_field.field);
            break;
        case OMC_KEY_BMFF_FIELD:
            out += "|field=";
            out += omc_ref_text(store, key->u.bmff_field.field);
            break;
        case OMC_KEY_JUMBF_FIELD:
            out += "|field=";
            out += omc_ref_text(store, key->u.jumbf_field.field);
            break;
        case OMC_KEY_JUMBF_CBOR_KEY:
            out += "|key=";
            out += omc_ref_text(store, key->u.jumbf_cbor_key.key);
            break;
        case OMC_KEY_PNG_TEXT:
            out += "|keyword=";
            out += omc_ref_text(store, key->u.png_text.keyword);
            out += "|field=";
            out += omc_ref_text(store, key->u.png_text.field);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_cpp_key(const openmeta::MetaStore& store,
                  const openmeta::MetaKey& key)
{
    std::string out;

    out = cpp_key_kind_name(key.kind);
    switch (key.kind) {
        case openmeta::MetaKeyKind::ExifTag:
            out += "|ifd=";
            out += cpp_span_text(store, key.data.exif_tag.ifd);
            out += "|tag=";
            out += std::to_string((unsigned int)key.data.exif_tag.tag);
            break;
        case openmeta::MetaKeyKind::Comment:
            break;
        case openmeta::MetaKeyKind::ExrAttribute:
            out += "|part=";
            out += std::to_string((unsigned int)
                                  key.data.exr_attribute.part_index);
            out += "|name=";
            out += cpp_span_text(store, key.data.exr_attribute.name);
            break;
        case openmeta::MetaKeyKind::IptcDataset:
            out += "|record=";
            out += std::to_string((unsigned int)key.data.iptc_dataset.record);
            out += "|dataset=";
            out += std::to_string((unsigned int)key.data.iptc_dataset.dataset);
            break;
        case openmeta::MetaKeyKind::XmpProperty:
            out += "|schema=";
            out += cpp_span_text(store, key.data.xmp_property.schema_ns);
            out += "|path=";
            out += cpp_span_text(store, key.data.xmp_property.property_path);
            break;
        case openmeta::MetaKeyKind::IccHeaderField:
            out += "|offset=";
            out += std::to_string((unsigned long long)
                                  key.data.icc_header_field.offset);
            break;
        case openmeta::MetaKeyKind::IccTag:
            out += "|sig=";
            out += std::to_string((unsigned long long)key.data.icc_tag.signature);
            break;
        case openmeta::MetaKeyKind::PhotoshopIrb:
            out += "|id=";
            out += std::to_string((unsigned int)
                                  key.data.photoshop_irb.resource_id);
            break;
        case openmeta::MetaKeyKind::GeotiffKey:
            out += "|id=";
            out += std::to_string((unsigned int)key.data.geotiff_key.key_id);
            break;
        case openmeta::MetaKeyKind::PrintImField:
            out += "|field=";
            out += cpp_span_text(store, key.data.printim_field.field);
            break;
        case openmeta::MetaKeyKind::BmffField:
            out += "|field=";
            out += cpp_span_text(store, key.data.bmff_field.field);
            break;
        case openmeta::MetaKeyKind::JumbfField:
            out += "|field=";
            out += cpp_span_text(store, key.data.jumbf_field.field);
            break;
        case openmeta::MetaKeyKind::JumbfCborKey:
            out += "|key=";
            out += cpp_span_text(store, key.data.jumbf_cbor_key.key);
            break;
        case openmeta::MetaKeyKind::PngText:
            out += "|keyword=";
            out += cpp_span_text(store, key.data.png_text.keyword);
            out += "|field=";
            out += cpp_span_text(store, key.data.png_text.field);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_omc_value(const omc_store* store, const omc_val* value)
{
    std::string out;

    out = omc_val_kind_name(value->kind);
    out += "|elem=";
    out += omc_elem_name(value->elem_type);
    out += "|enc=";
    out += omc_text_enc_name(value->text_encoding);
    out += "|count=";
    out += std::to_string((unsigned long long)value->count);

    switch (value->kind) {
        case OMC_VAL_EMPTY:
            break;
        case OMC_VAL_SCALAR:
            out += "|value=";
            switch (value->elem_type) {
                case OMC_ELEM_I8:
                case OMC_ELEM_I16:
                case OMC_ELEM_I32:
                case OMC_ELEM_I64:
                    out += std::to_string((long long)value->u.i64);
                    break;
                case OMC_ELEM_F32_BITS:
                    out += std::to_string((unsigned long long)value->u.f32_bits);
                    break;
                case OMC_ELEM_F64_BITS:
                    out += std::to_string((unsigned long long)value->u.f64_bits);
                    break;
                case OMC_ELEM_URATIONAL:
                    out += std::to_string((unsigned long long)
                                          value->u.ur.numer);
                    out += "/";
                    out += std::to_string((unsigned long long)
                                          value->u.ur.denom);
                    break;
                case OMC_ELEM_SRATIONAL:
                    out += std::to_string((long long)value->u.sr.numer);
                    out += "/";
                    out += std::to_string((long long)value->u.sr.denom);
                    break;
                default:
                    out += std::to_string((unsigned long long)value->u.u64);
                    break;
            }
            break;
        case OMC_VAL_ARRAY:
        case OMC_VAL_BYTES:
            out += "|hex=";
            out += omc_ref_hex(store, value->u.ref);
            break;
        case OMC_VAL_TEXT:
            out += "|text=";
            out += omc_ref_text(store, value->u.ref);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_cpp_value(const openmeta::MetaStore& store,
                    const openmeta::MetaValue& value)
{
    std::string out;

    out = cpp_val_kind_name(value.kind);
    out += "|elem=";
    out += cpp_elem_name(value.elem_type);
    out += "|enc=";
    out += cpp_text_enc_name(value.text_encoding);
    out += "|count=";
    out += std::to_string((unsigned long long)value.count);

    switch (value.kind) {
        case openmeta::MetaValueKind::Empty:
            break;
        case openmeta::MetaValueKind::Scalar:
            out += "|value=";
            switch (value.elem_type) {
                case openmeta::MetaElementType::I8:
                case openmeta::MetaElementType::I16:
                case openmeta::MetaElementType::I32:
                case openmeta::MetaElementType::I64:
                    out += std::to_string((long long)value.data.i64);
                    break;
                case openmeta::MetaElementType::F32:
                    out += std::to_string((unsigned long long)value.data.f32_bits);
                    break;
                case openmeta::MetaElementType::F64:
                    out += std::to_string((unsigned long long)value.data.f64_bits);
                    break;
                case openmeta::MetaElementType::URational:
                    out += std::to_string((unsigned long long)value.data.ur.numer);
                    out += "/";
                    out += std::to_string((unsigned long long)value.data.ur.denom);
                    break;
                case openmeta::MetaElementType::SRational:
                    out += std::to_string((long long)value.data.sr.numer);
                    out += "/";
                    out += std::to_string((long long)value.data.sr.denom);
                    break;
                default:
                    out += std::to_string((unsigned long long)value.data.u64);
                    break;
            }
            break;
        case openmeta::MetaValueKind::Array:
        case openmeta::MetaValueKind::Bytes:
            out += "|hex=";
            out += cpp_span_hex(store, value.data.span);
            break;
        case openmeta::MetaValueKind::Text:
            out += "|text=";
            out += cpp_span_text(store, value.data.span);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_omc_flags(omc_entry_flags flags)
{
    std::string out;

    if (flags == OMC_ENTRY_FLAG_NONE) {
        return "None";
    }
    if ((flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
        out += "Deleted,";
    }
    if ((flags & OMC_ENTRY_FLAG_DIRTY) != 0U) {
        out += "Dirty,";
    }
    if ((flags & OMC_ENTRY_FLAG_DERIVED) != 0U) {
        out += "Derived,";
    }
    if ((flags & OMC_ENTRY_FLAG_TRUNCATED) != 0U) {
        out += "Truncated,";
    }
    if ((flags & OMC_ENTRY_FLAG_UNREADABLE) != 0U) {
        out += "Unreadable,";
    }
    if (!out.empty()) {
        out.pop_back();
    }
    return out;
}

static std::string
canonical_cpp_flags(openmeta::EntryFlags flags)
{
    std::string out;

    if (flags == openmeta::EntryFlags::None) {
        return "None";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Deleted)) {
        out += "Deleted,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Dirty)) {
        out += "Dirty,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Derived)) {
        out += "Derived,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Truncated)) {
        out += "Truncated,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Unreadable)) {
        out += "Unreadable,";
    }
    if (!out.empty()) {
        out.pop_back();
    }
    return out;
}

static std::string
canonical_omc_entry(const omc_store* store, const omc_entry* entry)
{
    std::string out;

    out = canonical_omc_key(store, &entry->key);
    out += "||";
    out += canonical_omc_value(store, &entry->value);
    out += "||flags=";
    out += canonical_omc_flags(entry->flags);
    return out;
}

static std::string
canonical_cpp_entry(const openmeta::MetaStore& store,
                    const openmeta::Entry& entry)
{
    std::string out;

    out = canonical_cpp_key(store, entry.key);
    out += "||";
    out += canonical_cpp_value(store, entry.value);
    out += "||flags=";
    out += canonical_cpp_flags(entry.flags);
    return out;
}

static std::vector<std::string>
read_omc_records(const ByteVec& file_bytes, bool decode_makernote)
{
    omc_store store;
    omc_read_res res;
    omc_read_opts opts;
    std::array<omc_blk_ref, 128> blocks {};
    std::array<omc_exif_ifd_ref, 128> ifds {};
    std::array<omc_u8, 65536> payload {};
    std::array<omc_u32, 256> scratch {};
    std::vector<std::string> out;
    omc_size i;

    omc_store_init(&store);
    omc_read_opts_init(&opts);
    if (decode_makernote) {
        opts.exif.decode_makernote = 1;
    }
    res = omc_read_simple(file_bytes.data(), (omc_size)file_bytes.size(),
                          &store, blocks.data(),
                          (omc_u32)blocks.size(), ifds.data(),
                          (omc_u32)ifds.size(), payload.data(),
                          (omc_size)payload.size(), scratch.data(),
                          (omc_u32)scratch.size(), &opts);
    if (res.scan.status == OMC_SCAN_MALFORMED) {
        std::fprintf(stderr, "omc scan failed with malformed status\n");
        std::exit(1);
    }

    out.reserve((std::size_t)store.entry_count);
    for (i = 0U; i < store.entry_count; ++i) {
        out.push_back(canonical_omc_entry(&store, &store.entries[i]));
    }
    omc_store_fini(&store);
    std::sort(out.begin(), out.end());
    return out;
}

static std::vector<std::string>
read_cpp_records(const ByteVec& file_bytes, bool decode_makernote)
{
    openmeta::MetaStore store;
    openmeta::SimpleMetaDecodeOptions opts {};
    std::array<openmeta::ContainerBlockRef, 128> blocks {};
    std::array<openmeta::ExifIfdRef, 128> ifds {};
    std::array<std::byte, 65536> payload {};
    std::array<std::uint32_t, 256> scratch {};
    openmeta::SimpleMetaResult res;
    std::vector<std::string> out;
    std::size_t i;

    if (decode_makernote) {
        opts.exif.decode_makernote = true;
    }

    res = openmeta::simple_meta_read(as_byte_span(file_bytes), store, blocks,
                                     ifds, payload, scratch, opts);
    if (res.scan.status == openmeta::ScanStatus::Malformed) {
        std::fprintf(stderr, "openmeta scan failed with malformed status\n");
        std::exit(1);
    }

    out.reserve(store.entries().size());
    for (i = 0U; i < store.entries().size(); ++i) {
        out.push_back(canonical_cpp_entry(store, store.entries()[i]));
    }
    std::sort(out.begin(), out.end());
    return out;
}

static bool
compare_records(const char* case_name, const std::vector<std::string>& omc,
                const std::vector<std::string>& cpp)
{
    std::size_t i;
    std::size_t j;
    unsigned shown;

    if (omc == cpp) {
        return true;
    }

    std::fprintf(stderr, "%s parity mismatch\n", case_name);
    std::fprintf(stderr, "  omc entries: %zu\n", omc.size());
    std::fprintf(stderr, "  cpp entries: %zu\n", cpp.size());

    i = 0U;
    j = 0U;
    shown = 0U;
    while ((i < omc.size() || j < cpp.size()) && shown < 16U) {
        if (i < omc.size() && j < cpp.size() && omc[i] == cpp[j]) {
            i += 1U;
            j += 1U;
            continue;
        }
        if (j >= cpp.size()
            || (i < omc.size() && omc[i] < cpp[j])) {
            std::fprintf(stderr, "  only-omc: %s\n", omc[i].c_str());
            i += 1U;
            shown += 1U;
            continue;
        }
        std::fprintf(stderr, "  only-cpp: %s\n", cpp[j].c_str());
        j += 1U;
        shown += 1U;
    }
    return false;
}

static bool
run_case(const char* case_name, const ByteVec& file_bytes,
         bool decode_makernote)
{
    std::vector<std::string> omc;
    std::vector<std::string> cpp;

    omc = read_omc_records(file_bytes, decode_makernote);
    cpp = read_cpp_records(file_bytes, decode_makernote);
    return compare_records(case_name, omc, cpp);
}

}  // namespace

int
main()
{
    bool ok;

    ok = true;
    ok = run_case("jpeg_comment", build_jpeg_comment_fixture(), false) && ok;
    ok = run_case("jpeg_all", build_jpeg_all_fixture(), false) && ok;
    ok = run_case("png_text", build_png_text_fixture(), false) && ok;
    ok = run_case("tiff_geotiff", build_tiff_geotiff_fixture(), false) && ok;
    ok = run_case("tiff_printim", build_tiff_printim_fixture(), false) && ok;
    ok = run_case("tiff_fuji_makernote",
                  build_tiff_fuji_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_flir_makernote",
                  build_tiff_flir_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nintendo_makernote",
                  build_tiff_nintendo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_hp_type6_makernote",
                  build_tiff_hp_type6_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_makernote",
                  build_tiff_sony_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9050b_makernote",
                  build_tiff_sony_tag9050b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag2010i_makernote",
                  build_tiff_sony_tag2010i_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_shotinfo_makernote",
                  build_tiff_sony_shotinfo_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag202a_makernote",
                  build_tiff_sony_tag202a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9405b_makernote",
                  build_tiff_sony_tag9405b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9416_makernote",
                  build_tiff_sony_tag9416_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9050a_makernote",
                  build_tiff_sony_tag9050a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9050c_makernote",
                  build_tiff_sony_tag9050c_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag2010b_makernote",
                  build_tiff_sony_tag2010b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag2010e_makernote",
                  build_tiff_sony_tag2010e_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9400a_makernote",
                  build_tiff_sony_tag9400a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9404b_makernote",
                  build_tiff_sony_tag9404b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9405a_makernote",
                  build_tiff_sony_tag9405a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag940e_makernote",
                  build_tiff_sony_tag940e_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag940e_afinfo_makernote",
                  build_tiff_sony_tag940e_afinfo_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_makernote",
                  build_tiff_canon_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_custom_functions2_makernote",
                  build_tiff_canon_custom_functions2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_afinfo2_makernote",
                  build_tiff_canon_afinfo2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_filterinfo_makernote",
                  build_tiff_canon_filterinfo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_timeinfo_makernote",
                  build_tiff_canon_timeinfo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_camera_info_psinfo_makernote",
                  build_tiff_canon_camera_info_psinfo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_colordata8_makernote",
                  build_tiff_canon_colordata8_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_makernote",
                  build_tiff_nikon_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_binary_makernote",
                  build_tiff_nikon_binary_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_info_blocks_makernote",
                  build_tiff_nikon_info_blocks_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_preview_settings_aftune_makernote",
                  build_tiff_nikon_preview_settings_aftune_fixture(), true)
         && ok;

    return ok ? 0 : 1;
}
