#include "omc/omc_scan.h"

#include <assert.h>
#include <string.h>

static void
test_scan_jpeg(void)
{
    static const omc_u8 jpeg_bytes[] = {
        0xFFU, 0xD8U,
        0xFFU, 0xE1U, 0x00U, 0x10U,
        'E', 'x', 'i', 'f', 0x00U, 0x00U, 'I', 'I', 0x2AU, 0x00U,
        0x08U, 0x00U, 0x00U, 0x00U,
        0xFFU, 0xE1U, 0x00U, 0x23U,
        'h', 't', 't', 'p', ':', '/', '/', 'n', 's', '.', 'a', 'd', 'o', 'b',
        'e', '.', 'c', 'o', 'm', '/', 'x', 'a', 'p', '/', '1', '.', '0', '/',
        0x00U,
        '<', 'x', '/', '>',
        0xFFU, 0xE2U, 0x00U, 0x13U,
        'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0x00U,
        0x01U, 0x02U,
        'A', 'B', 'C',
        0xFFU, 0xEDU, 0x00U, 0x14U,
        'P', 'h', 'o', 't', 'o', 's', 'h', 'o', 'p', ' ', '3', '.', '0', 0x00U,
        '8', 'B', 'I', 'M',
        0xFFU, 0xFEU, 0x00U, 0x04U, 'O', 'K',
        0xFFU, 0xD9U
    };
    omc_blk_ref blocks[5];
    omc_scan_res res;

    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(jpeg_bytes, sizeof(jpeg_bytes), blocks, 5U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 5U);
    assert(res.needed == 5U);

    assert(blocks[0].format == OMC_SCAN_FMT_JPEG);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[0].data_offset == 12U);
    assert(blocks[0].data_size == 8U);
    assert(blocks[0].id == 0xFFE1U);

    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[1].data_size == 4U);
    assert(memcmp(jpeg_bytes + (omc_size)blocks[1].data_offset, "<x/>", 4U)
           == 0);

    assert(blocks[2].kind == OMC_BLK_ICC);
    assert(blocks[2].chunking == OMC_BLK_CHUNK_JPEG_APP2_SEQ);
    assert(blocks[2].part_index == 0U);
    assert(blocks[2].part_count == 2U);
    assert(blocks[2].data_size == 3U);

    assert(blocks[3].kind == OMC_BLK_PS_IRB);
    assert(blocks[3].chunking == OMC_BLK_CHUNK_PS_IRB_8BIM);
    assert(blocks[3].data_size == 4U);

    assert(blocks[4].kind == OMC_BLK_COMMENT);
    assert(blocks[4].data_size == 2U);
}

static void
test_scan_tiff(void)
{
    static const omc_u8 tiff_bytes[] = {
        'I', 'I', 0x2AU, 0x00U,
        0x08U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U
    };
    omc_blk_ref block;
    omc_scan_res res;

    memset(&block, 0, sizeof(block));
    res = omc_scan_tiff(tiff_bytes, sizeof(tiff_bytes), &block, 1U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 1U);
    assert(res.needed == 1U);
    assert(block.format == OMC_SCAN_FMT_TIFF);
    assert(block.kind == OMC_BLK_EXIF);
    assert(block.outer_size == sizeof(tiff_bytes));
    assert(block.data_size == sizeof(tiff_bytes));
}

static void
test_scan_measure_and_truncation(void)
{
    static const omc_u8 tiff_bytes[] = {
        'M', 'M', 0x00U, 0x2AU,
        0x00U, 0x00U, 0x00U, 0x08U,
        0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U
    };
    omc_scan_res res;

    res = omc_scan_tiff(tiff_bytes, sizeof(tiff_bytes), (omc_blk_ref*)0, 0U);
    assert(res.status == OMC_SCAN_TRUNCATED);
    assert(res.written == 0U);
    assert(res.needed == 1U);

    res = omc_scan_meas_tiff(tiff_bytes, sizeof(tiff_bytes));
    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 0U);
    assert(res.needed == 1U);
}

int
main(void)
{
    test_scan_jpeg();
    test_scan_tiff();
    test_scan_measure_and_truncation();
    return 0;
}
