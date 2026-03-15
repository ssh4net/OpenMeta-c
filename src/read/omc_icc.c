#include "omc/omc_icc.h"

#include <string.h>

static int
omc_icc_match(const omc_u8* bytes, omc_size size, omc_u64 offset,
              const char* lit, omc_u64 lit_size)
{
    if (offset > (omc_u64)size || lit_size > ((omc_u64)size - offset)) {
        return 0;
    }
    return memcmp(bytes + (omc_size)offset, lit, (omc_size)lit_size) == 0;
}

static int
omc_icc_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 offset,
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
omc_icc_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
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

static int
omc_icc_read_i32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                   omc_s32* out_value)
{
    omc_u32 tmp;

    if (out_value == (omc_s32*)0) {
        return 0;
    }
    if (!omc_icc_read_u32be(bytes, size, offset, &tmp)) {
        return 0;
    }
    *out_value = (omc_s32)tmp;
    return 1;
}

static int
omc_icc_read_u64be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                   omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 8U) {
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

static void
omc_icc_merge_status(omc_icc_res* out, omc_icc_status status)
{
    if (status == OMC_ICC_OK || out == (omc_icc_res*)0) {
        return;
    }
    if (out->status == OMC_ICC_NOMEM || out->status == OMC_ICC_LIMIT) {
        return;
    }
    if (status == OMC_ICC_NOMEM || status == OMC_ICC_LIMIT) {
        out->status = status;
        return;
    }
    if (out->status == OMC_ICC_MALFORMED) {
        return;
    }
    if (status == OMC_ICC_MALFORMED) {
        out->status = status;
        return;
    }
    if (out->status == OMC_ICC_OK) {
        out->status = status;
    }
}

static omc_icc_status
omc_icc_store_bytes(omc_store* store, const void* src, omc_size size,
                    omc_byte_ref* out_ref)
{
    omc_status st;

    st = omc_arena_append(&store->arena, src, size, out_ref);
    if (st == OMC_STATUS_OK) {
        return OMC_ICC_OK;
    }
    if (st == OMC_STATUS_NO_MEMORY) {
        return OMC_ICC_NOMEM;
    }
    return OMC_ICC_LIMIT;
}

static omc_icc_status
omc_icc_add_entry(omc_store* store, const omc_key* key, const omc_val* value,
                  omc_block_id block, omc_u32 order, omc_u32 wire_count,
                  omc_entry_flags flags)
{
    omc_entry entry;
    omc_status st;

    memset(&entry, 0, sizeof(entry));
    entry.key = *key;
    entry.value = *value;
    entry.origin.block = block;
    entry.origin.order_in_block = order;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = wire_count;
    entry.flags = flags;

    st = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    if (st == OMC_STATUS_OK) {
        return OMC_ICC_OK;
    }
    if (st == OMC_STATUS_NO_MEMORY) {
        return OMC_ICC_NOMEM;
    }
    return OMC_ICC_LIMIT;
}

static omc_icc_status
omc_icc_emit_header_u32(omc_store* store, omc_block_id block, omc_u32 order,
                        omc_u32 offset, omc_u32 value)
{
    omc_key key;
    omc_val val;

    omc_key_make_icc_header_field(&key, offset);
    omc_val_make_u32(&val, value);
    return omc_icc_add_entry(store, &key, &val, block, order, 1U,
                             OMC_ENTRY_FLAG_NONE);
}

static omc_icc_status
omc_icc_emit_header_u64(omc_store* store, omc_block_id block, omc_u32 order,
                        omc_u32 offset, omc_u64 value)
{
    omc_key key;
    omc_val val;

    omc_key_make_icc_header_field(&key, offset);
    omc_val_make_u64(&val, value);
    return omc_icc_add_entry(store, &key, &val, block, order, 1U,
                             OMC_ENTRY_FLAG_NONE);
}

static omc_icc_status
omc_icc_emit_header_bytes(omc_store* store, omc_block_id block, omc_u32 order,
                          omc_u32 offset, const omc_u8* bytes, omc_size size)
{
    omc_key key;
    omc_val val;
    omc_byte_ref ref;
    omc_icc_status status;

    omc_key_make_icc_header_field(&key, offset);
    status = omc_icc_store_bytes(store, bytes, size, &ref);
    if (status != OMC_ICC_OK) {
        return status;
    }
    omc_val_make_bytes(&val, ref);
    return omc_icc_add_entry(store, &key, &val, block, order, (omc_u32)size,
                             OMC_ENTRY_FLAG_NONE);
}

static omc_icc_status
omc_icc_emit_header_u16_array(omc_store* store, omc_block_id block,
                              omc_u32 order, omc_u32 offset,
                              const omc_u16* values, omc_u32 count)
{
    omc_key key;
    omc_val val;
    omc_byte_ref ref;
    omc_icc_status status;
    omc_size bytes_size;

    if (count > ((omc_u32)(~(omc_size)0) / (omc_u32)sizeof(omc_u16))) {
        return OMC_ICC_LIMIT;
    }

    bytes_size = (omc_size)count * sizeof(omc_u16);
    status = omc_icc_store_bytes(store, values, bytes_size, &ref);
    if (status != OMC_ICC_OK) {
        return status;
    }

    omc_key_make_icc_header_field(&key, offset);
    omc_val_init(&val);
    val.kind = OMC_VAL_ARRAY;
    val.elem_type = OMC_ELEM_U16;
    val.count = count;
    val.u.ref = ref;
    return omc_icc_add_entry(store, &key, &val, block, order, count,
                             OMC_ENTRY_FLAG_NONE);
}

static omc_icc_status
omc_icc_emit_header_srational_array(omc_store* store, omc_block_id block,
                                    omc_u32 order, omc_u32 offset,
                                    const omc_srational* values,
                                    omc_u32 count)
{
    omc_key key;
    omc_val val;
    omc_byte_ref ref;
    omc_icc_status status;
    omc_size bytes_size;

    if (count > ((omc_u32)(~(omc_size)0) / (omc_u32)sizeof(omc_srational))) {
        return OMC_ICC_LIMIT;
    }

    bytes_size = (omc_size)count * sizeof(omc_srational);
    status = omc_icc_store_bytes(store, values, bytes_size, &ref);
    if (status != OMC_ICC_OK) {
        return status;
    }

    omc_key_make_icc_header_field(&key, offset);
    omc_val_init(&val);
    val.kind = OMC_VAL_ARRAY;
    val.elem_type = OMC_ELEM_SRATIONAL;
    val.count = count;
    val.u.ref = ref;
    return omc_icc_add_entry(store, &key, &val, block, order, count,
                             OMC_ENTRY_FLAG_NONE);
}

static omc_icc_status
omc_icc_emit_tag(omc_store* store, omc_block_id block, omc_u32 order,
                 omc_u32 signature, const omc_u8* bytes, omc_size size)
{
    omc_key key;
    omc_val val;
    omc_byte_ref ref;
    omc_icc_status status;

    omc_key_make_icc_tag(&key, signature);
    status = omc_icc_store_bytes(store, bytes, size, &ref);
    if (status != OMC_ICC_OK) {
        return status;
    }
    omc_val_make_bytes(&val, ref);
    return omc_icc_add_entry(store, &key, &val, block, order, (omc_u32)size,
                             OMC_ENTRY_FLAG_NONE);
}

void
omc_icc_opts_init(omc_icc_opts* opts)
{
    if (opts == (omc_icc_opts*)0) {
        return;
    }

    opts->limits.max_tags = 1U << 16;
    opts->limits.max_tag_bytes = 32U * 1024U * 1024U;
    opts->limits.max_total_tag_bytes = 64U * 1024U * 1024U;
}

omc_icc_res
omc_icc_dec(const omc_u8* icc_bytes, omc_size icc_size, omc_store* store,
            omc_block_id source_block, const omc_icc_opts* opts)
{
    omc_icc_opts local_opts;
    const omc_icc_opts* use_opts;
    omc_icc_res res;
    omc_u32 declared_size;
    omc_u32 order;
    omc_icc_status status;
    omc_u16 dt[6];
    int dt_ok;
    omc_s32 ill[3];
    int ill_ok;
    omc_u32 tag_count;
    omc_u64 table_bytes;
    omc_u64 total_tag_bytes;
    omc_u32 i;

    res.status = OMC_ICC_OK;
    res.entries_decoded = 0U;

    if (opts == (const omc_icc_opts*)0) {
        omc_icc_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        use_opts = opts;
    }

    if (icc_bytes == (const omc_u8*)0 || store == (omc_store*)0) {
        res.status = OMC_ICC_MALFORMED;
        return res;
    }
    if (icc_size < 132U) {
        res.status = OMC_ICC_UNSUPPORTED;
        return res;
    }
    if (!omc_icc_match(icc_bytes, icc_size, 36U, "acsp", 4U)) {
        res.status = OMC_ICC_UNSUPPORTED;
        return res;
    }

    if (!omc_icc_read_u32be(icc_bytes, icc_size, 0U, &declared_size)) {
        res.status = OMC_ICC_MALFORMED;
        return res;
    }
    if (declared_size != 0U && declared_size != (omc_u32)icc_size) {
        omc_icc_merge_status(&res, OMC_ICC_MALFORMED);
    }

    order = 0U;
    status = omc_icc_emit_header_u32(store, source_block, order, 0U,
                                     declared_size);
    if (status != OMC_ICC_OK) {
        res.status = status;
        return res;
    }
    order += 1U;
    res.entries_decoded += 1U;

    {
        const omc_u32 offs[] = { 4U, 8U, 12U, 16U, 20U, 36U, 40U, 44U, 48U,
                                 52U, 64U, 80U };
        omc_u32 n;
        for (n = 0U; n < (omc_u32)(sizeof(offs) / sizeof(offs[0])); ++n) {
            omc_u32 value32;

            if (omc_icc_read_u32be(icc_bytes, icc_size, offs[n], &value32)) {
                status = omc_icc_emit_header_u32(store, source_block, order,
                                                 offs[n], value32);
            } else {
                status = omc_icc_emit_header_bytes(
                    store, source_block, order, offs[n],
                    icc_bytes + (omc_size)offs[n], 4U);
                omc_icc_merge_status(&res, OMC_ICC_MALFORMED);
            }
            if (status != OMC_ICC_OK) {
                res.status = status;
                return res;
            }
            order += 1U;
            res.entries_decoded += 1U;
        }
    }

    dt_ok = 1;
    for (i = 0U; i < 6U; ++i) {
        if (!omc_icc_read_u16be(icc_bytes, icc_size, 24U + ((omc_u64)i * 2U),
                                &dt[i])) {
            dt_ok = 0;
        }
    }
    if (dt_ok) {
        status = omc_icc_emit_header_u16_array(store, source_block, order, 24U,
                                               dt, 6U);
    } else {
        status = omc_icc_emit_header_bytes(store, source_block, order, 24U,
                                           icc_bytes + 24U, 12U);
        omc_icc_merge_status(&res, OMC_ICC_MALFORMED);
    }
    if (status != OMC_ICC_OK) {
        res.status = status;
        return res;
    }
    order += 1U;
    res.entries_decoded += 1U;

    if (omc_icc_read_u64be(icc_bytes, icc_size, 56U, &table_bytes)) {
        status = omc_icc_emit_header_u64(store, source_block, order, 56U,
                                         table_bytes);
    } else {
        status = omc_icc_emit_header_bytes(store, source_block, order, 56U,
                                           icc_bytes + 56U, 8U);
        omc_icc_merge_status(&res, OMC_ICC_MALFORMED);
    }
    if (status != OMC_ICC_OK) {
        res.status = status;
        return res;
    }
    order += 1U;
    res.entries_decoded += 1U;

    ill_ok = 1;
    for (i = 0U; i < 3U; ++i) {
        if (!omc_icc_read_i32be(icc_bytes, icc_size, 68U + ((omc_u64)i * 4U),
                                &ill[i])) {
            ill_ok = 0;
        }
    }
    if (ill_ok) {
        omc_srational illum[3];

        illum[0].numer = ill[0];
        illum[0].denom = 65536;
        illum[1].numer = ill[1];
        illum[1].denom = 65536;
        illum[2].numer = ill[2];
        illum[2].denom = 65536;
        status = omc_icc_emit_header_srational_array(store, source_block, order,
                                                     68U, illum, 3U);
    } else {
        status = omc_icc_emit_header_bytes(store, source_block, order, 68U,
                                           icc_bytes + 68U, 12U);
        omc_icc_merge_status(&res, OMC_ICC_MALFORMED);
    }
    if (status != OMC_ICC_OK) {
        res.status = status;
        return res;
    }
    order += 1U;
    res.entries_decoded += 1U;

    status = omc_icc_emit_header_bytes(store, source_block, order, 84U,
                                       icc_bytes + 84U, 16U);
    if (status != OMC_ICC_OK) {
        res.status = status;
        return res;
    }
    order += 1U;
    res.entries_decoded += 1U;

    if (!omc_icc_read_u32be(icc_bytes, icc_size, 128U, &tag_count)) {
        res.status = OMC_ICC_MALFORMED;
        return res;
    }
    if (tag_count > use_opts->limits.max_tags) {
        res.status = OMC_ICC_LIMIT;
        return res;
    }

    table_bytes = 4U + ((omc_u64)tag_count * 12U);
    if (128U + table_bytes > (omc_u64)icc_size) {
        res.status = OMC_ICC_MALFORMED;
        return res;
    }

    total_tag_bytes = 0U;
    for (i = 0U; i < tag_count; ++i) {
        omc_u64 eoff;
        omc_u32 sig;
        omc_u32 off32;
        omc_u32 size32;
        omc_u64 off64;
        omc_u64 size64;

        eoff = 132U + ((omc_u64)i * 12U);
        if (!omc_icc_read_u32be(icc_bytes, icc_size, eoff + 0U, &sig)
            || !omc_icc_read_u32be(icc_bytes, icc_size, eoff + 4U, &off32)
            || !omc_icc_read_u32be(icc_bytes, icc_size, eoff + 8U, &size32)) {
            omc_icc_merge_status(&res, OMC_ICC_MALFORMED);
            continue;
        }

        if (size32 > use_opts->limits.max_tag_bytes) {
            omc_icc_merge_status(&res, OMC_ICC_LIMIT);
            continue;
        }

        size64 = size32;
        if (size64 > ((omc_u64)(~(omc_u64)0) - total_tag_bytes)) {
            omc_icc_merge_status(&res, OMC_ICC_LIMIT);
            continue;
        }
        total_tag_bytes += size64;
        if (use_opts->limits.max_total_tag_bytes != 0U
            && total_tag_bytes > use_opts->limits.max_total_tag_bytes) {
            omc_icc_merge_status(&res, OMC_ICC_LIMIT);
            continue;
        }

        off64 = off32;
        if (off64 > (omc_u64)icc_size || size64 > ((omc_u64)icc_size - off64)) {
            omc_icc_merge_status(&res, OMC_ICC_MALFORMED);
            continue;
        }

        status = omc_icc_emit_tag(store, source_block, order, sig,
                                  icc_bytes + (omc_size)off64,
                                  (omc_size)size64);
        if (status != OMC_ICC_OK) {
            res.status = status;
            return res;
        }
        order += 1U;
        res.entries_decoded += 1U;
    }

    return res;
}

omc_icc_res
omc_icc_meas(const omc_u8* icc_bytes, omc_size icc_size,
             const omc_icc_opts* opts)
{
    omc_store scratch;
    omc_icc_res res;

    omc_store_init(&scratch);
    res = omc_icc_dec(icc_bytes, icc_size, &scratch, OMC_INVALID_BLOCK_ID,
                      opts);
    omc_store_fini(&scratch);
    return res;
}
