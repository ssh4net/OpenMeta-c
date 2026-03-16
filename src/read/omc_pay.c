#include "omc/omc_pay.h"

#include <string.h>

#if OMC_HAVE_ZLIB
#include <zlib.h>
#endif

#if OMC_HAVE_BROTLI
#include <brotli/decode.h>
#endif

static void
omc_pay_res_init(omc_pay_res* res)
{
    res->status = OMC_PAY_OK;
    res->written = 0U;
    res->needed = 0U;
}

static int
omc_pay_same_stream(const omc_blk_ref* seed, const omc_blk_ref* cand)
{
    if (seed->format != cand->format || seed->kind != cand->kind
        || seed->chunking != cand->chunking || seed->id != cand->id) {
        return 0;
    }
    if (seed->group != 0U || cand->group != 0U) {
        return seed->group == cand->group;
    }
    if (seed->part_count != 0U && cand->part_count != 0U) {
        return seed->part_count == cand->part_count;
    }
    return 1;
}

static omc_u64
omc_pay_min_u64(omc_u64 a, omc_u64 b)
{
    return (a < b) ? a : b;
}

static int
omc_pay_copy_range(const omc_u8* file_bytes, omc_size file_size,
                   omc_u64 data_offset, omc_u64 data_size,
                   omc_u8* out_payload, omc_size out_cap,
                   omc_u64 out_offset, omc_pay_res* res)
{
    omc_size src_off;
    omc_size copy_size;
    omc_size dst_off;

    if (data_offset > (omc_u64)file_size
        || data_size > ((omc_u64)file_size - data_offset)) {
        res->status = OMC_PAY_MALFORMED;
        return 0;
    }
    if (out_offset > res->needed) {
        res->status = OMC_PAY_MALFORMED;
        return 0;
    }
    if (out_offset > (omc_u64)out_cap) {
        if (res->status == OMC_PAY_OK) {
            res->status = OMC_PAY_TRUNCATED;
        }
        return 1;
    }

    src_off = (omc_size)data_offset;
    copy_size = (omc_size)data_size;
    dst_off = (omc_size)out_offset;

    if (copy_size > out_cap - dst_off) {
        copy_size = out_cap - dst_off;
        if (res->status == OMC_PAY_OK) {
            res->status = OMC_PAY_TRUNCATED;
        }
    }

    if (copy_size != 0U && out_payload != (omc_u8*)0) {
        memcpy(out_payload + dst_off, file_bytes + src_off, copy_size);
    }
    if ((omc_u64)(dst_off + copy_size) > res->written) {
        res->written = (omc_u64)(dst_off + copy_size);
    }
    return 1;
}

#if OMC_HAVE_ZLIB
static int
omc_pay_inflate_zlib_range(const omc_u8* file_bytes, omc_size file_size,
                           omc_u64 data_offset, omc_u64 data_size,
                           omc_u8* out_payload, omc_size out_cap,
                           omc_u64 max_output_bytes, omc_pay_res* res)
{
    z_stream strm;
    omc_u64 in_off;
    omc_u64 total_out;
    int zr;
    omc_u8 scratch[256];
    uInt max_chunk;

    if (data_offset > (omc_u64)file_size
        || data_size > ((omc_u64)file_size - data_offset)) {
        res->status = OMC_PAY_MALFORMED;
        return 0;
    }

    memset(&strm, 0, sizeof(strm));
    in_off = 0U;
    total_out = 0U;
    max_chunk = (uInt)~(uInt)0;

    zr = inflateInit(&strm);
    if (zr != Z_OK) {
        res->status = (zr == Z_MEM_ERROR) ? OMC_PAY_NOMEM : OMC_PAY_UNSUPPORTED;
        return 0;
    }

    for (;;) {
        omc_u64 remain_limit;
        omc_u64 out_room;
        uInt out_avail;
        int use_scratch;
        omc_u64 produced;

        if (strm.avail_in == 0U && in_off < data_size) {
            omc_u64 feed_size;

            feed_size = data_size - in_off;
            feed_size = omc_pay_min_u64(feed_size, (omc_u64)max_chunk);
            strm.next_in =
                (Bytef*)(file_bytes + (omc_size)data_offset + (omc_size)in_off);
            strm.avail_in = (uInt)feed_size;
            in_off += feed_size;
        }

        if (total_out >= max_output_bytes) {
            res->status = OMC_PAY_LIMIT;
            (void)inflateEnd(&strm);
            res->needed = total_out;
            return 0;
        }
        remain_limit = max_output_bytes - total_out;

        use_scratch = 0;
        if (out_payload != (omc_u8*)0 && res->written < (omc_u64)out_cap) {
            out_room = (omc_u64)out_cap - res->written;
            out_room = omc_pay_min_u64(out_room, remain_limit);
            out_room = omc_pay_min_u64(out_room, (omc_u64)max_chunk);
            strm.next_out = (Bytef*)(out_payload + (omc_size)res->written);
            strm.avail_out = (uInt)out_room;
        } else {
            use_scratch = 1;
            out_room = omc_pay_min_u64((omc_u64)sizeof(scratch), remain_limit);
            out_room = omc_pay_min_u64(out_room, (omc_u64)max_chunk);
            strm.next_out = scratch;
            strm.avail_out = (uInt)out_room;
        }

        out_avail = strm.avail_out;
        zr = inflate(&strm, Z_NO_FLUSH);
        produced = (omc_u64)(out_avail - strm.avail_out);

        if (produced != 0U) {
            total_out += produced;
            if (use_scratch) {
                if (res->status == OMC_PAY_OK) {
                    res->status = OMC_PAY_TRUNCATED;
                }
            } else {
                res->written += produced;
            }
        }

        if (zr == Z_STREAM_END) {
            break;
        }
        if (zr == Z_OK) {
            if (produced == 0U && strm.avail_in == 0U && in_off >= data_size) {
                res->status = OMC_PAY_MALFORMED;
                (void)inflateEnd(&strm);
                res->needed = total_out;
                return 0;
            }
            continue;
        }
        if (zr == Z_MEM_ERROR) {
            res->status = OMC_PAY_NOMEM;
        } else if (zr == Z_BUF_ERROR && total_out >= max_output_bytes) {
            res->status = OMC_PAY_LIMIT;
        } else {
            res->status = OMC_PAY_MALFORMED;
        }
        (void)inflateEnd(&strm);
        res->needed = total_out;
        return 0;
    }

    if (inflateEnd(&strm) != Z_OK) {
        res->status = OMC_PAY_MALFORMED;
        res->needed = total_out;
        return 0;
    }

    res->needed = total_out;
    return 1;
}
#endif

#if OMC_HAVE_BROTLI
static int
omc_pay_brotli_range(const omc_u8* file_bytes, omc_size file_size,
                     omc_u64 data_offset, omc_u64 data_size,
                     omc_u8* out_payload, omc_size out_cap,
                     omc_u64 max_output_bytes, omc_pay_res* res)
{
    BrotliDecoderState* state;
    const uint8_t* next_in;
    size_t avail_in;
    omc_u64 total_out;
    omc_u8 scratch[256];

    if (data_offset > (omc_u64)file_size
        || data_size > ((omc_u64)file_size - data_offset)) {
        res->status = OMC_PAY_MALFORMED;
        return 0;
    }

    state = BrotliDecoderCreateInstance((brotli_alloc_func)0,
                                        (brotli_free_func)0, (void*)0);
    if (state == (BrotliDecoderState*)0) {
        res->status = OMC_PAY_NOMEM;
        return 0;
    }

    next_in = (const uint8_t*)(file_bytes + (omc_size)data_offset);
    avail_in = (size_t)data_size;
    total_out = 0U;

    for (;;) {
        size_t avail_out;
        uint8_t* next_out;
        int use_scratch;
        size_t out_before;
        omc_u64 produced;
        BrotliDecoderResult br;

        if (total_out >= max_output_bytes) {
            res->status = OMC_PAY_LIMIT;
            BrotliDecoderDestroyInstance(state);
            res->needed = total_out;
            return 0;
        }

        use_scratch = 0;
        if (out_payload != (omc_u8*)0 && res->written < (omc_u64)out_cap) {
            avail_out = (size_t)omc_pay_min_u64((omc_u64)out_cap - res->written,
                                                max_output_bytes - total_out);
            next_out = (uint8_t*)(out_payload + (omc_size)res->written);
        } else {
            use_scratch = 1;
            avail_out = (size_t)omc_pay_min_u64((omc_u64)sizeof(scratch),
                                                max_output_bytes - total_out);
            next_out = (uint8_t*)scratch;
        }

        out_before = avail_out;
        br = BrotliDecoderDecompressStream(state, &avail_in, &next_in,
                                           &avail_out, &next_out,
                                           (size_t*)0);
        produced = (omc_u64)(out_before - avail_out);

        if (produced != 0U) {
            total_out += produced;
            if (use_scratch) {
                if (res->status == OMC_PAY_OK) {
                    res->status = OMC_PAY_TRUNCATED;
                }
            } else {
                res->written += produced;
            }
        }

        if (br == BROTLI_DECODER_RESULT_SUCCESS) {
            break;
        }
        if (br == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            continue;
        }
        if (br == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
            res->status = OMC_PAY_MALFORMED;
        } else {
            res->status = OMC_PAY_MALFORMED;
        }
        BrotliDecoderDestroyInstance(state);
        res->needed = total_out;
        return 0;
    }

    BrotliDecoderDestroyInstance(state);
    res->needed = total_out;
    return 1;
}
#endif

void
omc_pay_opts_init(omc_pay_opts* opts)
{
    if (opts == (omc_pay_opts*)0) {
        return;
    }

    opts->decompress = 1;
    opts->limits.max_parts = 1U << 14;
    opts->limits.max_output_bytes = 64U * 1024U * 1024U;
}

omc_pay_res
omc_pay_ext(const omc_u8* file_bytes, omc_size file_size,
            const omc_blk_ref* blocks, omc_u32 block_count,
            omc_u32 seed_index, omc_u8* out_payload, omc_size out_cap,
            omc_u32* scratch_indices, omc_u32 scratch_cap,
            const omc_pay_opts* opts)
{
    omc_pay_opts local_opts;
    const omc_pay_opts* use_opts;
    const omc_blk_ref* seed;
    omc_pay_res res;
    omc_u32 i;
    omc_u32 part_count;
    omc_u64 total_size;

    omc_pay_res_init(&res);

    if (opts == (const omc_pay_opts*)0) {
        omc_pay_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        use_opts = opts;
    }

    if (file_bytes == (const omc_u8*)0 || blocks == (const omc_blk_ref*)0
        || seed_index >= block_count) {
        res.status = OMC_PAY_MALFORMED;
        return res;
    }

    seed = &blocks[seed_index];
    if (seed->compression != OMC_BLK_COMP_NONE) {
#if OMC_HAVE_ZLIB
        if (!use_opts->decompress
            || (seed->part_count != 0U && seed->part_count != 1U)) {
            res.status = OMC_PAY_UNSUPPORTED;
            return res;
        }
        if (seed->compression == OMC_BLK_COMP_DEFLATE) {
            if (seed->chunking != OMC_BLK_CHUNK_NONE) {
                res.status = OMC_PAY_UNSUPPORTED;
                return res;
            }
            if (!omc_pay_inflate_zlib_range(file_bytes, file_size,
                                            seed->data_offset, seed->data_size,
                                            out_payload, out_cap,
                                            use_opts->limits.max_output_bytes,
                                            &res)) {
                return res;
            }
            return res;
        }
#endif
#if OMC_HAVE_BROTLI
        if (seed->compression == OMC_BLK_COMP_BROTLI) {
            if (seed->chunking != OMC_BLK_CHUNK_NONE
                && seed->chunking != OMC_BLK_CHUNK_BROB_REALTYPE) {
                res.status = OMC_PAY_UNSUPPORTED;
                return res;
            }
            if (!use_opts->decompress
                || !omc_pay_brotli_range(file_bytes, file_size,
                                         seed->data_offset, seed->data_size,
                                         out_payload, out_cap,
                                         use_opts->limits.max_output_bytes,
                                         &res)) {
                if (res.status == OMC_PAY_OK) {
                    res.status = OMC_PAY_UNSUPPORTED;
                }
                return res;
            }
            return res;
        }
#endif
        (void)use_opts;
        res.status = OMC_PAY_UNSUPPORTED;
        return res;
    }

    if (seed->chunking == OMC_BLK_CHUNK_NONE
        && (seed->part_count == 0U || seed->part_count == 1U)) {
        res.needed = seed->data_size;
        if (res.needed > use_opts->limits.max_output_bytes) {
            res.status = OMC_PAY_LIMIT;
            return res;
        }
        if (!omc_pay_copy_range(file_bytes, file_size, seed->data_offset,
                                seed->data_size, out_payload, out_cap, 0U,
                                &res)) {
            return res;
        }
        return res;
    }

    if (seed->chunking != OMC_BLK_CHUNK_NONE
        && seed->chunking != OMC_BLK_CHUNK_JPEG_APP2_SEQ
        && seed->chunking != OMC_BLK_CHUNK_JPEG_APP11_SEQ) {
        res.status = OMC_PAY_UNSUPPORTED;
        return res;
    }

    part_count = seed->part_count;
    if (part_count == 0U) {
        res.status = OMC_PAY_MALFORMED;
        return res;
    }
    if (part_count > use_opts->limits.max_parts || part_count > scratch_cap
        || scratch_indices == (omc_u32*)0) {
        res.status = OMC_PAY_LIMIT;
        return res;
    }

    {
        omc_u32 found;
        found = 0U;
        for (i = 0U; i < block_count; ++i) {
            if (omc_pay_same_stream(seed, &blocks[i])) {
                if (found >= scratch_cap) {
                    res.status = OMC_PAY_LIMIT;
                    return res;
                }
                scratch_indices[found] = i;
                found += 1U;
            }
        }
        if (found != part_count) {
            res.status = OMC_PAY_MALFORMED;
            return res;
        }
    }

    {
        omc_u32 a;
        omc_u32 b;
        for (a = 0U; a < part_count; ++a) {
            for (b = a + 1U; b < part_count; ++b) {
                const omc_blk_ref* ba;
                const omc_blk_ref* bb;
                omc_u32 tmp;

                ba = &blocks[scratch_indices[a]];
                bb = &blocks[scratch_indices[b]];
                if (ba->part_index == bb->part_index) {
                    res.status = OMC_PAY_MALFORMED;
                    return res;
                }
                if (ba->part_index > bb->part_index) {
                    tmp = scratch_indices[a];
                    scratch_indices[a] = scratch_indices[b];
                    scratch_indices[b] = tmp;
                }
            }
        }
    }

    total_size = 0U;
    for (i = 0U; i < part_count; ++i) {
        const omc_blk_ref* part;

        part = &blocks[scratch_indices[i]];
        if (part->part_index != i) {
            res.status = OMC_PAY_MALFORMED;
            return res;
        }
        if (part->data_size > ((omc_u64)(~(omc_u64)0) - total_size)) {
            res.status = OMC_PAY_LIMIT;
            return res;
        }
        total_size += part->data_size;
    }

    res.needed = total_size;
    if (res.needed > use_opts->limits.max_output_bytes) {
        res.status = OMC_PAY_LIMIT;
        return res;
    }

    total_size = 0U;
    for (i = 0U; i < part_count; ++i) {
        const omc_blk_ref* part;

        part = &blocks[scratch_indices[i]];
        if (!omc_pay_copy_range(file_bytes, file_size, part->data_offset,
                                part->data_size, out_payload, out_cap,
                                total_size, &res)) {
            return res;
        }
        total_size += part->data_size;
    }

    return res;
}

omc_pay_res
omc_pay_meas(const omc_u8* file_bytes, omc_size file_size,
             const omc_blk_ref* blocks, omc_u32 block_count,
             omc_u32 seed_index, omc_u32* scratch_indices,
             omc_u32 scratch_cap, const omc_pay_opts* opts)
{
    omc_pay_res res;

    res = omc_pay_ext(file_bytes, file_size, blocks, block_count, seed_index,
                      (omc_u8*)0, 0U, scratch_indices, scratch_cap, opts);
    if (res.status == OMC_PAY_TRUNCATED) {
        res.status = OMC_PAY_OK;
    }
    res.written = 0U;
    return res;
}
