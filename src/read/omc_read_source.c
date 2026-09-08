#include "omc/omc_read_source.h"
#include <string.h>

#define OMC_SOURCE_MAX_SEGMENTS 1024U
#define OMC_SOURCE_MAX_IFDS 1024U

typedef struct omc_source_segment {
    omc_u64 original;
    omc_size compact;
    omc_size size;
} omc_source_segment;
typedef struct omc_source_collect {
    const omc_source_range *range;
    omc_source_state *state;
    const omc_read_source_opts *opts;
    omc_u8 *bytes;
    omc_size capacity;
    omc_size used;
    omc_read_source_status status;
} omc_source_collect;

static omc_u64
number(const omc_u8 *p, unsigned width, int little)
{
    omc_u64 n;
    unsigned i;
    n = 0U;
    for (i = 0U; i < width; ++i)
        n = (n << 8U) | p[little ? width - 1U - i : i];
    return n;
}
static int
read_bytes(omc_source_collect *c, omc_u64 offset, omc_u8 *out, omc_size size)
{
    if (offset > c->range->size || size > c->range->size - offset) {
        c->status = OMC_READ_SOURCE_MALFORMED;
        return 0;
    }
    if (omc_source_read(c->range, offset, out, size, c->state, &c->opts->io) !=
        OMC_SOURCE_OK) {
        c->status = OMC_READ_SOURCE_IO;
        return 0;
    }
    return 1;
}
static int
snapshot(omc_source_collect *c, omc_u64 offset, omc_u64 size)
{
    omc_size end;
    if (offset > c->capacity || size > c->capacity - offset) {
        c->status = OMC_READ_SOURCE_LIMIT;
        return 0;
    }
    end = (omc_size)(offset + size);
    if (end > c->used) {
        memset(c->bytes + c->used, 0, end - c->used);
        c->used = end;
    }
    return read_bytes(c, offset, c->bytes + (omc_size)offset, (omc_size)size);
}
static int
collect_jpeg(omc_source_collect *c, omc_source_segment *segments, omc_u32 *count)
{
    omc_u8 h[2];
    omc_u64 offset, marker_offset;
    unsigned marker, length;
    omc_size size;
    if (c->capacity < 4U) {
        c->status = OMC_READ_SOURCE_LIMIT;
        return 0;
    }
    c->bytes[0] = 0xFFU;
    c->bytes[1] = 0xD8U;
    c->used = 2U;
    *count = 0U;
    offset = 2U;
    while (offset + 2U <= c->range->size) {
        if (!read_bytes(c, offset, h, 2U))
            return 0;
        if (h[0] != 0xFFU) {
            c->status = OMC_READ_SOURCE_MALFORMED;
            return 0;
        }
        marker_offset = offset;
        marker = h[1];
        offset += 2U;
        while (marker == 0xFFU) {
            marker_offset = offset - 1U;
            if (offset == c->range->size)
                goto finished;
            if (!read_bytes(c, offset, h, 1U))
                return 0;
            marker = h[0];
            offset++;
        }
        if (marker == 0xD9U || marker == 0xDAU)
            break;
        if ((marker >= 0xD0U && marker <= 0xD7U) || marker == 1U)
            continue;
        if (!read_bytes(c, offset, h, 2U))
            return 0;
        length = (unsigned)number(h, 2U, 0);
        if (length < 2U || length > c->range->size - offset) {
            c->status = OMC_READ_SOURCE_MALFORMED;
            return 0;
        }
        if ((marker >= 0xE0U && marker <= 0xEFU) || marker == 0xFEU) {
            size = (omc_size)length + 2U;
            if (*count == OMC_SOURCE_MAX_SEGMENTS ||
                size > c->capacity - c->used - 2U) {
                c->status = OMC_READ_SOURCE_LIMIT;
                return 0;
            }
            segments[*count].original = marker_offset;
            segments[*count].compact = c->used;
            segments[*count].size = size;
            (*count)++;
            if (!read_bytes(c, marker_offset, c->bytes + c->used, size))
                return 0;
            c->used += size;
        }
        offset += length;
    }
finished:
    c->bytes[c->used++] = 0xFFU;
    c->bytes[c->used++] = 0xD9U;
    return 1;
}
static int
collect_chunk(omc_source_collect *c, omc_source_segment *segments,
              omc_u32 *count, omc_u64 offset, omc_u64 size,
              const omc_u8 *header)
{
    if (*count == OMC_SOURCE_MAX_SEGMENTS || size > c->capacity - c->used) {
        c->status = OMC_READ_SOURCE_LIMIT;
        return 0;
    }
    segments[*count].original = offset;
    segments[*count].compact = c->used;
    segments[*count].size = (omc_size)size;
    memcpy(c->bytes + c->used, header, 8U);
    if (!read_bytes(c, offset + 8U, c->bytes + c->used + 8U,
                    (omc_size)size - 8U))
        return 0;
    c->used += (omc_size)size;
    (*count)++;
    return 1;
}
static int
collect_png(omc_source_collect *c, omc_source_segment *segments, omc_u32 *count)
{
    static const omc_u8 signature[8] = {
        0x89U, 'P', 'N', 'G', 13U, 10U, 26U, 10U
    };
    omc_u8 h[8];
    omc_u64 offset, size;
    if (!read_bytes(c, 0U, h, 8U))
        return 0;
    if (memcmp(h, signature, 8U) != 0) {
        c->status = OMC_READ_SOURCE_UNSUPPORTED;
        return 0;
    }
    if (c->capacity < 8U) {
        c->status = OMC_READ_SOURCE_LIMIT;
        return 0;
    }
    memcpy(c->bytes, h, 8U);
    c->used = 8U;
    offset = 8U;
    *count = 0U;
    while (offset <= c->range->size && c->range->size - offset >= 12U) {
        if (!read_bytes(c, offset, h, 8U))
            return 0;
        size = number(h, 4U, 0) + 12U;
        if (size > c->range->size - offset) {
            c->status = OMC_READ_SOURCE_MALFORMED;
            return 0;
        }
        if (memcmp(h + 4U, "eXIf", 4U) == 0 ||
            memcmp(h + 4U, "iCCP", 4U) == 0 ||
            memcmp(h + 4U, "tEXt", 4U) == 0 ||
            memcmp(h + 4U, "zTXt", 4U) == 0 ||
            memcmp(h + 4U, "iTXt", 4U) == 0 ||
            memcmp(h + 4U, "caBX", 4U) == 0) {
            if (!collect_chunk(c, segments, count, offset, size, h))
                return 0;
        }
        offset += size;
        if (memcmp(h + 4U, "IEND", 4U) == 0)
            break;
    }
    /* The metadata scanner accepts EOF without IEND and does not check CRCs.
     * Only retained metadata chunks need to be present in the compact input. */
    return 1;
}
static int
collect_webp(omc_source_collect *c, omc_source_segment *segments, omc_u32 *count)
{
    omc_u8 h[12];
    omc_u64 offset, end, size;
    omc_u32 riff_size;
    unsigned i;
    if (!read_bytes(c, 0U, h, 12U))
        return 0;
    if (memcmp(h, "RIFF", 4U) != 0 || memcmp(h + 8U, "WEBP", 4U) != 0) {
        c->status = OMC_READ_SOURCE_UNSUPPORTED;
        return 0;
    }
    if (c->capacity < 12U) {
        c->status = OMC_READ_SOURCE_LIMIT;
        return 0;
    }
    memcpy(c->bytes, h, 12U);
    c->used = 12U;
    end = number(h + 4U, 4U, 1) + 8U;
    if (end > c->range->size)
        end = c->range->size;
    offset = 12U;
    *count = 0U;
    while (offset <= end && end - offset >= 8U) {
        if (!read_bytes(c, offset, h, 8U))
            return 0;
        size = number(h + 4U, 4U, 1);
        size += 8U + (size & 1U);
        if (size > end - offset) {
            c->status = OMC_READ_SOURCE_MALFORMED;
            return 0;
        }
        if (memcmp(h, "EXIF", 4U) == 0 || memcmp(h, "XMP ", 4U) == 0 ||
            memcmp(h, "ICCP", 4U) == 0 || memcmp(h, "C2PA", 4U) == 0) {
            if (!collect_chunk(c, segments, count, offset, size, h))
                return 0;
        }
        offset += size;
    }
    /* Retain valid RIFF framing for the shared contiguous metadata decoder. */
    riff_size = (omc_u32)(c->used - 8U);
    for (i = 0U; i < 4U; ++i) {
        c->bytes[4U + i] = (omc_u8)(riff_size & 255U);
        riff_size >>= 8U;
    }
    return 1;
}
static int
queue_ifd(omc_source_collect *c, omc_u64 *queue, omc_u32 *count, omc_u64 offset)
{
    omc_u32 i;
    if (offset == 0U)
        return 1;
    for (i = 0U; i < *count; ++i)
        if (queue[i] == offset)
            return 1;
    if (*count == OMC_SOURCE_MAX_IFDS ||
        *count >= c->opts->decode.exif.limits.max_ifds) {
        c->status = OMC_READ_SOURCE_LIMIT;
        return 0;
    }
    queue[(*count)++] = offset;
    return 1;
}
static int
collect_tiff(omc_source_collect *c)
{
    static const unsigned widths[19] = {0U, 1U, 1U, 2U, 4U, 8U, 1U, 1U, 2U, 4U,
                                        8U, 4U, 8U, 4U, 0U, 0U, 8U, 8U, 8U};
    omc_u64 queue[OMC_SOURCE_MAX_IFDS];
    omc_u64 ifd, count, total, table, entry, n, value, size, next, j;
    omc_u32 queued, processed;
    unsigned version, cw, ew, vw, tag, type, width;
    int little;
    if (!snapshot(c, 0U, 8U))
        return 0;
    little = c->bytes[0] == 'I';
    version = (unsigned)number(c->bytes + 2U, 2U, little);
    if (version == 43U) {
        if (!snapshot(c, 8U, 8U))
            return 0;
        if (number(c->bytes + 4U, 2U, little) != 8U ||
            number(c->bytes + 6U, 2U, little) != 0U) {
            c->status = OMC_READ_SOURCE_MALFORMED;
            return 0;
        }
        cw = 8U;
        ew = 20U;
        vw = 8U;
        next = number(c->bytes + 8U, 8U, little);
    } else if (version == 42U) {
        cw = 2U;
        ew = 12U;
        vw = 4U;
        next = number(c->bytes + 4U, 4U, little);
    } else {
        c->status = OMC_READ_SOURCE_UNSUPPORTED;
        return 0;
    }
    queued = 0U;
    processed = 0U;
    total = 0U;
    if (!queue_ifd(c, queue, &queued, next))
        return 0;
    while (processed < queued) {
        ifd = queue[processed++];
        if (!snapshot(c, ifd, cw))
            return 0;
        count = number(c->bytes + (omc_size)ifd, cw, little);
        if (count > c->opts->decode.exif.limits.max_entries_per_ifd ||
            total > c->opts->decode.exif.limits.max_total_entries ||
            count > c->opts->decode.exif.limits.max_total_entries - total) {
            c->status = OMC_READ_SOURCE_LIMIT;
            return 0;
        }
        total += count;
        table = count * ew;
        if (!snapshot(c, ifd + cw, table + vw))
            return 0;
        next = number(c->bytes + (omc_size)(ifd + cw + table), vw, little);
        if (!queue_ifd(c, queue, &queued, next))
            return 0;
        for (n = 0U; n < count; ++n) {
            entry = ifd + cw + n * ew;
            tag = (unsigned)number(c->bytes + (omc_size)entry, 2U, little);
            type = (unsigned)number(c->bytes + (omc_size)entry + 2U, 2U, little);
            width = type < 19U ? widths[type] : 0U;
            if (!width) {
                c->status = OMC_READ_SOURCE_UNSUPPORTED;
                return 0;
            }
            size = number(c->bytes + (omc_size)entry + 4U, vw, little);
            if (size > ~(omc_u64)0 / width) {
                c->status = OMC_READ_SOURCE_LIMIT;
                return 0;
            }
            size *= width;
            if (size > c->opts->decode.exif.limits.max_value_bytes) {
                c->status = OMC_READ_SOURCE_LIMIT;
                return 0;
            }
            value = entry + (vw == 8U ? 12U : 8U);
            if (size > vw) {
                value = number(c->bytes + (omc_size)value, vw, little);
                if (!snapshot(c, value, size))
                    return 0;
            }
            if (tag == 0x927CU && size && c->opts->decode.exif.decode_makernote) {
                c->status = OMC_READ_SOURCE_UNSUPPORTED;
                return 0;
            }
            if (tag == 0x014AU || tag == 0x8769U || tag == 0x8825U || tag == 0xA005U) {
                if (type != 3U && type != 4U && type != 13U && type != 16U &&
                    type != 18U) {
                    c->status = OMC_READ_SOURCE_UNSUPPORTED;
                    return 0;
                }
                /* EXIF/GPS/Interop use one offset; SubIFDs may contain an array. */
                for (j = 0U; j < size; j += width) {
                    if (!queue_ifd(
                            c, queue, &queued,
                            number(c->bytes + (omc_size)(value + j), width, little)))
                        return 0;
                    if (tag != 0x014AU)
                        break;
                }
            }
        }
    }
    return 1;
}
static void
remap_block(omc_blk_ref *block, const omc_source_range *range,
            const omc_source_segment *segments, omc_u32 count, int tiff)
{
    omc_u32 i;
    if (tiff) {
        block->outer_offset = 0U;
        block->outer_size = range->size;
        block->data_offset = 0U;
        block->data_size = range->size;
        return;
    }
    for (i = 0U; i < count; ++i) {
        if (block->outer_offset >= segments[i].compact &&
            block->outer_offset - segments[i].compact < segments[i].size) {
            block->outer_offset =
                segments[i].original + block->outer_offset - segments[i].compact;
            block->data_offset =
                segments[i].original + block->data_offset - segments[i].compact;
            return;
        }
    }
}
void
omc_read_source_opts_init(omc_read_source_opts *opts)
{
    if (opts != NULL) {
        omc_read_opts_init(&opts->decode);
        omc_source_limits_init(&opts->io);
    }
}
omc_read_source_res
omc_read_source(const omc_source_range *range, omc_store *store,
                omc_read_source_workspace *w, omc_source_state *state,
                const omc_read_source_opts *opts)
{
    omc_read_source_res res;
    omc_read_source_opts defaults;
    omc_source_collect c;
    omc_source_segment segments[OMC_SOURCE_MAX_SEGMENTS];
    omc_u32 count;
    omc_size i, before, size;
    const omc_u8 *data;
    omc_u8 signature[2];
    int tiff;
    memset(&res, 0, sizeof(res));
    if (!omc_source_range_valid(range) || store == NULL || w == NULL || state == NULL ||
        (w->block_capacity && w->blocks == NULL) ||
        (w->ifd_capacity && w->ifds == NULL) ||
        (w->payload_capacity && w->payload == NULL) ||
        (w->payload_index_capacity && w->payload_indices == NULL) ||
        (w->metadata_capacity && w->metadata == NULL)) {
        res.status = OMC_READ_SOURCE_INVALID_ARGUMENT;
        return res;
    }
    if (state->code != OMC_SOURCE_OK) {
        res.status = OMC_READ_SOURCE_IO;
        return res;
    }
    if (opts == NULL) {
        omc_read_source_opts_init(&defaults);
        opts = &defaults;
    }
    before = store->block_count;
    count = 0U;
    tiff = 0;
    if (range->source.contiguous_data != NULL) {
        data = range->source.contiguous_data + (omc_size)range->source_offset;
        size = (omc_size)range->size;
    } else {
        memset(&c, 0, sizeof(c));
        c.range = range;
        c.state = state;
        c.opts = opts;
        c.bytes = w->metadata;
        c.capacity = w->metadata_capacity;
        if (!read_bytes(&c, 0U, signature, 2U)) {
            res.status = c.status;
            return res;
        }
        if (signature[0] == 0xFFU && signature[1] == 0xD8U) {
            if (!collect_jpeg(&c, segments, &count)) {
                res.status = c.status;
                res.scratch_used = c.used;
                return res;
            }
        } else if ((signature[0] == 'I' && signature[1] == 'I') ||
                   (signature[0] == 'M' && signature[1] == 'M')) {
            tiff = 1;
            if (!collect_tiff(&c)) {
                res.status = c.status;
                res.scratch_used = c.used;
                return res;
            }
        } else if (signature[0] == 0x89U && signature[1] == 'P') {
            if (!collect_png(&c, segments, &count)) {
                res.status = c.status;
                res.scratch_used = c.used;
                return res;
            }
        } else if (signature[0] == 'R' && signature[1] == 'I') {
            if (!collect_webp(&c, segments, &count)) {
                res.status = c.status;
                res.scratch_used = c.used;
                return res;
            }
        } else {
            res.status = OMC_READ_SOURCE_UNSUPPORTED;
            return res;
        }
        data = c.bytes;
        size = c.used;
        res.scratch_used = c.used;
    }
    res.decoded =
        omc_read_simple(data, size, store, w->blocks, w->block_capacity, w->ifds,
                        w->ifd_capacity, w->payload, w->payload_capacity,
                        w->payload_indices, w->payload_index_capacity, &opts->decode);
    if (range->source.contiguous_data == NULL) {
        for (i = 0U; i < res.decoded.scan.written; ++i)
            remap_block(&w->blocks[i], range, segments, count, tiff);
        for (i = before; i < store->block_count; ++i)
            remap_block(&store->blocks[i], range, segments, count, tiff);
    }
    return res;
}
