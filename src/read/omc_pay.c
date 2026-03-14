#include "omc/omc_pay.h"

#include <string.h>

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
        && seed->chunking != OMC_BLK_CHUNK_JPEG_APP2_SEQ) {
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
