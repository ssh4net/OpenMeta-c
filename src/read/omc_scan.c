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

static omc_u64
omc_scan_find_match(const omc_u8* bytes, omc_size size, omc_u64 start,
                    omc_u64 end, const char* sig, omc_size sig_size)
{
    omc_u64 last;
    omc_u64 off;

    if (bytes == (const omc_u8*)0 || sig == (const char*)0) {
        return ~(omc_u64)0;
    }
    if (start > end || end > (omc_u64)size || sig_size == 0U) {
        return ~(omc_u64)0;
    }
    if ((omc_u64)sig_size > end - start) {
        return ~(omc_u64)0;
    }

    last = end - (omc_u64)sig_size;
    for (off = start; off <= last; ++off) {
        if (memcmp(bytes + (omc_size)off, sig, sig_size) == 0) {
            return off;
        }
    }
    return ~(omc_u64)0;
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
omc_scan_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    return 1;
}

static int
omc_scan_read_u32le(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 3U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 0U]) << 0);
    return 1;
}

static int
omc_scan_read_u64be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                    omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 8U) {
        return 0;
    }

    *out_value = (((omc_u64)bytes[(omc_size)offset + 0U]) << 56)
                 | (((omc_u64)bytes[(omc_size)offset + 1U]) << 48)
                 | (((omc_u64)bytes[(omc_size)offset + 2U]) << 40)
                 | (((omc_u64)bytes[(omc_size)offset + 3U]) << 32)
                 | (((omc_u64)bytes[(omc_size)offset + 4U]) << 24)
                 | (((omc_u64)bytes[(omc_size)offset + 5U]) << 16)
                 | (((omc_u64)bytes[(omc_size)offset + 6U]) << 8)
                 | (((omc_u64)bytes[(omc_size)offset + 7U]) << 0);
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

static int
omc_scan_skip_gif_sub_blocks(const omc_u8* bytes, omc_size size,
                             omc_u64 start, omc_u64* out_end)
{
    omc_u64 p;

    if (bytes == (const omc_u8*)0 || out_end == (omc_u64*)0
        || start > (omc_u64)size) {
        return 0;
    }

    p = start;
    while (p < (omc_u64)size) {
        omc_u8 sub_size;

        sub_size = bytes[(omc_size)p];
        p += 1U;
        if (sub_size == 0U) {
            *out_end = p;
            return 1;
        }
        if ((omc_u64)sub_size > ((omc_u64)size - p)) {
            return 0;
        }
        p += (omc_u64)sub_size;
    }
    return 0;
}

static omc_u64
omc_scan_fnv1a64(const omc_u8* bytes, omc_size size, omc_u64 offset,
                 omc_u64 span_size)
{
    const omc_u64 offset_basis = (((omc_u64)0xCBF29CE4U) << 32)
                                 | (omc_u64)0x84222325U;
    const omc_u64 prime = (((omc_u64)0x00000100U) << 32)
                          | (omc_u64)0x000001B3U;
    omc_u64 hash;
    omc_u64 i;

    if (bytes == (const omc_u8*)0 || offset > (omc_u64)size
        || span_size > ((omc_u64)size - offset)) {
        return 0U;
    }

    hash = offset_basis;
    for (i = 0U; i < span_size; ++i) {
        hash ^= (omc_u64)bytes[(omc_size)(offset + i)];
        hash *= prime;
    }
    return hash;
}

static int
omc_scan_is_jpeg_app11_jumbf(const omc_blk_ref* block)
{
    return block != (const omc_blk_ref*)0 && block->format == OMC_SCAN_FMT_JPEG
           && block->kind == OMC_BLK_JUMBF && block->id == 0xFFEBU;
}

static int
omc_scan_validate_jpeg_app11_base(const omc_blk_ref* blocks, omc_u32 written,
                                  omc_u64 group, omc_u32 base,
                                  omc_u32* out_count)
{
    omc_u32 count;
    omc_u32 min_idx;
    omc_u32 max_idx;
    omc_u32 i;

    count = 0U;
    min_idx = ~(omc_u32)0;
    max_idx = 0U;

    for (i = 0U; i < written; ++i) {
        const omc_blk_ref* block;
        omc_u32 idx;

        block = &blocks[i];
        if (!omc_scan_is_jpeg_app11_jumbf(block) || block->group != group) {
            continue;
        }
        if (block->part_index < base) {
            return 0;
        }
        idx = block->part_index - base;
        if (idx < min_idx) {
            min_idx = idx;
        }
        if (idx > max_idx) {
            max_idx = idx;
        }
        count += 1U;
    }

    if (count == 0U || min_idx != 0U || max_idx + 1U != count) {
        return 0;
    }

    for (i = 0U; i < written; ++i) {
        const omc_blk_ref* a;
        omc_u32 ia;
        omc_u32 j;

        a = &blocks[i];
        if (!omc_scan_is_jpeg_app11_jumbf(a) || a->group != group) {
            continue;
        }
        ia = a->part_index - base;

        for (j = i + 1U; j < written; ++j) {
            const omc_blk_ref* b;
            omc_u32 ib;

            b = &blocks[j];
            if (!omc_scan_is_jpeg_app11_jumbf(b) || b->group != group) {
                continue;
            }
            ib = b->part_index - base;
            if (ia == ib) {
                return 0;
            }
        }
    }

    if (out_count != (omc_u32*)0) {
        *out_count = count;
    }
    return 1;
}

static void
omc_scan_jpeg_app11_include_header(omc_blk_ref* block)
{
    omc_u64 header_start;
    omc_u64 header_size;

    if (!omc_scan_is_jpeg_app11_jumbf(block)) {
        return;
    }

    header_start = block->outer_offset + 12U;
    if (block->data_offset <= header_start) {
        return;
    }

    header_size = block->data_offset - header_start;
    if ((header_size != 8U && header_size != 16U)
        || block->data_offset < header_size) {
        return;
    }

    block->data_offset -= header_size;
    block->data_size += header_size;
}

static void
omc_scan_normalize_jpeg_app11_jumbf(omc_scan_sink* sink)
{
    omc_u32 written;
    omc_u32 i;

    if (sink == (omc_scan_sink*)0 || sink->out_blocks == (omc_blk_ref*)0) {
        return;
    }

    written = sink->result.written;
    for (i = 0U; i < written; ++i) {
        omc_blk_ref* seed;
        omc_u32 count;
        omc_u32 base;
        int has_seq0;
        omc_u32 j;
        int seen_group;

        seed = &sink->out_blocks[i];
        if (!omc_scan_is_jpeg_app11_jumbf(seed) || seed->part_count != 0U) {
            continue;
        }

        seen_group = 0;
        for (j = 0U; j < i; ++j) {
            const omc_blk_ref* prev;

            prev = &sink->out_blocks[j];
            if (omc_scan_is_jpeg_app11_jumbf(prev)
                && prev->group == seed->group) {
                seen_group = 1;
                break;
            }
        }
        if (seen_group) {
            continue;
        }

        count = 0U;
        has_seq0 = 0;
        for (j = 0U; j < written; ++j) {
            const omc_blk_ref* block;

            block = &sink->out_blocks[j];
            if (!omc_scan_is_jpeg_app11_jumbf(block)
                || block->group != seed->group) {
                continue;
            }
            count += 1U;
            if (block->part_index == 0U) {
                has_seq0 = 1;
            }
        }
        if (count == 0U) {
            continue;
        }

        base = has_seq0 ? 0U : 1U;
        if (!omc_scan_validate_jpeg_app11_base(
                sink->out_blocks, written, seed->group, base, &count)) {
            omc_u32 alt_base;

            alt_base = (base == 0U) ? 1U : 0U;
            if (omc_scan_validate_jpeg_app11_base(
                    sink->out_blocks, written, seed->group, alt_base,
                    &count)) {
                base = alt_base;
            } else {
                for (j = 0U; j < written; ++j) {
                    omc_blk_ref* block;

                    block = &sink->out_blocks[j];
                    if (!omc_scan_is_jpeg_app11_jumbf(block)
                        || block->group != seed->group) {
                        continue;
                    }
                    block->part_index = 0U;
                    block->part_count = 1U;
                    omc_scan_jpeg_app11_include_header(block);
                }
                continue;
            }
        }

        for (j = 0U; j < written; ++j) {
            omc_blk_ref* block;

            block = &sink->out_blocks[j];
            if (!omc_scan_is_jpeg_app11_jumbf(block)
                || block->group != seed->group) {
                continue;
            }
            block->part_index -= base;
            block->part_count = count;
        }

        for (j = 0U; j < written; ++j) {
            omc_blk_ref* block;

            block = &sink->out_blocks[j];
            if (!omc_scan_is_jpeg_app11_jumbf(block)
                || block->group != seed->group || block->part_index != 0U) {
                continue;
            }
            omc_scan_jpeg_app11_include_header(block);
            break;
        }
    }
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
            } else if (seg_payload_size >= 4U
                       && omc_scan_match(bytes, size, seg_payload_off,
                                         "QVCI", 4U)) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_JPEG;
                block.kind = OMC_BLK_MAKERNOTE;
                block.outer_offset = marker_off;
                block.outer_size = seg_total_size;
                block.data_offset = seg_payload_off;
                block.data_size = seg_payload_size;
                block.id = marker;
                block.aux_u32 = OMC_FOURCC('Q', 'V', 'C', 'I');
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
        } else if (marker == 0xFFEBU) {
            if (seg_payload_size >= 16U
                && bytes[(omc_size)seg_payload_off + 0U] == (omc_u8)'J'
                && bytes[(omc_size)seg_payload_off + 1U] == (omc_u8)'P') {
                omc_u32 seq;
                omc_u32 size32;
                omc_u32 type;
                omc_u64 header_size;
                omc_u64 box_size;

                seq = 0U;
                size32 = 0U;
                type = 0U;
                header_size = 8U;
                box_size = 0U;

                if (!omc_scan_read_u32be(bytes, size, seg_payload_off + 4U,
                                         &seq)
                    || !omc_scan_read_u32be(bytes, size, seg_payload_off + 8U,
                                            &size32)
                    || !omc_scan_read_u32be(bytes, size, seg_payload_off + 12U,
                                            &type)) {
                    sink.result.status = OMC_SCAN_MALFORMED;
                    return sink.result;
                }

                box_size = size32;
                if (size32 == 1U) {
                    if (seg_payload_size < 24U
                        || !omc_scan_read_u64be(bytes, size,
                                                seg_payload_off + 16U,
                                                &box_size)) {
                        sink.result.status = OMC_SCAN_MALFORMED;
                        return sink.result;
                    }
                    header_size = 16U;
                }

                if (box_size >= header_size
                    && seg_payload_size >= 8U + header_size) {
                    omc_u64 header_off;

                    omc_scan_init_block(&block);
                    block.format = OMC_SCAN_FMT_JPEG;
                    block.kind = OMC_BLK_JUMBF;
                    block.chunking = OMC_BLK_CHUNK_JPEG_APP11_SEQ;
                    block.outer_offset = marker_off;
                    block.outer_size = seg_total_size;
                    block.data_offset = seg_payload_off + 8U + header_size;
                    block.data_size = seg_payload_size - (8U + header_size);
                    block.id = marker;
                    block.aux_u32 = type;
                    block.part_index = seq;
                    block.logical_size = box_size;
                    header_off = seg_payload_off + 8U;
                    block.group = omc_scan_fnv1a64(bytes, size, header_off,
                                                   header_size);
                    if (block.group == 0U) {
                        block.group = 1U;
                    }
                    omc_scan_sink_emit(&sink, &block);
                }
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

    omc_scan_normalize_jpeg_app11_jumbf(&sink);
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
omc_scan_crw(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_u32 root_off;
    int little_endian;
    omc_blk_ref block;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 14U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    little_endian = 0;
    if (bytes[0] == (omc_u8)'I' && bytes[1] == (omc_u8)'I') {
        little_endian = 1;
    } else if (!(bytes[0] == (omc_u8)'M' && bytes[1] == (omc_u8)'M')) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }
    if (!omc_scan_match(bytes, size, 6U, "HEAPCCDR", 8U)) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    if (little_endian) {
        if (!omc_scan_read_u32le(bytes, size, 2U, &root_off)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
    } else {
        if (!omc_scan_read_u32be(bytes, size, 2U, &root_off)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
    }
    if (root_off < 14U || (omc_u64)root_off > (omc_u64)size) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    omc_scan_init_block(&block);
    block.format = OMC_SCAN_FMT_CRW;
    block.kind = OMC_BLK_CIFF;
    block.outer_offset = 0U;
    block.outer_size = (omc_u64)size;
    block.data_offset = 0U;
    block.data_size = (omc_u64)size;
    block.id = OMC_FOURCC('C', 'R', 'W', ' ');
    block.aux_u32 = root_off;
    omc_scan_sink_emit(&sink, &block);
    return sink.result;
}

omc_scan_res
omc_scan_meas_crw(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_crw(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_raf(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    static const char k_xmp_sig[] = "http://ns.adobe.com/xap/1.0/\0";
    static const char k_close_xmpmeta[] = "</x:xmpmeta>";
    static const char k_close_rdf[] = "</rdf:RDF>";
    omc_scan_sink sink;
    omc_blk_ref block;
    omc_scan_res tiff_res;
    omc_u64 tiff_off;
    omc_u64 max_search;
    omc_u64 sig_off;
    omc_u64 data_off;
    omc_u64 packet_end;
    omc_u64 end;
    omc_u64 close;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 16U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (!omc_scan_match(bytes, size, 0U, "FUJIFILMCCD-RAW ", 16U)) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    tiff_off = 160U;
    if (tiff_off >= (omc_u64)size
        || !omc_scan_looks_like_tiff_header(bytes, size, tiff_off)) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    tiff_res = omc_scan_tiff(bytes + (omc_size)tiff_off,
                             size - (omc_size)tiff_off, &block, 1U);
    if (tiff_res.status != OMC_SCAN_OK || tiff_res.written != 1U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    block.format = OMC_SCAN_FMT_RAF;
    block.outer_offset += tiff_off;
    block.data_offset += tiff_off;
    omc_scan_sink_emit(&sink, &block);

    max_search = (omc_u64)size;
    if (max_search > 32U * 1024U * 1024U) {
        max_search = 32U * 1024U * 1024U;
    }
    sig_off = omc_scan_find_match(bytes, size, 0U, max_search, k_xmp_sig,
                                  sizeof(k_xmp_sig) - 1U);
    if (sig_off == ~(omc_u64)0) {
        return sink.result;
    }

    data_off = sig_off + (sizeof(k_xmp_sig) - 1U);
    packet_end = data_off + (512U * 1024U);
    if (packet_end > (omc_u64)size) {
        packet_end = (omc_u64)size;
    }

    end = packet_end;
    close = omc_scan_find_match(bytes, size, data_off, packet_end,
                                k_close_xmpmeta,
                                sizeof(k_close_xmpmeta) - 1U);
    if (close != ~(omc_u64)0) {
        end = close + (sizeof(k_close_xmpmeta) - 1U);
    } else {
        close = omc_scan_find_match(bytes, size, data_off, packet_end,
                                    k_close_rdf, sizeof(k_close_rdf) - 1U);
        if (close != ~(omc_u64)0) {
            end = close + (sizeof(k_close_rdf) - 1U);
        }
    }

    if (end > data_off && end > sig_off) {
        omc_scan_init_block(&block);
        block.format = OMC_SCAN_FMT_RAF;
        block.kind = OMC_BLK_XMP;
        block.outer_offset = sig_off;
        block.outer_size = end - sig_off;
        block.data_offset = data_off;
        block.data_size = end - data_off;
        block.id = OMC_FOURCC('x', 'm', 'p', ' ');
        omc_scan_sink_emit(&sink, &block);
    }

    return sink.result;
}

omc_scan_res
omc_scan_meas_raf(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_raf(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_x3f(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_u64 max_search;
    omc_u64 off;
    omc_blk_ref block;
    omc_scan_res tiff_res;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 4U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (!omc_scan_match(bytes, size, 0U, "FOVb", 4U)) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    max_search = (omc_u64)size;
    if (max_search > 4U * 1024U * 1024U) {
        max_search = 4U * 1024U * 1024U;
    }
    for (off = 0U; off + 10U <= max_search; ++off) {
        omc_u64 tiff_off;

        if (!omc_scan_match(bytes, size, off, "Exif", 4U)
            || bytes[(omc_size)off + 4U] != 0U
            || bytes[(omc_size)off + 5U] != 0U) {
            continue;
        }

        tiff_off = off + 6U;
        if (!omc_scan_looks_like_tiff_header(bytes, size, tiff_off)) {
            continue;
        }

        tiff_res = omc_scan_tiff(bytes + (omc_size)tiff_off,
                                 size - (omc_size)tiff_off, &block, 1U);
        if (tiff_res.status != OMC_SCAN_OK || tiff_res.written != 1U) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        block.format = OMC_SCAN_FMT_X3F;
        block.outer_offset += tiff_off;
        block.data_offset += tiff_off;
        omc_scan_sink_emit(&sink, &block);
        return sink.result;
    }

    sink.result.status = OMC_SCAN_MALFORMED;
    return sink.result;
}

omc_scan_res
omc_scan_meas_x3f(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_x3f(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_png(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    static const omc_u8 k_png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    omc_scan_sink sink;
    omc_u64 offset;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 8U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (memcmp(bytes, k_png_sig, 8U) != 0) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    offset = 8U;
    while (offset + 12U <= (omc_u64)size) {
        omc_u64 chunk_off;
        omc_u32 len;
        omc_u32 type;
        omc_u64 data_off;
        omc_u64 data_size;
        omc_u64 chunk_size;
        omc_blk_ref block;

        chunk_off = offset;
        if (!omc_scan_read_u32be(bytes, size, offset, &len)
            || !omc_scan_read_u32be(bytes, size, offset + 4U, &type)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        data_off = offset + 8U;
        data_size = len;
        chunk_size = 12U + data_size;
        if (data_off > (omc_u64)size || data_size > ((omc_u64)size - data_off)
            || 4U > (((omc_u64)size - data_off) - data_size)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        if (type == OMC_FOURCC('e', 'X', 'I', 'f')) {
            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_PNG;
            block.kind = OMC_BLK_EXIF;
            block.outer_offset = chunk_off;
            block.outer_size = chunk_size;
            block.data_offset = data_off;
            block.data_size = data_size;
            block.id = type;
            omc_scan_sink_emit(&sink, &block);
        } else if (type == OMC_FOURCC('i', 'C', 'C', 'P')) {
            omc_u64 p;
            omc_u64 end;

            p = data_off;
            end = data_off + data_size;
            while (p < end && bytes[(omc_size)p] != 0U) {
                p += 1U;
            }
            if (p + 2U <= end) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_PNG;
                block.kind = OMC_BLK_ICC;
                block.compression = OMC_BLK_COMP_DEFLATE;
                block.outer_offset = chunk_off;
                block.outer_size = chunk_size;
                block.data_offset = p + 2U;
                block.data_size = end - (p + 2U);
                block.id = type;
                omc_scan_sink_emit(&sink, &block);
            }
        } else if (type == OMC_FOURCC('i', 'T', 'X', 't')) {
            omc_u64 p;
            omc_u64 end;
            omc_u64 keyword_end;
            omc_u64 comp_flag_off;
            omc_u8 comp_flag;
            omc_u64 lang;
            omc_u64 trans;
            omc_u64 text_off;
            int is_xmp;

            p = data_off;
            end = data_off + data_size;
            while (p < end && bytes[(omc_size)p] != 0U) {
                p += 1U;
            }
            keyword_end = p;
            if (keyword_end + 3U > end) {
                goto next_png_chunk;
            }

            is_xmp = 0;
            if (keyword_end - data_off == 17U
                && omc_scan_match(bytes, size, data_off, "XML:com.adobe.xmp",
                                  17U)) {
                is_xmp = 1;
            }

            comp_flag_off = keyword_end + 1U;
            comp_flag = bytes[(omc_size)comp_flag_off];
            lang = comp_flag_off + 2U;
            while (lang < end && bytes[(omc_size)lang] != 0U) {
                lang += 1U;
            }
            if (lang >= end) {
                goto next_png_chunk;
            }
            trans = lang + 1U;
            while (trans < end && bytes[(omc_size)trans] != 0U) {
                trans += 1U;
            }
            if (trans >= end) {
                goto next_png_chunk;
            }

            text_off = trans + 1U;
            if (text_off <= end && is_xmp) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_PNG;
                block.kind = OMC_BLK_XMP;
                block.outer_offset = chunk_off;
                block.outer_size = chunk_size;
                block.data_offset = text_off;
                block.data_size = end - text_off;
                block.id = type;
                if (comp_flag != 0U) {
                    block.compression = OMC_BLK_COMP_DEFLATE;
                }
                omc_scan_sink_emit(&sink, &block);
            } else if (text_off <= end) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_PNG;
                block.kind = OMC_BLK_TEXT;
                block.outer_offset = chunk_off;
                block.outer_size = chunk_size;
                block.data_offset = text_off;
                block.data_size = end - text_off;
                block.id = type;
                if (comp_flag != 0U) {
                    block.compression = OMC_BLK_COMP_DEFLATE;
                }
                omc_scan_sink_emit(&sink, &block);
            }
        } else if (type == OMC_FOURCC('z', 'T', 'X', 't')) {
            omc_u64 p;
            omc_u64 end;

            p = data_off;
            end = data_off + data_size;
            while (p < end && bytes[(omc_size)p] != 0U) {
                p += 1U;
            }
            if (p + 2U <= end) {
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_PNG;
                block.kind = OMC_BLK_TEXT;
                block.compression = OMC_BLK_COMP_DEFLATE;
                block.outer_offset = chunk_off;
                block.outer_size = chunk_size;
                block.data_offset = p + 2U;
                block.data_size = end - (p + 2U);
                block.id = type;
                omc_scan_sink_emit(&sink, &block);
            }
        } else if (type == OMC_FOURCC('t', 'E', 'X', 't')) {
            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_PNG;
            block.kind = OMC_BLK_TEXT;
            block.outer_offset = chunk_off;
            block.outer_size = chunk_size;
            block.data_offset = data_off;
            block.data_size = data_size;
            block.id = type;
            omc_scan_sink_emit(&sink, &block);
        }

    next_png_chunk:
        offset += chunk_size;
        if (type == OMC_FOURCC('I', 'E', 'N', 'D')) {
            break;
        }
    }

    return sink.result;
}

omc_scan_res
omc_scan_meas_png(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_png(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_webp(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_u32 riff_size;
    omc_u64 file_end;
    omc_u64 offset;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 12U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (!omc_scan_match(bytes, size, 0U, "RIFF", 4U)
        || !omc_scan_match(bytes, size, 8U, "WEBP", 4U)) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }
    if (!omc_scan_read_u32le(bytes, size, 4U, &riff_size)) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    file_end = ((omc_u64)riff_size + 8U < (omc_u64)size)
                   ? ((omc_u64)riff_size + 8U)
                   : (omc_u64)size;
    offset = 12U;
    while (offset + 8U <= file_end) {
        omc_u64 chunk_off;
        omc_u32 type;
        omc_u32 size_le;
        omc_u64 data_off;
        omc_u64 data_size;
        omc_u64 next;
        omc_blk_ref block;

        chunk_off = offset;
        if (!omc_scan_read_u32be(bytes, size, offset, &type)
            || !omc_scan_read_u32le(bytes, size, offset + 4U, &size_le)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        data_off = offset + 8U;
        data_size = size_le;
        next = data_off + data_size;
        if (next > file_end) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }
        if ((data_size & 1U) != 0U) {
            next += 1U;
            if (next > file_end) {
                sink.result.status = OMC_SCAN_MALFORMED;
                return sink.result;
            }
        }

        if (type == OMC_FOURCC('E', 'X', 'I', 'F')) {
            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_WEBP;
            block.kind = OMC_BLK_EXIF;
            block.outer_offset = chunk_off;
            block.outer_size = next - chunk_off;
            block.data_offset = data_off;
            block.data_size = data_size;
            block.id = type;
            omc_scan_skip_exif_preamble(&block, bytes, size);
            omc_scan_sink_emit(&sink, &block);
        } else if (type == OMC_FOURCC('X', 'M', 'P', ' ')) {
            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_WEBP;
            block.kind = OMC_BLK_XMP;
            block.outer_offset = chunk_off;
            block.outer_size = next - chunk_off;
            block.data_offset = data_off;
            block.data_size = data_size;
            block.id = type;
            omc_scan_sink_emit(&sink, &block);
        } else if (type == OMC_FOURCC('I', 'C', 'C', 'P')) {
            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_WEBP;
            block.kind = OMC_BLK_ICC;
            block.outer_offset = chunk_off;
            block.outer_size = next - chunk_off;
            block.data_offset = data_off;
            block.data_size = data_size;
            block.id = type;
            omc_scan_sink_emit(&sink, &block);
        }

        offset = next;
    }

    return sink.result;
}

omc_scan_res
omc_scan_meas_webp(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_webp(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_gif(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_u64 offset;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 13U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (!omc_scan_match(bytes, size, 0U, "GIF87a", 6U)
        && !omc_scan_match(bytes, size, 0U, "GIF89a", 6U)) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    offset = 6U;
    if (offset + 7U > (omc_u64)size) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    {
        omc_u8 packed;

        packed = bytes[(omc_size)offset + 4U];
        offset += 7U;
        if ((packed & 0x80U) != 0U) {
            omc_u64 gct_bytes;

            gct_bytes = (omc_u64)3U << ((packed & 0x07U) + 1U);
            if (offset + gct_bytes > (omc_u64)size) {
                sink.result.status = OMC_SCAN_MALFORMED;
                return sink.result;
            }
            offset += gct_bytes;
        }
    }

    while (offset < (omc_u64)size) {
        omc_u8 introducer;

        introducer = bytes[(omc_size)offset];
        if (introducer == 0x3BU) {
            break;
        }
        if (introducer == 0x21U) {
            omc_u8 label;

            if (offset + 2U > (omc_u64)size) {
                sink.result.status = OMC_SCAN_MALFORMED;
                return sink.result;
            }
            label = bytes[(omc_size)offset + 1U];
            if (label == 0xFEU) {
                omc_u64 data_off;
                omc_u64 ext_end;
                omc_blk_ref block;

                data_off = offset + 2U;
                if (!omc_scan_skip_gif_sub_blocks(bytes, size, data_off,
                                                  &ext_end)) {
                    sink.result.status = OMC_SCAN_MALFORMED;
                    return sink.result;
                }
                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_GIF;
                block.kind = OMC_BLK_COMMENT;
                block.chunking = OMC_BLK_CHUNK_GIF_SUB;
                block.outer_offset = offset;
                block.outer_size = ext_end - offset;
                block.data_offset = data_off;
                block.data_size = ext_end - data_off;
                block.id = 0x21FEU;
                omc_scan_sink_emit(&sink, &block);
                offset = ext_end;
                continue;
            }
            if (label == 0xFFU) {
                omc_u8 app_block_size;
                omc_u64 app_id_off;
                omc_u64 data_off;
                omc_u64 ext_end;
                int is_xmp;
                int is_icc;

                if (offset + 3U > (omc_u64)size) {
                    sink.result.status = OMC_SCAN_MALFORMED;
                    return sink.result;
                }
                app_block_size = bytes[(omc_size)offset + 2U];
                if (offset + 3U + (omc_u64)app_block_size > (omc_u64)size) {
                    sink.result.status = OMC_SCAN_MALFORMED;
                    return sink.result;
                }

                app_id_off = offset + 3U;
                data_off = app_id_off + (omc_u64)app_block_size;
                if (!omc_scan_skip_gif_sub_blocks(bytes, size, data_off,
                                                  &ext_end)) {
                    sink.result.status = OMC_SCAN_MALFORMED;
                    return sink.result;
                }

                is_xmp = 0;
                is_icc = 0;
                if (app_block_size == 11U) {
                    is_xmp = omc_scan_match(bytes, size, app_id_off,
                                            "XMP Data", 8U)
                             && omc_scan_match(bytes, size, app_id_off + 8U,
                                               "XMP", 3U);
                    is_icc = omc_scan_match(bytes, size, app_id_off,
                                            "ICCRGBG1", 8U)
                             && omc_scan_match(bytes, size, app_id_off + 8U,
                                               "012", 3U);
                }
                if (is_xmp || is_icc) {
                    omc_blk_ref block;

                    omc_scan_init_block(&block);
                    block.format = OMC_SCAN_FMT_GIF;
                    block.kind = is_xmp ? OMC_BLK_XMP : OMC_BLK_ICC;
                    block.chunking = OMC_BLK_CHUNK_GIF_SUB;
                    block.outer_offset = offset;
                    block.outer_size = ext_end - offset;
                    block.data_offset = data_off;
                    block.data_size = ext_end - data_off;
                    block.id = 0x21FFU;
                    omc_scan_sink_emit(&sink, &block);
                }

                offset = ext_end;
                continue;
            }

            if (!omc_scan_skip_gif_sub_blocks(bytes, size, offset + 2U,
                                              &offset)) {
                sink.result.status = OMC_SCAN_MALFORMED;
                return sink.result;
            }
            continue;
        }

        if (introducer == 0x2CU) {
            omc_u8 packed;

            if (offset + 10U > (omc_u64)size) {
                sink.result.status = OMC_SCAN_MALFORMED;
                return sink.result;
            }
            packed = bytes[(omc_size)offset + 9U];
            offset += 10U;
            if ((packed & 0x80U) != 0U) {
                omc_u64 lct_bytes;

                lct_bytes = (omc_u64)3U << ((packed & 0x07U) + 1U);
                if (offset + lct_bytes > (omc_u64)size) {
                    sink.result.status = OMC_SCAN_MALFORMED;
                    return sink.result;
                }
                offset += lct_bytes;
            }
            if (offset + 1U > (omc_u64)size) {
                sink.result.status = OMC_SCAN_MALFORMED;
                return sink.result;
            }
            offset += 1U;
            if (!omc_scan_skip_gif_sub_blocks(bytes, size, offset, &offset)) {
                sink.result.status = OMC_SCAN_MALFORMED;
                return sink.result;
            }
            continue;
        }

        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }

    return sink.result;
}

omc_scan_res
omc_scan_meas_gif(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_gif(bytes, size, (omc_blk_ref*)0, 0U));
}

typedef struct omc_bmff_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
    int has_uuid;
    omc_u8 uuid[16];
} omc_bmff_box;

typedef struct omc_bmff_item {
    omc_u32 item_id;
    omc_u32 item_type;
    omc_blk_kind kind;
} omc_bmff_item;

typedef struct omc_bmff_dref_table {
    int parsed;
    omc_u32 count;
    int self_contained[32];
} omc_bmff_dref_table;

typedef struct omc_bmff_iloc_item_refs {
    omc_u32 from_id;
    omc_u16 to_count;
    omc_u32 to_ids[32];
} omc_bmff_iloc_item_refs;

typedef struct omc_bmff_iloc_ref_table {
    int parsed;
    omc_u32 count;
    omc_bmff_iloc_item_refs refs[32];
} omc_bmff_iloc_ref_table;

typedef struct omc_bmff_iloc_extent_map {
    omc_u64 logical_begin;
    omc_u64 logical_end;
    omc_u64 file_off;
    omc_u64 file_len;
} omc_bmff_iloc_extent_map;

typedef struct omc_bmff_iloc_item_layout {
    omc_u32 item_id;
    omc_u32 construction_method;
    int valid;
    omc_u16 extent_count;
    omc_u64 total_len;
    omc_bmff_iloc_extent_map extents[32];
} omc_bmff_iloc_item_layout;

typedef struct omc_bmff_resolved_part {
    omc_u64 file_off;
    omc_u64 len;
} omc_bmff_resolved_part;

static omc_u8
omc_scan_ascii_lower(omc_u8 c)
{
    if (c >= (omc_u8)'A' && c <= (omc_u8)'Z') {
        return (omc_u8)(c + ((omc_u8)'a' - (omc_u8)'A'));
    }
    return c;
}

static int
omc_scan_bytes_eq(const omc_u8* a, const omc_u8* b, omc_size n)
{
    omc_size i;

    for (i = 0U; i < n; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int
omc_scan_span_eq_icase(const omc_u8* bytes, omc_u64 start, omc_u64 end,
                       const char* lit)
{
    omc_size lit_len;
    omc_size i;

    if (bytes == (const omc_u8*)0 || lit == (const char*)0) {
        return 0;
    }
    lit_len = strlen(lit);
    if (start > end || (omc_u64)lit_len != end - start) {
        return 0;
    }
    for (i = 0U; i < lit_len; ++i) {
        if (omc_scan_ascii_lower(bytes[(omc_size)start + i])
            != omc_scan_ascii_lower((omc_u8)lit[i])) {
            return 0;
        }
    }
    return 1;
}

static int
omc_scan_find_cstring_end(const omc_u8* bytes, omc_size size,
                          omc_u64 start, omc_u64 limit, omc_u64* out_end)
{
    omc_u64 p;

    if (bytes == (const omc_u8*)0 || out_end == (omc_u64*)0) {
        return 0;
    }
    if (start > limit || limit > (omc_u64)size) {
        return 0;
    }

    p = start;
    while (p < limit && bytes[(omc_size)p] != 0U) {
        p += 1U;
    }
    if (p >= limit) {
        return 0;
    }
    *out_end = p;
    return 1;
}

static int
omc_scan_bmff_mime_is_xmp(const omc_u8* bytes, omc_u64 start, omc_u64 end)
{
    return omc_scan_span_eq_icase(bytes, start, end, "application/rdf+xml")
           || omc_scan_span_eq_icase(bytes, start, end, "application/xmp+xml")
           || omc_scan_span_eq_icase(bytes, start, end, "text/xml")
           || omc_scan_span_eq_icase(bytes, start, end, "application/xml");
}

static int
omc_scan_bmff_mime_is_jumbf(const omc_u8* bytes, omc_u64 start, omc_u64 end)
{
    return omc_scan_span_eq_icase(bytes, start, end, "application/jumbf")
           || omc_scan_span_eq_icase(bytes, start, end, "application/c2pa");
}

static int
omc_scan_parse_bmff_box(const omc_u8* bytes, omc_size size,
                        omc_u64 offset, omc_u64 parent_end,
                        omc_bmff_box* out_box)
{
    omc_u32 size32;
    omc_u32 type;
    omc_u64 header_size;
    omc_u64 box_size;
    omc_size i;

    if (out_box == (omc_bmff_box*)0) {
        return 0;
    }
    if (offset > parent_end || parent_end > (omc_u64)size) {
        return 0;
    }
    if (offset + 8U > parent_end || offset + 8U > (omc_u64)size) {
        return 0;
    }
    if (!omc_scan_read_u32be(bytes, size, offset + 0U, &size32)
        || !omc_scan_read_u32be(bytes, size, offset + 4U, &type)) {
        return 0;
    }

    header_size = 8U;
    box_size = size32;
    if (size32 == 1U) {
        if (!omc_scan_read_u64be(bytes, size, offset + 8U, &box_size)) {
            return 0;
        }
        header_size = 16U;
    } else if (size32 == 0U) {
        box_size = parent_end - offset;
    }

    if (box_size < header_size) {
        return 0;
    }
    if (offset + box_size > parent_end || offset + box_size > (omc_u64)size) {
        return 0;
    }

    memset(out_box, 0, sizeof(*out_box));
    out_box->offset = offset;
    out_box->size = box_size;
    out_box->header_size = header_size;
    out_box->type = type;

    if (type == OMC_FOURCC('u', 'u', 'i', 'd')) {
        if (header_size + 16U > box_size) {
            return 0;
        }
        out_box->has_uuid = 1;
        for (i = 0U; i < 16U; ++i) {
            out_box->uuid[i] = bytes[(omc_size)(offset + header_size + i)];
        }
        out_box->header_size += 16U;
    }

    return 1;
}

static void
omc_scan_skip_bmff_exif_offset(omc_blk_ref* block, const omc_u8* bytes,
                               omc_size size);

static int
omc_scan_jp2_emit_uuid_payload(const omc_u8* bytes, omc_size size,
                               const omc_bmff_box* box, omc_scan_sink* sink)
{
    static const omc_u8 k_uuid_exif[16] = {
        0x4AU, 0x70U, 0x67U, 0x54U, 0x69U, 0x66U, 0x66U, 0x45U,
        0x78U, 0x69U, 0x66U, 0x2DU, 0x3EU, 0x4AU, 0x50U, 0x32U
    };
    static const omc_u8 k_uuid_xmp[16] = {
        0xBEU, 0x7AU, 0xCFU, 0xCBU, 0x97U, 0xA9U, 0x42U, 0xE8U,
        0x9CU, 0x71U, 0x99U, 0x94U, 0x91U, 0xE3U, 0xAFU, 0xACU
    };
    static const omc_u8 k_uuid_iptc[16] = {
        0x33U, 0xC7U, 0xA4U, 0xD2U, 0xB8U, 0x1DU, 0x47U, 0x23U,
        0xA0U, 0xBAU, 0xF1U, 0xA3U, 0xE0U, 0x97U, 0xADU, 0x38U
    };
    static const omc_u8 k_uuid_geotiff[16] = {
        0xB1U, 0x4BU, 0xF8U, 0xBDU, 0x08U, 0x3DU, 0x4BU, 0x43U,
        0xA5U, 0xAEU, 0x8CU, 0xD7U, 0xD5U, 0xA6U, 0xCEU, 0x03U
    };
    omc_blk_ref block;

    if (box == (const omc_bmff_box*)0 || !box->has_uuid) {
        return 0;
    }

    omc_scan_init_block(&block);
    block.format = OMC_SCAN_FMT_JP2;
    block.outer_offset = box->offset;
    block.outer_size = box->size;
    block.data_offset = box->offset + box->header_size;
    block.data_size = box->size - box->header_size;
    block.id = box->type;
    block.chunking = OMC_BLK_CHUNK_JP2_UUID;

    if (omc_scan_bytes_eq(box->uuid, k_uuid_exif, 16U)) {
        block.kind = OMC_BLK_EXIF;
        omc_scan_skip_exif_preamble(&block, bytes, size);
        omc_scan_sink_emit(sink, &block);
        return 1;
    }
    if (omc_scan_bytes_eq(box->uuid, k_uuid_xmp, 16U)) {
        block.kind = OMC_BLK_XMP;
        omc_scan_sink_emit(sink, &block);
        return 1;
    }
    if (omc_scan_bytes_eq(box->uuid, k_uuid_iptc, 16U)) {
        block.kind = OMC_BLK_IPTC_IIM;
        omc_scan_sink_emit(sink, &block);
        return 1;
    }
    if (omc_scan_bytes_eq(box->uuid, k_uuid_geotiff, 16U)) {
        block.kind = OMC_BLK_EXIF;
        omc_scan_sink_emit(sink, &block);
        return 1;
    }
    return 0;
}

static int
omc_scan_jp2_emit_direct_metadata_box(const omc_u8* bytes, omc_size size,
                                      const omc_bmff_box* box,
                                      omc_scan_sink* sink)
{
    omc_u64 payload_off;
    omc_u64 payload_size;

    if (box == (const omc_bmff_box*)0) {
        return 0;
    }

    payload_off = box->offset + box->header_size;
    payload_size = box->size - box->header_size;

    if (box->type == OMC_FOURCC('x', 'm', 'l', ' ')) {
        omc_blk_ref block;

        omc_scan_init_block(&block);
        block.format = OMC_SCAN_FMT_JP2;
        block.kind = OMC_BLK_XMP;
        block.outer_offset = box->offset;
        block.outer_size = box->size;
        block.data_offset = payload_off;
        block.data_size = payload_size;
        block.id = box->type;
        omc_scan_sink_emit(sink, &block);
        return 1;
    }

    if (box->type == OMC_FOURCC('E', 'x', 'i', 'f')) {
        omc_blk_ref block;

        omc_scan_init_block(&block);
        block.format = OMC_SCAN_FMT_JP2;
        block.kind = OMC_BLK_EXIF;
        block.outer_offset = box->offset;
        block.outer_size = box->size;
        block.data_offset = payload_off;
        block.data_size = payload_size;
        block.id = box->type;
        omc_scan_skip_bmff_exif_offset(&block, bytes, size);
        omc_scan_skip_exif_preamble(&block, bytes, size);
        omc_scan_sink_emit(sink, &block);
        return 1;
    }

    if (box->type == OMC_FOURCC('c', 'o', 'l', 'r') && payload_size >= 3U) {
        omc_u8 method;

        method = bytes[(omc_size)payload_off];
        if (method == 2U || method == 3U) {
            omc_blk_ref block;

            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_JP2;
            block.kind = OMC_BLK_ICC;
            block.outer_offset = box->offset;
            block.outer_size = box->size;
            block.data_offset = payload_off + 3U;
            block.data_size = payload_size - 3U;
            block.id = box->type;
            block.aux_u32 = method;
            omc_scan_sink_emit(sink, &block);
            return 1;
        }
    }

    return 0;
}

omc_scan_res
omc_scan_jp2(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_u32 first_size;
    omc_u32 first_type;
    omc_u64 off;
    omc_u64 end;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 12U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (!omc_scan_read_u32be(bytes, size, 0U, &first_size)
        || !omc_scan_read_u32be(bytes, size, 4U, &first_type)) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (first_size != 12U || first_type != OMC_FOURCC('j', 'P', ' ', ' ')) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }
    if (!omc_scan_read_u32be(bytes, size, 8U, &first_size)
        || first_size != 0x0D0A870AU) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    off = 0U;
    end = (omc_u64)size;
    while (off < end) {
        omc_bmff_box box;

        if (!omc_scan_parse_bmff_box(bytes, size, off, end, &box)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        (void)omc_scan_jp2_emit_direct_metadata_box(bytes, size, &box, &sink);
        if (box.type == OMC_FOURCC('u', 'u', 'i', 'd')) {
            (void)omc_scan_jp2_emit_uuid_payload(bytes, size, &box, &sink);
        }
        if (box.type == OMC_FOURCC('j', 'p', '2', 'h')) {
            omc_u64 child_off;
            omc_u64 child_end;

            child_off = box.offset + box.header_size;
            child_end = box.offset + box.size;
            while (child_off < child_end) {
                omc_bmff_box child;

                if (!omc_scan_parse_bmff_box(bytes, size, child_off, child_end,
                                             &child)) {
                    break;
                }
                (void)omc_scan_jp2_emit_direct_metadata_box(bytes, size,
                                                            &child, &sink);
                child_off += child.size;
                if (child.size == 0U) {
                    break;
                }
            }
        }

        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }

    return sink.result;
}

omc_scan_res
omc_scan_meas_jp2(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_jp2(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_jxl(const omc_u8* bytes, omc_size size,
             omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_u32 first_size;
    omc_u32 first_type;
    omc_u64 off;
    omc_u64 end;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 12U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (!omc_scan_read_u32be(bytes, size, 0U, &first_size)
        || !omc_scan_read_u32be(bytes, size, 4U, &first_type)) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (first_size != 12U || first_type != OMC_FOURCC('J', 'X', 'L', ' ')) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }
    if (!omc_scan_read_u32be(bytes, size, 8U, &first_size)
        || first_size != 0x0D0A870AU) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    off = 0U;
    end = (omc_u64)size;
    while (off < end) {
        omc_bmff_box box;
        omc_u64 payload_off;
        omc_u64 payload_size;

        if (!omc_scan_parse_bmff_box(bytes, size, off, end, &box)) {
            sink.result.status = OMC_SCAN_MALFORMED;
            return sink.result;
        }

        payload_off = box.offset + box.header_size;
        payload_size = box.size - box.header_size;

        if (box.type == OMC_FOURCC('E', 'x', 'i', 'f')) {
            omc_blk_ref block;

            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_JXL;
            block.kind = OMC_BLK_EXIF;
            block.outer_offset = box.offset;
            block.outer_size = box.size;
            block.data_offset = payload_off;
            block.data_size = payload_size;
            block.id = box.type;
            omc_scan_skip_bmff_exif_offset(&block, bytes, size);
            omc_scan_sink_emit(&sink, &block);
        } else if (box.type == OMC_FOURCC('x', 'm', 'l', ' ')) {
            omc_blk_ref block;

            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_JXL;
            block.kind = OMC_BLK_XMP;
            block.outer_offset = box.offset;
            block.outer_size = box.size;
            block.data_offset = payload_off;
            block.data_size = payload_size;
            block.id = box.type;
            omc_scan_sink_emit(&sink, &block);
        } else if (box.type == OMC_FOURCC('j', 'u', 'm', 'b')
                   || box.type == OMC_FOURCC('c', '2', 'p', 'a')) {
            omc_blk_ref block;

            omc_scan_init_block(&block);
            block.format = OMC_SCAN_FMT_JXL;
            block.kind = OMC_BLK_JUMBF;
            block.outer_offset = box.offset;
            block.outer_size = box.size;
            block.data_offset = payload_off;
            block.data_size = payload_size;
            block.id = box.type;
            omc_scan_sink_emit(&sink, &block);
        } else if (box.type == OMC_FOURCC('b', 'r', 'o', 'b')
                   && payload_size >= 4U) {
            omc_u32 realtype;

            if (omc_scan_read_u32be(bytes, size, payload_off, &realtype)) {
                omc_blk_ref block;

                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_JXL;
                block.kind = OMC_BLK_COMP_METADATA;
                block.compression = OMC_BLK_COMP_BROTLI;
                block.chunking = OMC_BLK_CHUNK_BROB_REALTYPE;
                block.outer_offset = box.offset;
                block.outer_size = box.size;
                block.data_offset = payload_off + 4U;
                block.data_size = payload_size - 4U;
                block.id = box.type;
                block.aux_u32 = realtype;
                omc_scan_sink_emit(&sink, &block);
            }
        }

        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }

    return sink.result;
}

omc_scan_res
omc_scan_meas_jxl(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_jxl(bytes, size, (omc_blk_ref*)0, 0U));
}

static int
omc_scan_bmff_type_looks_ascii(omc_u32 type)
{
    omc_u32 i;

    for (i = 0U; i < 4U; ++i) {
        omc_u8 b;

        b = (omc_u8)((type >> ((3U - i) * 8U)) & 0xFFU);
        if (b < 0x20U || b > 0x7EU) {
            return 0;
        }
    }
    return 1;
}

static int
omc_scan_bmff_payload_may_contain_boxes(const omc_u8* bytes, omc_size size,
                                        omc_u64 payload_off, omc_u64 payload_end)
{
    omc_u32 size32;
    omc_u32 type;
    omc_u64 size64;

    if (payload_off + 8U > payload_end || payload_end > (omc_u64)size) {
        return 0;
    }
    if (!omc_scan_read_u32be(bytes, size, payload_off + 0U, &size32)
        || !omc_scan_read_u32be(bytes, size, payload_off + 4U, &type)) {
        return 0;
    }
    if (!omc_scan_bmff_type_looks_ascii(type)) {
        return 0;
    }

    if (size32 == 0U) {
        return 1;
    }
    if (size32 == 1U) {
        if (!omc_scan_read_u64be(bytes, size, payload_off + 8U, &size64)) {
            return 0;
        }
        if (size64 < 16U) {
            return 0;
        }
        return payload_off + size64 <= payload_end;
    }
    if (size32 < 8U) {
        return 0;
    }
    return payload_off + (omc_u64)size32 <= payload_end;
}

static void
omc_scan_skip_bmff_exif_offset(omc_blk_ref* block, const omc_u8* bytes,
                               omc_size size)
{
    omc_u32 tiff_off;
    omc_u64 skip;

    if (block == (omc_blk_ref*)0 || block->data_size < 4U) {
        return;
    }
    if (!omc_scan_read_u32be(bytes, size, block->data_offset, &tiff_off)) {
        return;
    }
    skip = 4U + (omc_u64)tiff_off;
    if (skip >= block->data_size) {
        return;
    }
    block->data_offset += skip;
    block->data_size -= skip;
}

static omc_scan_fmt
omc_scan_bmff_format_from_ftyp(const omc_u8* bytes, omc_size size,
                               const omc_bmff_box* ftyp)
{
    omc_u64 payload_off;
    omc_u64 payload_size;
    omc_u32 major_brand;
    int is_heif;
    int is_avif;
    int is_cr3;
    int is_jp2;
    omc_u64 off;

    if (ftyp == (const omc_bmff_box*)0) {
        return OMC_SCAN_FMT_UNKNOWN;
    }

    payload_off = ftyp->offset + ftyp->header_size;
    payload_size = ftyp->size - ftyp->header_size;
    if (payload_size < 8U) {
        return OMC_SCAN_FMT_UNKNOWN;
    }
    if (!omc_scan_read_u32be(bytes, size, payload_off, &major_brand)) {
        return OMC_SCAN_FMT_UNKNOWN;
    }

    is_heif = 0;
    is_avif = 0;
    is_cr3 = 0;
    is_jp2 = 0;

#define OMC_NOTE_BMFF_BRAND(v) \
    do { \
        if ((v) == OMC_FOURCC('c', 'r', 'x', ' ') \
            || (v) == OMC_FOURCC('C', 'R', '3', ' ')) { \
            is_cr3 = 1; \
        } \
        if ((v) == OMC_FOURCC('a', 'v', 'i', 'f') \
            || (v) == OMC_FOURCC('a', 'v', 'i', 's')) { \
            is_avif = 1; \
        } \
        if ((v) == OMC_FOURCC('m', 'i', 'f', '1') \
            || (v) == OMC_FOURCC('m', 's', 'f', '1') \
            || (v) == OMC_FOURCC('h', 'e', 'i', 'c') \
            || (v) == OMC_FOURCC('h', 'e', 'i', 'x') \
            || (v) == OMC_FOURCC('h', 'e', 'v', 'c') \
            || (v) == OMC_FOURCC('h', 'e', 'v', 'x')) { \
            is_heif = 1; \
        } \
        if ((v) == OMC_FOURCC('j', 'p', '2', ' ') \
            || (v) == OMC_FOURCC('j', 'p', 'x', ' ') \
            || (v) == OMC_FOURCC('j', 'p', 'm', ' ') \
            || (v) == OMC_FOURCC('m', 'j', 'p', '2') \
            || (v) == OMC_FOURCC('j', 'p', 'h', ' ') \
            || (v) == OMC_FOURCC('j', 'h', 'c', ' ') \
            || (v) == OMC_FOURCC('j', 'p', 'f', ' ')) { \
            is_jp2 = 1; \
        } \
    } while (0)

    OMC_NOTE_BMFF_BRAND(major_brand);
    off = payload_off + 8U;
    while (off + 4U <= payload_off + payload_size) {
        omc_u32 brand;

        if (!omc_scan_read_u32be(bytes, size, off, &brand)) {
            return OMC_SCAN_FMT_UNKNOWN;
        }
        OMC_NOTE_BMFF_BRAND(brand);
        off += 4U;
    }

#undef OMC_NOTE_BMFF_BRAND

    if (is_cr3) {
        return OMC_SCAN_FMT_CR3;
    }
    if (is_avif) {
        return OMC_SCAN_FMT_AVIF;
    }
    if (is_jp2) {
        return OMC_SCAN_FMT_JP2;
    }
    if (is_heif) {
        return OMC_SCAN_FMT_HEIF;
    }
    return OMC_SCAN_FMT_UNKNOWN;
}

static int
omc_scan_bmff_find_ftyp(const omc_u8* bytes, omc_size size,
                        omc_bmff_box* out_ftyp)
{
    omc_u64 off;
    omc_u32 seen;
    omc_bmff_box box;

    if (out_ftyp == (omc_bmff_box*)0) {
        return 0;
    }

    off = 0U;
    seen = 0U;
    while (off + 8U <= (omc_u64)size && seen < (1U << 14)) {
        seen += 1U;
        if (!omc_scan_parse_bmff_box(bytes, size, off, (omc_u64)size, &box)) {
            return 0;
        }
        if (box.type == OMC_FOURCC('f', 't', 'y', 'p')) {
            *out_ftyp = box;
            return 1;
        }
        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }
    return 0;
}

static int
omc_scan_bmff_uuid_is(const omc_bmff_box* box, const omc_u8* uuid)
{
    if (box == (const omc_bmff_box*)0 || uuid == (const omc_u8*)0
        || !box->has_uuid) {
        return 0;
    }
    return omc_scan_bytes_eq(box->uuid, uuid, 16U);
}

static void
omc_scan_bmff_emit_uuid_payload(omc_scan_sink* sink, omc_scan_fmt format,
                                omc_blk_kind kind, const omc_bmff_box* box)
{
    omc_blk_ref block;

    omc_scan_init_block(&block);
    block.format = format;
    block.kind = kind;
    block.outer_offset = box->offset;
    block.outer_size = box->size;
    block.data_offset = box->offset + box->header_size;
    block.data_size = box->size - box->header_size;
    block.id = box->type;
    omc_scan_sink_emit(sink, &block);
}

static int
omc_scan_bmff_collect_meta_items(const omc_u8* bytes, omc_size size,
                                 const omc_bmff_box* iinf,
                                 omc_bmff_item* out_items,
                                 omc_u32 item_cap, omc_u32* out_count)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u32 entry_count;
    omc_u64 p;
    omc_u32 seen;

    if (iinf == (const omc_bmff_box*)0 || out_count == (omc_u32*)0) {
        return 0;
    }
    *out_count = 0U;

    payload_off = iinf->offset + iinf->header_size;
    payload_end = iinf->offset + iinf->size;
    if (payload_off + 4U > payload_end) {
        return 0;
    }

    p = payload_off + 4U;
    if (bytes[(omc_size)payload_off] < 2U) {
        omc_u16 ec16;

        if (!omc_scan_read_u16be(bytes, size, p, &ec16)) {
            return 0;
        }
        entry_count = ec16;
        p += 2U;
    } else {
        if (!omc_scan_read_u32be(bytes, size, p, &entry_count)) {
            return 0;
        }
        p += 4U;
    }
    if (entry_count > 4096U) {
        return 0;
    }

    seen = 0U;
    while (p + 8U <= payload_end && seen < entry_count) {
        omc_bmff_box infe;

        if (!omc_scan_parse_bmff_box(bytes, size, p, payload_end, &infe)) {
            return 0;
        }
        if (infe.type == OMC_FOURCC('i', 'n', 'f', 'e')) {
            omc_u64 q;
            omc_u64 infe_end;
            omc_u32 item_id;
            omc_u32 item_type;
            omc_blk_kind kind;
            omc_u64 name_end;

            q = infe.offset + infe.header_size;
            infe_end = infe.offset + infe.size;
            if (q + 4U > infe_end) {
                return 0;
            }

            item_id = 0U;
            item_type = 0U;
            kind = OMC_BLK_UNKNOWN;

            if (bytes[(omc_size)q] == 2U) {
                omc_u16 id16;

                q += 4U;
                if (!omc_scan_read_u16be(bytes, size, q, &id16)) {
                    return 0;
                }
                item_id = id16;
                q += 2U;
            } else if (bytes[(omc_size)q] >= 3U) {
                q += 4U;
                if (!omc_scan_read_u32be(bytes, size, q, &item_id)) {
                    return 0;
                }
                q += 4U;
            } else {
                p += infe.size;
                if (infe.size == 0U) {
                    break;
                }
                seen += 1U;
                continue;
            }

            q += 2U;
            if (!omc_scan_read_u32be(bytes, size, q, &item_type)) {
                return 0;
            }
            q += 4U;

            if (!omc_scan_find_cstring_end(bytes, size, q, infe_end,
                                           &name_end)) {
                return 0;
            }
            q = name_end + 1U;

            if (item_type == OMC_FOURCC('E', 'x', 'i', 'f')) {
                kind = OMC_BLK_EXIF;
            } else if (item_type == OMC_FOURCC('x', 'm', 'l', ' ')) {
                kind = OMC_BLK_XMP;
            } else if (item_type == OMC_FOURCC('j', 'u', 'm', 'b')
                       || item_type == OMC_FOURCC('c', '2', 'p', 'a')) {
                kind = OMC_BLK_JUMBF;
            } else if (item_type == OMC_FOURCC('m', 'i', 'm', 'e')) {
                omc_u64 ct_end;

                if (!omc_scan_find_cstring_end(bytes, size, q, infe_end,
                                               &ct_end)) {
                    ct_end = infe_end;
                }
                if (omc_scan_bmff_mime_is_xmp(bytes, q, ct_end)) {
                    kind = OMC_BLK_XMP;
                } else if (omc_scan_bmff_mime_is_jumbf(bytes, q, ct_end)) {
                    kind = OMC_BLK_JUMBF;
                }
            }

            if (kind != OMC_BLK_UNKNOWN) {
                if (*out_count < item_cap) {
                    out_items[*out_count].item_id = item_id;
                    out_items[*out_count].item_type = item_type;
                    out_items[*out_count].kind = kind;
                }
                *out_count += 1U;
            }
        }

        p += infe.size;
        if (infe.size == 0U) {
            break;
        }
        seen += 1U;
    }

    if (*out_count > item_cap) {
        *out_count = item_cap;
    }
    return 1;
}

static const omc_bmff_item*
omc_scan_bmff_find_item(const omc_bmff_item* items, omc_u32 item_count,
                        omc_u32 item_id)
{
    omc_u32 i;

    for (i = 0U; i < item_count; ++i) {
        if (items[i].item_id == item_id) {
            return &items[i];
        }
    }
    return (const omc_bmff_item*)0;
}

static int
omc_scan_bmff_parse_dref_table(const omc_u8* bytes, omc_size size,
                               const omc_bmff_box* dref,
                               omc_bmff_dref_table* out_table)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u32 entry_count;
    omc_u64 off;
    omc_u32 idx;
    omc_u32 take;

    if (out_table == (omc_bmff_dref_table*)0) {
        return 0;
    }
    memset(out_table, 0, sizeof(*out_table));
    if (dref == (const omc_bmff_box*)0) {
        return 0;
    }

    payload_off = dref->offset + dref->header_size;
    payload_end = dref->offset + dref->size;
    if (payload_off + 8U > payload_end || payload_end > (omc_u64)size) {
        return 0;
    }
    if (!omc_scan_read_u32be(bytes, size, payload_off + 4U, &entry_count)) {
        return 0;
    }

    off = payload_off + 8U;
    take = entry_count < (1U << 16) ? entry_count : (1U << 16);
    idx = 0U;
    while (off + 8U <= payload_end && idx < take) {
        omc_bmff_box ent;
        int self_contained;

        if (!omc_scan_parse_bmff_box(bytes, size, off, payload_end, &ent)) {
            return 0;
        }

        self_contained = 0;
        if (ent.type == OMC_FOURCC('u', 'r', 'l', ' ')
            || ent.type == OMC_FOURCC('u', 'r', 'n', ' ')) {
            omc_u64 ent_payload_off;
            omc_u64 ent_payload_end;
            omc_u32 vf;

            ent_payload_off = ent.offset + ent.header_size;
            ent_payload_end = ent.offset + ent.size;
            vf = 0U;
            if (ent_payload_off + 4U <= ent_payload_end
                && ent_payload_end <= (omc_u64)size
                && omc_scan_read_u32be(bytes, size, ent_payload_off, &vf)) {
                self_contained = ((vf & 0x000001U) != 0U);
            }
        }

        if (idx < 32U) {
            out_table->self_contained[idx] = self_contained;
        }
        idx += 1U;
        off += ent.size;
        if (ent.size == 0U) {
            break;
        }
    }

    out_table->count = idx;
    out_table->parsed = 1;
    return 1;
}

static int
omc_scan_bmff_data_ref_is_self_contained(const omc_bmff_dref_table* table,
                                         omc_u16 data_ref)
{
    omc_u32 idx;

    if (data_ref == 0U) {
        return 1;
    }
    if (table == (const omc_bmff_dref_table*)0 || !table->parsed) {
        return data_ref == 1U;
    }

    idx = (omc_u32)data_ref - 1U;
    if (idx >= table->count || idx >= 32U) {
        return 0;
    }
    return table->self_contained[idx];
}

static omc_bmff_iloc_item_refs*
omc_scan_bmff_find_or_add_iloc_refs(omc_bmff_iloc_ref_table* table,
                                    omc_u32 from_id)
{
    omc_u32 i;

    if (table == (omc_bmff_iloc_ref_table*)0) {
        return (omc_bmff_iloc_item_refs*)0;
    }

    for (i = 0U; i < table->count; ++i) {
        if (table->refs[i].from_id == from_id) {
            return &table->refs[i];
        }
    }
    if (table->count >= 32U) {
        return (omc_bmff_iloc_item_refs*)0;
    }

    memset(&table->refs[table->count], 0, sizeof(table->refs[table->count]));
    table->refs[table->count].from_id = from_id;
    table->count += 1U;
    return &table->refs[table->count - 1U];
}

static const omc_bmff_iloc_item_refs*
omc_scan_bmff_find_iloc_refs(const omc_bmff_iloc_ref_table* table,
                             omc_u32 from_id)
{
    omc_u32 i;

    if (table == (const omc_bmff_iloc_ref_table*)0 || !table->parsed) {
        return (const omc_bmff_iloc_item_refs*)0;
    }

    for (i = 0U; i < table->count; ++i) {
        if (table->refs[i].from_id == from_id) {
            return &table->refs[i];
        }
    }
    return (const omc_bmff_iloc_item_refs*)0;
}

static int
omc_scan_bmff_iloc_refs_to_item(const omc_bmff_iloc_ref_table* table,
                                omc_u32 item_id)
{
    omc_u32 i;
    omc_u32 j;

    if (table == (const omc_bmff_iloc_ref_table*)0 || !table->parsed) {
        return 0;
    }

    for (i = 0U; i < table->count; ++i) {
        const omc_bmff_iloc_item_refs* refs;

        refs = &table->refs[i];
        for (j = 0U; j < refs->to_count; ++j) {
            if (refs->to_ids[j] == item_id) {
                return 1;
            }
        }
    }
    return 0;
}

static int
omc_scan_bmff_lookup_iloc_reference_item_id(
    const omc_bmff_iloc_ref_table* table, omc_u32 from_id,
    omc_u64 extent_index_1based, omc_u32* out_item_id)
{
    const omc_bmff_iloc_item_refs* refs;
    omc_u64 idx0;

    if (out_item_id == (omc_u32*)0) {
        return 0;
    }
    refs = omc_scan_bmff_find_iloc_refs(table, from_id);
    if (refs == (const omc_bmff_iloc_item_refs*)0
        || extent_index_1based == 0U) {
        return 0;
    }

    idx0 = extent_index_1based - 1U;
    if (idx0 > (omc_u64)0xFFFFFFFFU || idx0 >= (omc_u64)refs->to_count) {
        return 0;
    }
    if (refs->to_ids[(omc_u32)idx0] == 0U) {
        return 0;
    }

    *out_item_id = refs->to_ids[(omc_u32)idx0];
    return 1;
}

static int
omc_scan_bmff_parse_iref_iloc_table(const omc_u8* bytes, omc_size size,
                                    const omc_bmff_box* iref,
                                    const omc_bmff_item* items,
                                    omc_u32 item_count,
                                    omc_bmff_iloc_ref_table* out_table)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 version;
    omc_u64 child_off;
    omc_u32 seen;

    if (iref == (const omc_bmff_box*)0
        || out_table == (omc_bmff_iloc_ref_table*)0) {
        return 0;
    }

    memset(out_table, 0, sizeof(*out_table));
    payload_off = iref->offset + iref->header_size;
    payload_end = iref->offset + iref->size;
    if (payload_off + 4U > payload_end || payload_end > (omc_u64)size) {
        return 0;
    }

    version = bytes[(omc_size)payload_off];
    if (version > 1U) {
        return 0;
    }

    child_off = payload_off + 4U;
    seen = 0U;
    while (child_off + 8U <= payload_end) {
        omc_bmff_box child;

        seen += 1U;
        if (seen > (1U << 16)) {
            return 0;
        }
        if (!omc_scan_parse_bmff_box(bytes, size, child_off, payload_end,
                                     &child)) {
            return 0;
        }
        child_off += child.size;
        if (child.size == 0U) {
            break;
        }
        if (child.type == OMC_FOURCC('i', 'l', 'o', 'c')) {
            omc_u64 p;
            omc_u64 child_end;

            p = child.offset + child.header_size;
            child_end = child.offset + child.size;
            if (p > child_end || child_end > (omc_u64)size) {
                return 0;
            }

            while (p < child_end) {
                omc_u32 from_id;
                omc_u16 ref_count;
                omc_bmff_iloc_item_refs* refs;
                omc_u16 i;

                from_id = 0U;
                ref_count = 0U;
                refs = (omc_bmff_iloc_item_refs*)0;

                if (version == 0U) {
                    omc_u16 id16;

                    if (!omc_scan_read_u16be(bytes, size, p, &id16)) {
                        return 0;
                    }
                    from_id = id16;
                    p += 2U;
                } else {
                    if (!omc_scan_read_u32be(bytes, size, p, &from_id)) {
                        return 0;
                    }
                    p += 4U;
                }

                if (!omc_scan_read_u16be(bytes, size, p, &ref_count)) {
                    return 0;
                }
                p += 2U;

                if (omc_scan_bmff_find_item(items, item_count, from_id)
                    != (const omc_bmff_item*)0) {
                    refs = omc_scan_bmff_find_or_add_iloc_refs(out_table,
                                                               from_id);
                }

                for (i = 0U; i < ref_count; ++i) {
                    omc_u32 to_id;

                    to_id = 0U;
                    if (version == 0U) {
                        omc_u16 to16;

                        if (!omc_scan_read_u16be(bytes, size, p, &to16)) {
                            return 0;
                        }
                        to_id = to16;
                        p += 2U;
                    } else {
                        if (!omc_scan_read_u32be(bytes, size, p, &to_id)) {
                            return 0;
                        }
                        p += 4U;
                    }

                    if (refs != (omc_bmff_iloc_item_refs*)0
                        && refs->to_count < 32U && to_id != 0U) {
                        refs->to_ids[refs->to_count] = to_id;
                        refs->to_count += 1U;
                    }
                }
            }
        }
    }

    out_table->parsed = 1;
    return 1;
}

static omc_bmff_iloc_item_layout*
omc_scan_bmff_find_or_add_layout(omc_bmff_iloc_item_layout* layouts,
                                 omc_u32* io_count, omc_u32 item_id)
{
    omc_u32 i;
    omc_u32 count;

    if (layouts == (omc_bmff_iloc_item_layout*)0 || io_count == (omc_u32*)0) {
        return (omc_bmff_iloc_item_layout*)0;
    }

    count = *io_count;
    if (count > 32U) {
        count = 32U;
    }
    for (i = 0U; i < count; ++i) {
        if (layouts[i].item_id == item_id) {
            return &layouts[i];
        }
    }
    if (count >= 32U) {
        return (omc_bmff_iloc_item_layout*)0;
    }

    memset(&layouts[count], 0, sizeof(layouts[count]));
    layouts[count].item_id = item_id;
    *io_count = count + 1U;
    return &layouts[count];
}

static const omc_bmff_iloc_item_layout*
omc_scan_bmff_find_layout(const omc_bmff_iloc_item_layout* layouts,
                          omc_u32 count, omc_u32 item_id)
{
    omc_u32 i;

    if (layouts == (const omc_bmff_iloc_item_layout*)0) {
        return (const omc_bmff_iloc_item_layout*)0;
    }
    if (count > 32U) {
        count = 32U;
    }
    for (i = 0U; i < count; ++i) {
        if (layouts[i].item_id == item_id) {
            return &layouts[i];
        }
    }
    return (const omc_bmff_iloc_item_layout*)0;
}

static int
omc_scan_bmff_resolve_logical_slice_to_file_parts(
    const omc_bmff_iloc_item_layout* layout, omc_u64 logical_off,
    omc_u64 len, omc_bmff_resolved_part* out_parts, omc_u32 out_cap,
    omc_u32* out_count)
{
    omc_u64 logical_end;
    omc_u32 start_idx;
    int found;
    omc_u64 cur;
    omc_u32 take;
    omc_u32 i;

    if (layout == (const omc_bmff_iloc_item_layout*)0 || !layout->valid
        || out_parts == (omc_bmff_resolved_part*)0
        || out_count == (omc_u32*)0) {
        return 0;
    }

    *out_count = 0U;
    if (len == 0U || logical_off > ((omc_u64)(~(omc_u64)0) - len)) {
        return 0;
    }
    logical_end = logical_off + len;

    start_idx = 0U;
    found = 0;
    for (i = 0U; i < layout->extent_count; ++i) {
        const omc_bmff_iloc_extent_map* ex;

        ex = &layout->extents[i];
        if (logical_off >= ex->logical_begin && logical_off < ex->logical_end) {
            start_idx = i;
            found = 1;
            break;
        }
    }
    if (!found) {
        return 0;
    }

    cur = logical_off;
    take = 0U;
    for (i = start_idx; i < layout->extent_count && cur < logical_end; ++i) {
        const omc_bmff_iloc_extent_map* ex;
        omc_u64 end;
        omc_u64 part_len;
        omc_u64 delta;
        omc_u64 file_off;

        ex = &layout->extents[i];
        if (cur < ex->logical_begin) {
            return 0;
        }

        end = logical_end < ex->logical_end ? logical_end : ex->logical_end;
        if (end <= cur) {
            continue;
        }
        part_len = end - cur;
        delta = cur - ex->logical_begin;
        if (delta > ((omc_u64)(~(omc_u64)0) - ex->file_off)) {
            return 0;
        }
        file_off = ex->file_off + delta;
        if (delta > ex->file_len || part_len > ex->file_len - delta) {
            return 0;
        }

        if (take >= out_cap) {
            return 0;
        }
        out_parts[take].file_off = file_off;
        out_parts[take].len = part_len;
        take += 1U;
        cur += part_len;
    }

    if (cur != logical_end) {
        return 0;
    }
    *out_count = take;
    return 1;
}

static int
omc_scan_read_be_n(const omc_u8* bytes, omc_size size, omc_u64 offset,
                   omc_u32 n, omc_u64* out_value)
{
    omc_u32 i;
    omc_u64 v;

    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (n == 0U) {
        *out_value = 0U;
        return 1;
    }
    if (n > 8U) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < (omc_u64)n) {
        return 0;
    }

    v = 0U;
    for (i = 0U; i < n; ++i) {
        v = (v << 8) | (omc_u64)bytes[(omc_size)offset + i];
    }
    *out_value = v;
    return 1;
}

static void
omc_scan_bmff_emit_iloc_items(const omc_u8* bytes, omc_size size,
                              const omc_bmff_box* iloc,
                              const omc_bmff_box* idat,
                              const omc_bmff_box* iref,
                              const omc_bmff_dref_table* dref,
                              const omc_bmff_item* items,
                              omc_u32 item_count, omc_scan_fmt format,
                              omc_scan_sink* sink)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 version;
    omc_u64 p;
    omc_u32 off_size;
    omc_u32 len_size;
    omc_u32 base_size;
    omc_u32 idx_size;
    omc_u32 record_count;
    omc_u64 idat_payload_off;
    omc_u64 idat_payload_end;
    int has_idat;
    omc_bmff_iloc_ref_table iloc_refs;
    omc_bmff_iloc_item_layout layouts[32];
    omc_u32 layout_count;
    omc_u32 i;

    payload_off = iloc->offset + iloc->header_size;
    payload_end = iloc->offset + iloc->size;
    if (payload_off + 4U > payload_end) {
        sink->result.status = OMC_SCAN_MALFORMED;
        return;
    }

    version = bytes[(omc_size)payload_off + 0U];
    p = payload_off + 4U;
    off_size = (omc_u32)((bytes[(omc_size)p + 0U] >> 4) & 0x0FU);
    len_size = (omc_u32)((bytes[(omc_size)p + 0U] >> 0) & 0x0FU);
    base_size = (omc_u32)((bytes[(omc_size)p + 1U] >> 4) & 0x0FU);
    idx_size = (omc_u32)((bytes[(omc_size)p + 1U] >> 0) & 0x0FU);
    p += 2U;
    if (off_size > 8U || len_size > 8U || base_size > 8U || idx_size > 8U) {
        sink->result.status = OMC_SCAN_MALFORMED;
        return;
    }

    if (version < 2U) {
        omc_u16 c16;

        if (!omc_scan_read_u16be(bytes, size, p, &c16)) {
            sink->result.status = OMC_SCAN_MALFORMED;
            return;
        }
        record_count = c16;
        p += 2U;
    } else {
        if (!omc_scan_read_u32be(bytes, size, p, &record_count)) {
            sink->result.status = OMC_SCAN_MALFORMED;
            return;
        }
        p += 4U;
    }
    if (record_count > (1U << 16)) {
        sink->result.status = OMC_SCAN_MALFORMED;
        return;
    }

    has_idat = 0;
    idat_payload_off = 0U;
    idat_payload_end = 0U;
    memset(&iloc_refs, 0, sizeof(iloc_refs));
    memset(layouts, 0, sizeof(layouts));
    layout_count = 0U;
    if (idat != (const omc_bmff_box*)0 && idat->size > 0U) {
        idat_payload_off = idat->offset + idat->header_size;
        idat_payload_end = idat->offset + idat->size;
        has_idat = 1;
    }

    if (iref != (const omc_bmff_box*)0 && iref->size > 0U) {
        (void)omc_scan_bmff_parse_iref_iloc_table(bytes, size, iref, items,
                                                  item_count, &iloc_refs);
    }

    if (iloc_refs.parsed && iloc_refs.count > 0U) {
        omc_u64 q;

        q = p;
        for (i = 0U; i < record_count && q < payload_end; ++i) {
            omc_u32 item_id;
            omc_u32 construction_method;
            omc_u16 data_ref;
            omc_u64 base_off;
            omc_u16 extent_count;
            omc_u64 extent_hdr;
            omc_u64 extent_rec;
            omc_u64 extents_bytes;

            item_id = 0U;
            construction_method = 0U;
            data_ref = 0U;
            base_off = 0U;
            extent_count = 0U;
            extent_hdr = 0U;
            extent_rec = 0U;
            extents_bytes = 0U;

            if (version < 2U) {
                omc_u16 id16;

                if (!omc_scan_read_u16be(bytes, size, q, &id16)) {
                    sink->result.status = OMC_SCAN_MALFORMED;
                    return;
                }
                item_id = id16;
                q += 2U;
            } else {
                if (!omc_scan_read_u32be(bytes, size, q, &item_id)) {
                    sink->result.status = OMC_SCAN_MALFORMED;
                    return;
                }
                q += 4U;
            }

            if (version == 1U || version == 2U) {
                omc_u16 cm;

                if (!omc_scan_read_u16be(bytes, size, q, &cm)) {
                    sink->result.status = OMC_SCAN_MALFORMED;
                    return;
                }
                construction_method = (omc_u32)(cm & 0x000FU);
                q += 2U;
            }

            if (!omc_scan_read_u16be(bytes, size, q, &data_ref)
                || !omc_scan_read_be_n(bytes, size, q + 2U, base_size,
                                       &base_off)
                || !omc_scan_read_u16be(bytes, size, q + 2U + base_size,
                                        &extent_count)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            q += 4U + base_size;

            if (extent_count > (1U << 14)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }

            extent_hdr = ((version == 1U || version == 2U)
                              ? (omc_u64)idx_size
                              : 0U);
            extent_rec = extent_hdr + (omc_u64)off_size + (omc_u64)len_size;
            if (extent_rec != 0U
                && (omc_u64)extent_count > (((omc_u64)(~(omc_u64)0))
                                            / extent_rec)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            extents_bytes = extent_rec * (omc_u64)extent_count;
            if (q + extents_bytes > payload_end) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }

            if (!omc_scan_bmff_iloc_refs_to_item(&iloc_refs, item_id)) {
                q += extents_bytes;
                continue;
            }

            {
                omc_bmff_iloc_item_layout* layout;

                layout = omc_scan_bmff_find_or_add_layout(layouts,
                                                          &layout_count,
                                                          item_id);
                if (layout == (omc_bmff_iloc_item_layout*)0) {
                    q += extents_bytes;
                    continue;
                }

                memset(layout, 0, sizeof(*layout));
                layout->item_id = item_id;
                layout->construction_method = construction_method;
                layout->valid = 0;

                if (!omc_scan_bmff_data_ref_is_self_contained(dref, data_ref)
                    || (construction_method != 0U
                        && construction_method != 1U)) {
                    q += extents_bytes;
                    continue;
                }

                {
                    omc_u64 base_file_off;
                    omc_u64 logical_off;
                    int ok;
                    omc_u16 e;

                    if (construction_method == 1U) {
                        if (!has_idat || idat_payload_off
                            > ((omc_u64)(~(omc_u64)0) - base_off)) {
                            q += extents_bytes;
                            continue;
                        }
                        base_file_off = idat_payload_off + base_off;
                    } else {
                        base_file_off = base_off;
                    }

                    logical_off = 0U;
                    ok = 1;
                    layout->extent_count = 0U;
                    for (e = 0U; e < extent_count; ++e) {
                        omc_u64 extent_off;
                        omc_u64 extent_len;
                        omc_u64 file_off;

                        extent_off = 0U;
                        extent_len = 0U;
                        file_off = base_file_off;

                        if ((version == 1U || version == 2U) && idx_size > 0U) {
                            omc_u64 discard;

                            if (!omc_scan_read_be_n(bytes, size, q, idx_size,
                                                    &discard)) {
                                sink->result.status = OMC_SCAN_MALFORMED;
                                return;
                            }
                            q += idx_size;
                        }

                        if (!omc_scan_read_be_n(bytes, size, q, off_size,
                                                &extent_off)
                            || !omc_scan_read_be_n(bytes, size, q + off_size,
                                                   len_size, &extent_len)) {
                            sink->result.status = OMC_SCAN_MALFORMED;
                            return;
                        }
                        q += off_size + len_size;

                        if (file_off > ((omc_u64)(~(omc_u64)0) - extent_off)) {
                            ok = 0;
                            continue;
                        }
                        file_off += extent_off;

                        if (extent_len == 0U && len_size == 0U
                            && extent_count == 1U) {
                            if (construction_method == 1U && has_idat
                                && file_off <= idat_payload_end) {
                                extent_len = idat_payload_end - file_off;
                            } else {
                                ok = 0;
                                continue;
                            }
                        }

                        if (file_off > (omc_u64)size
                            || extent_len > ((omc_u64)size - file_off)) {
                            ok = 0;
                            continue;
                        }
                        if (construction_method == 1U
                            && file_off + extent_len > idat_payload_end) {
                            ok = 0;
                            continue;
                        }

                        if (e < 32U) {
                            omc_bmff_iloc_extent_map* ex;

                            ex = &layout->extents[e];
                            ex->logical_begin = logical_off;
                            if (extent_len > ((omc_u64)(~(omc_u64)0)
                                              - logical_off)) {
                                ok = 0;
                                ex->logical_end = logical_off;
                            } else {
                                ex->logical_end = logical_off + extent_len;
                            }
                            ex->file_off = file_off;
                            ex->file_len = extent_len;
                            layout->extent_count
                                = (omc_u16)(layout->extent_count + 1U);
                        } else {
                            ok = 0;
                        }

                        if (extent_len <= ((omc_u64)(~(omc_u64)0)
                                           - logical_off)) {
                            logical_off += extent_len;
                        } else {
                            ok = 0;
                        }
                    }

                    layout->total_len = logical_off;
                    layout->valid = ok
                                    && layout->extent_count == extent_count;
                }
            }
        }
    }

    for (i = 0U; i < record_count && p < payload_end; ++i) {
        omc_u32 item_id;
        omc_u32 construction_method;
        omc_u16 data_ref;
        omc_u64 base_off;
        omc_u16 extent_count;
        const omc_bmff_item* item;
        omc_u32 e;
        omc_u64 logical_off;

        item_id = 0U;
        construction_method = 0U;
        data_ref = 0U;
        base_off = 0U;
        extent_count = 0U;

        if (version < 2U) {
            omc_u16 id16;

            if (!omc_scan_read_u16be(bytes, size, p, &id16)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            item_id = id16;
            p += 2U;
        } else {
            if (!omc_scan_read_u32be(bytes, size, p, &item_id)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            p += 4U;
        }

        if (version == 1U || version == 2U) {
            omc_u16 cm;

            if (!omc_scan_read_u16be(bytes, size, p, &cm)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            construction_method = (omc_u32)(cm & 0x000FU);
            p += 2U;
        }

        if (!omc_scan_read_u16be(bytes, size, p, &data_ref)
            || !omc_scan_read_be_n(bytes, size, p + 2U, base_size, &base_off)
            || !omc_scan_read_u16be(bytes, size, p + 2U + base_size,
                                    &extent_count)) {
            sink->result.status = OMC_SCAN_MALFORMED;
            return;
        }
        p += 4U + base_size;

        if (extent_count > (1U << 14)) {
            sink->result.status = OMC_SCAN_MALFORMED;
            return;
        }

        item = omc_scan_bmff_find_item(items, item_count, item_id);
        {
            omc_u64 extent_hdr;
            omc_u64 extent_rec;
            omc_u64 extents_bytes;

            extent_hdr = ((version == 1U || version == 2U)
                              ? (omc_u64)idx_size
                              : 0U);
            extent_rec = extent_hdr + (omc_u64)off_size + (omc_u64)len_size;
            if (extent_rec != 0U
                && (omc_u64)extent_count > (((omc_u64)(~(omc_u64)0))
                                            / extent_rec)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            extents_bytes = extent_rec * (omc_u64)extent_count;
            if (p + extents_bytes > payload_end) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }

            if (item == (const omc_bmff_item*)0
                || !omc_scan_bmff_data_ref_is_self_contained(dref, data_ref)) {
                p += extents_bytes;
                continue;
            }

            if (construction_method == 2U) {
                omc_bmff_resolved_part parts[128];
                omc_u32 part_count;
                int all_ok;
                omc_u32 part_idx;

                if (!iloc_refs.parsed || iloc_refs.count == 0U
                    || extent_count == 0U || extent_count > 64U) {
                    p += extents_bytes;
                    continue;
                }

                memset(parts, 0, sizeof(parts));
                part_count = 0U;
                all_ok = 1;
                for (e = 0U; e < extent_count; ++e) {
                    omc_u64 extent_index;
                    omc_u64 extent_off;
                    omc_u64 extent_len;
                    omc_u32 ref_item_id;
                    const omc_bmff_iloc_item_layout* layout;
                    omc_u64 logical_src_off;
                    omc_bmff_resolved_part tmp_parts[32];
                    omc_u32 tmp_count;
                    omc_u32 j;

                    extent_index = 1U;
                    extent_off = 0U;
                    extent_len = 0U;
                    ref_item_id = 0U;
                    layout = (const omc_bmff_iloc_item_layout*)0;
                    logical_src_off = 0U;
                    memset(tmp_parts, 0, sizeof(tmp_parts));
                    tmp_count = 0U;

                    if ((version == 1U || version == 2U) && idx_size > 0U) {
                        if (!omc_scan_read_be_n(bytes, size, p, idx_size,
                                                &extent_index)) {
                            sink->result.status = OMC_SCAN_MALFORMED;
                            return;
                        }
                        p += idx_size;
                    } else if (idx_size == 0U && extent_count > 1U) {
                        extent_index = (omc_u64)e + 1U;
                    }

                    if (!omc_scan_read_be_n(bytes, size, p, off_size,
                                            &extent_off)
                        || !omc_scan_read_be_n(bytes, size, p + off_size,
                                               len_size, &extent_len)) {
                        sink->result.status = OMC_SCAN_MALFORMED;
                        return;
                    }
                    p += off_size + len_size;

                    if (!omc_scan_bmff_lookup_iloc_reference_item_id(
                            &iloc_refs, item_id, extent_index, &ref_item_id)) {
                        all_ok = 0;
                        continue;
                    }

                    layout = omc_scan_bmff_find_layout(layouts, layout_count,
                                                       ref_item_id);
                    if (layout == (const omc_bmff_iloc_item_layout*)0
                        || !layout->valid) {
                        all_ok = 0;
                        continue;
                    }

                    logical_src_off = base_off;
                    if (logical_src_off > ((omc_u64)(~(omc_u64)0)
                                           - extent_off)) {
                        all_ok = 0;
                        continue;
                    }
                    logical_src_off += extent_off;

                    if (extent_len == 0U && len_size == 0U
                        && extent_count == 1U) {
                        if (logical_src_off <= layout->total_len) {
                            extent_len = layout->total_len - logical_src_off;
                        } else {
                            all_ok = 0;
                            continue;
                        }
                    }

                    if (!omc_scan_bmff_resolve_logical_slice_to_file_parts(
                            layout, logical_src_off, extent_len, tmp_parts,
                            32U, &tmp_count)) {
                        all_ok = 0;
                        continue;
                    }

                    for (j = 0U; j < tmp_count; ++j) {
                        if (part_count >= 128U) {
                            all_ok = 0;
                            break;
                        }
                        parts[part_count] = tmp_parts[j];
                        part_count += 1U;
                    }
                }

                if (!all_ok || part_count == 0U) {
                    continue;
                }

                logical_off = 0U;
                for (part_idx = 0U; part_idx < part_count; ++part_idx) {
                    omc_blk_ref block;

                    if (parts[part_idx].len == 0U) {
                        continue;
                    }

                    omc_scan_init_block(&block);
                    block.format = format;
                    block.kind = item->kind;
                    block.outer_offset = parts[part_idx].file_off;
                    block.outer_size = parts[part_idx].len;
                    block.data_offset = parts[part_idx].file_off;
                    block.data_size = parts[part_idx].len;
                    block.id = item->item_type;
                    block.group = item_id;

                    if (block.kind == OMC_BLK_EXIF && part_idx == 0U) {
                        omc_scan_skip_bmff_exif_offset(&block, bytes, size);
                        omc_scan_skip_exif_preamble(&block, bytes, size);
                    }
                    if (part_count > 1U) {
                        block.part_index = part_idx;
                        block.part_count = part_count;
                        block.logical_offset = logical_off;
                    }
                    logical_off += block.data_size;
                    omc_scan_sink_emit(sink, &block);
                }
                continue;
            }
        }

        logical_off = 0U;
        for (e = 0U; e < extent_count; ++e) {
            omc_u64 extent_off;
            omc_u64 extent_len;
            omc_u64 file_off;
            omc_blk_ref block;

            if ((version == 1U || version == 2U) && idx_size > 0U) {
                omc_u64 discard;

                if (!omc_scan_read_be_n(bytes, size, p, idx_size, &discard)) {
                    sink->result.status = OMC_SCAN_MALFORMED;
                    return;
                }
                p += idx_size;
            }

            if (!omc_scan_read_be_n(bytes, size, p, off_size, &extent_off)
                || !omc_scan_read_be_n(bytes, size, p + off_size, len_size,
                                       &extent_len)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            p += off_size + len_size;

            if (construction_method == 1U) {
                if (!has_idat || idat_payload_off > ((omc_u64)(~(omc_u64)0)
                                                     - base_off)) {
                    continue;
                }
                file_off = idat_payload_off + base_off;
            } else if (construction_method == 0U) {
                file_off = base_off;
            } else {
                continue;
            }
            if (file_off > ((omc_u64)(~(omc_u64)0) - extent_off)) {
                continue;
            }
            file_off += extent_off;

            if (extent_len == 0U && len_size == 0U && extent_count == 1U) {
                if (construction_method == 1U && has_idat
                    && file_off <= idat_payload_end) {
                    extent_len = idat_payload_end - file_off;
                } else {
                    continue;
                }
            }

            if (file_off > (omc_u64)size || extent_len > ((omc_u64)size - file_off)) {
                continue;
            }
            if (construction_method == 1U
                && file_off + extent_len > idat_payload_end) {
                continue;
            }

            omc_scan_init_block(&block);
            block.format = format;
            block.kind = item->kind;
            block.outer_offset = file_off;
            block.outer_size = extent_len;
            block.data_offset = file_off;
            block.data_size = extent_len;
            block.id = item->item_type;
            block.group = item_id;

            if (block.kind == OMC_BLK_EXIF && e == 0U) {
                omc_scan_skip_bmff_exif_offset(&block, bytes, size);
                omc_scan_skip_exif_preamble(&block, bytes, size);
            }
            if (extent_count > 1U) {
                block.part_index = e;
                block.part_count = extent_count;
                block.logical_offset = logical_off;
            }
            logical_off += block.data_size;
            omc_scan_sink_emit(sink, &block);
        }
    }
}

static void
omc_scan_bmff_scan_ipco_for_icc(const omc_u8* bytes, omc_size size,
                                const omc_bmff_box* ipco, omc_scan_fmt format,
                                omc_scan_sink* sink)
{
    omc_u64 off;
    omc_u64 end;
    omc_u32 seen;

    off = ipco->offset + ipco->header_size;
    end = ipco->offset + ipco->size;
    seen = 0U;
    while (off + 8U <= end && seen < (1U << 16)) {
        omc_bmff_box child;

        seen += 1U;
        if (!omc_scan_parse_bmff_box(bytes, size, off, end, &child)) {
            break;
        }
        if (child.type == OMC_FOURCC('c', 'o', 'l', 'r')
            && child.size - child.header_size >= 4U) {
            omc_u64 payload_off;
            omc_u64 payload_size;
            omc_u32 colr_type;

            payload_off = child.offset + child.header_size;
            payload_size = child.size - child.header_size;
            if (omc_scan_read_u32be(bytes, size, payload_off, &colr_type)
                && (colr_type == OMC_FOURCC('p', 'r', 'o', 'f')
                    || colr_type == OMC_FOURCC('r', 'I', 'C', 'C'))) {
                omc_blk_ref block;

                omc_scan_init_block(&block);
                block.format = format;
                block.kind = OMC_BLK_ICC;
                block.outer_offset = child.offset;
                block.outer_size = child.size;
                block.data_offset = payload_off + 4U;
                block.data_size = payload_size - 4U;
                block.id = child.type;
                block.aux_u32 = colr_type;
                omc_scan_sink_emit(sink, &block);
            }
        }

        off += child.size;
        if (child.size == 0U) {
            break;
        }
    }
}

static void
omc_scan_bmff_scan_iprp_for_icc(const omc_u8* bytes, omc_size size,
                                const omc_bmff_box* iprp, omc_scan_fmt format,
                                omc_scan_sink* sink)
{
    omc_u64 off;
    omc_u64 end;
    omc_u32 seen;

    off = iprp->offset + iprp->header_size;
    end = iprp->offset + iprp->size;
    seen = 0U;
    while (off + 8U <= end && seen < (1U << 16)) {
        omc_bmff_box child;

        seen += 1U;
        if (!omc_scan_parse_bmff_box(bytes, size, off, end, &child)) {
            break;
        }
        if (child.type == OMC_FOURCC('i', 'p', 'c', 'o')) {
            omc_scan_bmff_scan_ipco_for_icc(bytes, size, &child, format, sink);
        }
        off += child.size;
        if (child.size == 0U) {
            break;
        }
    }
}

static void
omc_scan_bmff_scan_meta_box(const omc_u8* bytes, omc_size size,
                            const omc_bmff_box* meta, omc_scan_fmt format,
                            omc_scan_sink* sink)
{
    omc_u64 child_off;
    omc_u64 child_end;
    omc_bmff_box iinf;
    omc_bmff_box iloc;
    omc_bmff_box idat;
    omc_bmff_box iref;
    omc_bmff_box iprp;
    omc_bmff_dref_table dref;
    int has_iinf;
    int has_iloc;
    int has_idat;
    int has_iref;
    int has_iprp;

    child_off = meta->offset + meta->header_size + 4U;
    child_end = meta->offset + meta->size;
    if (child_off > child_end) {
        sink->result.status = OMC_SCAN_MALFORMED;
        return;
    }

    memset(&iinf, 0, sizeof(iinf));
    memset(&iloc, 0, sizeof(iloc));
    memset(&idat, 0, sizeof(idat));
    memset(&iref, 0, sizeof(iref));
    memset(&iprp, 0, sizeof(iprp));
    memset(&dref, 0, sizeof(dref));
    has_iinf = 0;
    has_iloc = 0;
    has_idat = 0;
    has_iref = 0;
    has_iprp = 0;

    while (child_off + 8U <= child_end) {
        omc_bmff_box child;

        if (!omc_scan_parse_bmff_box(bytes, size, child_off, child_end, &child)) {
            break;
        }
        if (child.type == OMC_FOURCC('i', 'i', 'n', 'f')) {
            iinf = child;
            has_iinf = 1;
        } else if (child.type == OMC_FOURCC('i', 'l', 'o', 'c')) {
            iloc = child;
            has_iloc = 1;
        } else if (child.type == OMC_FOURCC('i', 'd', 'a', 't')) {
            idat = child;
            has_idat = 1;
        } else if (child.type == OMC_FOURCC('i', 'r', 'e', 'f')) {
            iref = child;
            has_iref = 1;
        } else if (child.type == OMC_FOURCC('i', 'p', 'r', 'p')) {
            iprp = child;
            has_iprp = 1;
        } else if (child.type == OMC_FOURCC('d', 'i', 'n', 'f')) {
            omc_u64 off;
            omc_u64 end;

            off = child.offset + child.header_size;
            end = child.offset + child.size;
            while (off + 8U <= end) {
                omc_bmff_box dinf_child;

                if (!omc_scan_parse_bmff_box(bytes, size, off, end,
                                             &dinf_child)) {
                    break;
                }
                if (dinf_child.type == OMC_FOURCC('d', 'r', 'e', 'f')) {
                    (void)omc_scan_bmff_parse_dref_table(bytes, size,
                                                         &dinf_child, &dref);
                }
                off += dinf_child.size;
                if (dinf_child.size == 0U) {
                    break;
                }
            }
        }
        child_off += child.size;
        if (child.size == 0U) {
            break;
        }
    }

    if (has_iinf && has_iloc) {
        omc_bmff_item items[32];
        omc_u32 item_count;

        item_count = 0U;
        if (!omc_scan_bmff_collect_meta_items(bytes, size, &iinf, items, 32U,
                                              &item_count)) {
            sink->result.status = OMC_SCAN_MALFORMED;
            return;
        }
        omc_scan_bmff_emit_iloc_items(
            bytes, size, &iloc,
            has_idat ? &idat : (const omc_bmff_box*)0,
            has_iref ? &iref : (const omc_bmff_box*)0, &dref, items,
            item_count, format, sink);
        if (sink->result.status != OMC_SCAN_OK
            && sink->result.status != OMC_SCAN_TRUNCATED) {
            return;
        }
    }

    if (has_iprp) {
        omc_scan_bmff_scan_iprp_for_icc(bytes, size, &iprp, format, sink);
    }
}

static int
omc_scan_bmff_is_container_box(omc_u32 type)
{
    return type == OMC_FOURCC('m', 'o', 'o', 'v')
           || type == OMC_FOURCC('t', 'r', 'a', 'k')
           || type == OMC_FOURCC('m', 'd', 'i', 'a')
           || type == OMC_FOURCC('m', 'i', 'n', 'f')
           || type == OMC_FOURCC('s', 't', 'b', 'l')
           || type == OMC_FOURCC('e', 'd', 't', 's')
           || type == OMC_FOURCC('d', 'i', 'n', 'f')
           || type == OMC_FOURCC('u', 'd', 't', 'a');
}

static int
omc_scan_bmff_is_cr3_cmt_box(omc_u32 type)
{
    return type == OMC_FOURCC('C', 'M', 'T', '1')
           || type == OMC_FOURCC('C', 'M', 'T', '2')
           || type == OMC_FOURCC('C', 'M', 'T', '3')
           || type == OMC_FOURCC('C', 'M', 'T', '4');
}

static int
omc_scan_bmff_adjust_block_to_tiff(const omc_u8* bytes, omc_size size,
                                   omc_blk_ref* block)
{
    omc_scan_skip_bmff_exif_offset(block, bytes, size);
    omc_scan_skip_exif_preamble(block, bytes, size);
    return omc_scan_looks_like_tiff_header(bytes, size, block->data_offset);
}

static void
omc_scan_bmff_scan_cr3_uuid(const omc_u8* bytes, omc_size size,
                            const omc_bmff_box* box, omc_scan_sink* sink)
{
    typedef struct omc_bmff_range {
        omc_u64 begin;
        omc_u64 end;
        omc_u32 depth;
    } omc_bmff_range;

    omc_bmff_range stack[64];
    omc_u32 sp;
    omc_u32 seen;

    sp = 0U;
    seen = 0U;
    stack[sp].begin = box->offset + box->header_size;
    stack[sp].end = box->offset + box->size;
    stack[sp].depth = 0U;
    sp += 1U;

    while (sp > 0U) {
        omc_bmff_range range;
        omc_u64 off;

        sp -= 1U;
        range = stack[sp];
        if (range.depth > 12U) {
            continue;
        }

        off = range.begin;
        while (off + 8U <= range.end) {
            omc_bmff_box child;
            omc_u64 payload_off;
            omc_u64 payload_end;

            seen += 1U;
            if (seen > (1U << 16)) {
                sink->result.status = OMC_SCAN_MALFORMED;
                return;
            }
            if (!omc_scan_parse_bmff_box(bytes, size, off, range.end, &child)) {
                break;
            }

            payload_off = child.offset + child.header_size;
            payload_end = child.offset + child.size;

            if (omc_scan_bmff_is_cr3_cmt_box(child.type)) {
                omc_blk_ref block;

                omc_scan_init_block(&block);
                block.format = OMC_SCAN_FMT_CR3;
                block.kind = OMC_BLK_EXIF;
                block.outer_offset = child.offset;
                block.outer_size = child.size;
                block.data_offset = payload_off;
                block.data_size = child.size - child.header_size;
                block.id = child.type;
                if (omc_scan_bmff_adjust_block_to_tiff(bytes, size, &block)) {
                    omc_scan_sink_emit(sink, &block);
                }
            } else if (range.depth + 1U <= 12U && sp < 64U
                       && omc_scan_bmff_payload_may_contain_boxes(bytes, size,
                                                                  payload_off,
                                                                  payload_end)) {
                stack[sp].begin = payload_off;
                stack[sp].end = payload_end;
                stack[sp].depth = range.depth + 1U;
                sp += 1U;
            }

            off += child.size;
            if (child.size == 0U) {
                break;
            }
        }
    }
}

static void
omc_scan_bmff_scan_boxes(const omc_u8* bytes, omc_size size,
                         omc_u64 begin, omc_u64 end, omc_u32 depth,
                         omc_scan_fmt format, omc_scan_sink* sink)
{
    static const omc_u8 k_uuid_exif[16] = {
        0x4AU, 0x70U, 0x67U, 0x54U, 0x69U, 0x66U, 0x66U, 0x45U,
        0x78U, 0x69U, 0x66U, 0x2DU, 0x3EU, 0x4AU, 0x50U, 0x32U
    };
    static const omc_u8 k_uuid_xmp[16] = {
        0xBEU, 0x7AU, 0xCFU, 0xCBU, 0x97U, 0xA9U, 0x42U, 0xE8U,
        0x9CU, 0x71U, 0x99U, 0x94U, 0x91U, 0xE3U, 0xAFU, 0xACU
    };
    static const omc_u8 k_uuid_geotiff[16] = {
        0xB1U, 0x4BU, 0xF8U, 0xBDU, 0x08U, 0x3DU, 0x4BU, 0x43U,
        0xA5U, 0xAEU, 0x8CU, 0xD7U, 0xD5U, 0xA6U, 0xCEU, 0x03U
    };
    static const omc_u8 k_uuid_cr3[16] = {
        0x85U, 0xC0U, 0xB6U, 0x87U, 0x82U, 0x0FU, 0x11U, 0xE0U,
        0x81U, 0x11U, 0xF4U, 0xCEU, 0x46U, 0x2BU, 0x6AU, 0x48U
    };
    omc_u64 off;
    omc_u32 seen;

    if (depth > 8U) {
        return;
    }

    off = begin;
    seen = 0U;
    while (off < end && sink->result.status == OMC_SCAN_OK) {
        omc_bmff_box box;

        seen += 1U;
        if (seen > (1U << 18)) {
            sink->result.status = OMC_SCAN_MALFORMED;
            return;
        }
        if (!omc_scan_parse_bmff_box(bytes, size, off, end, &box)) {
            sink->result.status = OMC_SCAN_MALFORMED;
            return;
        }

        if (box.type == OMC_FOURCC('m', 'e', 't', 'a')) {
            omc_scan_bmff_scan_meta_box(bytes, size, &box, format, sink);
        } else if (format == OMC_SCAN_FMT_JP2
                   && omc_scan_jp2_emit_direct_metadata_box(bytes, size, &box,
                                                            sink)) {
            /* handled */
        } else if (box.type == OMC_FOURCC('E', 'x', 'i', 'f')) {
            omc_blk_ref block;

            omc_scan_init_block(&block);
            block.format = format;
            block.kind = OMC_BLK_EXIF;
            block.outer_offset = box.offset;
            block.outer_size = box.size;
            block.data_offset = box.offset + box.header_size;
            block.data_size = box.size - box.header_size;
            block.id = box.type;
            omc_scan_skip_bmff_exif_offset(&block, bytes, size);
            if (format != OMC_SCAN_FMT_JXL) {
                omc_scan_skip_exif_preamble(&block, bytes, size);
            }
            omc_scan_sink_emit(sink, &block);
        } else if (box.type == OMC_FOURCC('x', 'm', 'l', ' ')) {
            omc_blk_ref block;

            omc_scan_init_block(&block);
            block.format = format;
            block.kind = OMC_BLK_XMP;
            block.outer_offset = box.offset;
            block.outer_size = box.size;
            block.data_offset = box.offset + box.header_size;
            block.data_size = box.size - box.header_size;
            block.id = box.type;
            omc_scan_sink_emit(sink, &block);
        } else if (format == OMC_SCAN_FMT_JXL
                   && box.type == OMC_FOURCC('b', 'r', 'o', 'b')
                   && box.size - box.header_size >= 4U) {
            omc_u32 realtype;

            if (omc_scan_read_u32be(bytes, size, box.offset + box.header_size,
                                    &realtype)) {
                omc_blk_ref block;

                omc_scan_init_block(&block);
                block.format = format;
                block.kind = OMC_BLK_COMP_METADATA;
                block.compression = OMC_BLK_COMP_BROTLI;
                block.chunking = OMC_BLK_CHUNK_BROB_REALTYPE;
                block.outer_offset = box.offset;
                block.outer_size = box.size;
                block.data_offset = box.offset + box.header_size + 4U;
                block.data_size = box.size - box.header_size - 4U;
                block.id = box.type;
                block.aux_u32 = realtype;
                omc_scan_sink_emit(sink, &block);
            }
        } else if (box.type == OMC_FOURCC('j', 'u', 'm', 'b')
                   || box.type == OMC_FOURCC('c', '2', 'p', 'a')) {
            omc_blk_ref block;

            omc_scan_init_block(&block);
            block.format = format;
            block.kind = OMC_BLK_JUMBF;
            block.outer_offset = box.offset;
            block.outer_size = box.size;
            block.data_offset = box.offset + box.header_size;
            block.data_size = box.size - box.header_size;
            block.id = box.type;
            omc_scan_sink_emit(sink, &block);
        } else if (box.type == OMC_FOURCC('u', 'u', 'i', 'd') && box.has_uuid) {
            if (format == OMC_SCAN_FMT_JP2
                && omc_scan_jp2_emit_uuid_payload(bytes, size, &box, sink)) {
                /* handled */
            } else if (omc_scan_bmff_uuid_is(&box, k_uuid_xmp)) {
                omc_scan_bmff_emit_uuid_payload(sink, format, OMC_BLK_XMP, &box);
            } else if (omc_scan_bmff_uuid_is(&box, k_uuid_exif)
                       || omc_scan_bmff_uuid_is(&box, k_uuid_geotiff)) {
                omc_blk_ref block;

                omc_scan_init_block(&block);
                block.format = format;
                block.kind = OMC_BLK_EXIF;
                block.outer_offset = box.offset;
                block.outer_size = box.size;
                block.data_offset = box.offset + box.header_size;
                block.data_size = box.size - box.header_size;
                block.id = box.type;
                omc_scan_skip_exif_preamble(&block, bytes, size);
                omc_scan_sink_emit(sink, &block);
            } else if (format == OMC_SCAN_FMT_CR3
                       && omc_scan_bmff_uuid_is(&box, k_uuid_cr3)) {
                omc_scan_bmff_scan_cr3_uuid(bytes, size, &box, sink);
            }
        } else if (omc_scan_bmff_is_container_box(box.type)) {
            omc_u64 child_off;
            omc_u64 child_end;

            child_off = box.offset + box.header_size;
            child_end = box.offset + box.size;
            if (child_off < child_end) {
                omc_scan_bmff_scan_boxes(bytes, size, child_off, child_end,
                                         depth + 1U, format, sink);
            }
        }

        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }
}

omc_scan_res
omc_scan_bmff(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_scan_sink sink;
    omc_bmff_box ftyp;
    omc_scan_fmt format;

    omc_scan_sink_init(&sink, out_blocks, out_cap);

    if (bytes == (const omc_u8*)0) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (size < 8U) {
        sink.result.status = OMC_SCAN_MALFORMED;
        return sink.result;
    }
    if (!omc_scan_bmff_find_ftyp(bytes, size, &ftyp)) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    format = omc_scan_bmff_format_from_ftyp(bytes, size, &ftyp);
    if (format == OMC_SCAN_FMT_UNKNOWN) {
        sink.result.status = OMC_SCAN_UNSUPPORTED;
        return sink.result;
    }

    omc_scan_bmff_scan_boxes(bytes, size, 0U, (omc_u64)size, 0U, format, &sink);
    return sink.result;
}

omc_scan_res
omc_scan_meas_bmff(const omc_u8* bytes, omc_size size)
{
    return omc_scan_normalize_measure(
        omc_scan_bmff(bytes, size, (omc_blk_ref*)0, 0U));
}

omc_scan_res
omc_scan_auto(const omc_u8* bytes, omc_size size,
              omc_blk_ref* out_blocks, omc_u32 out_cap)
{
    omc_u32 first_size;
    omc_u32 first_type;
    omc_u16 tiff_version;

    if (bytes == (const omc_u8*)0) {
        return omc_scan_jpeg(bytes, size, out_blocks, out_cap);
    }

    if (size >= 2U && bytes[0] == 0xFFU && bytes[1] == 0xD8U) {
        return omc_scan_jpeg(bytes, size, out_blocks, out_cap);
    }

    if (size >= 8U && bytes[0] == 0x89U && bytes[1] == 0x50U
        && bytes[2] == 0x4EU && bytes[3] == 0x47U && bytes[4] == 0x0DU
        && bytes[5] == 0x0AU && bytes[6] == 0x1AU && bytes[7] == 0x0AU) {
        return omc_scan_png(bytes, size, out_blocks, out_cap);
    }

    if (size >= 12U && omc_scan_match(bytes, size, 0U, "RIFF", 4U)
        && omc_scan_match(bytes, size, 8U, "WEBP", 4U)) {
        return omc_scan_webp(bytes, size, out_blocks, out_cap);
    }

    if (size >= 6U
        && (omc_scan_match(bytes, size, 0U, "GIF87a", 6U)
            || omc_scan_match(bytes, size, 0U, "GIF89a", 6U))) {
        return omc_scan_gif(bytes, size, out_blocks, out_cap);
    }

    if (size >= 16U && omc_scan_match(bytes, size, 0U, "FUJIFILMCCD-RAW ", 16U)) {
        return omc_scan_raf(bytes, size, out_blocks, out_cap);
    }

    if (size >= 4U && omc_scan_match(bytes, size, 0U, "FOVb", 4U)) {
        return omc_scan_x3f(bytes, size, out_blocks, out_cap);
    }

    if (size >= 14U
        && ((bytes[0] == (omc_u8)'I' && bytes[1] == (omc_u8)'I')
            || (bytes[0] == (omc_u8)'M' && bytes[1] == (omc_u8)'M'))
        && omc_scan_match(bytes, size, 6U, "HEAPCCDR", 8U)) {
        return omc_scan_crw(bytes, size, out_blocks, out_cap);
    }

    if (size >= 12U
        && omc_scan_read_u32be(bytes, size, 0U, &first_size)
        && omc_scan_read_u32be(bytes, size, 4U, &first_type)
        && first_size == 12U) {
        if (first_type == OMC_FOURCC('j', 'P', ' ', ' ')) {
            return omc_scan_jp2(bytes, size, out_blocks, out_cap);
        }
        if (first_type == OMC_FOURCC('J', 'X', 'L', ' ')) {
            return omc_scan_jxl(bytes, size, out_blocks, out_cap);
        }
    }

    if (size >= 8U) {
        omc_bmff_box ftyp;

        if (omc_scan_bmff_find_ftyp(bytes, size, &ftyp)) {
            return omc_scan_bmff(bytes, size, out_blocks, out_cap);
        }
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
