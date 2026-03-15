#include "omc/omc_irb.h"

#include <string.h>

static int
omc_irb_match(const omc_u8* bytes, omc_size size, omc_u64 off, const char* lit,
              omc_u64 lit_size)
{
    if (off > (omc_u64)size || lit_size > ((omc_u64)size - off)) {
        return 0;
    }
    return memcmp(bytes + (omc_size)off, lit, (omc_size)lit_size) == 0;
}

static int
omc_irb_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
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
omc_irb_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
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

static omc_u64
omc_irb_pad2(omc_u64 n)
{
    return (n + 1U) & ~(omc_u64)1U;
}

static omc_size
omc_irb_trailing_zero_start(const omc_u8* bytes, omc_size size)
{
    omc_size end;

    end = size;
    while (end > 0U && bytes[end - 1U] == 0U) {
        end -= 1U;
    }
    return end;
}

void
omc_irb_opts_init(omc_irb_opts* opts)
{
    if (opts == (omc_irb_opts*)0) {
        return;
    }

    opts->decode_iptc_iim = 1;
    opts->limits.max_resources = 1U << 16;
    opts->limits.max_total_bytes = 64U * 1024U * 1024U;
    opts->limits.max_resource_len = 32U * 1024U * 1024U;
    omc_iptc_opts_init(&opts->iptc);
}

omc_irb_res
omc_irb_dec(const omc_u8* irb_bytes, omc_size irb_size, omc_store* store,
            omc_block_id source_block, const omc_irb_opts* opts)
{
    omc_irb_opts local_opts;
    const omc_irb_opts* use_opts;
    omc_irb_res res;
    omc_size trailing_zero_start;
    omc_u64 total_value_bytes;
    omc_u64 p;
    omc_u32 order;

    res.status = OMC_IRB_OK;
    res.resources_decoded = 0U;
    res.entries_decoded = 0U;
    res.iptc_entries_decoded = 0U;

    if (opts == (const omc_irb_opts*)0) {
        omc_irb_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        use_opts = opts;
    }

    if (irb_bytes == (const omc_u8*)0 || store == (omc_store*)0) {
        res.status = OMC_IRB_MALFORMED;
        return res;
    }
    if (irb_size == 0U || !omc_irb_match(irb_bytes, irb_size, 0U, "8BIM", 4U)) {
        res.status = OMC_IRB_UNSUPPORTED;
        return res;
    }
    if (use_opts->limits.max_total_bytes != 0U
        && (omc_u64)irb_size > use_opts->limits.max_total_bytes) {
        res.status = OMC_IRB_LIMIT;
        return res;
    }

    trailing_zero_start = omc_irb_trailing_zero_start(irb_bytes, irb_size);
    total_value_bytes = 0U;
    p = 0U;
    order = 0U;
    while (p < (omc_u64)irb_size) {
        omc_u16 resource_id;
        omc_u64 name_total;
        omc_u32 data_len32;
        omc_u64 data_len;
        omc_u64 data_off;
        omc_u64 padded_len;
        omc_byte_ref ref;
        omc_entry entry;
        omc_status st;

        if (order >= use_opts->limits.max_resources) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }
        if ((omc_u64)irb_size - p < 4U) {
            break;
        }
        if (!omc_irb_match(irb_bytes, irb_size, p, "8BIM", 4U)) {
            if ((omc_size)p >= trailing_zero_start) {
                break;
            }
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += 4U;

        if (!omc_irb_read_u16be(irb_bytes, irb_size, p, &resource_id)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += 2U;

        if (p >= (omc_u64)irb_size) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        name_total = omc_irb_pad2(1U + irb_bytes[(omc_size)p]);
        if (name_total > ((omc_u64)irb_size - p)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += name_total;

        if (!omc_irb_read_u32be(irb_bytes, irb_size, p, &data_len32)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }
        p += 4U;
        if (data_len32 > use_opts->limits.max_resource_len) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }

        data_len = data_len32;
        data_off = p;
        padded_len = omc_irb_pad2(data_len);
        if (padded_len > ((omc_u64)irb_size - data_off)) {
            res.status = OMC_IRB_MALFORMED;
            return res;
        }

        if (data_len > ((omc_u64)(~(omc_u64)0) - total_value_bytes)) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }
        total_value_bytes += data_len;
        if (use_opts->limits.max_total_bytes != 0U
            && total_value_bytes > use_opts->limits.max_total_bytes) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }
        if (data_len > (omc_u64)(~(omc_u32)0)) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }

        st = omc_arena_append(&store->arena, irb_bytes + (omc_size)data_off,
                              (omc_size)data_len, &ref);
        if (st == OMC_STATUS_NO_MEMORY) {
            res.status = OMC_IRB_NOMEM;
            return res;
        }
        if (st != OMC_STATUS_OK) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }

        memset(&entry, 0, sizeof(entry));
        omc_key_make_photoshop_irb(&entry.key, resource_id);
        omc_val_make_bytes(&entry.value, ref);
        entry.origin.block = source_block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type.family = OMC_WIRE_OTHER;
        entry.origin.wire_type.code = 0U;
        entry.origin.wire_count = (omc_u32)data_len;
        entry.flags = OMC_ENTRY_FLAG_NONE;

        st = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
        if (st == OMC_STATUS_NO_MEMORY) {
            res.status = OMC_IRB_NOMEM;
            return res;
        }
        if (st != OMC_STATUS_OK) {
            res.status = OMC_IRB_LIMIT;
            return res;
        }

        res.resources_decoded += 1U;
        res.entries_decoded += 1U;

        if (use_opts->decode_iptc_iim && resource_id == 0x0404U) {
            omc_iptc_res iptc_res;

            iptc_res = omc_iptc_dec(irb_bytes + (omc_size)data_off,
                                    (omc_size)data_len, store, source_block,
                                    OMC_ENTRY_FLAG_DERIVED, &use_opts->iptc);
            if (iptc_res.status == OMC_IPTC_OK) {
                res.iptc_entries_decoded += iptc_res.entries_decoded;
            } else if (iptc_res.status == OMC_IPTC_NOMEM) {
                res.status = OMC_IRB_NOMEM;
                return res;
            } else if (iptc_res.status == OMC_IPTC_LIMIT) {
                res.status = OMC_IRB_LIMIT;
                return res;
            }
        }

        order += 1U;
        p = data_off + padded_len;
    }

    return res;
}

omc_irb_res
omc_irb_meas(const omc_u8* irb_bytes, omc_size irb_size,
             const omc_irb_opts* opts)
{
    omc_store scratch;
    omc_irb_res res;

    omc_store_init(&scratch);
    res = omc_irb_dec(irb_bytes, irb_size, &scratch, OMC_INVALID_BLOCK_ID,
                      opts);
    omc_store_fini(&scratch);
    return res;
}
