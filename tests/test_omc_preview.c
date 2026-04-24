#include "omc/omc_preview.h"

#include "omc_test_assert.h"

static void
append_u8(omc_u8* out, omc_size cap, omc_size* size, omc_u8 value)
{
    OMC_TEST_REQUIRE(out != (omc_u8*)0);
    OMC_TEST_REQUIRE(size != (omc_size*)0);
    OMC_TEST_REQUIRE(*size < cap);
    out[*size] = value;
    *size += 1U;
}

static void
append_u16le(omc_u8* out, omc_size cap, omc_size* size, omc_u16 value)
{
    append_u8(out, cap, size, (omc_u8)(value & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 8) & 0xFFU));
}

static void
append_u32le(omc_u8* out, omc_size cap, omc_size* size, omc_u32 value)
{
    append_u8(out, cap, size, (omc_u8)(value & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 24) & 0xFFU));
}

static void
append_u32be(omc_u8* out, omc_size cap, omc_size* size, omc_u32 value)
{
    append_u8(out, cap, size, (omc_u8)((value >> 24) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)(value & 0xFFU));
}

static void
append_u64be(omc_u8* out, omc_size cap, omc_size* size, omc_u64 value)
{
    append_u8(out, cap, size, (omc_u8)((value >> 56) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 48) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 40) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 32) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 24) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, cap, size, (omc_u8)(value & 0xFFU));
}

static void
append_text(omc_u8* out, omc_size cap, omc_size* size, const char* text)
{
    omc_size i;

    OMC_TEST_REQUIRE(text != (const char*)0);
    for (i = 0U; text[i] != '\0'; ++i) {
        append_u8(out, cap, size, (omc_u8)text[i]);
    }
}

static omc_size
make_tiff_with_ifd1_jpeg_preview(omc_u8* out, omc_size cap)
{
    omc_size size;

    size = 0U;
    append_text(out, cap, &size, "II");
    append_u16le(out, cap, &size, 42U);
    append_u32le(out, cap, &size, 8U);

    append_u16le(out, cap, &size, 0U);
    append_u32le(out, cap, &size, 14U);

    append_u16le(out, cap, &size, 2U);
    append_u16le(out, cap, &size, 0x0201U);
    append_u16le(out, cap, &size, 4U);
    append_u32le(out, cap, &size, 1U);
    append_u32le(out, cap, &size, 44U);

    append_u16le(out, cap, &size, 0x0202U);
    append_u16le(out, cap, &size, 4U);
    append_u32le(out, cap, &size, 1U);
    append_u32le(out, cap, &size, 4U);

    append_u32le(out, cap, &size, 0U);

    append_u8(out, cap, &size, 0xFFU);
    append_u8(out, cap, &size, 0xD8U);
    append_u8(out, cap, &size, 0xFFU);
    append_u8(out, cap, &size, 0xD9U);
    return size;
}

static omc_size
make_tiff_with_jpg_from_raw(omc_u8* out, omc_size cap, int jpeg_soi)
{
    omc_size size;

    size = 0U;
    append_text(out, cap, &size, "II");
    append_u16le(out, cap, &size, 42U);
    append_u32le(out, cap, &size, 8U);

    append_u16le(out, cap, &size, 1U);
    append_u16le(out, cap, &size, 0x002EU);
    append_u16le(out, cap, &size, 7U);
    append_u32le(out, cap, &size, 6U);
    append_u32le(out, cap, &size, 26U);
    append_u32le(out, cap, &size, 0U);

    if (jpeg_soi) {
        append_u8(out, cap, &size, 0xFFU);
        append_u8(out, cap, &size, 0xD8U);
    } else {
        append_u8(out, cap, &size, 0x00U);
        append_u8(out, cap, &size, 0x11U);
    }
    append_u8(out, cap, &size, 0x01U);
    append_u8(out, cap, &size, 0x02U);
    append_u8(out, cap, &size, 0xFFU);
    append_u8(out, cap, &size, 0xD9U);
    return size;
}

static omc_size
make_cr3_with_uuid_prvw_jpeg_preview(omc_u8* out, omc_size cap)
{
    static const omc_u8 k_uuid[16] = {
        0xEAU, 0xF4U, 0x2BU, 0x5EU, 0x1CU, 0x98U, 0x4BU, 0x88U,
        0xB9U, 0xFBU, 0xB7U, 0xDCU, 0x40U, 0x6EU, 0x4DU, 0x16U
    };
    omc_size size;
    omc_size i;

    size = 0U;

    append_u32be(out, cap, &size, 24U);
    append_text(out, cap, &size, "ftyp");
    append_text(out, cap, &size, "crx ");
    append_u32be(out, cap, &size, 0U);
    append_text(out, cap, &size, "crx ");
    append_text(out, cap, &size, "isom");

    append_u32be(out, cap, &size, 60U);
    append_text(out, cap, &size, "uuid");
    for (i = 0U; i < sizeof(k_uuid); ++i) {
        append_u8(out, cap, &size, k_uuid[i]);
    }

    append_u64be(out, cap, &size, 1U);
    append_u32be(out, cap, &size, 28U);
    append_text(out, cap, &size, "PRVW");

    for (i = 0U; i < 12U; ++i) {
        append_u8(out, cap, &size, 0U);
    }

    append_u32be(out, cap, &size, 4U);
    append_u8(out, cap, &size, 0xFFU);
    append_u8(out, cap, &size, 0xD8U);
    append_u8(out, cap, &size, 0xFFU);
    append_u8(out, cap, &size, 0xD9U);
    return size;
}

static void
test_preview_find_exif_jpeg_interchange_candidate(void)
{
    omc_u8 bytes[128];
    omc_size size;
    omc_blk_ref blocks[8];
    omc_preview_candidate previews[8];
    omc_scan_res scan;
    omc_preview_scan_res res;
    omc_preview_extract_res extracted;
    omc_u8 out_bytes[4];
    const omc_preview_candidate* preview;

    size = make_tiff_with_ifd1_jpeg_preview(bytes, sizeof(bytes));
    scan = omc_scan_auto(bytes, size, blocks, 8U);
    OMC_TEST_REQUIRE_U64_EQ(scan.status, OMC_SCAN_OK);
    OMC_TEST_REQUIRE_U64_EQ(scan.written, 1U);

    res = omc_preview_find_candidates(bytes, size, blocks, scan.written,
                                      previews, 8U,
                                      (const omc_preview_scan_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_PREVIEW_SCAN_OK);
    OMC_TEST_REQUIRE_U64_EQ(res.written, 1U);
    OMC_TEST_REQUIRE_U64_EQ(res.needed, 1U);

    preview = &previews[0];
    OMC_TEST_CHECK_U64_EQ(preview->kind, OMC_PREVIEW_EXIF_JPEG_INTERCHANGE);
    OMC_TEST_CHECK_U64_EQ(preview->offset_tag, 0x0201U);
    OMC_TEST_CHECK_U64_EQ(preview->length_tag, 0x0202U);
    OMC_TEST_CHECK_U64_EQ(preview->file_offset, 44U);
    OMC_TEST_CHECK_U64_EQ(preview->size, 4U);
    OMC_TEST_CHECK(preview->has_jpeg_soi_signature != 0);

    extracted = omc_preview_extract_candidate(
        bytes, size, preview, out_bytes, sizeof(out_bytes),
        (const omc_preview_extract_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(extracted.status, OMC_PREVIEW_EXTRACT_OK);
    OMC_TEST_CHECK_U64_EQ(extracted.written, 4U);
    OMC_TEST_CHECK_U64_EQ(out_bytes[0], 0xFFU);
    OMC_TEST_CHECK_U64_EQ(out_bytes[1], 0xD8U);
}

static void
test_preview_scan_finds_jpg_from_raw_candidate(void)
{
    omc_u8 bytes[128];
    omc_size size;
    omc_blk_ref blocks[8];
    omc_preview_candidate previews[8];
    omc_preview_scan_opts opts;
    omc_preview_scan_res res;
    const omc_preview_candidate* preview;

    size = make_tiff_with_jpg_from_raw(bytes, sizeof(bytes), 1);
    omc_preview_scan_opts_init(&opts);
    opts.include_exif_jpeg_interchange = 0;
    opts.include_jpg_from_raw = 1;

    res = omc_preview_scan_candidates(bytes, size, blocks, 8U, previews, 8U,
                                      &opts);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_PREVIEW_SCAN_OK);
    OMC_TEST_REQUIRE_U64_EQ(res.written, 1U);

    preview = &previews[0];
    OMC_TEST_CHECK_U64_EQ(preview->kind, OMC_PREVIEW_EXIF_JPG_FROM_RAW);
    OMC_TEST_CHECK_U64_EQ(preview->offset_tag, 0x002EU);
    OMC_TEST_CHECK_U64_EQ(preview->file_offset, 26U);
    OMC_TEST_CHECK_U64_EQ(preview->size, 6U);
    OMC_TEST_CHECK(preview->has_jpeg_soi_signature != 0);
}

static void
test_preview_require_jpeg_soi_filters_nonjpeg_candidate(void)
{
    omc_u8 bytes[128];
    omc_size size;
    omc_blk_ref blocks[8];
    omc_preview_candidate previews[8];
    omc_preview_scan_opts opts;
    omc_preview_scan_res res;

    size = make_tiff_with_jpg_from_raw(bytes, sizeof(bytes), 0);
    omc_preview_scan_opts_init(&opts);
    opts.include_exif_jpeg_interchange = 0;
    opts.include_jpg_from_raw = 1;
    opts.require_jpeg_soi = 1;

    res = omc_preview_scan_candidates(bytes, size, blocks, 8U, previews, 8U,
                                      &opts);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_PREVIEW_SCAN_OK);
    OMC_TEST_CHECK_U64_EQ(res.written, 0U);
    OMC_TEST_CHECK_U64_EQ(res.needed, 0U);
}

static void
test_preview_extraction_checks_output_and_limits(void)
{
    omc_u8 bytes[128];
    omc_size size;
    omc_blk_ref blocks[8];
    omc_preview_candidate previews[8];
    omc_preview_scan_res scan;
    omc_preview_extract_opts opts;
    omc_preview_extract_res small;
    omc_preview_extract_res limited;
    omc_u8 too_small[2];
    omc_u8 out_bytes[8];
    const omc_preview_candidate* preview;

    size = make_tiff_with_ifd1_jpeg_preview(bytes, sizeof(bytes));
    scan = omc_preview_scan_candidates(bytes, size, blocks, 8U, previews, 8U,
                                       (const omc_preview_scan_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(scan.status, OMC_PREVIEW_SCAN_OK);
    OMC_TEST_REQUIRE_U64_EQ(scan.written, 1U);

    preview = &previews[0];
    omc_preview_extract_opts_init(&opts);
    opts.max_output_bytes = 1024U;

    small = omc_preview_extract_candidate(bytes, size, preview, too_small,
                                          sizeof(too_small), &opts);
    OMC_TEST_CHECK_U64_EQ(small.status, OMC_PREVIEW_EXTRACT_TRUNCATED);
    OMC_TEST_CHECK_U64_EQ(small.needed, 4U);

    opts.max_output_bytes = 3U;
    limited = omc_preview_extract_candidate(bytes, size, preview, out_bytes,
                                            sizeof(out_bytes), &opts);
    OMC_TEST_CHECK_U64_EQ(limited.status, OMC_PREVIEW_EXTRACT_LIMIT);
}

static void
test_preview_scan_finds_cr3_prvw_jpeg_candidate(void)
{
    omc_u8 bytes[128];
    omc_size size;
    omc_blk_ref blocks[8];
    omc_preview_candidate previews[8];
    omc_preview_scan_opts opts;
    omc_preview_scan_res res;
    omc_preview_extract_res extracted;
    omc_u8 out_bytes[4];
    const omc_preview_candidate* preview;

    size = make_cr3_with_uuid_prvw_jpeg_preview(bytes, sizeof(bytes));
    omc_preview_scan_opts_init(&opts);
    opts.include_exif_jpeg_interchange = 0;
    opts.include_jpg_from_raw = 0;
    opts.include_cr3_prvw_jpeg = 1;

    res = omc_preview_scan_candidates(bytes, size, blocks, 8U, previews, 8U,
                                      &opts);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_PREVIEW_SCAN_OK);
    OMC_TEST_REQUIRE_U64_EQ(res.written, 1U);

    preview = &previews[0];
    OMC_TEST_CHECK_U64_EQ(preview->kind, OMC_PREVIEW_CR3_PRVW_JPEG);
    OMC_TEST_CHECK_U64_EQ(preview->format, OMC_SCAN_FMT_CR3);
    OMC_TEST_CHECK_U64_EQ(preview->file_offset, 80U);
    OMC_TEST_CHECK_U64_EQ(preview->size, 4U);
    OMC_TEST_CHECK(preview->has_jpeg_soi_signature != 0);

    extracted = omc_preview_extract_candidate(
        bytes, size, preview, out_bytes, sizeof(out_bytes),
        (const omc_preview_extract_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(extracted.status, OMC_PREVIEW_EXTRACT_OK);
    OMC_TEST_CHECK_U64_EQ(extracted.written, 4U);
    OMC_TEST_CHECK_U64_EQ(out_bytes[0], 0xFFU);
    OMC_TEST_CHECK_U64_EQ(out_bytes[1], 0xD8U);
    OMC_TEST_CHECK_U64_EQ(out_bytes[3], 0xD9U);
}

int
main(void)
{
    test_preview_find_exif_jpeg_interchange_candidate();
    test_preview_scan_finds_jpg_from_raw_candidate();
    test_preview_require_jpeg_soi_filters_nonjpeg_candidate();
    test_preview_extraction_checks_output_and_limits();
    test_preview_scan_finds_cr3_prvw_jpeg_candidate();
    return omc_test_finish();
}
