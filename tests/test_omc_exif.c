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
    append_bytes(out, &size, "II");
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
    append_bytes(out, &size, make);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, model);
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
    append_raw(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, "II");
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
    append_bytes(out, &size, "0101");
    append_u8(out, &size, 1U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_dist_pos, off_payload);
    append_bytes(out, &size, "0100");
    append_u8(out, &size, 1U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_flash_pos, off_payload);
    append_bytes(out, &size, "0106");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0xAAU);
    append_u8(out, &size, 0xBBU);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_multi_pos, off_payload);
    append_bytes(out, &size, "0100");
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 0U);
    append_u32le(out, &size, 3U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_afinfo_pos, off_payload);
    append_bytes(out, &size, "0100");
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
    append_bytes(out, &size, "0100");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u16le(out, &size, 99U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_retouch_pos, off_payload);
    append_bytes(out, &size, "0100");
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
    append_raw(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, "II");
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
    append_bytes(out, &size, "0200");
    append_bytes(out, &size, "NEUTRAL");
    for (i = 0U; i < 13U; ++i) {
        append_u8(out, &size, 0U);
    }
    append_bytes(out, &size, "STANDARD");
    for (i = 0U; i < 12U; ++i) {
        append_u8(out, &size, 0U);
    }
    for (i = 0U; i < 21U; ++i) {
        append_u8(out, &size, 0U);
    }
    append_u8(out, &size, 15U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_iso_pos, off_payload);
    append_bytes(out, &size, "0100");
    append_u32le(out, &size, 0U);
    append_u16le(out, &size, 400U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_hdr_pos, off_payload);
    append_bytes(out, &size, "0100");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 4U);

    off_payload = (omc_u32)(size - 10U);
    write_u32le_at(out, off_loc_pos, off_payload);
    append_bytes(out, &size, "0100");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, "TOKYO-JP");

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
    append_raw(out, &size, "Nikon\0", 6U);
    append_u8(out, &size, 2U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    append_bytes(out, &size, "II");
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
    append_raw(out, &size, cam, cam_size);
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
    append_raw(out, &size, "SONY", 4U);
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
    append_raw(out, &size, "Standard", 8U);
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
    append_raw(out, &size, blob, sizeof(blob));
    return size;
}

static omc_size
make_sony_makernote_tag9400_ciphered(omc_u8* out)
{
    omc_u8 plain[0x80];

    memset(plain, 0, sizeof(plain));
    plain[0x0000U] = 0x07U;
    plain[0x0009U] = 1U;
    plain[0x000AU] = 2U;
    plain[0x0016U] = 3U;
    plain[0x001EU] = 4U;
    plain[0x0029U] = 5U;
    plain[0x002AU] = 6U;
    plain[0x0012U] = 0x44U;
    plain[0x0013U] = 0x33U;
    plain[0x0014U] = 0x22U;
    plain[0x0015U] = 0x11U;
    plain[0x001AU] = 0x88U;
    plain[0x001BU] = 0x77U;
    plain[0x001CU] = 0x66U;
    plain[0x001DU] = 0x55U;
    plain[0x0053U] = 0xE7U;
    plain[0x0054U] = 0x07U;
    return make_sony_makernote_ciphered_blob(out, 0x9400U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag9405b_ciphered(omc_u8* out)
{
    omc_u8 plain[0x84];
    omc_u32 i;

    memset(plain, 0, sizeof(plain));
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
        write_u16le_at(plain, 0x0064U + (i * 2U), (omc_u16)(i - 8U));
    }
    return make_sony_makernote_ciphered_blob(out, 0x9405U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag9416_ciphered(omc_u8* out)
{
    omc_u8 plain[0x96];
    omc_u32 i;

    memset(plain, 0, sizeof(plain));
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
        write_u16le_at(plain, 0x004FU + (i * 2U), (omc_u16)(20U + i));
    }
    return make_sony_makernote_ciphered_blob(out, 0x9416U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag9400a_ciphered(omc_u8* out)
{
    omc_u8 plain[0x53];

    memset(plain, 0, sizeof(plain));
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
    return make_sony_makernote_ciphered_blob(out, 0x9400U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag202a_ciphered(omc_u8* out)
{
    omc_u8 plain[2];

    memset(plain, 0, sizeof(plain));
    plain[0x0001U] = 7U;
    return make_sony_makernote_ciphered_blob(out, 0x202AU, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag9404b_ciphered(omc_u8* out)
{
    omc_u8 plain[0x20];

    memset(plain, 0, sizeof(plain));
    plain[0x000CU] = 12U;
    plain[0x000EU] = 14U;
    write_u16le_at(plain, 0x001EU, 0x2345U);
    return make_sony_makernote_ciphered_blob(out, 0x9404U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag9050a_ciphered(omc_u8* out)
{
    omc_u8 plain[0x01C1];

    memset(plain, 0, sizeof(plain));
    plain[0x0000U] = 1U;
    plain[0x0001U] = 2U;
    write_u16le_at(plain, 0x0020U, 10U);
    write_u16le_at(plain, 0x0022U, 20U);
    write_u16le_at(plain, 0x0024U, 30U);
    plain[0x0031U] = 0x31U;
    write_u32le_at(plain, 0x0032U, 0x44332211U);
    write_u16le_at(plain, 0x003AU, 0x1234U);
    write_u16le_at(plain, 0x003CU, 0x5678U);
    plain[0x003FU] = 0x3FU;
    plain[0x0067U] = 0x67U;
    plain[0x0105U] = 0x15U;
    plain[0x0106U] = 0x16U;
    write_u16le_at(plain, 0x0107U, 0x0107U);
    write_u16le_at(plain, 0x0109U, 0x0109U);
    plain[0x010BU] = 0x1BU;
    plain[0x0114U] = 0x24U;
    write_u32le_at(plain, 0x01AAU, 0x89ABCDEFU);
    write_u32le_at(plain, 0x01BDU, 0x10203040U);
    return make_sony_makernote_ciphered_blob(out, 0x9050U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag9405a_ciphered(omc_u8* out)
{
    omc_u8 plain[0x06EA];
    omc_u32 i;

    memset(plain, 0, sizeof(plain));
    plain[0x0600U] = 1U;
    plain[0x0601U] = 2U;
    plain[0x0603U] = 3U;
    plain[0x0604U] = 4U;
    write_u16le_at(plain, 0x0605U, 0x0605U);
    write_u16le_at(plain, 0x0608U, 0x0608U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain, 0x064AU + (i * 2U), (omc_u16)(30U + i));
    }
    for (i = 0U; i < 32U; ++i) {
        write_u16le_at(plain, 0x066AU + (i * 2U), (omc_u16)(i - 20U));
    }
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain, 0x06CAU + (i * 2U), (omc_u16)(100U + i));
    }
    return make_sony_makernote_ciphered_blob(out, 0x9405U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag2010b_ciphered(omc_u8* out)
{
    omc_u8 plain[0x1A50];
    omc_u32 i;

    memset(plain, 0, sizeof(plain));
    write_u32le_at(plain, 0x0000U, 0x11223344U);
    write_u32le_at(plain, 0x0004U, 0x55667788U);
    write_u32le_at(plain, 0x0008U, 0x99AABBCCU);
    plain[0x0324U] = 0x24U;
    plain[0x1128U] = 0x28U;
    plain[0x112CU] = 0x2CU;
    write_u16le_at(plain, 0x113EU, 0x113EU);
    write_u16le_at(plain, 0x1140U, 0x1140U);
    write_u16le_at(plain, 0x1218U, 0x1218U);
    write_u16le_at(plain, 0x114CU, (omc_u16)-7);
    write_u16le_at(plain, 0x1180U, 1U);
    write_u16le_at(plain, 0x1182U, 2U);
    write_u16le_at(plain, 0x1184U, 3U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain, 0x1A23U + (i * 2U), (omc_u16)(200U + i));
    }
    for (i = 0U; i < 0x798U; ++i) {
        plain[0x04B4U + i] = (omc_u8)i;
    }
    return make_sony_makernote_ciphered_blob(out, 0x2010U, plain,
                                             sizeof(plain));
}

static omc_size
make_sony_makernote_tag2010e_ciphered(omc_u8* out)
{
    omc_u8 plain[0x1A90];
    omc_u32 i;

    memset(plain, 0, sizeof(plain));
    write_u32le_at(plain, 0x0000U, 0xA1B2C3D4U);
    write_u32le_at(plain, 0x0004U, 0x10213243U);
    write_u32le_at(plain, 0x0008U, 0x55667788U);
    plain[0x021CU] = 0x21U;
    plain[0x0328U] = 0x32U;
    plain[0x115CU] = 0x5CU;
    plain[0x1160U] = 0x60U;
    write_u16le_at(plain, 0x1172U, 0x1172U);
    write_u16le_at(plain, 0x1174U, 0x1174U);
    write_u16le_at(plain, 0x1254U, 0x1254U);
    write_u16le_at(plain, 0x1180U, (omc_u16)-9);
    write_u16le_at(plain, 0x11B4U, 4U);
    write_u16le_at(plain, 0x11B6U, 5U);
    write_u16le_at(plain, 0x11B8U, 6U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain, 0x1870U + (i * 2U), (omc_u16)(300U + i));
    }
    plain[0x1891U] = 0x91U;
    plain[0x1892U] = 0x92U;
    write_u16le_at(plain, 0x1893U, 0x1893U);
    write_u16le_at(plain, 0x1896U, 0x1896U);
    plain[0x192CU] = 0x2CU;
    plain[0x1A88U] = 0x88U;
    for (i = 0U; i < 0x798U; ++i) {
        plain[0x04B8U + i] = (omc_u8)(0x80U + i);
    }
    return make_sony_makernote_ciphered_blob(out, 0x2010U, plain,
                                             sizeof(plain));
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

static const omc_entry*
find_exif_entry_typed(const omc_store* store, const char* ifd_name,
                      omc_u16 tag, omc_val_kind kind, omc_elem_type elem_type)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ifd;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        if (entry->key.u.exif_tag.tag != tag || entry->value.kind != kind
            || entry->value.elem_type != elem_type) {
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

static void
test_nikon_makernote_binary_subdirs(void)
{
    omc_u8 makernote[512];
    omc_u8 tiff[768];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;
    omc_const_bytes view;

    makernote_size = make_nikon_makernote_with_binary_subdirs(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Nikon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_nikon_distortinfo_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert((entry->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 1U);

    entry = find_exif_entry(&store, "mk_nikon_flashinfo0106_0", 0x0006U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.count == 2U);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 2U);
    assert(view.data[0] == 0xAAU);
    assert(view.data[1] == 0xBBU);

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

    entry = find_exif_entry(&store, "mk_nikon_multiexposure_0", 0x0003U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 3U);

    entry = find_exif_entry(&store, "mk_nikon_afinfo2v0100_0", 0x0008U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 5U);
    assert(view.data[0] == 0xAAU);
    assert(view.data[1] == 0xBBU);
    assert(view.data[2] == 0xCCU);
    assert(view.data[3] == 0xDDU);
    assert(view.data[4] == 0xEEU);

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

    entry = find_exif_entry(&store, "mk_nikon_afinfo2v0100_0", 0x001CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 1U);

    entry = find_exif_entry(&store, "mk_nikon_fileinfo_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_nikon_fileinfo_0", 0x0003U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 99U);

    entry = find_exif_entry(&store, "mk_nikon_retouchinfo_0", 0x0005U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I8);
    assert(entry->value.u.i64 == -1);

    omc_store_fini(&store);
}

static void
test_nikon_makernote_info_blocks(void)
{
    omc_u8 makernote[512];
    omc_u8 tiff[768];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;
    omc_const_bytes view;

    makernote_size = make_nikon_makernote_with_info_blocks(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Nikon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_nikon_picturecontrol2_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 7U);
    assert(memcmp(view.data, "NEUTRAL", 7U) == 0);

    entry = find_exif_entry(&store, "mk_nikon_picturecontrol2_0", 0x0041U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 15U);

    entry = find_exif_entry(&store, "mk_nikon_worldtime_0", 0x0000U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == -540);

    entry = find_exif_entry(&store, "mk_nikon_hdrinfo_0", 0x0007U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 4U);

    entry = find_exif_entry(&store, "mk_nikon_locationinfo_0", 0x0009U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == 8U);
    assert(memcmp(view.data, "TOKYO-JP", 8U) == 0);

    omc_store_fini(&store);
}

static void
test_canon_custom_functions2_makernote(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_custom_functions2_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canoncustom_functions2_0", 0x0101U);
    assert(entry != (const omc_entry*)0);
    assert((entry->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0U);

    omc_store_fini(&store);
}

static void
test_canon_afinfo2_makernote(void)
{
    omc_u8 makernote[256];
    omc_u8 tiff[512];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_afinfo2_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_afinfo2_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert((entry->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 9U);

    entry = find_exif_entry(&store, "mk_canon_afinfo2_0", 0x0008U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.count == 9U);

    entry = find_exif_entry(&store, "mk_canon_afinfo2_0", 0x260AU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.count == 9U);

    entry = find_exif_entry(&store, "mk_canon_afinfo2_0", 0x000EU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0U);

    entry = find_exif_entry(&store, "mk_canon_afinfo2_0", 0x260EU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0U);

    omc_store_fini(&store);
}

static void
test_nikon_preview_settings_and_aftune_makernote(void)
{
    omc_u8 makernote[512];
    omc_u8 tiff[768];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_nikon_makernote_with_preview_settings_and_aftune(
        makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Nikon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_nikonsettings_main_0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 6400U);

    entry = find_exif_entry(&store, "mk_nikonsettings_main_0", 0x0046U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 1U);

    entry = find_exif_entry(&store, "mk_nikon_aftune_0", 0x0002U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I8);
    assert(entry->value.u.i64 == -3);

    entry = find_exif_entry(&store, "mk_nikon_aftune_0", 0x0003U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I8);
    assert(entry->value.u.i64 == 5);

    omc_store_fini(&store);
}

static void
test_sony_makernote_and_postpass(void)
{
    omc_u8 makernote[8192];
    omc_u8 tiff[16384];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;
    omc_const_bytes view;

    makernote_size = make_sony_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag2010i_0", 0x0217U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == 0x1234);
    entry = find_exif_entry(&store, "mk_sony_tag2010i_0", 0x0252U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.count == 3U);
    entry = find_exif_entry(&store, "mk_sony_tag2010i_0", 0x17D0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    assert(entry->value.count == 32U);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
test_sony_model_selected_variants(void)
{
    omc_u8 makernote[8192];
    omc_u8 tiff[16384];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_sony_makernote_tag9050a_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Sony", "Lunar", makernote, makernote_size,
        (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9050a_0", 0x0020U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.count == 3U);
    entry = find_exif_entry(&store, "mk_sony_tag9050a_0", 0x01AAU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x89ABCDEFU);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag9050b_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Sony", "ILCE-7M4", makernote, makernote_size,
        (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9050c_0", 0x0026U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.count == 3U);
    entry = find_exif_entry(&store, "mk_sony_tag9050c_0", 0x003AU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x44332211U);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag2010b_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Sony", "Lunar", makernote, makernote_size,
        (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag2010b_0", 0x114CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == -7);
    entry = find_exif_entry(&store, "mk_sony_meterinfo_0", 0x0000U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    assert(entry->value.count == 0x006CU);
    assert(find_exif_entry(&store, "mk_sony_meterinfo9_0", 0x0000U)
           == (const omc_entry*)0);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag2010e_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Sony", "Stellar", makernote, makernote_size,
        (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag2010e_0", 0x1180U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == -9);
    entry = find_exif_entry(&store, "mk_sony_meterinfo_0", 0x0000U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    assert(entry->value.count == 0x006CU);
    assert(find_exif_entry(&store, "mk_sony_meterinfo9_0", 0x0000U)
           == (const omc_entry*)0);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag9400a_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Sony", "SLT-A99", makernote, makernote_size,
        (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9400a_0", 0x0008U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x01020304U);
    entry = find_exif_entry(&store, "mk_sony_tag9400a_0", 0x0044U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 3000U);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag9404b_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Sony", "Lunar", makernote, makernote_size,
        (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9404b_0", 0x001EU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0x2345U);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag9405a_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Sony", "SLT-A99", makernote, makernote_size,
        (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9405a_0", 0x0605U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 0x0605U);
    entry = find_exif_entry(&store, "mk_sony_tag9405a_0", 0x066AU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.count == 32U);
    omc_store_fini(&store);
}

static void
test_sony_extra_derived_blocks(void)
{
    omc_u8 makernote[8192];
    omc_u8 tiff[16384];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_sony_makernote_tag202a_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag202a_0", 0x0001U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag9405b_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9405b_0", 0x0010U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_URATIONAL);
    assert(entry->value.u.ur.numer == 1U);
    assert(entry->value.u.ur.denom == 250U);
    entry = find_exif_entry(&store, "mk_sony_tag9405b_0", 0x0064U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.count == 16U);
    omc_store_fini(&store);

    makernote_size = make_sony_makernote_tag9416_ciphered(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Sony", makernote, makernote_size, (omc_u32)makernote_size);
    assert(patch_sony_makernote_value_offset_in_tiff(tiff, tiff_size));
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_sony_tag9416_0", 0x000CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_URATIONAL);
    assert(entry->value.u.ur.numer == 5U);
    assert(entry->value.u.ur.denom == 8U);
    entry = find_exif_entry(&store, "mk_sony_tag9416_0", 0x004FU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.count == 16U);
    omc_store_fini(&store);
}

static void
test_canon_filterinfo_makernote(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;
    omc_const_bytes view;
    omc_u32 vals[2];

    makernote_size = make_canon_filterinfo_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

    entry = find_exif_entry(&store, "mk_canon_filterinfo_0", 0x0402U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 2U);

    entry = find_exif_entry(&store, "mk_canon_filterinfo_0", 0x0403U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.count == 2U);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size >= 8U);
    memcpy(vals, view.data, sizeof(vals));
    assert(vals[0] == 300U);
    assert(vals[1] == 700U);

    omc_store_fini(&store);
}

static void
test_canon_timeinfo_makernote(void)
{
    omc_u8 makernote[128];
    omc_u8 tiff[256];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_timeinfo_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID,
                       (omc_exif_ifd_ref*)0, 0U, &opts);
    assert(res.status == OMC_EXIF_OK);

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

    entry = find_exif_entry(&store, "mk_canon_timeinfo_0", 0x0003U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 1U);

    omc_store_fini(&store);
}

static void
test_canon_camera_info_psinfo_makernote(void)
{
    omc_u8 makernote[2048];
    omc_u8 tiff[4096];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_camera_info_psinfo_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);

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

    entry = find_exif_entry(&store, "mk_canon_camerainfo_0", 0x0095U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(omc_arena_view(&store.arena, entry->value.u.ref).size == 0U);

    entry = find_exif_entry(&store, "mk_canon_psinfo_0", 0x0004U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_I32);
    assert(entry->value.u.i64 == 3);

    entry = find_exif_entry(&store, "mk_canon_psinfo_0", 0x00D8U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 129U);

    omc_store_fini(&store);
}

static void
test_canon_colordata8_makernote(void)
{
    omc_u8 makernote[4096];
    omc_u8 tiff[8192];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_colordata8_makernote(makernote);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);

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
test_canon_camera_info_model_and_psinfo2_makernote(void)
{
    omc_u8 cam[1024];
    omc_u8 makernote[2048];
    omc_u8 tiff[4096];
    omc_size cam_size;
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);

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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);

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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);

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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;

    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);

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
test_canon_camera_info_additional_cohorts_makernote(void)
{
    omc_u8 cam[2048];
    omc_u8 makernote[4096];
    omc_u8 tiff[8192];
    omc_size cam_size;
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
test_canon_colordata_counted_families_makernote(void)
{
    omc_u8 makernote[8192];
    omc_u8 tiff[16384];
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;

    makernote_size = make_canon_colordata_counted_makernote(makernote, 1338U,
                                                            7U);
    tiff_size = make_test_tiff_with_make_and_makernote_count(
        tiff, "Canon", makernote, makernote_size, (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
test_canon_camera_info_extended_fixed_fields_makernote(void)
{
    omc_u8 cam[2048];
    omc_u8 makernote[2048];
    omc_u8 tiff[4096];
    omc_size cam_size;
    omc_size makernote_size;
    omc_size tiff_size;
    omc_store store;
    omc_exif_ifd_ref ifds[8];
    omc_exif_opts opts;
    omc_exif_res res;
    const omc_entry* entry;
    omc_const_bytes view;

    cam_size = make_canon_camera_info_blob_with_u16(cam, 0x0048U, 5300U);
    makernote_size = make_canon_camera_info_blob_makernote(makernote, cam,
                                                           cam_size);
    tiff_size = make_test_tiff_with_make_model_and_makernote_count(
        tiff, "Canon", "Canon EOS-1DS", makernote, makernote_size,
        (omc_u32)makernote_size);
    omc_store_init(&store);
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
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
    omc_exif_opts_init(&opts);
    opts.decode_makernote = 1;
    res = omc_exif_dec(tiff, tiff_size, &store, OMC_INVALID_BLOCK_ID, ifds, 8U,
                       &opts);
    assert(res.status == OMC_EXIF_OK);
    entry = find_exif_entry(&store, "mk_canon_camerainfo5d_0", 0x011CU);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 0x01020304U);
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
    test_nikon_makernote_binary_subdirs();
    test_nikon_makernote_info_blocks();
    test_canon_custom_functions2_makernote();
    test_canon_afinfo2_makernote();
    test_nikon_preview_settings_and_aftune_makernote();
    test_sony_makernote_and_postpass();
    test_sony_model_selected_variants();
    test_sony_extra_derived_blocks();
    test_canon_filterinfo_makernote();
    test_canon_timeinfo_makernote();
    test_canon_camera_info_psinfo_makernote();
    test_canon_colordata8_makernote();
    test_canon_colordata_counted_families_makernote();
    test_canon_camera_info_model_and_psinfo2_makernote();
    test_canon_camera_info_additional_cohorts_makernote();
    test_canon_camera_info_extended_fixed_fields_makernote();
    return 0;
}
