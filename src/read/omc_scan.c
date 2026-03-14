#include "omc/omc_scan.h"

#include <string.h>

typedef struct omc_scan_sink {
    omc_blk_ref* out_blocks;
    omc_u32 out_cap;
    omc_scan_res result;
} omc_scan_sink;

typedef struct omc_tiff_cfg {
    int little_endian;
    int big_tiff;
} omc_tiff_cfg;

static void
omc_scan_sink_init(omc_scan_sink* sink, omc_blk_ref* out_blocks,
                   omc_u32 out_cap)
{
    sink->out_blocks = out_blocks;
    sink->out_cap = out_cap;
    sink->result.status = OMC_SCAN_OK;
    sink->result.written = 0U;
    sink->result.needed = 0U;
}

static void
omc_scan_sink_emit(omc_scan_sink* sink, const omc_blk_ref* block)
{
    omc_u32 index;

    index = sink->result.needed;
    sink->result.needed += 1U;
    if (index < sink->out_cap && sink->out_blocks != (omc_blk_ref*)0) {
        sink->out_blocks[index] = *block;
        sink->result.written = index + 1U;
    } else if (sink->result.status == OMC_SCAN_OK) {
        sink->result.status = OMC_SCAN_TRUNCATED;
    }
}

static int
omc_scan_match(const omc_u8* bytes, omc_size size, omc_u64 offset,
               const char* sig, omc_size sig_size)
{
    if (offset > (omc_u64)size) {
        return 0;
    }
    if ((omc_u64)sig_size > ((omc_u64)size - offset)) {
        return 0;
    }
    return memcmp(bytes + (omc_size)offset, sig, sig_size) == 0;
}

static int
omc_scan_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 2U) {
        return 0;
    }

    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                           | ((omc_u16)bytes[(omc_size)offset + 1U]));
    return 1;
}

static int
omc_scan_read_tiff_u16(omc_tiff_cfg cfg, const omc_u8* bytes, omc_size size,
                       omc_u64 offset, omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 2U) {
        return 0;
    }

    if (cfg.little_endian) {
        *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 1U]) << 8)
                               | ((omc_u16)bytes[(omc_size)offset + 0U]));
    } else {
        *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                               | ((omc_u16)bytes[(omc_size)offset + 1U]));
    }
    return 1;
}

static int
omc_scan_read_tiff_u32(omc_tiff_cfg cfg, const omc_u8* bytes, omc_size size,
                       omc_u64 offset, omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    if (cfg.little_endian) {
        *out_value = (((omc_u32)bytes[(omc_size)offset + 3U]) << 24)
                     | (((omc_u32)bytes[(omc_size)offset + 2U]) << 16)
                     | (((omc_u32)bytes[(omc_size)offset + 1U]) << 8)
                     | (((omc_u32)bytes[(omc_size)offset + 0U]) << 0);
    } else {
        *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                     | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                     | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                     | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    }
    return 1;
}

static int
omc_scan_read_tiff_u64(omc_tiff_cfg cfg, const omc_u8* bytes, omc_size size,
                       omc_u64 offset, omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 8U) {
        return 0;
    }

    if (cfg.little_endian) {
        *out_value = (((omc_u64)bytes[(omc_size)offset + 7U]) << 56)
                     | (((omc_u64)bytes[(omc_size)offset + 6U]) << 48)
                     | (((omc_u64)bytes[(omc_size)offset + 5U]) << 40)
                     | (((omc_u64)bytes[(omc_size)offset + 4U]) << 32)
                     | (((omc_u64)bytes[(omc_size)offset + 3U]) << 24)
                     | (((omc_u64)bytes[(omc_size)offset + 2U]) << 16)
                     | (((omc_u64)bytes[(omc_size)offset + 1U]) << 8)
                     | (((omc_u64)bytes[(omc_size)offset + 0U]) << 0);
    } else {
        *out_value = (((omc_u64)bytes[(omc_size)offset + 0U]) << 56)
                     | (((omc_u64)bytes[(omc_size)offset + 1U]) << 48)
                     | (((omc_u64)bytes[(omc_size)offset + 2U]) << 40)
                     | (((omc_u64)bytes[(omc_size)offset + 3U]) << 32)
                     | (((omc_u64)bytes[(omc_size)offset + 4U]) << 24)
                     | (((omc_u64)bytes[(omc_size)offset + 5U]) << 16)
                     | (((omc_u64)bytes[(omc_size)offset + 6U]) << 8)
                     | (((omc_u64)bytes[(omc_size)offset + 7U]) << 0);
    }
    return 1;
}

static int
omc_scan_looks_like_tiff_header(const omc_u8* bytes, omc_size size,
                                omc_u64 offset)
{
    omc_u8 a;
    omc_u8 b;
    omc_u8 c;
    omc_u8 d;

    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    a = bytes[(omc_size)offset + 0U];
    b = bytes[(omc_size)offset + 1U];
    c = bytes[(omc_size)offset + 2U];
    d = bytes[(omc_size)offset + 3U];

    return ((a == (omc_u8)'I' && b == (omc_u8)'I' && c == 0x2AU && d == 0x00U)
            || (a == (omc_u8)'M' && b == (omc_u8)'M' && c == 0x00U
                && d == 0x2AU));
}

static void
omc_scan_init_block(omc_blk_ref* block)
{
    memset(block, 0, sizeof(*block));
    block->format = OMC_SCAN_FMT_UNKNOWN;
    block->kind = OMC_BLK_UNKNOWN;
    block->compression = OMC_BLK_COMP_NONE;
    block->chunking = OMC_BLK_CHUNK_NONE;
}

static void
omc_scan_skip_exif_preamble(omc_blk_ref* block, const omc_u8* bytes,
                            omc_size size)
{
    omc_u64 tiff_off;

    if (block->data_size < 10U) {
        return;
    }
    if (!omc_scan_match(bytes, size, block->data_offset, "Exif", 4U)) {
        return;
    }
    if (bytes[(omc_size)block->data_offset + 4U] != 0U) {
        return;
    }

    tiff_off = block->data_offset + 6U;
    if (!omc_scan_looks_like_tiff_header(bytes, size, tiff_off)) {
        return;
    }

    block->data_offset += 6U;
    block->data_size -= 6U;
}

static omc_scan_res
omc_scan_normalize_measure(omc_scan_res res)
{
    if (res.status == OMC_SCAN_TRUNCATED) {
        res.status = OMC_SCAN_OK;
    }
    res.written = 0U;
    return res;
}

omc_scan_res
omc_scan_jpeg(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_u64 offset;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 2U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (bytes[0] != 0xFFU || bytes[1] != 0xD8U) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    offset = 2U;
    while (offset + 2U <= (omc_u64)size) {
        omc_u64 prefix_off;
        omc_u64 marker_off;
        omc_u8 marker_lo;
        omc_u16 marker;
        omc_u16 seg_len;
        omc_u64 seg_payload_off;
        omc_u64 seg_payload_size;
        omc_u64 seg_total_size;
        omc_blk_ref block;

        if (bytes[(omc_size)offset] != 0xFFU) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        prefix_off = offset;
        while (offset < (omc_u64)size && bytes[(omc_size)offset] == 0xFFU) {
            offset += 1U;
        }
        if (offset >= (omc_u64)size) {
            break;
        }
        if (offset <= prefix_off) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        marker_off = offset - 1U;
        marker_lo = bytes[(omc_size)offset];
        offset += 1U;
        marker = (omc_u16)(0xFF00U | (omc_u16)marker_lo);

        if (marker == 0xFFD9U || marker == 0xFFDAU) {
            break;
        }
        if ((marker >= 0xFFD0U && marker <= 0xFFD7U) || marker == 0xFF01U) {
            continue;
        }

        if (!omc_scan_read_u16be(bytes, size, offset, &seg_len)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
        if (seg_len < 2U) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        seg_payload_off = offset + 2U;
        seg_payload_size = (omc_u64)(seg_len - 2U);
        seg_total_size = 2U + (omc_u64)seg_len;

        if (seg_payload_off > (omc_u64)size
            || seg_payload_size > ((omc_u64)size - seg_payload_off)
            || marker_off > (omc_u64)size
            || seg_total_size > ((omc_u64)size - marker_off)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        if (marker == 0xFFE1U) {
            if (seg_payload_size >= 10U
                && omc_scan_match(bytes, size, seg_payload_off, "Exif", 4U)
                && bytes[(omc_size)seg_payload_off + 4U] == 0U) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_JPEG;
                block.kind = OMC_BLK_EXIF;
                block.outer_offset = marker_off;
                block.outer_size = seg_total_size;
                block.data_offset = seg_payload_off;
                block.data_size = seg_payload_size;
                block.id = marker;
                omc_scan_skip_exif_preamble(&block, bytes, size);
                if (block.data_offset != seg_payload_off) {
                    omc_scan_sink_emit(&sink, &block);
                }
            } else if (omc_scan_match(bytes, size, seg_payload_off,
                                      "http://ns.adobe.com/xap/1.0/\0", 29U)) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_JPEG;
                block.kind = OMC_BLK_XMP;
                block.outer_offset = marker_off;
                block.outer_size = seg_total_size;
                block.data_offset = seg_payload_off + 29U;
                block.data_size = seg_payload_size - 29U;
                block.id = marker;
                omc_scan_sink_emit(&sink, &block);
            }
        } else if (marker == 0xFFE2U) {
            if (seg_payload_size >= 14U
                && omc_scan_match(bytes, size, seg_payload_off,
                                  "ICC_PROFILE\0", 12U)) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_JPEG;
                block.kind = OMC_BLK_ICC;
                block.chunking = OMC_BLK_CHUNK_JPEG_APP2_SEQ;
                block.outer_offset = marker_off;
                block.outer_size = seg_total_size;
                block.data_offset = seg_payload_off + 14U;
                block.data_size = seg_payload_size - 14U;
                block.id = marker;
                if (bytes[(omc_size)seg_payload_off + 12U] > 0U) {
                    block.part_index =
                        (omc_u32)bytes[(omc_size)seg_payload_off + 12U] - 1U;
                } else {
                    block.part_index = 0U;
                }
                block.part_count =
                    (omc_u32)bytes[(omc_size)seg_payload_off + 13U];
                omc_scan_sink_emit(&sink, &block);
            } else if (seg_payload_size >= 4U
                       && omc_scan_match(bytes, size, seg_payload_off,
                                         "MPF\0", 4U)) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_JPEG;
                block.kind = OMC_BLK_MPF;
                block.outer_offset = marker_off;
                block.outer_size = seg_total_size;
                block.data_offset = seg_payload_off + 4U;
                block.data_size = seg_payload_size - 4U;
                block.id = marker;
                omc_scan_sink_emit(&sink, &block);
            }
        } else if (marker == 0xFFEDU) {
            if (seg_payload_size >= 14U
                && omc_scan_match(bytes, size, seg_payload_off,
                                  "Photoshop 3.0\0", 14U)) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_JPEG;
                block.kind = OMC_BLK_PS_IRB;
                block.chunking = OMC_BLK_CHUNK_PS_IRB_8BIM;
                block.outer_offset = marker_off;
                block.outer_size = seg_total_size;
                block.data_offset = seg_payload_off + 14U;
                block.data_size = seg_payload_size - 14U;
                block.id = marker;
                omc_scan_sink_emit(&sink, &block);
            }
        } else if (marker == 0xFFFEU) {
            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_JPEG;
            block.kind = OMC_BLK_COMMENT;
            block.outer_offset = marker_off;
            block.outer_size = seg_total_size;
            block.data_offset = seg_payload_off;
            block.data_size = seg_payload_size;
            block.id = marker;
            omc_scan_sink_emit(&sink, &block);
        }

        offset = seg_payload_off + seg_payload_size;
    }

    return sink.result;
}

omc_scan_res
omc_scan_meas_jpeg(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_jpeg(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_tiff(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_tiff_cfg cfg;
    omc_u16 version;
    omc_u64 first_ifd;
    omc_blk_ref block;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 8U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    if (bytes[0] == (omc_u8)'I' && bytes[1] == (omc_u8)'I') {
        cfg.little_endian = 1;
    } else if (bytes[0] == (omc_u8)'M' && bytes[1] == (omc_u8)'M') {
        cfg.little_endian = 0;
    } else {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    if (!omc_scan_read_tiff_u16(cfg, bytes, size, 2U, &version)) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    if (version == 42U || version == 0x0055U || version == 0x4F52U) {
        cfg.big_tiff = 0;
    } else if (version == 43U) {
        cfg.big_tiff = 1;
    } else {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    if (!cfg.big_tiff) {
        omc_u32 off32;

        if (!omc_scan_read_tiff_u32(cfg, bytes, size, 4U, &off32)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
        first_ifd = off32;
    } else {
        omc_u16 off_size;
        omc_u16 reserved;

        if (size < 16U) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
        if (!omc_scan_read_tiff_u16(cfg, bytes, size, 4U, &off_size)
            || !omc_scan_read_tiff_u16(cfg, bytes, size, 6U, &reserved)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
        if (off_size != 8U || reserved != 0U) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
        if (!omc_scan_read_tiff_u64(cfg, bytes, size, 8U, &first_ifd)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
    }

    if (first_ifd >= (omc_u64)size) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    omc_scan_init_block(&block);
    block.format = OMC_SCAN_FMT_TIFF;
    block.kind = OMC_BLK_EXIF;
    block.outer_offset = 0U;
    block.outer_size = (omc_u64)size;
    block.data_offset = 0U;
    block.data_size = (omc_u64)size;
    block.id = 0U;
    omc_scan_sink_emit(&sink, &block);

    return sink.result;
}

omc_scan_res
omc_scan_meas_tiff(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_tiff(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_auto(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_u16 tiff_version;

    if (bytes == (const omc_u8*)0) {
        return omc_scan_jpeg(bytes, size, out_blocks, out_cap);
    }

    if (size >= 2U && bytes[0] == 0xFFU && bytes[1] == 0xD8U) {
        return omc_scan_jpeg(bytes, size, out_blocks, out_cap);
    }

    if (size >= 4U
        && ((bytes[0] == (omc_u8)'I' && bytes[1] == (omc_u8)'I')
            || (bytes[0] == (omc_u8)'M' && bytes[1] == (omc_u8)'M'))) {
        if (bytes[0] == (omc_u8)'I') {
            tiff_version = (omc_u16)((((omc_u16)bytes[3]) << 8)
                                     | ((omc_u16)bytes[2]));
        } else {
            tiff_version = (omc_u16)((((omc_u16)bytes[2]) << 8)
                                     | ((omc_u16)bytes[3]));
        }
        if (tiff_version == 42U || tiff_version == 43U
            || tiff_version == 0x0055U || tiff_version == 0x4F52U) {
            return omc_scan_tiff(bytes, size, out_blocks, out_cap);
        }
    }

    {
        omc_scan_res res;
        res.status = OMC_SCAN_UNSUPPORTED;
        res.written = 0U;
        res.needed = 0U;
        return res;
    }
}

omc_scan_res
omc_scan_meas_auto(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_auto(bytes, size, (omc_blk_ref*)0, 0U));
}
