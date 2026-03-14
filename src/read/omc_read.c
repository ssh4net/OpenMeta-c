#include "omc/omc_read.h"

static void
omc_read_init_res(omc_read_res* res)
{
    res->scan.status = OMC_SCAN_OK;
    res->scan.written = 0U;
    res->scan.needed = 0U;
    res->pay.status = OMC_PAY_OK;
    res->pay.written = 0U;
    res->pay.needed = 0U;
    res->exif.status = OMC_EXIF_OK;
    res->exif.ifds_written = 0U;
    res->exif.ifds_needed = 0U;
    res->exif.entries_decoded = 0U;
    res->exif.limit_reason = OMC_EXIF_LIM_NONE;
    res->exif.limit_ifd_offset = 0U;
    res->exif.limit_tag = 0U;
    res->entries_added = 0U;
}

static void
omc_read_opts_copy(omc_read_opts* dst, const omc_read_opts* src)
{
    *dst = *src;
}

static void
omc_read_merge_pay(omc_pay_res* dst, omc_pay_res src)
{
    if (src.needed > dst->needed) {
        dst->needed = src.needed;
    }
    if (src.written > dst->written) {
        dst->written = src.written;
    }
    if (dst->status == OMC_PAY_LIMIT || dst->status == OMC_PAY_NOMEM) {
        return;
    }
    if (src.status == OMC_PAY_LIMIT || src.status == OMC_PAY_NOMEM) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_PAY_MALFORMED) {
        return;
    }
    if (src.status == OMC_PAY_MALFORMED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_PAY_UNSUPPORTED) {
        return;
    }
    if (src.status == OMC_PAY_UNSUPPORTED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_PAY_TRUNCATED) {
        return;
    }
    if (src.status == OMC_PAY_TRUNCATED) {
        dst->status = src.status;
    }
}

static void
omc_read_merge_exif(omc_exif_res* dst, omc_exif_res src)
{
    dst->ifds_written += src.ifds_written;
    dst->ifds_needed += src.ifds_needed;
    dst->entries_decoded += src.entries_decoded;
    if (dst->status == OMC_EXIF_LIMIT || dst->status == OMC_EXIF_NOMEM) {
        return;
    }
    if (src.status == OMC_EXIF_LIMIT || src.status == OMC_EXIF_NOMEM) {
        dst->status = src.status;
        dst->limit_reason = src.limit_reason;
        dst->limit_ifd_offset = src.limit_ifd_offset;
        dst->limit_tag = src.limit_tag;
        return;
    }
    if (dst->status == OMC_EXIF_MALFORMED) {
        return;
    }
    if (src.status == OMC_EXIF_MALFORMED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_EXIF_UNSUPPORTED) {
        return;
    }
    if (src.status == OMC_EXIF_UNSUPPORTED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_EXIF_TRUNCATED) {
        return;
    }
    if (src.status == OMC_EXIF_TRUNCATED) {
        dst->status = src.status;
    }
}

void
omc_read_opts_init(omc_read_opts* opts)
{
    if (opts == (omc_read_opts*)0) {
        return;
    }

    omc_exif_opts_init(&opts->exif);
    omc_pay_opts_init(&opts->pay);
}

omc_read_res
omc_read_simple(const omc_u8* file_bytes, omc_size file_size,
                omc_store* store, omc_blk_ref* out_blocks, omc_u32 block_cap,
                omc_exif_ifd_ref* out_ifds, omc_u32 ifd_cap,
                omc_u8* payload, omc_size payload_cap,
                omc_u32* payload_scratch_indices, omc_u32 payload_scratch_cap,
                const omc_read_opts* opts)
{
    omc_read_res res;
    omc_read_opts local_opts;
    const omc_read_opts* use_opts;
    omc_u32 entries_before;
    omc_u32 i;

    omc_read_init_res(&res);

    if (opts == (const omc_read_opts*)0) {
        omc_read_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        omc_read_opts_copy(&local_opts, opts);
        use_opts = &local_opts;
    }

    if (file_bytes == (const omc_u8*)0 || store == (omc_store*)0) {
        res.exif.status = OMC_EXIF_NOMEM;
        return res;
    }

    entries_before = (omc_u32)store->entry_count;
    res.scan = omc_scan_auto(file_bytes, file_size, out_blocks, block_cap);

    for (i = 0U; i < res.scan.written; ++i) {
        const omc_blk_ref* block;
        omc_exif_res exif_res;

        block = &out_blocks[i];
        if (block->kind != OMC_BLK_EXIF) {
            continue;
        }

        if (block->format == OMC_SCAN_FMT_TIFF && block->data_offset == 0U
            && block->data_size == (omc_u64)file_size) {
            exif_res = omc_exif_dec(file_bytes, file_size, store, out_ifds,
                                    ifd_cap, &use_opts->exif);
            omc_read_merge_exif(&res.exif, exif_res);
            continue;
        }

        {
            omc_pay_res pay_res;

            pay_res = omc_pay_ext(file_bytes, file_size, out_blocks,
                                  res.scan.written, i, payload, payload_cap,
                                  payload_scratch_indices, payload_scratch_cap,
                                  &use_opts->pay);
            omc_read_merge_pay(&res.pay, pay_res);
            if (pay_res.status != OMC_PAY_OK) {
                continue;
            }

            exif_res = omc_exif_dec(payload, (omc_size)pay_res.written, store,
                                    out_ifds, ifd_cap, &use_opts->exif);
            omc_read_merge_exif(&res.exif, exif_res);
        }
    }

    res.entries_added = (omc_u32)(store->entry_count - entries_before);
    return res;
}
