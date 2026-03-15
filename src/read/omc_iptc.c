#include "omc/omc_iptc.h"

#include <string.h>

static int
omc_iptc_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
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
omc_iptc_read_length(const omc_u8* bytes, omc_size size, omc_u64 offset,
                     omc_u64* out_value_len, omc_u64* out_header_len)
{
    omc_u16 len16;

    if (out_value_len == (omc_u64*)0 || out_header_len == (omc_u64*)0) {
        return 0;
    }
    if (!omc_iptc_read_u16be(bytes, size, offset, &len16)) {
        return 0;
    }
    if ((len16 & 0x8000U) == 0U) {
        *out_value_len = len16;
        *out_header_len = 2U;
        return 1;
    }

    {
        omc_u16 nbytes;
        omc_u32 value32;
        omc_u16 i;

        nbytes = (omc_u16)(len16 & 0x7FFFU);
        if (nbytes == 0U || nbytes > 4U) {
            return 0;
        }
        if (offset > (omc_u64)size
            || (omc_u64)(2U + nbytes) > ((omc_u64)size - offset)) {
            return 0;
        }

        value32 = 0U;
        for (i = 0U; i < nbytes; ++i) {
            value32 = (value32 << 8)
                      | (omc_u32)bytes[(omc_size)offset + 2U + i];
        }
        *out_value_len = value32;
        *out_header_len = (omc_u64)(2U + nbytes);
        return 1;
    }
}

void
omc_iptc_opts_init(omc_iptc_opts* opts)
{
    if (opts == (omc_iptc_opts*)0) {
        return;
    }

    opts->limits.max_datasets = 200000U;
    opts->limits.max_dataset_bytes = 8U * 1024U * 1024U;
    opts->limits.max_total_bytes = 64U * 1024U * 1024U;
}

omc_iptc_res
omc_iptc_dec(const omc_u8* iptc_bytes, omc_size iptc_size, omc_store* store,
             omc_block_id source_block, omc_entry_flags flags,
             const omc_iptc_opts* opts)
{
    omc_iptc_opts local_opts;
    const omc_iptc_opts* use_opts;
    omc_iptc_res res;
    omc_u64 total_value_bytes;
    omc_u64 p;
    omc_u32 order;

    res.status = OMC_IPTC_OK;
    res.entries_decoded = 0U;

    if (opts == (const omc_iptc_opts*)0) {
        omc_iptc_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        use_opts = opts;
    }

    if (iptc_bytes == (const omc_u8*)0 || store == (omc_store*)0) {
        res.status = OMC_IPTC_MALFORMED;
        return res;
    }
    if (iptc_size == 0U || iptc_bytes[0] != 0x1CU) {
        res.status = OMC_IPTC_UNSUPPORTED;
        return res;
    }
    if (use_opts->limits.max_total_bytes != 0U
        && (omc_u64)iptc_size > use_opts->limits.max_total_bytes) {
        res.status = OMC_IPTC_LIMIT;
        return res;
    }

    total_value_bytes = 0U;
    p = 0U;
    order = 0U;
    while (p < (omc_u64)iptc_size) {
        omc_u16 record;
        omc_u16 dataset;
        omc_u64 value_len;
        omc_u64 header_len;
        omc_u64 value_off;
        omc_byte_ref ref;
        omc_entry entry;
        omc_status st;

        if (order >= use_opts->limits.max_datasets) {
            res.status = OMC_IPTC_LIMIT;
            return res;
        }
        if (p > (omc_u64)iptc_size || 5U > ((omc_u64)iptc_size - p)) {
            res.status = OMC_IPTC_MALFORMED;
            return res;
        }
        if (iptc_bytes[(omc_size)p] != 0x1CU) {
            res.status = OMC_IPTC_MALFORMED;
            return res;
        }

        record = iptc_bytes[(omc_size)p + 1U];
        dataset = iptc_bytes[(omc_size)p + 2U];
        if (!omc_iptc_read_length(iptc_bytes, iptc_size, p + 3U, &value_len,
                                  &header_len)) {
            res.status = OMC_IPTC_MALFORMED;
            return res;
        }
        if (value_len > use_opts->limits.max_dataset_bytes) {
            res.status = OMC_IPTC_LIMIT;
            return res;
        }

        value_off = p + 3U + header_len;
        if (value_off > (omc_u64)iptc_size
            || value_len > ((omc_u64)iptc_size - value_off)) {
            res.status = OMC_IPTC_MALFORMED;
            return res;
        }

        if (value_len > ((omc_u64)(~(omc_u64)0) - total_value_bytes)) {
            res.status = OMC_IPTC_LIMIT;
            return res;
        }
        total_value_bytes += value_len;
        if (use_opts->limits.max_total_bytes != 0U
            && total_value_bytes > use_opts->limits.max_total_bytes) {
            res.status = OMC_IPTC_LIMIT;
            return res;
        }
        if (value_len > (omc_u64)(~(omc_u32)0)) {
            res.status = OMC_IPTC_LIMIT;
            return res;
        }

        st = omc_arena_append(&store->arena, iptc_bytes + (omc_size)value_off,
                              (omc_size)value_len, &ref);
        if (st == OMC_STATUS_NO_MEMORY) {
            res.status = OMC_IPTC_NOMEM;
            return res;
        }
        if (st != OMC_STATUS_OK) {
            res.status = OMC_IPTC_LIMIT;
            return res;
        }

        memset(&entry, 0, sizeof(entry));
        omc_key_make_iptc_dataset(&entry.key, record, dataset);
        omc_val_make_bytes(&entry.value, ref);
        entry.origin.block = source_block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type.family = OMC_WIRE_OTHER;
        entry.origin.wire_type.code = 0U;
        entry.origin.wire_count = (omc_u32)value_len;
        entry.flags = flags;

        st = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
        if (st == OMC_STATUS_NO_MEMORY) {
            res.status = OMC_IPTC_NOMEM;
            return res;
        }
        if (st != OMC_STATUS_OK) {
            res.status = OMC_IPTC_LIMIT;
            return res;
        }

        res.entries_decoded += 1U;
        order += 1U;
        p = value_off + value_len;
    }

    return res;
}

omc_iptc_res
omc_iptc_meas(const omc_u8* iptc_bytes, omc_size iptc_size,
              const omc_iptc_opts* opts)
{
    omc_store scratch;
    omc_iptc_res res;

    omc_store_init(&scratch);
    res = omc_iptc_dec(iptc_bytes, iptc_size, &scratch, OMC_INVALID_BLOCK_ID,
                       OMC_ENTRY_FLAG_NONE, opts);
    omc_store_fini(&scratch);
    return res;
}
