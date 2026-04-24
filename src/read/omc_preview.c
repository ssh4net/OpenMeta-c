#include "omc/omc_preview.h"

#include <string.h>

typedef struct omc_preview_tiff_cfg {
    int little_endian;
} omc_preview_tiff_cfg;

typedef struct omc_preview_ifd_entry {
    omc_u16 tag;
    omc_u16 type;
    omc_u32 count;
    omc_u32 value_or_off;
} omc_preview_ifd_entry;

typedef struct omc_preview_bmff_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
    int has_uuid;
    omc_u8 uuid[16];
} omc_preview_bmff_box;

typedef struct omc_preview_cr3_state {
    int truncated;
    omc_u32 boxes_seen;
} omc_preview_cr3_state;

static int
omc_preview_read_u16le(const omc_u8* bytes, omc_size size, omc_u64 offset,
                       omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || offset > (omc_u64)size
        || ((omc_u64)size - offset) < 2U) {
        return 0;
    }

    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 1U]) << 8)
                           | ((omc_u16)bytes[(omc_size)offset + 0U]));
    return 1;
}

static int
omc_preview_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                       omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || offset > (omc_u64)size
        || ((omc_u64)size - offset) < 2U) {
        return 0;
    }

    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                           | ((omc_u16)bytes[(omc_size)offset + 1U]));
    return 1;
}

static int
omc_preview_read_u32le(const omc_u8* bytes, omc_size size, omc_u64 offset,
                       omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || offset > (omc_u64)size
        || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 3U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 0U]) << 0);
    return 1;
}

static int
omc_preview_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                       omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || offset > (omc_u64)size
        || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    return 1;
}

static int
omc_preview_read_u64be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                       omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || offset > (omc_u64)size
        || ((omc_u64)size - offset) < 8U) {
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
omc_preview_read_tiff_u16(omc_preview_tiff_cfg cfg, const omc_u8* bytes,
                          omc_size size, omc_u64 offset, omc_u16* out_value)
{
    if (cfg.little_endian) {
        return omc_preview_read_u16le(bytes, size, offset, out_value);
    }
    return omc_preview_read_u16be(bytes, size, offset, out_value);
}

static int
omc_preview_read_tiff_u32(omc_preview_tiff_cfg cfg, const omc_u8* bytes,
                          omc_size size, omc_u64 offset, omc_u32* out_value)
{
    if (cfg.little_endian) {
        return omc_preview_read_u32le(bytes, size, offset, out_value);
    }
    return omc_preview_read_u32be(bytes, size, offset, out_value);
}

static int
omc_preview_parse_tiff_header(const omc_u8* bytes, omc_size size,
                              omc_preview_tiff_cfg* cfg,
                              omc_u64* out_first_ifd)
{
    omc_u16 magic;
    omc_u32 ifd0;

    if (cfg == (omc_preview_tiff_cfg*)0 || out_first_ifd == (omc_u64*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || size < 8U) {
        return 0;
    }

    if (bytes[0] == (omc_u8)'I' && bytes[1] == (omc_u8)'I') {
        cfg->little_endian = 1;
    } else if (bytes[0] == (omc_u8)'M' && bytes[1] == (omc_u8)'M') {
        cfg->little_endian = 0;
    } else {
        return 0;
    }

    if (!omc_preview_read_tiff_u16(*cfg, bytes, size, 2U, &magic)
        || !omc_preview_read_tiff_u32(*cfg, bytes, size, 4U, &ifd0)) {
        return 0;
    }
    if (magic != 42U) {
        return 0;
    }

    *out_first_ifd = (omc_u64)ifd0;
    return 1;
}

static omc_u64
omc_preview_tiff_type_size(omc_u16 type)
{
    switch (type) {
    case 1:
    case 2:
    case 6:
    case 7: return 1U;
    case 3:
    case 8: return 2U;
    case 4:
    case 9:
    case 11: return 4U;
    case 5:
    case 10:
    case 12: return 8U;
    default: return 0U;
    }
}

static int
omc_preview_read_ifd_entry(omc_preview_tiff_cfg cfg, const omc_u8* bytes,
                           omc_size size, omc_u64 entry_off,
                           omc_preview_ifd_entry* out_entry)
{
    omc_preview_ifd_entry entry;

    if (out_entry == (omc_preview_ifd_entry*)0) {
        return 0;
    }
    if (entry_off > (omc_u64)size || ((omc_u64)size - entry_off) < 12U) {
        return 0;
    }

    if (!omc_preview_read_tiff_u16(cfg, bytes, size, entry_off + 0U,
                                   &entry.tag)
        || !omc_preview_read_tiff_u16(cfg, bytes, size, entry_off + 2U,
                                      &entry.type)
        || !omc_preview_read_tiff_u32(cfg, bytes, size, entry_off + 4U,
                                      &entry.count)
        || !omc_preview_read_tiff_u32(cfg, bytes, size, entry_off + 8U,
                                      &entry.value_or_off)) {
        return 0;
    }

    *out_entry = entry;
    return 1;
}

static int
omc_preview_entry_scalar_u32(omc_preview_tiff_cfg cfg,
                             omc_preview_ifd_entry entry,
                             omc_u32* out_value)
{
    if (out_value == (omc_u32*)0 || entry.count == 0U) {
        return 0;
    }
    if (entry.type == 4U) {
        *out_value = entry.value_or_off;
        return 1;
    }
    if (entry.type == 3U) {
        if (cfg.little_endian) {
            *out_value = entry.value_or_off & 0xFFFFU;
        } else {
            *out_value = (entry.value_or_off >> 16) & 0xFFFFU;
        }
        return 1;
    }
    return 0;
}

static int
omc_preview_is_jpeg_soi(const omc_u8* file_bytes, omc_size file_size,
                        omc_u64 offset, omc_u64 size)
{
    if (file_bytes == (const omc_u8*)0 || size < 2U) {
        return 0;
    }
    if (offset > (omc_u64)file_size || ((omc_u64)file_size - offset) < 2U) {
        return 0;
    }
    return file_bytes[(omc_size)offset + 0U] == 0xFFU
           && file_bytes[(omc_size)offset + 1U] == 0xD8U;
}

static int
omc_preview_contains_ifd_offset(const omc_u64* values, omc_u32 count,
                                omc_u64 off)
{
    omc_u32 i;

    for (i = 0U; i < count; ++i) {
        if (values[i] == off) {
            return 1;
        }
    }
    return 0;
}

static int
omc_preview_push_ifd_offset(omc_u64* queue, omc_u32 cap, omc_u32* count,
                            omc_u64 off)
{
    if (count == (omc_u32*)0) {
        return 0;
    }
    if (off == 0U || omc_preview_contains_ifd_offset(queue, *count, off)) {
        return 1;
    }
    if (*count >= cap) {
        return 0;
    }
    queue[*count] = off;
    *count += 1U;
    return 1;
}

static omc_preview_scan_status
omc_preview_add_candidate(const omc_u8* file_bytes, omc_size file_size,
                          omc_preview_candidate* out_candidates,
                          omc_u32 out_cap, omc_u32* written, omc_u32* needed,
                          const omc_preview_scan_opts* opts,
                          const omc_preview_candidate* in_candidate)
{
    omc_preview_candidate candidate;

    if (written == (omc_u32*)0 || needed == (omc_u32*)0
        || opts == (const omc_preview_scan_opts*)0
        || in_candidate == (const omc_preview_candidate*)0) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }

    candidate = *in_candidate;
    candidate.has_jpeg_soi_signature
        = omc_preview_is_jpeg_soi(file_bytes, file_size, candidate.file_offset,
                                  candidate.size);
    if (opts->require_jpeg_soi && !candidate.has_jpeg_soi_signature) {
        return OMC_PREVIEW_SCAN_OK;
    }

    if (*written < out_cap && out_candidates != (omc_preview_candidate*)0) {
        out_candidates[*written] = candidate;
        *written += 1U;
        *needed += 1U;
        return OMC_PREVIEW_SCAN_OK;
    }

    *needed += 1U;
    return OMC_PREVIEW_SCAN_TRUNCATED;
}

static int
omc_preview_parse_bmff_box(const omc_u8* bytes, omc_size size, omc_u64 offset,
                           omc_u64 parent_end, omc_preview_bmff_box* out_box)
{
    omc_u32 size32;
    omc_u32 type;
    omc_u64 header_size;
    omc_u64 box_size;
    omc_u64 uuid_off;
    omc_u32 i;

    if (out_box == (omc_preview_bmff_box*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0 || offset + 8U > parent_end
        || offset + 8U > (omc_u64)size) {
        return 0;
    }
    if (!omc_preview_read_u32be(bytes, size, offset + 0U, &size32)
        || !omc_preview_read_u32be(bytes, size, offset + 4U, &type)) {
        return 0;
    }

    header_size = 8U;
    box_size = (omc_u64)size32;
    if (size32 == 1U) {
        if (!omc_preview_read_u64be(bytes, size, offset + 8U, &box_size)) {
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

    out_box->offset = offset;
    out_box->size = box_size;
    out_box->header_size = header_size;
    out_box->type = type;
    out_box->has_uuid = 0;
    memset(out_box->uuid, 0, sizeof(out_box->uuid));

    if (type == OMC_FOURCC('u', 'u', 'i', 'd')) {
        if (header_size + 16U > box_size) {
            return 0;
        }
        uuid_off = offset + header_size;
        if (uuid_off + 16U > (omc_u64)size) {
            return 0;
        }
        for (i = 0U; i < 16U; ++i) {
            out_box->uuid[i] = bytes[(omc_size)uuid_off + (omc_size)i];
        }
        out_box->has_uuid = 1;
        out_box->header_size += 16U;
    }

    return 1;
}

static int
omc_preview_bmff_is_cr3_brand(omc_u32 brand)
{
    return brand == OMC_FOURCC('c', 'r', 'x', ' ')
           || brand == OMC_FOURCC('C', 'R', '3', ' ');
}

static int
omc_preview_bmff_has_cr3_brand(const omc_u8* file_bytes, omc_size file_size)
{
    omc_u64 off;
    omc_u32 seen;
    omc_preview_bmff_box box;

    if (file_bytes == (const omc_u8*)0 || file_size < 8U) {
        return 0;
    }

    off = 0U;
    seen = 0U;
    while (off + 8U <= (omc_u64)file_size) {
        omc_u64 payload_off;
        omc_u64 payload_size;
        omc_u32 major_brand;
        omc_u64 brands_off;
        omc_u64 brands_end;
        omc_u64 p;

        seen += 1U;
        if (seen > (1U << 14)) {
            return 0;
        }

        if (!omc_preview_parse_bmff_box(file_bytes, file_size, off,
                                        (omc_u64)file_size, &box)) {
            return 0;
        }
        if (box.type != OMC_FOURCC('f', 't', 'y', 'p')) {
            off += box.size;
            if (box.size == 0U) {
                break;
            }
            continue;
        }

        payload_off = box.offset + box.header_size;
        payload_size = box.size - box.header_size;
        if (payload_size < 8U) {
            return 0;
        }
        if (!omc_preview_read_u32be(file_bytes, file_size, payload_off,
                                    &major_brand)) {
            return 0;
        }
        if (omc_preview_bmff_is_cr3_brand(major_brand)) {
            return 1;
        }

        brands_off = payload_off + 8U;
        brands_end = payload_off + payload_size;
        for (p = brands_off; p + 4U <= brands_end; p += 4U) {
            omc_u32 brand;

            if (!omc_preview_read_u32be(file_bytes, file_size, p, &brand)) {
                return 0;
            }
            if (omc_preview_bmff_is_cr3_brand(brand)) {
                return 1;
            }
        }
        return 0;
    }

    return 0;
}

static int
omc_preview_bmff_box_can_have_children(omc_u32 type)
{
    switch (type) {
    case OMC_FOURCC('m', 'o', 'o', 'v'):
    case OMC_FOURCC('t', 'r', 'a', 'k'):
    case OMC_FOURCC('m', 'd', 'i', 'a'):
    case OMC_FOURCC('m', 'i', 'n', 'f'):
    case OMC_FOURCC('s', 't', 'b', 'l'):
    case OMC_FOURCC('u', 'd', 't', 'a'):
    case OMC_FOURCC('m', 'e', 't', 'a'):
    case OMC_FOURCC('i', 'p', 'r', 'p'):
    case OMC_FOURCC('i', 'p', 'c', 'o'):
    case OMC_FOURCC('m', 'o', 'o', 'f'):
    case OMC_FOURCC('t', 'r', 'a', 'f'): return 1;
    default: return 0;
    }
}

static int
omc_preview_uuid_equals(const omc_u8* a, const omc_u8* b)
{
    if (a == (const omc_u8*)0 || b == (const omc_u8*)0) {
        return 0;
    }
    return memcmp(a, b, 16U) == 0;
}

static omc_preview_scan_status
omc_preview_collect_cr3_prvw_candidate_from_uuid_box(
    const omc_u8* file_bytes, omc_size file_size,
    omc_preview_bmff_box uuid_box, omc_preview_candidate* out_candidates,
    omc_u32 out_cap, omc_u32* written, omc_u32* needed,
    const omc_preview_scan_opts* opts)
{
    omc_u64 payload_off;
    omc_u64 payload_size;
    omc_u64 prvw_off;
    omc_u64 prvw_end;
    omc_preview_bmff_box prvw;
    omc_u64 jpeg_rel;
    omc_u32 jpeg_len32;
    omc_u64 jpeg_len;
    omc_u64 jpeg_off;
    omc_u64 prvw_payload_off;
    omc_u64 prvw_end_abs;
    omc_preview_candidate candidate;

    payload_off = uuid_box.offset + uuid_box.header_size;
    payload_size = uuid_box.size - uuid_box.header_size;
    if (payload_off > (omc_u64)file_size
        || payload_size > ((omc_u64)file_size - payload_off)) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }
    if (payload_size < 36U) {
        return OMC_PREVIEW_SCAN_OK;
    }

    prvw_off = payload_off + 8U;
    prvw_end = payload_off + payload_size;
    if (!omc_preview_parse_bmff_box(file_bytes, file_size, prvw_off, prvw_end,
                                    &prvw)) {
        return OMC_PREVIEW_SCAN_OK;
    }
    if (prvw.type != OMC_FOURCC('P', 'R', 'V', 'W')) {
        return OMC_PREVIEW_SCAN_OK;
    }

    jpeg_rel = 32U;
    if (payload_size < jpeg_rel + 2U) {
        return OMC_PREVIEW_SCAN_OK;
    }
    if (!omc_preview_read_u32be(file_bytes, file_size,
                                payload_off + jpeg_rel - 4U, &jpeg_len32)) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }
    jpeg_len = (omc_u64)jpeg_len32;
    if (jpeg_len == 0U || jpeg_len > opts->limits.max_preview_bytes) {
        return OMC_PREVIEW_SCAN_OK;
    }
    if (jpeg_len > payload_size - jpeg_rel) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }

    jpeg_off = payload_off + jpeg_rel;
    if (jpeg_off > (omc_u64)file_size
        || jpeg_len > ((omc_u64)file_size - jpeg_off)) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }

    prvw_payload_off = prvw.offset + prvw.header_size;
    prvw_end_abs = prvw.offset + prvw.size;
    if (jpeg_off < prvw_payload_off || jpeg_off + jpeg_len > prvw_end_abs) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }
    if (!omc_preview_is_jpeg_soi(file_bytes, file_size, jpeg_off, jpeg_len)) {
        return OMC_PREVIEW_SCAN_OK;
    }

    candidate.kind = OMC_PREVIEW_CR3_PRVW_JPEG;
    candidate.format = OMC_SCAN_FMT_CR3;
    candidate.block_index = 0U;
    candidate.offset_tag = 0U;
    candidate.length_tag = 0U;
    candidate.file_offset = jpeg_off;
    candidate.size = jpeg_len;
    candidate.has_jpeg_soi_signature = 0;
    return omc_preview_add_candidate(file_bytes, file_size, out_candidates,
                                     out_cap, written, needed, opts,
                                     &candidate);
}

static omc_preview_scan_status
omc_preview_scan_bmff_range_for_cr3_prvw_uuid(
    const omc_u8* file_bytes, omc_size file_size, omc_u64 start, omc_u64 end,
    omc_u32 depth, const omc_u8* uuid, omc_preview_candidate* out_candidates,
    omc_u32 out_cap, omc_u32* written, omc_u32* needed,
    const omc_preview_scan_opts* opts, omc_preview_cr3_state* state)
{
    omc_u64 off;

    if (state == (omc_preview_cr3_state*)0) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }
    if (depth > 16U) {
        return OMC_PREVIEW_SCAN_LIMIT;
    }

    off = start;
    while (off + 8U <= end && off + 8U <= (omc_u64)file_size) {
        omc_preview_bmff_box box;

        state->boxes_seen += 1U;
        if (state->boxes_seen > (1U << 16)) {
            return OMC_PREVIEW_SCAN_LIMIT;
        }

        if (!omc_preview_parse_bmff_box(file_bytes, file_size, off, end,
                                        &box)) {
            return OMC_PREVIEW_SCAN_MALFORMED;
        }

        if (box.type == OMC_FOURCC('u', 'u', 'i', 'd') && box.has_uuid
            && omc_preview_uuid_equals(box.uuid, uuid)) {
            omc_preview_scan_status one;

            one = omc_preview_collect_cr3_prvw_candidate_from_uuid_box(
                file_bytes, file_size, box, out_candidates, out_cap, written,
                needed, opts);
            if (one == OMC_PREVIEW_SCAN_TRUNCATED) {
                state->truncated = 1;
            } else if (one == OMC_PREVIEW_SCAN_LIMIT
                       || one == OMC_PREVIEW_SCAN_MALFORMED) {
                return one;
            }
        }

        if (omc_preview_bmff_box_can_have_children(box.type)) {
            omc_u64 child_off;
            omc_u64 child_end;
            omc_preview_scan_status inner;

            child_off = box.offset + box.header_size;
            child_end = box.offset + box.size;
            if (box.type == OMC_FOURCC('m', 'e', 't', 'a')) {
                if (child_end - child_off < 4U) {
                    return OMC_PREVIEW_SCAN_MALFORMED;
                }
                child_off += 4U;
            }

            if (child_off <= child_end) {
                inner = omc_preview_scan_bmff_range_for_cr3_prvw_uuid(
                    file_bytes, file_size, child_off, child_end, depth + 1U,
                    uuid, out_candidates, out_cap, written, needed, opts,
                    state);
                if (inner == OMC_PREVIEW_SCAN_TRUNCATED) {
                    state->truncated = 1;
                } else if (inner == OMC_PREVIEW_SCAN_LIMIT
                           || inner == OMC_PREVIEW_SCAN_MALFORMED) {
                    return inner;
                }
            }
        }

        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }

    return state->truncated ? OMC_PREVIEW_SCAN_TRUNCATED
                            : OMC_PREVIEW_SCAN_OK;
}

static omc_preview_scan_status
omc_preview_collect_cr3_prvw_candidates(
    const omc_u8* file_bytes, omc_size file_size,
    omc_preview_candidate* out_candidates, omc_u32 out_cap, omc_u32* written,
    omc_u32* needed, const omc_preview_scan_opts* opts)
{
    static const omc_u8 k_cr3_prvw_uuid[16] = {
        0xEAU, 0xF4U, 0x2BU, 0x5EU, 0x1CU, 0x98U, 0x4BU, 0x88U,
        0xB9U, 0xFBU, 0xB7U, 0xDCU, 0x40U, 0x6EU, 0x4DU, 0x16U
    };
    omc_preview_cr3_state state;

    if (!opts->include_cr3_prvw_jpeg) {
        return OMC_PREVIEW_SCAN_UNSUPPORTED;
    }
    if (!omc_preview_bmff_has_cr3_brand(file_bytes, file_size)) {
        return OMC_PREVIEW_SCAN_UNSUPPORTED;
    }

    state.truncated = 0;
    state.boxes_seen = 0U;
    return omc_preview_scan_bmff_range_for_cr3_prvw_uuid(
        file_bytes, file_size, 0U, (omc_u64)file_size, 0U, k_cr3_prvw_uuid,
        out_candidates, out_cap, written, needed, opts, &state);
}

static omc_preview_scan_status
omc_preview_collect_tiff_candidates(const omc_u8* file_bytes,
                                    omc_size file_size, omc_blk_ref block,
                                    omc_u32 block_index,
                                    omc_preview_candidate* out_candidates,
                                    omc_u32 out_cap, omc_u32* written,
                                    omc_u32* needed,
                                    const omc_preview_scan_opts* opts)
{
    omc_preview_tiff_cfg cfg;
    omc_u64 ifd0;
    omc_u64 ifd_queue[256];
    omc_u32 ifd_cap;
    omc_u32 ifd_count;
    omc_u32 ifd_index;
    omc_u32 total_entries;
    int truncated;

    if (block.data_offset > (omc_u64)file_size
        || block.data_size > ((omc_u64)file_size - block.data_offset)) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }
    if (!omc_preview_parse_tiff_header(file_bytes + (omc_size)block.data_offset,
                                       (omc_size)block.data_size, &cfg,
                                       &ifd0)) {
        return OMC_PREVIEW_SCAN_UNSUPPORTED;
    }
    if (ifd0 == 0U || ifd0 > block.data_size) {
        return OMC_PREVIEW_SCAN_MALFORMED;
    }

    ifd_cap = opts->limits.max_ifds;
    if (ifd_cap > 256U) {
        ifd_cap = 256U;
    }
    ifd_count = 0U;
    ifd_index = 0U;
    if (!omc_preview_push_ifd_offset(ifd_queue, ifd_cap, &ifd_count, ifd0)) {
        return OMC_PREVIEW_SCAN_LIMIT;
    }

    total_entries = 0U;
    truncated = 0;
    while (ifd_index < ifd_count) {
        omc_u64 ifd_off;
        omc_u16 entry_count;
        omc_u64 ifd_bytes;
        omc_u16 ei;
        int have_jif_off;
        int have_jif_len;
        omc_u32 jif_off;
        omc_u32 jif_len;

        ifd_off = ifd_queue[ifd_index++];
        if (ifd_off + 2U > block.data_size) {
            return OMC_PREVIEW_SCAN_MALFORMED;
        }
        if (!omc_preview_read_tiff_u16(cfg,
                                       file_bytes + (omc_size)block.data_offset,
                                       (omc_size)block.data_size, ifd_off,
                                       &entry_count)) {
            return OMC_PREVIEW_SCAN_MALFORMED;
        }

        ifd_bytes = (omc_u64)2U + ((omc_u64)entry_count * (omc_u64)12U)
                    + (omc_u64)4U;
        if (ifd_off + ifd_bytes > block.data_size) {
            return OMC_PREVIEW_SCAN_MALFORMED;
        }
        if (total_entries + (omc_u32)entry_count > opts->limits.max_total_entries) {
            return OMC_PREVIEW_SCAN_LIMIT;
        }
        total_entries += (omc_u32)entry_count;

        have_jif_off = 0;
        have_jif_len = 0;
        jif_off = 0U;
        jif_len = 0U;
        for (ei = 0U; ei < entry_count; ++ei) {
            omc_u64 entry_off;
            omc_preview_ifd_entry entry;

            entry_off = ifd_off + (omc_u64)2U + ((omc_u64)ei * (omc_u64)12U);
            if (!omc_preview_read_ifd_entry(
                    cfg, file_bytes + (omc_size)block.data_offset,
                    (omc_size)block.data_size, entry_off, &entry)) {
                return OMC_PREVIEW_SCAN_MALFORMED;
            }

            if (entry.tag == 0x0201U
                && opts->include_exif_jpeg_interchange) {
                have_jif_off = omc_preview_entry_scalar_u32(cfg, entry,
                                                            &jif_off);
            } else if (entry.tag == 0x0202U
                       && opts->include_exif_jpeg_interchange) {
                have_jif_len = omc_preview_entry_scalar_u32(cfg, entry,
                                                            &jif_len);
            } else if ((entry.tag == 0x002EU || entry.tag == 0x0127U)
                       && opts->include_jpg_from_raw) {
                omc_u64 elem_size;
                omc_u64 byte_count;
                omc_u64 local_off;
                omc_preview_candidate candidate;
                omc_preview_scan_status one;

                elem_size = omc_preview_tiff_type_size(entry.type);
                if (elem_size == 0U) {
                    continue;
                }
                if (entry.count > 0U
                    && elem_size > opts->limits.max_preview_bytes
                                       / (omc_u64)entry.count) {
                    return OMC_PREVIEW_SCAN_LIMIT;
                }
                byte_count = elem_size * (omc_u64)entry.count;
                if (byte_count == 0U
                    || byte_count > opts->limits.max_preview_bytes) {
                    continue;
                }
                if (byte_count <= 4U) {
                    continue;
                }

                local_off = (omc_u64)entry.value_or_off;
                if (local_off > block.data_size
                    || byte_count > (block.data_size - local_off)) {
                    return OMC_PREVIEW_SCAN_MALFORMED;
                }
                if (block.data_offset > (omc_u64)file_size
                    || local_off > ((omc_u64)file_size - block.data_offset)
                    || byte_count
                           > ((omc_u64)file_size - block.data_offset
                              - local_off)) {
                    return OMC_PREVIEW_SCAN_MALFORMED;
                }

                candidate.kind = (entry.tag == 0x002EU)
                                     ? OMC_PREVIEW_EXIF_JPG_FROM_RAW
                                     : OMC_PREVIEW_EXIF_JPG_FROM_RAW2;
                candidate.format = block.format;
                candidate.block_index = block_index;
                candidate.offset_tag = entry.tag;
                candidate.length_tag = 0U;
                candidate.file_offset = block.data_offset + local_off;
                candidate.size = byte_count;
                candidate.has_jpeg_soi_signature = 0;
                one = omc_preview_add_candidate(
                    file_bytes, file_size, out_candidates, out_cap, written,
                    needed, opts, &candidate);
                if (one == OMC_PREVIEW_SCAN_TRUNCATED) {
                    truncated = 1;
                } else if (one != OMC_PREVIEW_SCAN_OK) {
                    return one;
                }
            }

            if (entry.tag == 0x8769U || entry.tag == 0x8825U
                || entry.tag == 0xA005U) {
                omc_u32 child;

                child = 0U;
                if (omc_preview_entry_scalar_u32(cfg, entry, &child)
                    && child != 0U) {
                    if (!omc_preview_push_ifd_offset(ifd_queue, ifd_cap,
                                                     &ifd_count,
                                                     (omc_u64)child)) {
                        return OMC_PREVIEW_SCAN_LIMIT;
                    }
                }
            } else if (entry.tag == 0x014AU) {
                omc_u64 elem_size;
                omc_u64 bytes_needed;

                elem_size = omc_preview_tiff_type_size(entry.type);
                if (elem_size != 4U || entry.count == 0U) {
                    continue;
                }
                bytes_needed = (omc_u64)entry.count * elem_size;
                if (bytes_needed <= 4U) {
                    omc_u32 one_ifd;

                    one_ifd = entry.value_or_off;
                    if (one_ifd != 0U
                        && !omc_preview_push_ifd_offset(ifd_queue, ifd_cap,
                                                        &ifd_count,
                                                        (omc_u64)one_ifd)) {
                        return OMC_PREVIEW_SCAN_LIMIT;
                    }
                } else {
                    omc_u64 off;
                    omc_u32 ai;

                    off = (omc_u64)entry.value_or_off;
                    if (off > block.data_size
                        || bytes_needed > (block.data_size - off)) {
                        return OMC_PREVIEW_SCAN_MALFORMED;
                    }
                    for (ai = 0U; ai < entry.count; ++ai) {
                        omc_u32 one_ifd;
                        omc_u64 value_off;

                        value_off = off + ((omc_u64)ai * 4U);
                        if (!omc_preview_read_tiff_u32(
                                cfg, file_bytes + (omc_size)block.data_offset,
                                (omc_size)block.data_size, value_off,
                                &one_ifd)) {
                            return OMC_PREVIEW_SCAN_MALFORMED;
                        }
                        if (one_ifd != 0U
                            && !omc_preview_push_ifd_offset(
                                   ifd_queue, ifd_cap, &ifd_count,
                                   (omc_u64)one_ifd)) {
                            return OMC_PREVIEW_SCAN_LIMIT;
                        }
                    }
                }
            }
        }

        if (have_jif_off && have_jif_len && jif_len != 0U) {
            omc_u64 off64;
            omc_u64 len64;
            omc_preview_candidate candidate;
            omc_preview_scan_status one;

            off64 = (omc_u64)jif_off;
            len64 = (omc_u64)jif_len;
            if (len64 > opts->limits.max_preview_bytes) {
                return OMC_PREVIEW_SCAN_LIMIT;
            }
            if (off64 > block.data_size || len64 > (block.data_size - off64)) {
                return OMC_PREVIEW_SCAN_MALFORMED;
            }
            if (block.data_offset > (omc_u64)file_size
                || off64 > ((omc_u64)file_size - block.data_offset)
                || len64 > ((omc_u64)file_size - block.data_offset - off64)) {
                return OMC_PREVIEW_SCAN_MALFORMED;
            }

            candidate.kind = OMC_PREVIEW_EXIF_JPEG_INTERCHANGE;
            candidate.format = block.format;
            candidate.block_index = block_index;
            candidate.offset_tag = 0x0201U;
            candidate.length_tag = 0x0202U;
            candidate.file_offset = block.data_offset + off64;
            candidate.size = len64;
            candidate.has_jpeg_soi_signature = 0;
            one = omc_preview_add_candidate(file_bytes, file_size,
                                            out_candidates, out_cap, written,
                                            needed, opts, &candidate);
            if (one == OMC_PREVIEW_SCAN_TRUNCATED) {
                truncated = 1;
            } else if (one != OMC_PREVIEW_SCAN_OK) {
                return one;
            }
        }

        {
            omc_u32 next_ifd;

            if (!omc_preview_read_tiff_u32(
                    cfg, file_bytes + (omc_size)block.data_offset,
                    (omc_size)block.data_size,
                    ifd_off + (omc_u64)2U
                        + ((omc_u64)entry_count * (omc_u64)12U),
                    &next_ifd)) {
                return OMC_PREVIEW_SCAN_MALFORMED;
            }
            if (next_ifd != 0U
                && !omc_preview_push_ifd_offset(ifd_queue, ifd_cap, &ifd_count,
                                                (omc_u64)next_ifd)) {
                return OMC_PREVIEW_SCAN_LIMIT;
            }
        }
    }

    return truncated ? OMC_PREVIEW_SCAN_TRUNCATED : OMC_PREVIEW_SCAN_OK;
}

void
omc_preview_scan_opts_init(omc_preview_scan_opts* opts)
{
    if (opts == (omc_preview_scan_opts*)0) {
        return;
    }

    opts->include_exif_jpeg_interchange = 1;
    opts->include_jpg_from_raw = 1;
    opts->include_cr3_prvw_jpeg = 1;
    opts->require_jpeg_soi = 0;
    opts->limits.max_ifds = 256U;
    opts->limits.max_total_entries = 8192U;
    opts->limits.max_preview_bytes
        = (omc_u64)512U * (omc_u64)1024U * (omc_u64)1024U;
}

omc_preview_scan_res
omc_preview_find_candidates(const omc_u8* file_bytes, omc_size file_size,
                            const omc_blk_ref* blocks, omc_u32 block_count,
                            omc_preview_candidate* out_candidates,
                            omc_u32 out_cap,
                            const omc_preview_scan_opts* opts)
{
    omc_preview_scan_opts local_opts;
    const omc_preview_scan_opts* cfg;
    omc_preview_scan_res result;
    int supported;
    int truncated;
    omc_u32 i;

    result.status = OMC_PREVIEW_SCAN_UNSUPPORTED;
    result.written = 0U;
    result.needed = 0U;
    if (file_bytes == (const omc_u8*)0) {
        result.status = OMC_PREVIEW_SCAN_MALFORMED;
        return result;
    }
    if (block_count != 0U && blocks == (const omc_blk_ref*)0) {
        result.status = OMC_PREVIEW_SCAN_MALFORMED;
        return result;
    }

    if (opts != (const omc_preview_scan_opts*)0) {
        cfg = opts;
    } else {
        omc_preview_scan_opts_init(&local_opts);
        cfg = &local_opts;
    }

    supported = 0;
    truncated = 0;
    for (i = 0U; i < block_count; ++i) {
        omc_preview_scan_status one;

        if (blocks[i].kind != OMC_BLK_EXIF) {
            continue;
        }
        if (blocks[i].part_count > 1U && blocks[i].part_index != 0U) {
            continue;
        }

        supported = 1;
        one = omc_preview_collect_tiff_candidates(
            file_bytes, file_size, blocks[i], i, out_candidates, out_cap,
            &result.written, &result.needed, cfg);
        if (one == OMC_PREVIEW_SCAN_TRUNCATED) {
            truncated = 1;
            continue;
        }
        if (one == OMC_PREVIEW_SCAN_LIMIT
            || one == OMC_PREVIEW_SCAN_MALFORMED) {
            result.status = one;
            return result;
        }
    }

    {
        omc_preview_scan_status cr3;

        cr3 = omc_preview_collect_cr3_prvw_candidates(
            file_bytes, file_size, out_candidates, out_cap, &result.written,
            &result.needed, cfg);
        if (cr3 == OMC_PREVIEW_SCAN_TRUNCATED) {
            supported = 1;
            truncated = 1;
        } else if (cr3 == OMC_PREVIEW_SCAN_OK) {
            supported = 1;
        } else if (cr3 == OMC_PREVIEW_SCAN_LIMIT
                   || cr3 == OMC_PREVIEW_SCAN_MALFORMED) {
            result.status = cr3;
            return result;
        }
    }

    if (truncated) {
        result.status = OMC_PREVIEW_SCAN_TRUNCATED;
    } else if (!supported) {
        result.status = OMC_PREVIEW_SCAN_UNSUPPORTED;
    } else {
        result.status = OMC_PREVIEW_SCAN_OK;
    }
    return result;
}

omc_preview_scan_res
omc_preview_scan_candidates(const omc_u8* file_bytes, omc_size file_size,
                            omc_blk_ref* blocks_scratch, omc_u32 block_cap,
                            omc_preview_candidate* out_candidates,
                            omc_u32 out_cap,
                            const omc_preview_scan_opts* opts)
{
    omc_scan_res scan;
    omc_preview_scan_res result;

    result.status = OMC_PREVIEW_SCAN_UNSUPPORTED;
    result.written = 0U;
    result.needed = 0U;
    if (file_bytes == (const omc_u8*)0) {
        result.status = OMC_PREVIEW_SCAN_MALFORMED;
        return result;
    }

    scan = omc_scan_auto(file_bytes, file_size, blocks_scratch, block_cap);
    if (scan.status == OMC_SCAN_UNSUPPORTED) {
        result.status = OMC_PREVIEW_SCAN_UNSUPPORTED;
        return result;
    }
    if (scan.status == OMC_SCAN_MALFORMED) {
        result.status = OMC_PREVIEW_SCAN_MALFORMED;
        return result;
    }

    result = omc_preview_find_candidates(file_bytes, file_size, blocks_scratch,
                                         scan.written, out_candidates, out_cap,
                                         opts);
    if (scan.status == OMC_SCAN_TRUNCATED
        && result.status == OMC_PREVIEW_SCAN_OK) {
        result.status = OMC_PREVIEW_SCAN_TRUNCATED;
    }
    return result;
}

void
omc_preview_extract_opts_init(omc_preview_extract_opts* opts)
{
    if (opts == (omc_preview_extract_opts*)0) {
        return;
    }

    opts->max_output_bytes
        = (omc_u64)128U * (omc_u64)1024U * (omc_u64)1024U;
    opts->require_jpeg_soi = 0;
}

omc_preview_extract_res
omc_preview_extract_candidate(const omc_u8* file_bytes, omc_size file_size,
                              const omc_preview_candidate* candidate,
                              omc_u8* out_bytes, omc_size out_cap,
                              const omc_preview_extract_opts* opts)
{
    omc_preview_extract_opts local_opts;
    const omc_preview_extract_opts* cfg;
    omc_preview_extract_res result;

    result.status = OMC_PREVIEW_EXTRACT_OK;
    result.written = 0U;
    result.needed = 0U;
    if (candidate != (const omc_preview_candidate*)0) {
        result.needed = candidate->size;
    }

    if (file_bytes == (const omc_u8*)0
        || candidate == (const omc_preview_candidate*)0) {
        result.status = OMC_PREVIEW_EXTRACT_MALFORMED;
        return result;
    }

    if (opts != (const omc_preview_extract_opts*)0) {
        cfg = opts;
    } else {
        omc_preview_extract_opts_init(&local_opts);
        cfg = &local_opts;
    }

    if (candidate->size > cfg->max_output_bytes) {
        result.status = OMC_PREVIEW_EXTRACT_LIMIT;
        return result;
    }
    if (candidate->file_offset > (omc_u64)file_size
        || candidate->size > ((omc_u64)file_size - candidate->file_offset)) {
        result.status = OMC_PREVIEW_EXTRACT_MALFORMED;
        return result;
    }
    if (cfg->require_jpeg_soi
        && !omc_preview_is_jpeg_soi(file_bytes, file_size,
                                    candidate->file_offset, candidate->size)) {
        result.status = OMC_PREVIEW_EXTRACT_MALFORMED;
        return result;
    }
    if (candidate->size > (omc_u64)out_cap) {
        result.status = OMC_PREVIEW_EXTRACT_TRUNCATED;
        return result;
    }

    if (candidate->size != 0U) {
        if (out_bytes == (omc_u8*)0) {
            result.status = OMC_PREVIEW_EXTRACT_MALFORMED;
            return result;
        }
        memcpy(out_bytes, file_bytes + (omc_size)candidate->file_offset,
               (omc_size)candidate->size);
    }
    result.written = candidate->size;
    return result;
}
