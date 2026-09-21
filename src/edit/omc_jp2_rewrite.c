#include "omc/omc_jp2_rewrite.h"
#include "omc/omc_scan.h"

#include <string.h>

static omc_u32
omc_jp2_u32be(const omc_u8 *p)
{
    return ((omc_u32)p[0] << 24U) | ((omc_u32)p[1] << 16U)
           | ((omc_u32)p[2] << 8U) | (omc_u32)p[3];
}

static omc_u64
omc_jp2_u64be(const omc_u8 *p)
{
    omc_u64 value = 0U;
    unsigned i;
    for (i = 0U; i < 8U; ++i)
        value = (value << 8U) | p[i];
    return value;
}

static omc_status
omc_jp2_append(omc_arena *out, const omc_u8 *data, omc_size size)
{
    omc_byte_ref ref;
    if (size == 0U)
        return OMC_STATUS_OK;
    return omc_arena_append(out, data, size, &ref);
}

static omc_status
omc_jp2_append_box(omc_arena *out, omc_u32 type,
                   const omc_u8 *payload, omc_size payload_size)
{
    omc_u8 header[8];
    omc_status status;
    if ((omc_u64)payload_size > (omc_u64)0xFFFFFFFFU - 8U)
        return OMC_STATUS_OVERFLOW;
    header[0] = (omc_u8)((payload_size + 8U) >> 24U);
    header[1] = (omc_u8)((payload_size + 8U) >> 16U);
    header[2] = (omc_u8)((payload_size + 8U) >> 8U);
    header[3] = (omc_u8)(payload_size + 8U);
    header[4] = (omc_u8)(type >> 24U);
    header[5] = (omc_u8)(type >> 16U);
    header[6] = (omc_u8)(type >> 8U);
    header[7] = (omc_u8)type;
    status = omc_jp2_append(out, header, sizeof(header));
    if (status == OMC_STATUS_OK)
        status = omc_jp2_append(out, payload, payload_size);
    return status;
}

static int
omc_jp2_uuid_is(const omc_u8 *uuid, const omc_u8 *expected)
{
    return memcmp(uuid, expected, 16U) == 0;
}

void
omc_jp2_rewrite_opts_init(omc_jp2_rewrite_opts *opts)
{
    if (opts == (omc_jp2_rewrite_opts *)0)
        return;
    opts->replace_exif = 0;
    opts->replace_xmp = 1;
}

omc_status
omc_jp2_rewrite(const omc_u8 *input, omc_size input_size,
                const omc_u8 *exif_payload, omc_size exif_size,
                const omc_u8 *xmp_payload, omc_size xmp_size,
                const omc_jp2_rewrite_opts *opts_in, omc_arena *out,
                omc_jp2_rewrite_res *result)
{
    static const omc_u8 k_exif_uuid[16] = {
        0x4aU, 0x70U, 0x67U, 0x54U, 0x69U, 0x66U, 0x66U, 0x45U,
        0x78U, 0x69U, 0x66U, 0x2dU, 0x3eU, 0x4aU, 0x50U, 0x32U
    };
    static const omc_u8 k_xmp_uuid[16] = {
        0xbeU, 0x7aU, 0xcfU, 0xcbU, 0x97U, 0xa9U, 0x42U, 0xe8U,
        0x9cU, 0x71U, 0x99U, 0x94U, 0x91U, 0xe3U, 0xafU, 0xacU
    };
    omc_jp2_rewrite_opts opts;
    omc_size offset;
    omc_size terminal_offset;
    omc_size reserve_size;
    int saw_signature;
    int terminal;
    int terminal_kept;
    omc_status status;

    if (result == (omc_jp2_rewrite_res *)0 || out == (omc_arena *)0
        || input == (const omc_u8 *)0 || input_size < 8U
        || (exif_size != 0U && exif_payload == (const omc_u8 *)0)
        || (xmp_size != 0U && xmp_payload == (const omc_u8 *)0))
        return OMC_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    omc_jp2_rewrite_opts_init(&opts);
    if (opts_in != (const omc_jp2_rewrite_opts *)0)
        opts = *opts_in;
    if ((opts.replace_exif != 0 && opts.replace_exif != 1)
        || (opts.replace_xmp != 0 && opts.replace_xmp != 1)) {
        result->status = OMC_JP2_REWRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }
    reserve_size = input_size;
    if (opts.replace_exif) {
        if (reserve_size > ~(omc_size)0 - exif_size - 8U)
            return OMC_STATUS_OVERFLOW;
        reserve_size += exif_size + 8U;
    }
    if (opts.replace_xmp) {
        if (reserve_size > ~(omc_size)0 - xmp_size - 8U)
            return OMC_STATUS_OVERFLOW;
        reserve_size += xmp_size + 8U;
    }
    omc_arena_reset(out);
    status = omc_arena_reserve(out, reserve_size);
    if (status != OMC_STATUS_OK)
        return status;

    offset = 0U;
    terminal_offset = input_size;
    saw_signature = 0;
    terminal = 0;
    terminal_kept = 0;
    while (offset < input_size) {
        omc_u32 size32;
        omc_u32 type;
        omc_u64 box_size_u64;
        omc_size header_size;
        omc_size box_size;
        omc_size payload_offset;
        int remove;

        if (input_size - offset < 8U) {
            result->status = OMC_JP2_REWRITE_MALFORMED;
            omc_arena_reset(out);
            return OMC_STATUS_OK;
        }
        size32 = omc_jp2_u32be(input + offset);
        type = omc_jp2_u32be(input + offset + 4U);
        header_size = 8U;
        if (size32 == 1U) {
            if (input_size - offset < 16U) {
                result->status = OMC_JP2_REWRITE_MALFORMED;
                omc_arena_reset(out);
                return OMC_STATUS_OK;
            }
            box_size_u64 = omc_jp2_u64be(input + offset + 8U);
            header_size = 16U;
        } else if (size32 == 0U) {
            box_size_u64 = (omc_u64)(input_size - offset);
            terminal = 1;
            terminal_offset = offset;
        } else {
            box_size_u64 = size32;
        }
        if (box_size_u64 < (omc_u64)header_size
            || box_size_u64 > (omc_u64)(input_size - offset)) {
            result->status = OMC_JP2_REWRITE_MALFORMED;
            omc_arena_reset(out);
            return OMC_STATUS_OK;
        }
        box_size = (omc_size)box_size_u64;
        payload_offset = offset + header_size;
        if (type == (omc_u32)OMC_FOURCC('u', 'u', 'i', 'd')
            && box_size - header_size < 16U) {
            result->status = OMC_JP2_REWRITE_MALFORMED;
            omc_arena_reset(out);
            return OMC_STATUS_OK;
        }
        remove = 0;
        if (type == (omc_u32)OMC_FOURCC('j', 'P', ' ', ' '))
            saw_signature = 1;
        if (type == (omc_u32)OMC_FOURCC('E', 'x', 'i', 'f') && opts.replace_exif)
            remove = 1;
        if (type == (omc_u32)OMC_FOURCC('x', 'm', 'l', ' ') && opts.replace_xmp)
            remove = 1;
        if (type == (omc_u32)OMC_FOURCC('u', 'u', 'i', 'd')
            && box_size - header_size >= 16U) {
            const omc_u8 *uuid = input + payload_offset;
            if ((opts.replace_exif && omc_jp2_uuid_is(uuid, k_exif_uuid))
                || (opts.replace_xmp && omc_jp2_uuid_is(uuid, k_xmp_uuid)))
                remove = 1;
        }
        if (!remove) {
            status = omc_jp2_append(out, input + offset, box_size);
            if (status != OMC_STATUS_OK)
                return status;
            if (terminal)
                terminal_kept = 1;
        } else if (type == (omc_u32)OMC_FOURCC('E', 'x', 'i', 'f')
                   || (type == (omc_u32)OMC_FOURCC('u', 'u', 'i', 'd')
                       && box_size - header_size >= 16U
                       && opts.replace_exif
                       && omc_jp2_uuid_is(input + payload_offset,
                                          k_exif_uuid))) {
            result->removed_exif += 1U;
        } else {
            result->removed_xmp += 1U;
        }
        offset += box_size;
        if (terminal) {
            offset = input_size;
            break;
        }
    }
    if (!saw_signature || offset != input_size) {
        result->status = OMC_JP2_REWRITE_MALFORMED;
        omc_arena_reset(out);
        return OMC_STATUS_OK;
    }
    if (terminal) {
        /* A size-zero box consumes the remainder. Rebuild with the terminal
         * box at the end so inserted metadata never follows it. */
        omc_size keep_size = input_size - terminal_offset;
        omc_arena rebuilt;
        omc_size prefix_size = terminal_kept ? out->size - keep_size
                                             : out->size;
        omc_arena_init(&rebuilt);
        status = omc_arena_reserve(&rebuilt, out->size + exif_size + xmp_size + 16U);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&rebuilt);
            return status;
        }
        status = omc_jp2_append(&rebuilt, out->data, prefix_size);
        if (status == OMC_STATUS_OK && opts.replace_exif && exif_size != 0U)
            status = omc_jp2_append_box(&rebuilt, (omc_u32)OMC_FOURCC('E','x','i','f'), exif_payload, exif_size);
        if (status == OMC_STATUS_OK && opts.replace_xmp && xmp_size != 0U)
            status = omc_jp2_append_box(&rebuilt, (omc_u32)OMC_FOURCC('x','m','l',' '), xmp_payload, xmp_size);
        if (status == OMC_STATUS_OK && terminal_kept)
            status = omc_jp2_append(&rebuilt, input + terminal_offset, keep_size);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&rebuilt);
            return status;
        }
        omc_arena_reset(out);
        status = omc_arena_reserve(out, rebuilt.size);
        if (status == OMC_STATUS_OK)
            status = omc_jp2_append(out, rebuilt.data, rebuilt.size);
        omc_arena_fini(&rebuilt);
        if (status != OMC_STATUS_OK)
            return status;
    } else {
        if (opts.replace_exif && exif_size != 0U) {
            status = omc_jp2_append_box(out, (omc_u32)OMC_FOURCC('E','x','i','f'), exif_payload, exif_size);
            if (status != OMC_STATUS_OK)
                return status;
        }
        if (opts.replace_xmp && xmp_size != 0U) {
            status = omc_jp2_append_box(out, (omc_u32)OMC_FOURCC('x','m','l',' '), xmp_payload, xmp_size);
            if (status != OMC_STATUS_OK)
                return status;
        }
    }
    result->inserted_exif = opts.replace_exif && exif_size != 0U ? 1U : 0U;
    result->inserted_xmp = opts.replace_xmp && xmp_size != 0U ? 1U : 0U;
    result->status = OMC_JP2_REWRITE_OK;
    result->needed = out->size;
    result->written = out->size;
    return OMC_STATUS_OK;
}
