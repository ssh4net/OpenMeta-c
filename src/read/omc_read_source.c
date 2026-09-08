#include "read/omc_read_internal.h"
#include "omc/omc_edit.h"
#include "omc/omc_read_source.h"
#include <string.h>

#define OMC_SOURCE_MAX_SEGMENTS 1024U

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
static void
remap_block(omc_blk_ref *block, const omc_source_segment *segments, omc_u32 count)
{
    omc_u32 i;
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
            omc_store candidate;
            omc_exif_source_res exif_source;
            memset(&exif_source, 0, sizeof(exif_source));
            omc_store_init(&candidate);
            if (omc_edit_commit(store, NULL, 0U, &candidate) != OMC_STATUS_OK) {
                res.status = OMC_READ_SOURCE_LIMIT;
                return res;
            }
            res.decoded = omc_read_tiff_source(range, &candidate, w, state,
                                                opts, &exif_source);
            res.value_scratch_needed = exif_source.value_scratch_needed;
            res.scratch_used = exif_source.value_scratch_used;
            res.nested_payloads_skipped = exif_source.nested_payloads_skipped;
            if (state->code != OMC_SOURCE_OK)
                res.status = OMC_READ_SOURCE_IO;
            else if (exif_source.value_scratch_needed != 0U ||
                     res.decoded.exif.status == OMC_EXIF_LIMIT ||
                     res.decoded.exif.status == OMC_EXIF_NOMEM)
                res.status = OMC_READ_SOURCE_LIMIT;
            else if (res.decoded.scan.status == OMC_SCAN_UNSUPPORTED)
                res.status = OMC_READ_SOURCE_UNSUPPORTED;
            else if (res.decoded.scan.status == OMC_SCAN_MALFORMED)
                res.status = OMC_READ_SOURCE_MALFORMED;
            if (res.status == OMC_READ_SOURCE_OK) {
                omc_store_fini(store);
                *store = candidate;
            } else {
                omc_store_fini(&candidate);
            }
            return res;
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
            remap_block(&w->blocks[i], segments, count);
        for (i = before; i < store->block_count; ++i)
            remap_block(&store->blocks[i], segments, count);
    }
    return res;
}
