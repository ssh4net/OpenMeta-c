#include "omc/omc_exif.h"

#include <string.h>

#define OMC_EXIF_TASK_CAP 256U

typedef struct omc_exif_cfg {
    int little_endian;
    int big_tiff;
    omc_u64 first_ifd;
    omc_u64 count_size;
    omc_u64 entry_size;
    omc_u64 next_size;
    omc_u64 inline_size;
} omc_exif_cfg;

typedef struct omc_exif_task {
    omc_exif_ifd_kind kind;
    omc_u32 index;
    omc_u64 offset;
} omc_exif_task;

typedef struct omc_exif_ctx {
    const omc_u8* bytes;
    omc_size size;
    omc_store* store;
    omc_block_id source_block;
    int measure_only;
    omc_exif_opts opts;
    omc_exif_cfg cfg;
    omc_exif_res res;
    omc_exif_ifd_ref* out_ifds;
    omc_u32 ifd_cap;
    omc_exif_task tasks[OMC_EXIF_TASK_CAP];
    omc_u32 task_head;
    omc_u32 task_count;
    omc_u32 next_ifd_index;
    omc_u32 next_subifd_index;
} omc_exif_ctx;

typedef struct omc_exif_geotiff_tag_ref {
    int present;
    omc_u16 type;
    omc_u32 count32;
    const omc_u8* raw;
    omc_u64 raw_size;
} omc_exif_geotiff_tag_ref;

static void
omc_exif_res_init(omc_exif_res* res)
{
    res->status = OMC_EXIF_OK;
    res->ifds_written = 0U;
    res->ifds_needed = 0U;
    res->entries_decoded = 0U;
    res->limit_reason = OMC_EXIF_LIM_NONE;
    res->limit_ifd_offset = 0U;
    res->limit_tag = 0U;
}

static void
omc_exif_update_status(omc_exif_res* out, omc_exif_status status)
{
    if (out->status == OMC_EXIF_LIMIT || out->status == OMC_EXIF_NOMEM) {
        return;
    }
    if (status == OMC_EXIF_LIMIT || status == OMC_EXIF_NOMEM) {
        out->status = status;
        return;
    }
    if (out->status == OMC_EXIF_MALFORMED) {
        return;
    }
    if (status == OMC_EXIF_MALFORMED) {
        out->status = status;
        return;
    }
    if (out->status == OMC_EXIF_UNSUPPORTED) {
        return;
    }
    if (status == OMC_EXIF_UNSUPPORTED) {
        out->status = status;
        return;
    }
    if (out->status == OMC_EXIF_TRUNCATED) {
        return;
    }
    if (status == OMC_EXIF_TRUNCATED) {
        out->status = status;
    }
}

static void
omc_exif_mark_limit(omc_exif_ctx* ctx, omc_exif_lim_rsn reason,
                    omc_u64 ifd_offset, omc_u16 tag)
{
    ctx->res.status = OMC_EXIF_LIMIT;
    ctx->res.limit_reason = reason;
    ctx->res.limit_ifd_offset = ifd_offset;
    ctx->res.limit_tag = tag;
}

static int
omc_exif_read_u16(omc_exif_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u16* out_value)
{
    if (out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 2U) {
        return 0;
    }
    if (cfg.little_endian) {
        *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 1U]) << 8)
                               | ((omc_u16)bytes[(omc_size)offset + 0U]));
    } else {
        *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                               | ((omc_u16)bytes[(omc_size)offset + 1U]));
    }
    return 1;
}

static int
omc_exif_read_u32(omc_exif_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }
    if (cfg.little_endian) {
        *out_value = (((omc_u32)bytes[(omc_size)offset + 3U]) << 24)
                     | (((omc_u32)bytes[(omc_size)offset + 2U]) << 16)
                     | (((omc_u32)bytes[(omc_size)offset + 1U]) << 8)
                     | (((omc_u32)bytes[(omc_size)offset + 0U]) << 0);
    } else {
        *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                     | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                     | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                     | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    }
    return 1;
}

static int
omc_exif_read_u64(omc_exif_cfg cfg, const omc_u8* bytes, omc_size size,
                  omc_u64 offset, omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 8U) {
        return 0;
    }
    if (cfg.little_endian) {
        *out_value = (((omc_u64)bytes[(omc_size)offset + 7U]) << 56)
                     | (((omc_u64)bytes[(omc_size)offset + 6U]) << 48)
                     | (((omc_u64)bytes[(omc_size)offset + 5U]) << 40)
                     | (((omc_u64)bytes[(omc_size)offset + 4U]) << 32)
                     | (((omc_u64)bytes[(omc_size)offset + 3U]) << 24)
                     | (((omc_u64)bytes[(omc_size)offset + 2U]) << 16)
                     | (((omc_u64)bytes[(omc_size)offset + 1U]) << 8)
                     | (((omc_u64)bytes[(omc_size)offset + 0U]) << 0);
    } else {
        *out_value = (((omc_u64)bytes[(omc_size)offset + 0U]) << 56)
                     | (((omc_u64)bytes[(omc_size)offset + 1U]) << 48)
                     | (((omc_u64)bytes[(omc_size)offset + 2U]) << 40)
                     | (((omc_u64)bytes[(omc_size)offset + 3U]) << 32)
                     | (((omc_u64)bytes[(omc_size)offset + 4U]) << 24)
                     | (((omc_u64)bytes[(omc_size)offset + 5U]) << 16)
                     | (((omc_u64)bytes[(omc_size)offset + 6U]) << 8)
                     | (((omc_u64)bytes[(omc_size)offset + 7U]) << 0);
    }
    return 1;
}

static int
omc_exif_mul_u64(omc_u64 a, omc_u64 b, omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (a != 0U && b > ((omc_u64)(~(omc_u64)0) / a)) {
        return 0;
    }
    *out_value = a * b;
    return 1;
}

static int
omc_exif_elem_size(omc_u16 type, omc_u32* out_size)
{
    if (out_size == (omc_u32*)0) {
        return 0;
    }

    switch (type) {
    case 1:
    case 2:
    case 6:
    case 7:
    case 129: *out_size = 1U; return 1;
    case 3:
    case 8: *out_size = 2U; return 1;
    case 4:
    case 9:
    case 11:
    case 13: *out_size = 4U; return 1;
    case 5:
    case 10:
    case 12:
    case 16:
    case 17:
    case 18: *out_size = 8U; return 1;
    default: break;
    }

    return 0;
}

static int
omc_exif_resolve_raw(const omc_exif_ctx* ctx, omc_u64 entry_value_off,
                     omc_u16 type, omc_u64 count,
                     const omc_u8** out_ptr, omc_u64* out_size)
{
    omc_u32 elem_size;
    omc_u64 total_size;
    omc_u64 value_offset;

    if (!omc_exif_elem_size(type, &elem_size)) {
        return 0;
    }
    if (!omc_exif_mul_u64((omc_u64)elem_size, count, &total_size)) {
        return 0;
    }

    if (entry_value_off > (omc_u64)ctx->size
        || ctx->cfg.inline_size > ((omc_u64)ctx->size - entry_value_off)) {
        return 0;
    }

    if (total_size <= ctx->cfg.inline_size) {
        *out_ptr = ctx->bytes + (omc_size)entry_value_off;
        *out_size = total_size;
        return 1;
    }

    if (!ctx->cfg.big_tiff) {
        omc_u32 off32;

        if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size,
                               entry_value_off, &off32)) {
            return 0;
        }
        value_offset = off32;
    } else {
        if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size,
                               entry_value_off, &value_offset)) {
            return 0;
        }
    }

    if (value_offset > (omc_u64)ctx->size
        || total_size > ((omc_u64)ctx->size - value_offset)) {
        return 0;
    }

    *out_ptr = ctx->bytes + (omc_size)value_offset;
    *out_size = total_size;
    return 1;
}

static omc_exif_status
omc_exif_store_ref(omc_exif_ctx* ctx, const omc_u8* bytes, omc_u64 size,
                   omc_byte_ref* out_ref)
{
    omc_status status;

    if (size > (omc_u64)(~(omc_u32)0)) {
        return OMC_EXIF_LIMIT;
    }

    status = omc_arena_append(&ctx->store->arena, bytes, (omc_size)size,
                              out_ref);
    if (status == OMC_STATUS_OK) {
        return OMC_EXIF_OK;
    }
    if (status == OMC_STATUS_NO_MEMORY) {
        return OMC_EXIF_NOMEM;
    }
    return OMC_EXIF_LIMIT;
}

static omc_exif_status
omc_exif_store_cstr_len(omc_exif_ctx* ctx, const char* text, omc_size len,
                        omc_byte_ref* out_ref)
{
    return omc_exif_store_ref(ctx, (const omc_u8*)text, len, out_ref);
}

static int
omc_exif_ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static int
omc_exif_ascii_starts_with_nocase(const omc_u8* text, omc_u32 text_size,
                                  const char* prefix)
{
    omc_u32 i;

    if (text == (const omc_u8*)0 || prefix == (const char*)0) {
        return 0;
    }

    for (i = 0U; prefix[i] != '\0'; ++i) {
        if (i >= text_size) {
            return 0;
        }
        if (omc_exif_ascii_tolower((int)text[i])
            != omc_exif_ascii_tolower((int)(unsigned char)prefix[i])) {
            return 0;
        }
    }
    return 1;
}

static const omc_entry*
omc_exif_find_first_entry(const omc_store* store, const char* ifd_name,
                          omc_u16 tag)
{
    omc_size i;
    omc_size ifd_name_size;

    if (store == (const omc_store*)0 || ifd_name == (const char*)0) {
        return (const omc_entry*)0;
    }

    ifd_name_size = strlen(ifd_name);
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag) {
            continue;
        }
        ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_view.size == ifd_name_size
            && memcmp(ifd_view.data, ifd_name, ifd_name_size) == 0) {
            return entry;
        }
    }

    return (const omc_entry*)0;
}

static int
omc_exif_find_first_text(const omc_store* store, const char* ifd_name,
                         omc_u16 tag, const omc_u8** out_text,
                         omc_u32* out_size)
{
    const omc_entry* entry;
    omc_const_bytes view;

    if (out_text == (const omc_u8**)0 || out_size == (omc_u32*)0) {
        return 0;
    }

    entry = omc_exif_find_first_entry(store, ifd_name, tag);
    if (entry == (const omc_entry*)0 || entry->value.kind != OMC_VAL_TEXT) {
        return 0;
    }

    view = omc_arena_view(&store->arena, entry->value.u.ref);
    if (view.size > (omc_size)(~(omc_u32)0)) {
        return 0;
    }
    *out_text = view.data;
    *out_size = (omc_u32)view.size;
    return 1;
}

static int
omc_exif_read_u16le_raw(const omc_u8* bytes, omc_u64 size, omc_u64 offset,
                        omc_u16* out_value)
{
    if (bytes == (const omc_u8*)0 || out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > size || (size - offset) < 2U) {
        return 0;
    }

    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 1U]) << 8)
                           | ((omc_u16)bytes[(omc_size)offset + 0U]));
    return 1;
}

static int
omc_exif_read_u32le_raw(const omc_u8* bytes, omc_u64 size, omc_u64 offset,
                        omc_u32* out_value)
{
    if (bytes == (const omc_u8*)0 || out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > size || (size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 3U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 0U]) << 0);
    return 1;
}

static omc_exif_status
omc_exif_add_derived_entry(omc_exif_ctx* ctx, const omc_key* key,
                           const omc_val* value, omc_u32 order_in_block,
                           omc_u32 wire_count)
{
    omc_entry entry;
    omc_status st;

    if (ctx == (omc_exif_ctx*)0 || key == (const omc_key*)0
        || value == (const omc_val*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        return OMC_EXIF_OK;
    }

    memset(&entry, 0, sizeof(entry));
    entry.key = *key;
    entry.value = *value;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = wire_count;
    entry.flags = OMC_ENTRY_FLAG_DERIVED;

    st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
    if (st == OMC_STATUS_NO_MEMORY) {
        return OMC_EXIF_NOMEM;
    }
    if (st != OMC_STATUS_OK) {
        return OMC_EXIF_LIMIT;
    }
    return OMC_EXIF_OK;
}

static omc_exif_status
omc_exif_emit_derived_exif_u8(omc_exif_ctx* ctx, const char* ifd_name,
                              omc_u16 tag, omc_u32 order_in_block,
                              omc_u8 value8)
{
    omc_key key;
    omc_val value;
    omc_byte_ref ifd_ref;
    omc_exif_status status;

    if (ifd_name == (const char*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        omc_key_init(&key);
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          1U);
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    omc_key_make_exif_tag(&key, ifd_ref, tag);
    omc_val_make_u8(&value, value8);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_derived_exif_u16(omc_exif_ctx* ctx, const char* ifd_name,
                               omc_u16 tag, omc_u32 order_in_block,
                               omc_u16 value16)
{
    omc_key key;
    omc_val value;
    omc_byte_ref ifd_ref;
    omc_exif_status status;

    if (ifd_name == (const char*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        omc_key_init(&key);
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          1U);
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    omc_key_make_exif_tag(&key, ifd_ref, tag);
    omc_val_make_u32(&value, value16);
    value.elem_type = OMC_ELEM_U16;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_derived_exif_text(omc_exif_ctx* ctx, const char* ifd_name,
                                omc_u16 tag, omc_u32 order_in_block,
                                const omc_u8* text, omc_u32 text_size)
{
    omc_key key;
    omc_val value;
    omc_byte_ref ifd_ref;
    omc_byte_ref text_ref;
    omc_exif_status status;

    if (ifd_name == (const char*)0 || text == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        omc_key_init(&key);
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          text_size);
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    status = omc_exif_store_ref(ctx, text, text_size, &text_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_key_make_exif_tag(&key, ifd_ref, tag);
    omc_val_make_text(&value, text_ref, OMC_TEXT_ASCII);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      text_size);
}

static omc_exif_status
omc_exif_emit_geotiff_u16(omc_exif_ctx* ctx, omc_u32 order_in_block,
                          omc_u16 key_id, omc_u16 value16)
{
    omc_key key;
    omc_val value;

    omc_key_make_geotiff_key(&key, key_id);
    omc_val_make_u32(&value, value16);
    value.elem_type = OMC_ELEM_U16;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_geotiff_f64_bits(omc_exif_ctx* ctx, omc_u32 order_in_block,
                               omc_u16 key_id, omc_u64 bits)
{
    omc_key key;
    omc_val value;

    omc_key_make_geotiff_key(&key, key_id);
    omc_val_make_f64_bits(&value, bits);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_geotiff_f64_bits_array(omc_exif_ctx* ctx, omc_u32 order_in_block,
                                     omc_u16 key_id, const omc_u8* raw,
                                     omc_u32 count)
{
    omc_key key;
    omc_val value;
    omc_byte_ref ref;
    omc_exif_status status;

    if (raw == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }

    omc_key_make_geotiff_key(&key, key_id);
    if (ctx->measure_only) {
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          count);
    }

    status = omc_exif_store_ref(ctx, raw, (omc_u64)count * 8U, &ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_val_init(&value);
    value.kind = OMC_VAL_ARRAY;
    value.elem_type = OMC_ELEM_F64_BITS;
    value.count = count;
    value.u.ref = ref;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      count);
}

static omc_exif_status
omc_exif_emit_geotiff_text(omc_exif_ctx* ctx, omc_u32 order_in_block,
                           omc_u16 key_id, const omc_u8* text,
                           omc_u32 text_size)
{
    omc_key key;
    omc_val value;
    omc_byte_ref ref;
    omc_exif_status status;

    omc_key_make_geotiff_key(&key, key_id);
    if (ctx->measure_only) {
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          text_size);
    }

    status = omc_exif_store_ref(ctx, text, text_size, &ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_val_make_text(&value, ref, OMC_TEXT_ASCII);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      text_size);
}

static omc_exif_status
omc_exif_emit_printim_text(omc_exif_ctx* ctx, omc_u32 order_in_block,
                           const char* field_name, omc_size field_size,
                           const omc_u8* text, omc_size text_size)
{
    omc_key key;
    omc_val value;
    omc_byte_ref field_ref;
    omc_byte_ref value_ref;
    omc_exif_status status;

    if (field_name == (const char*)0 || text == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }

    if (ctx->measure_only) {
        omc_key_init(&key);
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          (omc_u32)text_size);
    }

    status = omc_exif_store_cstr_len(ctx, field_name, field_size, &field_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    status = omc_exif_store_ref(ctx, text, text_size, &value_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_key_make_printim_field(&key, field_ref);
    omc_val_make_text(&value, value_ref, OMC_TEXT_ASCII);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      (omc_u32)text_size);
}

static omc_exif_status
omc_exif_emit_printim_u32(omc_exif_ctx* ctx, omc_u32 order_in_block,
                          const char* field_name, omc_size field_size,
                          omc_u32 value32)
{
    omc_key key;
    omc_val value;
    omc_byte_ref field_ref;
    omc_exif_status status;

    if (field_name == (const char*)0) {
        return OMC_EXIF_MALFORMED;
    }

    if (ctx->measure_only) {
        omc_key_init(&key);
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          1U);
    }

    status = omc_exif_store_cstr_len(ctx, field_name, field_size, &field_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_key_make_printim_field(&key, field_ref);
    omc_val_make_u32(&value, value32);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static void
omc_exif_trim_geotiff_ascii(const omc_u8** io_ptr, omc_u32* io_size)
{
    const omc_u8* text;
    omc_u32 size;

    if (io_ptr == (const omc_u8**)0 || io_size == (omc_u32*)0) {
        return;
    }

    text = *io_ptr;
    size = *io_size;
    while (size != 0U) {
        omc_u8 c;

        c = text[size - 1U];
        if (c == 0U || c == (omc_u8)'|') {
            size -= 1U;
            continue;
        }
        break;
    }
    *io_size = size;
}

static void
omc_exif_printim_hex4(char* out_buf, omc_u16 value16)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    out_buf[0] = '0';
    out_buf[1] = 'x';
    out_buf[2] = hex_digits[(value16 >> 12) & 0x0FU];
    out_buf[3] = hex_digits[(value16 >> 8) & 0x0FU];
    out_buf[4] = hex_digits[(value16 >> 4) & 0x0FU];
    out_buf[5] = hex_digits[(value16 >> 0) & 0x0FU];
}

static int
omc_exif_decode_printim(omc_exif_ctx* ctx, const omc_u8* raw, omc_u64 raw_size)
{
    omc_u16 entry_count;
    omc_u64 needed;
    omc_u32 order;
    omc_exif_status status;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || ctx->measure_only) {
        return 1;
    }
    if (raw_size > ctx->opts.limits.max_value_bytes) {
        return 1;
    }
    if (raw_size < 8U || memcmp(raw, "PrintIM\0", 8U) != 0) {
        return 1;
    }
    if (raw_size < 16U) {
        return 1;
    }
    if (!omc_exif_read_u16le_raw(raw, raw_size, 14U, &entry_count)) {
        return 1;
    }
    if (ctx->opts.limits.max_entries_per_ifd != 0U
        && entry_count > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }

    needed = 16U + ((omc_u64)entry_count * 6U);
    if (needed > raw_size) {
        return 1;
    }

    order = 0U;
    status = omc_exif_emit_printim_text(ctx, order, "version", 7U, raw + 8U,
                                        4U);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    for (i = 0U; i < (omc_u32)entry_count; ++i) {
        omc_u64 off;
        omc_u16 tag_id;
        omc_u32 value32;
        char field_name[6];

        off = 16U + ((omc_u64)i * 6U);
        if (!omc_exif_read_u16le_raw(raw, raw_size, off, &tag_id)
            || !omc_exif_read_u32le_raw(raw, raw_size, off + 2U, &value32)) {
            return 1;
        }

        omc_exif_printim_hex4(field_name, tag_id);
        status = omc_exif_emit_printim_u32(ctx, i + 1U, field_name,
                                           sizeof(field_name), value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_geotiff(omc_exif_ctx* ctx,
                        const omc_exif_geotiff_tag_ref* key_directory,
                        const omc_exif_geotiff_tag_ref* double_params,
                        const omc_exif_geotiff_tag_ref* ascii_params)
{
    omc_u16 hdr0;
    omc_u16 hdr1;
    omc_u16 hdr2;
    omc_u16 hdr3;
    omc_u64 needed_u16;
    int have_double;
    int have_ascii;
    omc_u32 order;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0
        || key_directory == (const omc_exif_geotiff_tag_ref*)0
        || double_params == (const omc_exif_geotiff_tag_ref*)0
        || ascii_params == (const omc_exif_geotiff_tag_ref*)0
        || ctx->measure_only) {
        return 1;
    }
    if (!key_directory->present) {
        return 1;
    }
    if (key_directory->type != 3U || key_directory->count32 < 4U) {
        return 1;
    }
    if (key_directory->raw_size != ((omc_u64)key_directory->count32 * 2U)) {
        return 1;
    }
    if (!omc_exif_read_u16(ctx->cfg, key_directory->raw,
                           (omc_size)key_directory->raw_size, 0U, &hdr0)
        || !omc_exif_read_u16(ctx->cfg, key_directory->raw,
                              (omc_size)key_directory->raw_size, 2U, &hdr1)
        || !omc_exif_read_u16(ctx->cfg, key_directory->raw,
                              (omc_size)key_directory->raw_size, 4U, &hdr2)
        || !omc_exif_read_u16(ctx->cfg, key_directory->raw,
                              (omc_size)key_directory->raw_size, 6U, &hdr3)) {
        return 1;
    }
    (void)hdr0;
    (void)hdr1;
    (void)hdr2;
    if (hdr3 == 0U) {
        return 1;
    }
    if (ctx->opts.limits.max_entries_per_ifd != 0U
        && hdr3 > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }

    needed_u16 = 4U + ((omc_u64)hdr3 * 4U);
    if (needed_u16 > (omc_u64)key_directory->count32) {
        return 1;
    }

    have_double = double_params->present && double_params->type == 12U
                  && double_params->raw_size
                         == ((omc_u64)double_params->count32 * 8U);
    have_ascii = ascii_params->present && ascii_params->type == 2U
                 && ascii_params->raw_size == (omc_u64)ascii_params->count32;
    order = 0U;

    for (i = 0U; i < (omc_u32)hdr3; ++i) {
        omc_u64 off;
        omc_u16 key_id;
        omc_u16 location;
        omc_u16 value_count;
        omc_u16 value_off;

        off = 8U + ((omc_u64)i * 8U);
        if (!omc_exif_read_u16(ctx->cfg, key_directory->raw,
                               (omc_size)key_directory->raw_size, off + 0U,
                               &key_id)
            || !omc_exif_read_u16(ctx->cfg, key_directory->raw,
                                  (omc_size)key_directory->raw_size, off + 2U,
                                  &location)
            || !omc_exif_read_u16(ctx->cfg, key_directory->raw,
                                  (omc_size)key_directory->raw_size, off + 4U,
                                  &value_count)
            || !omc_exif_read_u16(ctx->cfg, key_directory->raw,
                                  (omc_size)key_directory->raw_size, off + 6U,
                                  &value_off)) {
            return 1;
        }
        if (value_count == 0U) {
            continue;
        }

        if (location == 0U) {
            omc_exif_status status;

            status = omc_exif_emit_geotiff_u16(ctx, order, key_id, value_off);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            order += 1U;
            continue;
        }

        if (location == 0x87B0U && have_double) {
            omc_u32 idx;
            omc_u32 room;
            omc_u32 take;

            idx = value_off;
            if (idx >= double_params->count32) {
                continue;
            }
            room = double_params->count32 - idx;
            take = (value_count < room) ? value_count : room;
            if (take == 0U) {
                continue;
            }
            if (ctx->opts.limits.max_value_bytes != 0U
                && ((omc_u64)take * 8U) > ctx->opts.limits.max_value_bytes) {
                continue;
            }
            if (take == 1U) {
                omc_u64 bits;
                omc_exif_status status;

                if (!omc_exif_read_u64(ctx->cfg, double_params->raw,
                                       (omc_size)double_params->raw_size,
                                       (omc_u64)idx * 8U, &bits)) {
                    continue;
                }
                status = omc_exif_emit_geotiff_f64_bits(ctx, order, key_id,
                                                        bits);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
                order += 1U;
                continue;
            }
            if (take > 32U) {
                continue;
            }

            {
                omc_exif_status status;
                const omc_u8* src;

                src = double_params->raw + ((omc_size)idx * 8U);
                status = omc_exif_emit_geotiff_f64_bits_array(ctx, order,
                                                              key_id, src,
                                                              take);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
                order += 1U;
            }
            continue;
        }

        if (location == 0x87B1U && have_ascii) {
            omc_u32 idx2;
            omc_u32 room2;
            omc_u32 take2;

            idx2 = value_off;
            if (idx2 >= ascii_params->count32) {
                continue;
            }
            room2 = ascii_params->count32 - idx2;
            take2 = (value_count < room2) ? value_count : room2;
            if (ctx->opts.limits.max_value_bytes != 0U
                && take2 > ctx->opts.limits.max_value_bytes) {
                continue;
            }

            {
                const omc_u8* text;
                omc_u32 text_size;
                omc_exif_status status;

                text = ascii_params->raw + idx2;
                text_size = take2;
                omc_exif_trim_geotiff_ascii(&text, &text_size);
                status = omc_exif_emit_geotiff_text(ctx, order, key_id, text,
                                                    text_size);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
                order += 1U;
            }
        }
    }

    return 1;
}

static omc_size
omc_exif_u32_to_dec(char* out_buf, omc_u32 value)
{
    char tmp[16];
    omc_size n;
    omc_size i;

    n = 0U;
    do {
        tmp[n] = (char)('0' + (value % 10U));
        value /= 10U;
        n += 1U;
    } while (value != 0U);

    for (i = 0U; i < n; ++i) {
        out_buf[i] = tmp[n - i - 1U];
    }
    return n;
}

static omc_exif_status
omc_exif_make_token(omc_exif_ctx* ctx, omc_exif_ifd_kind kind, omc_u32 index,
                    omc_byte_ref* out_ref)
{
    const char* literal;
    const char* prefix;
    char buf[64];
    omc_size prefix_len;
    omc_size digits_len;

    literal = (const char*)0;
    prefix = (const char*)0;
    prefix_len = 0U;
    digits_len = 0U;

    switch (kind) {
    case OMC_EXIF_IFD:
        prefix = ctx->opts.tokens.ifd_prefix;
        break;
    case OMC_EXIF_SUB_IFD:
        prefix = ctx->opts.tokens.subifd_prefix;
        break;
    case OMC_EXIF_EXIF_IFD:
        literal = ctx->opts.tokens.exif_ifd_token;
        break;
    case OMC_EXIF_GPS_IFD:
        literal = ctx->opts.tokens.gps_ifd_token;
        break;
    case OMC_EXIF_INTEROP_IFD:
        literal = ctx->opts.tokens.interop_ifd_token;
        break;
    default:
        literal = "ifd";
        break;
    }

    if (literal != (const char*)0) {
        return omc_exif_store_cstr_len(ctx, literal, strlen(literal), out_ref);
    }

    prefix_len = strlen(prefix);
    if (prefix_len >= sizeof(buf)) {
        return OMC_EXIF_LIMIT;
    }
    memcpy(buf, prefix, prefix_len);
    digits_len = omc_exif_u32_to_dec(buf + prefix_len, index);
    if (prefix_len + digits_len > sizeof(buf)) {
        return OMC_EXIF_LIMIT;
    }
    return omc_exif_store_cstr_len(ctx, buf, prefix_len + digits_len, out_ref);
}

static void
omc_exif_emit_ifd(omc_exif_ctx* ctx, omc_exif_ifd_kind kind, omc_u32 index,
                  omc_u64 offset)
{
    omc_exif_ifd_ref ref;

    ctx->res.ifds_needed += 1U;
    if (ctx->out_ifds == (omc_exif_ifd_ref*)0 || ctx->ifd_cap == 0U) {
        return;
    }

    ref.kind = kind;
    ref.index = index;
    ref.offset = offset;
    ref.block = OMC_INVALID_BLOCK_ID;

    if (ctx->res.ifds_written < ctx->ifd_cap) {
        ctx->out_ifds[ctx->res.ifds_written] = ref;
        ctx->res.ifds_written += 1U;
    } else {
        omc_exif_update_status(&ctx->res, OMC_EXIF_TRUNCATED);
    }
}

static int
omc_exif_push_task(omc_exif_ctx* ctx, omc_exif_ifd_kind kind, omc_u32 index,
                   omc_u64 offset)
{
    omc_u32 slot;

    if (offset == 0U) {
        return 1;
    }
    if (ctx->task_count >= OMC_EXIF_TASK_CAP) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_IFDS, offset, 0U);
        return 0;
    }

    slot = (ctx->task_head + ctx->task_count) % OMC_EXIF_TASK_CAP;
    ctx->tasks[slot].kind = kind;
    ctx->tasks[slot].index = index;
    ctx->tasks[slot].offset = offset;
    ctx->task_count += 1U;
    return 1;
}

static int
omc_exif_pop_task(omc_exif_ctx* ctx, omc_exif_task* out_task)
{
    if (ctx->task_count == 0U) {
        return 0;
    }

    *out_task = ctx->tasks[ctx->task_head];
    ctx->task_head = (ctx->task_head + 1U) % OMC_EXIF_TASK_CAP;
    ctx->task_count -= 1U;
    return 1;
}

static int
omc_exif_parse_header(omc_exif_ctx* ctx)
{
    omc_u16 version;

    if (ctx->size < 8U) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (ctx->bytes[0] == (omc_u8)'I' && ctx->bytes[1] == (omc_u8)'I') {
        ctx->cfg.little_endian = 1;
    } else if (ctx->bytes[0] == (omc_u8)'M'
               && ctx->bytes[1] == (omc_u8)'M') {
        ctx->cfg.little_endian = 0;
    } else {
        ctx->res.status = OMC_EXIF_UNSUPPORTED;
        return 0;
    }

    if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, 2U, &version)) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (version == 42U || version == 0x0055U || version == 0x4F52U) {
        omc_u32 first_ifd32;

        ctx->cfg.big_tiff = 0;
        ctx->cfg.count_size = 2U;
        ctx->cfg.entry_size = 12U;
        ctx->cfg.next_size = 4U;
        ctx->cfg.inline_size = 4U;

        if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size, 4U,
                               &first_ifd32)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        ctx->cfg.first_ifd = first_ifd32;
        return 1;
    }

    if (version == 43U) {
        omc_u16 off_size;
        omc_u16 reserved;

        ctx->cfg.big_tiff = 1;
        ctx->cfg.count_size = 8U;
        ctx->cfg.entry_size = 20U;
        ctx->cfg.next_size = 8U;
        ctx->cfg.inline_size = 8U;

        if (ctx->size < 16U) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, 4U,
                               &off_size)
            || !omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, 6U,
                                  &reserved)
            || !omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size, 8U,
                                  &ctx->cfg.first_ifd)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        if (off_size != 8U || reserved != 0U) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        return 1;
    }

    ctx->res.status = OMC_EXIF_UNSUPPORTED;
    return 0;
}

static omc_exif_status
omc_exif_add_entry(omc_exif_ctx* ctx, const omc_byte_ref* token_ref,
                   omc_u16 tag, omc_u16 type, omc_u64 count,
                   const omc_u8* raw, omc_u64 raw_size, omc_u32 order_in_block,
                   omc_entry_flags flags)
{
    omc_entry entry;
    omc_status st;
    omc_u64 total_size;
    omc_u32 elem_size;
    int treat_as_bytes;
    omc_u64 text_size;
    omc_u64 i64v;
    omc_u16 u16a;
    omc_u32 u32a;
    omc_u32 u32b;
    omc_u64 u64a;

    memset(&entry, 0, sizeof(entry));
    entry.key.kind = OMC_KEY_EXIF_TAG;
    entry.key.u.exif_tag.ifd = *token_ref;
    entry.key.u.exif_tag.tag = tag;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = type;
    if (count > (omc_u64)(~(omc_u32)0)) {
        return OMC_EXIF_LIMIT;
    }
    entry.origin.wire_count = (omc_u32)count;
    entry.flags = flags;

    if (!omc_exif_elem_size(type, &elem_size)
        || !omc_exif_mul_u64((omc_u64)elem_size, count, &total_size)) {
        return OMC_EXIF_LIMIT;
    }

    if ((type == 2U || type == 129U || type == 7U || count > 1U)
        && total_size > ctx->opts.limits.max_value_bytes) {
        entry.flags |= OMC_ENTRY_FLAG_TRUNCATED;
        entry.value.kind = OMC_VAL_EMPTY;
        st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
        if (st == OMC_STATUS_NO_MEMORY) {
            return OMC_EXIF_NOMEM;
        }
        if (st != OMC_STATUS_OK) {
            return OMC_EXIF_LIMIT;
        }
        return OMC_EXIF_OK;
    }

    if (type == 2U || type == 129U) {
        text_size = raw_size;
        if (text_size != 0U && raw[text_size - 1U] == 0U) {
            text_size -= 1U;
        }
        treat_as_bytes = 0;
        {
            omc_u64 j;
            for (j = 0U; j < text_size; ++j) {
                if (raw[j] == 0U) {
                    treat_as_bytes = 1;
                    break;
                }
            }
        }
        if (!treat_as_bytes) {
            omc_byte_ref ref;
            omc_exif_status xs;

            xs = omc_exif_store_ref(ctx, raw, text_size, &ref);
            if (xs != OMC_EXIF_OK) {
                return xs;
            }
            entry.value.kind = OMC_VAL_TEXT;
            entry.value.elem_type = OMC_ELEM_U8;
            entry.value.text_encoding =
                (type == 129U) ? OMC_TEXT_UTF8 : OMC_TEXT_ASCII;
            entry.value.count = (omc_u32)text_size;
            entry.value.u.ref = ref;
            st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
            if (st == OMC_STATUS_NO_MEMORY) {
                return OMC_EXIF_NOMEM;
            }
            if (st != OMC_STATUS_OK) {
                return OMC_EXIF_LIMIT;
            }
            return OMC_EXIF_OK;
        }
    }

    if (type == 7U || count > 1U || type == 2U || type == 129U) {
        omc_byte_ref ref2;
        omc_exif_status xs2;

        xs2 = omc_exif_store_ref(ctx, raw, raw_size, &ref2);
        if (xs2 != OMC_EXIF_OK) {
            return xs2;
        }
        entry.value.kind = (type == 7U || type == 2U || type == 129U)
                               ? OMC_VAL_BYTES
                               : OMC_VAL_ARRAY;
        switch (type) {
        case 1U: entry.value.elem_type = OMC_ELEM_U8; break;
        case 3U: entry.value.elem_type = OMC_ELEM_U16; break;
        case 4U:
        case 13U: entry.value.elem_type = OMC_ELEM_U32; break;
        case 5U: entry.value.elem_type = OMC_ELEM_URATIONAL; break;
        case 6U: entry.value.elem_type = OMC_ELEM_I8; break;
        case 8U: entry.value.elem_type = OMC_ELEM_I16; break;
        case 9U: entry.value.elem_type = OMC_ELEM_I32; break;
        case 10U: entry.value.elem_type = OMC_ELEM_SRATIONAL; break;
        case 11U: entry.value.elem_type = OMC_ELEM_F32_BITS; break;
        case 12U: entry.value.elem_type = OMC_ELEM_F64_BITS; break;
        case 16U:
        case 18U: entry.value.elem_type = OMC_ELEM_U64; break;
        case 17U: entry.value.elem_type = OMC_ELEM_I64; break;
        default: entry.value.elem_type = OMC_ELEM_U8; break;
        }
        entry.value.count =
            (entry.value.kind == OMC_VAL_ARRAY) ? (omc_u32)count
                                                : (omc_u32)raw_size;
        entry.value.u.ref = ref2;
        st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
        if (st == OMC_STATUS_NO_MEMORY) {
            return OMC_EXIF_NOMEM;
        }
        if (st != OMC_STATUS_OK) {
            return OMC_EXIF_LIMIT;
        }
        return OMC_EXIF_OK;
    }

    entry.value.kind = OMC_VAL_SCALAR;
    entry.value.count = 1U;
    switch (type) {
    case 1U:
        entry.value.elem_type = OMC_ELEM_U8;
        entry.value.u.u64 = raw[0];
        break;
    case 3U:
        if (!omc_exif_read_u16(ctx->cfg, raw, (omc_size)raw_size, 0U, &u16a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_U16;
        entry.value.u.u64 = u16a;
        break;
    case 4U:
    case 13U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_U32;
        entry.value.u.u64 = u32a;
        break;
    case 5U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)
            || !omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 4U,
                                  &u32b)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_URATIONAL;
        entry.value.u.ur.numer = u32a;
        entry.value.u.ur.denom = u32b;
        break;
    case 6U:
        entry.value.elem_type = OMC_ELEM_I8;
        i64v = (omc_u64)(omc_s64)(signed char)raw[0];
        entry.value.u.i64 = (omc_s64)i64v;
        break;
    case 8U:
        if (!omc_exif_read_u16(ctx->cfg, raw, (omc_size)raw_size, 0U, &u16a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_I16;
        entry.value.u.i64 = (omc_s16)u16a;
        break;
    case 9U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_I32;
        entry.value.u.i64 = (omc_s32)u32a;
        break;
    case 10U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)
            || !omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 4U,
                                  &u32b)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_SRATIONAL;
        entry.value.u.sr.numer = (omc_s32)u32a;
        entry.value.u.sr.denom = (omc_s32)u32b;
        break;
    case 11U:
        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &u32a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_F32_BITS;
        entry.value.u.f32_bits = u32a;
        break;
    case 12U:
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U, &u64a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_F64_BITS;
        entry.value.u.f64_bits = u64a;
        break;
    case 16U:
    case 18U:
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U, &u64a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_U64;
        entry.value.u.u64 = u64a;
        break;
    case 17U:
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U, &u64a)) {
            return OMC_EXIF_MALFORMED;
        }
        entry.value.elem_type = OMC_ELEM_I64;
        entry.value.u.i64 = (omc_s64)u64a;
        break;
    default:
        return OMC_EXIF_UNSUPPORTED;
    }

    st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
    if (st == OMC_STATUS_NO_MEMORY) {
        return OMC_EXIF_NOMEM;
    }
    if (st != OMC_STATUS_OK) {
        return OMC_EXIF_LIMIT;
    }
    return OMC_EXIF_OK;
}

static int
omc_exif_process_ifd(omc_exif_ctx* ctx, omc_exif_task task);

typedef enum omc_exif_mn_vendor {
    OMC_EXIF_MN_UNKNOWN = 0,
    OMC_EXIF_MN_FUJI = 1,
    OMC_EXIF_MN_NIKON = 2,
    OMC_EXIF_MN_CANON = 3
} omc_exif_mn_vendor;

static omc_exif_cfg
omc_exif_make_classic_cfg(int little_endian)
{
    omc_exif_cfg cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.little_endian = little_endian;
    cfg.big_tiff = 0;
    cfg.first_ifd = 0U;
    cfg.count_size = 2U;
    cfg.entry_size = 12U;
    cfg.next_size = 4U;
    cfg.inline_size = 4U;
    return cfg;
}

static void
omc_exif_set_fuji_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_fuji";
    opts->tokens.subifd_prefix = "mk_fuji_subifd";
    opts->tokens.exif_ifd_token = "mk_fuji_exififd";
    opts->tokens.gps_ifd_token = "mk_fuji_gpsifd";
    opts->tokens.interop_ifd_token = "mk_fuji_interopifd";
}

static void
omc_exif_set_nikon_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_nikon";
    opts->tokens.subifd_prefix = "mk_nikon_subifd";
    opts->tokens.exif_ifd_token = "mk_nikon_exififd";
    opts->tokens.gps_ifd_token = "mk_nikon_gpsifd";
    opts->tokens.interop_ifd_token = "mk_nikon_interopifd";
}

static void
omc_exif_set_canon_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_canon";
    opts->tokens.subifd_prefix = "mk_canon_subifd";
    opts->tokens.exif_ifd_token = "mk_canon_exififd";
    opts->tokens.gps_ifd_token = "mk_canon_gpsifd";
    opts->tokens.interop_ifd_token = "mk_canon_interopifd";
}

static void
omc_exif_init_child_cfg(omc_exif_ctx* child, const omc_exif_ctx* parent,
                        const omc_u8* bytes, omc_size size,
                        const omc_exif_opts* opts, omc_exif_cfg cfg)
{
    memset(child, 0, sizeof(*child));
    child->bytes = bytes;
    child->size = size;
    child->store = parent->store;
    child->source_block = parent->source_block;
    child->measure_only = parent->measure_only;
    child->opts = *opts;
    child->cfg = cfg;
    child->res = parent->res;
}

static void
omc_exif_merge_makernote_child(omc_exif_ctx* parent, const omc_exif_ctx* child)
{
    parent->res.status = child->res.status;
    parent->res.entries_decoded = child->res.entries_decoded;
    parent->res.limit_reason = child->res.limit_reason;
    parent->res.limit_ifd_offset = child->res.limit_ifd_offset;
    parent->res.limit_tag = child->res.limit_tag;
}

static int
omc_exif_decode_tiff_stream(omc_exif_ctx* parent, const omc_u8* bytes,
                            omc_u64 size, const omc_exif_opts* opts)
{
    omc_exif_ctx child;
    omc_exif_task task;

    if (bytes == (const omc_u8*)0 || opts == (const omc_exif_opts*)0) {
        return 1;
    }
    if (size < 8U || size > (omc_u64)(~(omc_size)0)) {
        return 1;
    }

    omc_exif_init_child_cfg(&child, parent, bytes, (omc_size)size, opts,
                            omc_exif_make_classic_cfg(1));
    if (!omc_exif_parse_header(&child)) {
        omc_exif_merge_makernote_child(parent, &child);
        if (child.res.status == OMC_EXIF_MALFORMED
            || child.res.status == OMC_EXIF_LIMIT
            || child.res.status == OMC_EXIF_NOMEM) {
            return 0;
        }
        return 1;
    }
    if (!omc_exif_push_task(&child, OMC_EXIF_IFD, 0U, child.cfg.first_ifd)) {
        omc_exif_merge_makernote_child(parent, &child);
        return 0;
    }
    while (omc_exif_pop_task(&child, &task)) {
        if (!omc_exif_process_ifd(&child, task)) {
            break;
        }
    }

    omc_exif_merge_makernote_child(parent, &child);
    if (child.res.status == OMC_EXIF_LIMIT || child.res.status == OMC_EXIF_NOMEM
        || child.res.status == OMC_EXIF_MALFORMED) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_ifd_blob_cfg(omc_exif_ctx* parent, const omc_u8* bytes,
                             omc_u64 size, omc_u64 ifd_off,
                             const omc_exif_opts* opts, omc_exif_cfg cfg)
{
    omc_exif_ctx child;
    omc_exif_task task;

    if (bytes == (const omc_u8*)0 || opts == (const omc_exif_opts*)0) {
        return 1;
    }
    if (ifd_off >= size || size > (omc_u64)(~(omc_size)0)) {
        return 1;
    }

    omc_exif_init_child_cfg(&child, parent, bytes, (omc_size)size, opts, cfg);
    task.kind = OMC_EXIF_IFD;
    task.index = 0U;
    task.offset = ifd_off;
    if (!omc_exif_process_ifd(&child, task)) {
        omc_exif_merge_makernote_child(parent, &child);
        if (child.res.status == OMC_EXIF_LIMIT
            || child.res.status == OMC_EXIF_NOMEM
            || child.res.status == OMC_EXIF_MALFORMED) {
            return 0;
        }
        return 1;
    }

    while (omc_exif_pop_task(&child, &task)) {
        if (!omc_exif_process_ifd(&child, task)) {
            break;
        }
    }

    omc_exif_merge_makernote_child(parent, &child);
    if (child.res.status == OMC_EXIF_LIMIT || child.res.status == OMC_EXIF_NOMEM
        || child.res.status == OMC_EXIF_MALFORMED) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_ifd_blob(omc_exif_ctx* parent, const omc_u8* bytes,
                         omc_u64 size, omc_u64 ifd_off,
                         const omc_exif_opts* opts)
{
    return omc_exif_decode_ifd_blob_cfg(parent, bytes, size, ifd_off, opts,
                                        omc_exif_make_classic_cfg(1));
}

static int
omc_exif_decode_ifd_blob_loose_cfg(omc_exif_ctx* parent, const omc_u8* bytes,
                                   omc_u64 size, omc_u64 ifd_off,
                                   const omc_exif_opts* opts,
                                   omc_exif_cfg cfg)
{
    omc_exif_ctx child;
    omc_u16 entry_count16;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u32 entry_count32;
    omc_byte_ref token_ref;
    omc_exif_status tok_status;
    omc_u32 i;
    int emitted_any;

    if (bytes == (const omc_u8*)0 || opts == (const omc_exif_opts*)0) {
        return 0;
    }
    if (ifd_off >= size || size > (omc_u64)(~(omc_size)0)) {
        return 0;
    }

    omc_exif_init_child_cfg(&child, parent, bytes, (omc_size)size, opts, cfg);
    if (!omc_exif_read_u16(child.cfg, child.bytes, child.size, ifd_off,
                           &entry_count16)) {
        return 0;
    }
    if (entry_count16 == 0U
        || entry_count16 > child.opts.limits.max_entries_per_ifd) {
        return 0;
    }

    entry_count32 = entry_count16;
    entry_table_off = ifd_off + 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count32, 12U, &table_bytes)) {
        return 0;
    }
    if (entry_table_off > size || table_bytes > (size - entry_table_off)
        || 4U > ((size - entry_table_off) - table_bytes)) {
        return 0;
    }
    if (child.res.entries_decoded > child.opts.limits.max_total_entries
        || entry_count32
               > (child.opts.limits.max_total_entries
                  - child.res.entries_decoded)) {
        omc_exif_mark_limit(&child, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, ifd_off,
                            0U);
        omc_exif_merge_makernote_child(parent, &child);
        return 0;
    }

    emitted_any = 0;
    memset(&token_ref, 0, sizeof(token_ref));
    omc_exif_emit_ifd(&child, OMC_EXIF_IFD, 0U, ifd_off);
    if (!child.measure_only) {
        tok_status = omc_exif_make_token(&child, OMC_EXIF_IFD, 0U, &token_ref);
        if (tok_status != OMC_EXIF_OK) {
            omc_exif_update_status(&child.res, tok_status);
            omc_exif_merge_makernote_child(parent, &child);
            return 0;
        }
    }

    for (i = 0U; i < entry_count32; ++i) {
        omc_u64 entry_off;
        omc_u16 tag;
        omc_u16 type;
        omc_u32 count32;
        omc_u32 elem_size;
        omc_u64 count;
        omc_u64 raw_size;
        omc_u64 value_off;
        const omc_u8* raw;
        omc_exif_status estatus;
        int emit_pointer;

        entry_off = entry_table_off + ((omc_u64)i * 12U);
        if (!omc_exif_read_u16(child.cfg, child.bytes, child.size, entry_off,
                               &tag)
            || !omc_exif_read_u16(child.cfg, child.bytes, child.size,
                                  entry_off + 2U, &type)
            || !omc_exif_read_u32(child.cfg, child.bytes, child.size,
                                  entry_off + 4U, &count32)) {
            continue;
        }

        if (!omc_exif_elem_size(type, &elem_size)
            || !omc_exif_mul_u64((omc_u64)elem_size, (omc_u64)count32,
                                 &raw_size)) {
            continue;
        }

        count = count32;
        if (raw_size <= 4U) {
            value_off = entry_off + 8U;
        } else {
            omc_u32 off32;

            if (!omc_exif_read_u32(child.cfg, child.bytes, child.size,
                                   entry_off + 8U, &off32)) {
                continue;
            }
            value_off = off32;
        }

        if (value_off > size || raw_size > (size - value_off)) {
            continue;
        }
        raw = child.bytes + (omc_size)value_off;

        emit_pointer = 1;
        if ((tag == 0x8769U || tag == 0x8825U || tag == 0xA005U
             || tag == 0x014AU)
            && !child.opts.include_pointer_tags) {
            emit_pointer = 0;
        }

        if (!child.measure_only && emit_pointer) {
            estatus = omc_exif_add_entry(&child, &token_ref, tag, type, count,
                                         raw, raw_size, i,
                                         OMC_ENTRY_FLAG_NONE);
            if (estatus != OMC_EXIF_OK) {
                if (estatus == OMC_EXIF_LIMIT) {
                    omc_exif_mark_limit(&child, OMC_EXIF_LIM_VALUE_COUNT,
                                        ifd_off, tag);
                } else {
                    omc_exif_update_status(&child.res, estatus);
                }
                omc_exif_merge_makernote_child(parent, &child);
                return 0;
            }
        }
        if (emit_pointer) {
            child.res.entries_decoded += 1U;
            emitted_any = 1;
        }
    }

    omc_exif_merge_makernote_child(parent, &child);
    return emitted_any;
}

static int
omc_exif_decode_ifd_blob_loose(omc_exif_ctx* parent, const omc_u8* bytes,
                               omc_u64 size, omc_u64 ifd_off,
                               const omc_exif_opts* opts)
{
    return omc_exif_decode_ifd_blob_loose_cfg(parent, bytes, size, ifd_off,
                                              opts,
                                              omc_exif_make_classic_cfg(1));
}

static int
omc_exif_find_tiff_header(const omc_u8* raw, omc_u64 raw_size,
                          omc_u64 start_off, omc_u64 scan_limit,
                          omc_u64* out_off)
{
    omc_u64 off;
    omc_u64 limit;

    if (raw == (const omc_u8*)0 || out_off == (omc_u64*)0) {
        return 0;
    }
    if (start_off > raw_size) {
        return 0;
    }

    limit = raw_size;
    if (scan_limit != 0U && limit > scan_limit) {
        limit = scan_limit;
    }
    if (limit < 8U) {
        return 0;
    }

    for (off = start_off; off + 8U <= limit; ++off) {
        if (raw[off + 0U] == (omc_u8)'I' && raw[off + 1U] == (omc_u8)'I'
            && raw[off + 2U] == 42U && raw[off + 3U] == 0U) {
            *out_off = off;
            return 1;
        }
        if (raw[off + 0U] == (omc_u8)'M' && raw[off + 1U] == (omc_u8)'M'
            && raw[off + 2U] == 0U && raw[off + 3U] == 42U) {
            *out_off = off;
            return 1;
        }
    }

    return 0;
}

static omc_exif_mn_vendor
omc_exif_detect_makernote_vendor(omc_exif_ctx* ctx, const omc_u8* raw,
                                 omc_u64 raw_size)
{
    const omc_u8* make_text;
    omc_u32 make_size;

    if (raw != (const omc_u8*)0 && raw_size >= 6U
        && memcmp(raw, "Nikon\0", 6U) == 0) {
        return OMC_EXIF_MN_NIKON;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 8U
        && (memcmp(raw, "FUJIFILM", 8U) == 0
            || memcmp(raw, "GENERALE", 8U) == 0)) {
        return OMC_EXIF_MN_FUJI;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 10U
        && raw[0] == (omc_u8)'G' && raw[1] == (omc_u8)'E'
        && raw[2] == 0x0CU && raw[6] == 0x16U) {
        return OMC_EXIF_MN_FUJI;
    }

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return OMC_EXIF_MN_UNKNOWN;
    }
    if (!omc_exif_find_first_text(ctx->store, "ifd0", 0x010FU, &make_text,
                                  &make_size)) {
        return OMC_EXIF_MN_UNKNOWN;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Nikon")) {
        return OMC_EXIF_MN_NIKON;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Canon")) {
        return OMC_EXIF_MN_CANON;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "FUJIFILM")) {
        return OMC_EXIF_MN_FUJI;
    }
    return OMC_EXIF_MN_UNKNOWN;
}

static int
omc_exif_decode_fuji_signature_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                         omc_u64 raw_size)
{
    omc_u32 ifd_off32;
    omc_exif_opts mn_opts;

    if (raw == (const omc_u8*)0 || raw_size < 12U) {
        return 1;
    }
    if (memcmp(raw, "FUJIFILM", 8U) != 0
        && memcmp(raw, "GENERALE", 8U) != 0) {
        return 1;
    }
    if (!omc_exif_read_u32le_raw(raw, raw_size, 8U, &ifd_off32)) {
        return 1;
    }
    if ((omc_u64)ifd_off32 >= raw_size) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_fuji_tokens(&mn_opts);
    return omc_exif_decode_ifd_blob(ctx, raw, raw_size, (omc_u64)ifd_off32,
                                    &mn_opts);
}

static int
omc_exif_decode_fuji_ge2_candidate(omc_exif_ctx* ctx, const omc_u8* bytes,
                                   omc_u64 size, omc_u64 ifd_off)
{
    omc_exif_opts mn_opts;

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_fuji_tokens(&mn_opts);
    return omc_exif_decode_ifd_blob_loose(ctx, bytes, size, ifd_off, &mn_opts);
}

static int
omc_exif_decode_fuji_ge2_makernote(omc_exif_ctx* ctx, omc_u64 maker_note_off,
                                   omc_u64 maker_note_size)
{
    static const char ge2_magic[10] = { 'G', 'E', '\x0C', '\0', '\0',
                                        '\0', '\x16', '\0', '\0', '\0' };
    omc_u8 patched[4096];
    int handled;

    handled = 0;
    if (maker_note_off > (omc_u64)ctx->size
        || maker_note_size > ((omc_u64)ctx->size - maker_note_off)) {
        return 1;
    }
    if (maker_note_size < 12U
        || memcmp(ctx->bytes + (omc_size)maker_note_off, ge2_magic, 10U)
               != 0) {
        return 1;
    }

    if (maker_note_size >= 6U) {
        omc_u64 base0;
        omc_u64 n0;

        base0 = maker_note_off + 6U;
        n0 = maker_note_size - 6U;
        if (n0 >= 8U && n0 <= sizeof(patched) && base0 <= (omc_u64)ctx->size
            && n0 <= ((omc_u64)ctx->size - base0)) {
            memcpy(patched, ctx->bytes + (omc_size)base0, (omc_size)n0);
            patched[6] = 25U;
            patched[7] = 0U;
            if (omc_exif_decode_fuji_ge2_candidate(ctx, patched, n0, 6U)) {
                handled = 1;
            }
        }
    }

    if (maker_note_off >= 204U && maker_note_size <= ((omc_u64)(~(omc_u64)0) - 204U)) {
        omc_u64 base1;
        omc_u64 n1;

        base1 = maker_note_off - 204U;
        n1 = maker_note_size + 204U;
        if (n1 >= 218U && n1 <= sizeof(patched)
            && base1 <= (omc_u64)ctx->size && n1 <= ((omc_u64)ctx->size - base1)) {
            memcpy(patched, ctx->bytes + (omc_size)base1, (omc_size)n1);
            patched[216] = 25U;
            patched[217] = 0U;
            if (omc_exif_decode_fuji_ge2_candidate(ctx, patched, n1, 216U)) {
                handled = 1;
            }
        }
    }

    if (handled) {
        return 1;
    }
    return 1;
}

static int
omc_exif_decode_nikon_vrinfo(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x001FU);
    if (entry == (const omc_entry*)0 || entry->value.kind != OMC_VAL_BYTES) {
        return 1;
    }

    raw = omc_arena_view(&ctx->store->arena, entry->value.u.ref);
    if (raw.size < 7U) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_text(ctx, "mk_nikon_vrinfo_0", 0x0000U,
                                             0U, raw.data, 4U);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, "mk_nikon_vrinfo_0", 0x0004U,
                                           1U, raw.data[4U]);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, "mk_nikon_vrinfo_0", 0x0006U,
                                           2U, raw.data[6U]);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_canon_camera_settings(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x0001U);
    if (entry == (const omc_entry*)0 || entry->value.kind != OMC_VAL_ARRAY
        || entry->value.elem_type != OMC_ELEM_U16) {
        return 1;
    }

    raw = omc_arena_view(&ctx->store->arena, entry->value.u.ref);
    for (i = 0U; i < entry->value.count; ++i) {
        omc_u16 value16;
        omc_exif_status status;
        omc_u64 off;

        off = (omc_u64)i * 2U;
        if ((off + 2U) > raw.size
            || !omc_exif_read_u16le_raw(raw.data, raw.size, off, &value16)) {
            break;
        }
        status = omc_exif_emit_derived_exif_u16(
            ctx, "mk_canon_camerasettings_0", (omc_u16)i, i, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_nikon_makernote(omc_exif_ctx* ctx, omc_u64 maker_note_off,
                                const omc_u8* raw, omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg classic_cfg;
    omc_u64 hdr_rel;

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_nikon_tokens(&mn_opts);

    if (raw != (const omc_u8*)0 && raw_size >= 8U
        && omc_exif_find_tiff_header(raw, raw_size, 0U, 128U, &hdr_rel)) {
        omc_u64 hdr_abs;

        hdr_abs = maker_note_off + hdr_rel;
        if (hdr_abs < (omc_u64)ctx->size) {
            if (!omc_exif_decode_tiff_stream(ctx, ctx->bytes + (omc_size)hdr_abs,
                                             (omc_u64)ctx->size - hdr_abs,
                                             &mn_opts)) {
                return 0;
            }
            return omc_exif_decode_nikon_vrinfo(ctx);
        }
    }

    classic_cfg = omc_exif_make_classic_cfg(ctx->cfg.little_endian);
    if (raw != (const omc_u8*)0 && raw_size >= 8U
        && memcmp(raw, "Nikon\0", 6U) == 0 && raw[6U] == 1U && raw[7U] == 0U) {
        if (!omc_exif_decode_ifd_blob_cfg(ctx, ctx->bytes, ctx->size,
                                          maker_note_off + 8U, &mn_opts,
                                          classic_cfg)) {
            return 0;
        }
        return omc_exif_decode_nikon_vrinfo(ctx);
    }

    if (!omc_exif_decode_ifd_blob_cfg(ctx, ctx->bytes, ctx->size,
                                      maker_note_off, &mn_opts, classic_cfg)) {
        return 0;
    }
    return omc_exif_decode_nikon_vrinfo(ctx);
}

static int
omc_exif_decode_canon_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size)
{
    omc_exif_opts mn_opts;

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_canon_tokens(&mn_opts);

    if (!omc_exif_decode_ifd_blob_loose(ctx, raw, raw_size, 0U, &mn_opts)) {
        return 0;
    }
    return omc_exif_decode_canon_camera_settings(ctx);
}

static int
omc_exif_decode_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                          omc_u64 raw_size)
{
    omc_exif_mn_vendor vendor;
    omc_u64 maker_note_off;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size == 0U) {
        return 1;
    }
    if (raw < ctx->bytes || raw > (ctx->bytes + ctx->size)) {
        return 1;
    }

    maker_note_off = (omc_u64)(raw - ctx->bytes);
    vendor = omc_exif_detect_makernote_vendor(ctx, raw, raw_size);
    if (vendor == OMC_EXIF_MN_FUJI) {
        if (!omc_exif_decode_fuji_ge2_makernote(ctx, maker_note_off,
                                                raw_size)) {
            return 0;
        }
        if (!omc_exif_decode_fuji_signature_makernote(ctx, raw, raw_size)) {
            return 0;
        }
        return 1;
    }
    if (vendor == OMC_EXIF_MN_NIKON) {
        return omc_exif_decode_nikon_makernote(ctx, maker_note_off, raw,
                                               raw_size);
    }
    if (vendor == OMC_EXIF_MN_CANON) {
        return omc_exif_decode_canon_makernote(ctx, raw, raw_size);
    }
    return 1;
}

static int
omc_exif_schedule_pointer_task(omc_exif_ctx* ctx, omc_exif_ifd_kind kind,
                               omc_u32 index, omc_u16 type,
                               const omc_u8* raw, omc_u64 raw_size)
{
    omc_u64 offset;

    if (type == 4U || type == 13U) {
        omc_u32 off32;

        if (!omc_exif_read_u32(ctx->cfg, raw, (omc_size)raw_size, 0U, &off32)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        offset = off32;
    } else if (type == 16U || type == 18U) {
        if (!omc_exif_read_u64(ctx->cfg, raw, (omc_size)raw_size, 0U,
                               &offset)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
    } else {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (!omc_exif_push_task(ctx, kind, index, offset)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_process_ifd(omc_exif_ctx* ctx, omc_exif_task task)
{
    omc_u64 entry_count64;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u64 next_ifd_off;
    omc_u32 entry_count32;
    omc_byte_ref token_ref;
    omc_exif_status tok_status;
    omc_exif_geotiff_tag_ref geotiff_dir;
    omc_exif_geotiff_tag_ref geotiff_double;
    omc_exif_geotiff_tag_ref geotiff_ascii;
    omc_u32 i;

    if (ctx->res.ifds_needed >= ctx->opts.limits.max_ifds) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_IFDS, task.offset, 0U);
        return 0;
    }

    if (task.offset > (omc_u64)ctx->size
        || ctx->cfg.count_size > ((omc_u64)ctx->size - task.offset)) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (!ctx->cfg.big_tiff) {
        omc_u16 count16;

        if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, task.offset,
                               &count16)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        entry_count64 = count16;
    } else {
        if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size, task.offset,
                               &entry_count64)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
    }

    if (entry_count64 > (omc_u64)ctx->opts.limits.max_entries_per_ifd) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_IFD, task.offset, 0U);
        return 0;
    }
    if (entry_count64 > ((omc_u64)(~(omc_u32)0))) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_IFD, task.offset, 0U);
        return 0;
    }
    if (ctx->res.entries_decoded > ctx->opts.limits.max_total_entries
        || entry_count64
               > (omc_u64)(ctx->opts.limits.max_total_entries
                           - ctx->res.entries_decoded)) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, task.offset,
                            0U);
        return 0;
    }

    entry_count32 = (omc_u32)entry_count64;
    entry_table_off = task.offset + ctx->cfg.count_size;
    if (!omc_exif_mul_u64(ctx->cfg.entry_size, entry_count64, &table_bytes)) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_IFD, task.offset, 0U);
        return 0;
    }
    if (entry_table_off > (omc_u64)ctx->size
        || table_bytes > ((omc_u64)ctx->size - entry_table_off)
        || ctx->cfg.next_size > (((omc_u64)ctx->size - entry_table_off)
                                 - table_bytes)) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    omc_exif_emit_ifd(ctx, task.kind, task.index, task.offset);
    memset(&geotiff_dir, 0, sizeof(geotiff_dir));
    memset(&geotiff_double, 0, sizeof(geotiff_double));
    memset(&geotiff_ascii, 0, sizeof(geotiff_ascii));

    memset(&token_ref, 0, sizeof(token_ref));
    if (!ctx->measure_only) {
        tok_status = omc_exif_make_token(ctx, task.kind, task.index, &token_ref);
        if (tok_status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, tok_status);
            return 0;
        }
    }

    for (i = 0U; i < entry_count32; ++i) {
        omc_u64 entry_off;
        omc_u16 tag;
        omc_u16 type;
        omc_u64 count;
        omc_u64 value_off;
        const omc_u8* raw;
        omc_u64 raw_size;
        int emit_pointer;
        omc_exif_status estatus;

        entry_off = entry_table_off + ((omc_u64)i * ctx->cfg.entry_size);
        if (!omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, entry_off,
                               &tag)
            || !omc_exif_read_u16(ctx->cfg, ctx->bytes, ctx->size, entry_off + 2U,
                                  &type)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }

        if (!ctx->cfg.big_tiff) {
            omc_u32 count32;

            if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size,
                                   entry_off + 4U, &count32)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            count = count32;
            value_off = entry_off + 8U;
        } else {
            if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size,
                                   entry_off + 4U, &count)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            value_off = entry_off + 12U;
        }

        if (!omc_exif_resolve_raw(ctx, value_off, type, count, &raw, &raw_size)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }

        if (ctx->opts.decode_geotiff && count <= (omc_u64)(~(omc_u32)0)) {
            if (tag == 0x87AFU) {
                geotiff_dir.present = 1;
                geotiff_dir.type = type;
                geotiff_dir.count32 = (omc_u32)count;
                geotiff_dir.raw = raw;
                geotiff_dir.raw_size = raw_size;
            } else if (tag == 0x87B0U) {
                geotiff_double.present = 1;
                geotiff_double.type = type;
                geotiff_double.count32 = (omc_u32)count;
                geotiff_double.raw = raw;
                geotiff_double.raw_size = raw_size;
            } else if (tag == 0x87B1U) {
                geotiff_ascii.present = 1;
                geotiff_ascii.type = type;
                geotiff_ascii.count32 = (omc_u32)count;
                geotiff_ascii.raw = raw;
                geotiff_ascii.raw_size = raw_size;
            }
        }

        emit_pointer = 1;
        if ((tag == 0x8769U || tag == 0x8825U || tag == 0xA005U
             || tag == 0x014AU)
            && !ctx->opts.include_pointer_tags) {
            emit_pointer = 0;
        }

        if (!ctx->measure_only && emit_pointer) {
            estatus = omc_exif_add_entry(ctx, &token_ref, tag, type, count, raw,
                                         raw_size, i, OMC_ENTRY_FLAG_NONE);
            if (estatus != OMC_EXIF_OK) {
                if (estatus == OMC_EXIF_LIMIT) {
                    omc_exif_mark_limit(ctx, OMC_EXIF_LIM_VALUE_COUNT,
                                        task.offset, tag);
                } else {
                    omc_exif_update_status(&ctx->res, estatus);
                }
                return 0;
            }
        }
        if (emit_pointer) {
            ctx->res.entries_decoded += 1U;
        }

        if (ctx->opts.decode_printim && tag == 0xC4A5U && raw_size != 0U
            && raw_size <= ctx->opts.limits.max_value_bytes) {
            if (!omc_exif_decode_printim(ctx, raw, raw_size)) {
                return 0;
            }
        }
        if (ctx->opts.decode_makernote && tag == 0x927CU && raw_size != 0U
            && raw_size <= ctx->opts.limits.max_value_bytes) {
            if (!omc_exif_decode_makernote(ctx, raw, raw_size)) {
                return 0;
            }
        }

        if (tag == 0x8769U || tag == 0x8825U || tag == 0xA005U) {
            omc_exif_ifd_kind pkind;

            pkind = OMC_EXIF_EXIF_IFD;
            if (tag == 0x8825U) {
                pkind = OMC_EXIF_GPS_IFD;
            } else if (tag == 0xA005U) {
                pkind = OMC_EXIF_INTEROP_IFD;
            }
            if (!omc_exif_schedule_pointer_task(ctx, pkind, 0U, type, raw,
                                                raw_size)) {
                return 0;
            }
        } else if (tag == 0x014AU) {
            omc_u64 sub_index;
            const omc_u8* p;
            omc_u64 offv;

            p = raw;
            for (sub_index = 0U; sub_index < count; ++sub_index) {
                if (type == 4U || type == 13U) {
                    omc_u32 off32;

                    if (!omc_exif_read_u32(ctx->cfg, p, (omc_size)raw_size,
                                           (omc_u64)(sub_index * 4U), &off32)) {
                        ctx->res.status = OMC_EXIF_MALFORMED;
                        return 0;
                    }
                    offv = off32;
                } else if (type == 16U || type == 18U) {
                    if (!omc_exif_read_u64(ctx->cfg, p, (omc_size)raw_size,
                                           (omc_u64)(sub_index * 8U), &offv)) {
                        ctx->res.status = OMC_EXIF_MALFORMED;
                        return 0;
                    }
                } else {
                    ctx->res.status = OMC_EXIF_MALFORMED;
                    return 0;
                }
                if (!omc_exif_push_task(ctx, OMC_EXIF_SUB_IFD,
                                        ctx->next_subifd_index, offv)) {
                    return 0;
                }
                ctx->next_subifd_index += 1U;
            }
        }
    }

    if (ctx->opts.decode_geotiff) {
        if (!omc_exif_decode_geotiff(ctx, &geotiff_dir, &geotiff_double,
                                     &geotiff_ascii)) {
            return 0;
        }
    }

    next_ifd_off = entry_table_off + table_bytes;
    if (task.kind == OMC_EXIF_IFD) {
        omc_u64 next_offset;

        if (!ctx->cfg.big_tiff) {
            omc_u32 next32;

            if (!omc_exif_read_u32(ctx->cfg, ctx->bytes, ctx->size, next_ifd_off,
                                   &next32)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            next_offset = next32;
        } else {
            if (!omc_exif_read_u64(ctx->cfg, ctx->bytes, ctx->size, next_ifd_off,
                                   &next_offset)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
        }

        if (next_offset != 0U) {
            if (!omc_exif_push_task(ctx, OMC_EXIF_IFD,
                                    ctx->next_ifd_index + 1U, next_offset)) {
                return 0;
            }
            ctx->next_ifd_index += 1U;
        }
    }

    return 1;
}

static omc_exif_res
omc_exif_run(const omc_u8* tiff_bytes, omc_size tiff_size, omc_store* store,
             omc_block_id source_block, omc_exif_ifd_ref* out_ifds,
             omc_u32 ifd_cap,
             const omc_exif_opts* opts, int measure_only)
{
    omc_exif_ctx ctx;
    omc_exif_task task;

    memset(&ctx, 0, sizeof(ctx));
    ctx.bytes = tiff_bytes;
    ctx.size = tiff_size;
    ctx.store = store;
    ctx.source_block = source_block;
    ctx.measure_only = measure_only;
    ctx.out_ifds = out_ifds;
    ctx.ifd_cap = ifd_cap;
    omc_exif_res_init(&ctx.res);

    if (opts == (const omc_exif_opts*)0) {
        omc_exif_opts_init(&ctx.opts);
    } else {
        ctx.opts = *opts;
    }

    if (ctx.opts.limits.max_ifds == 0U) {
        ctx.opts.limits.max_ifds = 128U;
    }
    if (ctx.opts.limits.max_entries_per_ifd == 0U) {
        ctx.opts.limits.max_entries_per_ifd = 4096U;
    }
    if (ctx.opts.limits.max_total_entries == 0U) {
        ctx.opts.limits.max_total_entries = 200000U;
    }
    if (ctx.opts.limits.max_value_bytes == 0U) {
        ctx.opts.limits.max_value_bytes = 16U * 1024U * 1024U;
    }

    if (tiff_bytes == (const omc_u8*)0) {
        ctx.res.status = OMC_EXIF_MALFORMED;
        return ctx.res;
    }
    if (!measure_only && store == (omc_store*)0) {
        ctx.res.status = OMC_EXIF_NOMEM;
        return ctx.res;
    }

    if (!omc_exif_parse_header(&ctx)) {
        return ctx.res;
    }
    if (!omc_exif_push_task(&ctx, OMC_EXIF_IFD, 0U, ctx.cfg.first_ifd)) {
        return ctx.res;
    }

    while (omc_exif_pop_task(&ctx, &task)) {
        if (!omc_exif_process_ifd(&ctx, task)) {
            break;
        }
    }

    return ctx.res;
}

void
omc_exif_opts_init(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->include_pointer_tags = 1;
    opts->decode_printim = 1;
    opts->decode_geotiff = 1;
    opts->decode_makernote = 0;
    opts->decode_embedded_containers = 0;
    opts->tokens.ifd_prefix = "ifd";
    opts->tokens.subifd_prefix = "subifd";
    opts->tokens.exif_ifd_token = "exififd";
    opts->tokens.gps_ifd_token = "gpsifd";
    opts->tokens.interop_ifd_token = "interopifd";
    opts->limits.max_ifds = 128U;
    opts->limits.max_entries_per_ifd = 4096U;
    opts->limits.max_total_entries = 200000U;
    opts->limits.max_value_bytes = 16U * 1024U * 1024U;
}

omc_exif_res
omc_exif_dec(const omc_u8* tiff_bytes, omc_size tiff_size,
             omc_store* store, omc_block_id source_block,
             omc_exif_ifd_ref* out_ifds, omc_u32 ifd_cap,
             const omc_exif_opts* opts)
{
    return omc_exif_run(tiff_bytes, tiff_size, store, source_block, out_ifds,
                        ifd_cap, opts,
                        0);
}

omc_exif_res
omc_exif_meas(const omc_u8* tiff_bytes, omc_size tiff_size,
              const omc_exif_opts* opts)
{
    return omc_exif_run(tiff_bytes, tiff_size, (omc_store*)0,
                        OMC_INVALID_BLOCK_ID,
                        (omc_exif_ifd_ref*)0, 0U, opts, 1);
}
