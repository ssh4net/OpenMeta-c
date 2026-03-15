#include "omc/omc_read.h"

#include <string.h>

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
    res->icc.status = OMC_ICC_OK;
    res->icc.entries_decoded = 0U;
    res->iptc.status = OMC_IPTC_OK;
    res->iptc.entries_decoded = 0U;
    res->irb.status = OMC_IRB_OK;
    res->irb.resources_decoded = 0U;
    res->irb.entries_decoded = 0U;
    res->irb.iptc_entries_decoded = 0U;
    res->xmp.status = OMC_XMP_OK;
    res->xmp.entries_decoded = 0U;
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

static void
omc_read_merge_icc(omc_icc_res* dst, omc_icc_res src)
{
    dst->entries_decoded += src.entries_decoded;
    if (dst->status == OMC_ICC_NOMEM || dst->status == OMC_ICC_LIMIT) {
        return;
    }
    if (src.status == OMC_ICC_NOMEM || src.status == OMC_ICC_LIMIT) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_ICC_MALFORMED) {
        return;
    }
    if (src.status == OMC_ICC_MALFORMED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_ICC_UNSUPPORTED) {
        return;
    }
    if (src.status == OMC_ICC_UNSUPPORTED) {
        dst->status = src.status;
    }
}

static void
omc_read_merge_iptc(omc_iptc_res* dst, omc_iptc_res src)
{
    dst->entries_decoded += src.entries_decoded;
    if (dst->status == OMC_IPTC_NOMEM || dst->status == OMC_IPTC_LIMIT) {
        return;
    }
    if (src.status == OMC_IPTC_NOMEM || src.status == OMC_IPTC_LIMIT) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_IPTC_MALFORMED) {
        return;
    }
    if (src.status == OMC_IPTC_MALFORMED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_IPTC_UNSUPPORTED) {
        return;
    }
    if (src.status == OMC_IPTC_UNSUPPORTED) {
        dst->status = src.status;
    }
}

static void
omc_read_merge_irb(omc_irb_res* dst, omc_irb_res src)
{
    dst->resources_decoded += src.resources_decoded;
    dst->entries_decoded += src.entries_decoded;
    dst->iptc_entries_decoded += src.iptc_entries_decoded;
    if (dst->status == OMC_IRB_NOMEM || dst->status == OMC_IRB_LIMIT) {
        return;
    }
    if (src.status == OMC_IRB_NOMEM || src.status == OMC_IRB_LIMIT) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_IRB_MALFORMED) {
        return;
    }
    if (src.status == OMC_IRB_MALFORMED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_IRB_UNSUPPORTED) {
        return;
    }
    if (src.status == OMC_IRB_UNSUPPORTED) {
        dst->status = src.status;
    }
}

static void
omc_read_merge_xmp(omc_xmp_res* dst, omc_xmp_res src)
{
    dst->entries_decoded += src.entries_decoded;
    if (dst->status == OMC_XMP_NOMEM || dst->status == OMC_XMP_LIMIT) {
        return;
    }
    if (src.status == OMC_XMP_NOMEM || src.status == OMC_XMP_LIMIT) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_XMP_MALFORMED) {
        return;
    }
    if (src.status == OMC_XMP_MALFORMED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_XMP_UNSUPPORTED) {
        return;
    }
    if (src.status == OMC_XMP_UNSUPPORTED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_XMP_TRUNCATED) {
        return;
    }
    if (src.status == OMC_XMP_TRUNCATED) {
        dst->status = src.status;
    }
}

static int
omc_read_store_block(omc_store* store, const omc_blk_ref* block,
                     omc_block_id* out_id)
{
    omc_status st;
    omc_block_info info;

    info = *block;
    st = omc_store_add_block(store, &info, out_id);
    return st == OMC_STATUS_OK;
}

static int
omc_read_should_decode_block(const omc_blk_ref* block)
{
    if (block == (const omc_blk_ref*)0) {
        return 0;
    }
    if (block->part_count == 0U || block->part_count == 1U) {
        return 1;
    }
    return block->part_index == 0U;
}

static int
omc_read_value_bytes(const omc_store* store, const omc_entry* entry,
                     omc_const_bytes* out_view)
{
    if (store == (const omc_store*)0 || entry == (const omc_entry*)0
        || out_view == (omc_const_bytes*)0) {
        return 0;
    }

    out_view->data = (const omc_u8*)0;
    out_view->size = 0U;

    if (entry->value.kind == OMC_VAL_BYTES || entry->value.kind == OMC_VAL_TEXT
        || (entry->value.kind == OMC_VAL_ARRAY
            && (entry->value.elem_type == OMC_ELEM_U8
                || entry->value.elem_type == OMC_ELEM_I8))) {
        *out_view = omc_arena_view(&store->arena, entry->value.u.ref);
        return out_view->data != (const omc_u8*)0;
    }
    return 0;
}

static int
omc_read_block_view(const omc_u8* file_bytes, omc_size file_size,
                    const omc_blk_ref* blocks, omc_u32 block_count,
                    omc_u32 block_index, omc_u8* payload, omc_size payload_cap,
                    omc_u32* payload_scratch_indices,
                    omc_u32 payload_scratch_cap, const omc_pay_opts* opts,
                    omc_const_bytes* out_view, omc_pay_res* out_pay)
{
    const omc_blk_ref* block;

    if (out_view == (omc_const_bytes*)0 || out_pay == (omc_pay_res*)0) {
        return 0;
    }

    out_view->data = (const omc_u8*)0;
    out_view->size = 0U;
    out_pay->status = OMC_PAY_OK;
    out_pay->written = 0U;
    out_pay->needed = 0U;

    if (file_bytes == (const omc_u8*)0 || blocks == (const omc_blk_ref*)0
        || block_index >= block_count) {
        out_pay->status = OMC_PAY_MALFORMED;
        return 0;
    }

    block = &blocks[block_index];
    if (block->compression == OMC_BLK_COMP_NONE
        && (block->part_count == 0U || block->part_count == 1U)
        && block->chunking != OMC_BLK_CHUNK_JPEG_APP2_SEQ) {
        if (block->data_offset > (omc_u64)file_size
            || block->data_size > ((omc_u64)file_size - block->data_offset)) {
            out_pay->status = OMC_PAY_MALFORMED;
            return 0;
        }
        out_view->data = file_bytes + (omc_size)block->data_offset;
        out_view->size = (omc_size)block->data_size;
        return 1;
    }

    *out_pay = omc_pay_ext(file_bytes, file_size, blocks, block_count,
                           block_index, payload, payload_cap,
                           payload_scratch_indices, payload_scratch_cap, opts);
    if (out_pay->status != OMC_PAY_OK) {
        return 0;
    }

    out_view->data = payload;
    out_view->size = (omc_size)out_pay->written;
    return 1;
}

static int
omc_read_find_literal(const omc_u8* bytes, omc_size size, const char* lit)
{
    omc_size lit_len;
    omc_size i;

    if (bytes == (const omc_u8*)0 || lit == (const char*)0) {
        return 0;
    }

    lit_len = strlen(lit);
    if (lit_len == 0U || lit_len > size) {
        return 0;
    }

    for (i = 0U; i + lit_len <= size; ++i) {
        if (memcmp(bytes + i, lit, lit_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
omc_read_looks_like_xmp(const omc_u8* bytes, omc_size size)
{
    if (omc_read_find_literal(bytes, size, "<x:xmpmeta")
        || omc_read_find_literal(bytes, size, "<rdf:RDF")
        || omc_read_find_literal(bytes, size, "<rdf:Description")) {
        return 1;
    }
    return 0;
}

static void
omc_read_decode_tiff_embedded(const omc_read_opts* opts, omc_store* store,
                              omc_block_id block_id, omc_size entry_start,
                              omc_read_res* res)
{
    omc_size i;

    for (i = entry_start; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes value_bytes;

        entry = &store->entries[i];
        if (entry->origin.block != block_id
            || entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        if (entry->key.u.exif_tag.tag != 700U
            && entry->key.u.exif_tag.tag != 34675U
            && entry->key.u.exif_tag.tag != 33723U
            && entry->key.u.exif_tag.tag != 34377U) {
            continue;
        }
        if (!omc_read_value_bytes(store, entry, &value_bytes)) {
            continue;
        }

        if (entry->key.u.exif_tag.tag == 700U) {
            omc_xmp_res xmp_res;

            xmp_res = omc_xmp_dec(value_bytes.data, value_bytes.size, store,
                                  block_id, OMC_ENTRY_FLAG_NONE, &opts->xmp);
            omc_read_merge_xmp(&res->xmp, xmp_res);
        } else if (entry->key.u.exif_tag.tag == 34675U) {
            omc_icc_res icc_res;

            icc_res = omc_icc_dec(value_bytes.data, value_bytes.size, store,
                                  block_id, &opts->icc);
            omc_read_merge_icc(&res->icc, icc_res);
        } else if (entry->key.u.exif_tag.tag == 33723U) {
            omc_iptc_res iptc_res;

            iptc_res = omc_iptc_dec(value_bytes.data, value_bytes.size, store,
                                    block_id, OMC_ENTRY_FLAG_NONE,
                                    &opts->iptc);
            omc_read_merge_iptc(&res->iptc, iptc_res);
        } else {
            omc_irb_res irb_res;

            irb_res = omc_irb_dec(value_bytes.data, value_bytes.size, store,
                                  block_id, &opts->irb);
            omc_read_merge_irb(&res->irb, irb_res);
            res->iptc.entries_decoded += irb_res.iptc_entries_decoded;
        }
    }
}

void
omc_read_opts_init(omc_read_opts* opts)
{
    if (opts == (omc_read_opts*)0) {
        return;
    }

    omc_exif_opts_init(&opts->exif);
    omc_icc_opts_init(&opts->icc);
    omc_iptc_opts_init(&opts->iptc);
    omc_irb_opts_init(&opts->irb);
    omc_pay_opts_init(&opts->pay);
    omc_xmp_opts_init(&opts->xmp);
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
    omc_size entries_before;
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
        res.scan.status = OMC_SCAN_MALFORMED;
        res.exif.status = OMC_EXIF_MALFORMED;
        res.icc.status = OMC_ICC_MALFORMED;
        res.iptc.status = OMC_IPTC_MALFORMED;
        res.irb.status = OMC_IRB_MALFORMED;
        res.xmp.status = OMC_XMP_MALFORMED;
        return res;
    }

    entries_before = store->entry_count;
    res.scan = omc_scan_auto(file_bytes, file_size, out_blocks, block_cap);

    for (i = 0U; i < res.scan.written; ++i) {
        const omc_blk_ref* block;
        omc_block_id block_id;

        block = &out_blocks[i];
        if (!omc_read_store_block(store, block, &block_id)) {
            res.exif.status = OMC_EXIF_NOMEM;
            res.icc.status = OMC_ICC_NOMEM;
            res.iptc.status = OMC_IPTC_NOMEM;
            res.irb.status = OMC_IRB_NOMEM;
            res.xmp.status = OMC_XMP_NOMEM;
            break;
        }

        if (!omc_read_should_decode_block(block)) {
            continue;
        }

        if (block->kind == OMC_BLK_EXIF) {
            omc_exif_res exif_res;
            omc_const_bytes block_view;
            omc_pay_res pay_res;
            omc_size entry_start;

            entry_start = store->entry_count;
            if (block->format == OMC_SCAN_FMT_TIFF && block->data_offset == 0U
                && block->data_size == (omc_u64)file_size) {
                exif_res = omc_exif_dec(file_bytes, file_size, store, block_id,
                                        out_ifds, ifd_cap, &use_opts->exif);
                omc_read_merge_exif(&res.exif, exif_res);
                if (exif_res.status == OMC_EXIF_OK
                    || exif_res.status == OMC_EXIF_TRUNCATED) {
                    omc_read_decode_tiff_embedded(use_opts, store, block_id,
                                                  entry_start, &res);
                }
            } else {
                if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                         res.scan.written, i, payload,
                                         payload_cap, payload_scratch_indices,
                                         payload_scratch_cap, &use_opts->pay,
                                         &block_view, &pay_res)) {
                    omc_read_merge_pay(&res.pay, pay_res);
                    continue;
                }

                omc_read_merge_pay(&res.pay, pay_res);
                exif_res = omc_exif_dec(block_view.data, block_view.size,
                                        store, block_id, out_ifds, ifd_cap,
                                        &use_opts->exif);
                omc_read_merge_exif(&res.exif, exif_res);
            }
        } else if (block->kind == OMC_BLK_XMP) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;
            omc_xmp_res xmp_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            xmp_res = omc_xmp_dec(block_view.data, block_view.size, store,
                                  block_id, OMC_ENTRY_FLAG_NONE,
                                  &use_opts->xmp);
            omc_read_merge_xmp(&res.xmp, xmp_res);
        } else if (block->kind == OMC_BLK_ICC) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;
            omc_icc_res icc_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            icc_res = omc_icc_dec(block_view.data, block_view.size, store,
                                  block_id, &use_opts->icc);
            omc_read_merge_icc(&res.icc, icc_res);
        } else if (block->kind == OMC_BLK_PS_IRB) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;
            omc_irb_res irb_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            irb_res = omc_irb_dec(block_view.data, block_view.size, store,
                                  block_id, &use_opts->irb);
            omc_read_merge_irb(&res.irb, irb_res);
            res.iptc.entries_decoded += irb_res.iptc_entries_decoded;
        } else if (block->kind == OMC_BLK_IPTC_IIM) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;
            omc_iptc_res iptc_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            iptc_res = omc_iptc_dec(block_view.data, block_view.size, store,
                                    block_id, OMC_ENTRY_FLAG_NONE,
                                    &use_opts->iptc);
            omc_read_merge_iptc(&res.iptc, iptc_res);
        }
    }

    if (res.scan.written == 0U && omc_read_looks_like_xmp(file_bytes, file_size)) {
        omc_block_info xmp_block;
        omc_block_id xmp_block_id;
        omc_xmp_res xmp_res2;

        memset(&xmp_block, 0, sizeof(xmp_block));
        xmp_block.kind = OMC_BLK_XMP;
        xmp_block.data_size = (omc_u64)file_size;
        xmp_block.outer_size = (omc_u64)file_size;
        if (omc_store_add_block(store, &xmp_block, &xmp_block_id)
            == OMC_STATUS_OK) {
            xmp_res2 = omc_xmp_dec(file_bytes, file_size, store, xmp_block_id,
                                   OMC_ENTRY_FLAG_NONE, &use_opts->xmp);
            omc_read_merge_xmp(&res.xmp, xmp_res2);
        } else {
            res.xmp.status = OMC_XMP_NOMEM;
        }
    }

    res.entries_added = (omc_u32)(store->entry_count - entries_before);
    return res;
}
