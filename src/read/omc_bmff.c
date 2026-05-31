#include "omc/omc_bmff.h"

#include <string.h>

#define OMC_BMFF_FOURCC(a, b, c, d)                                      \
    ((((omc_u32)((omc_u8)(a))) << 24) | (((omc_u32)((omc_u8)(b))) << 16) \
     | (((omc_u32)((omc_u8)(c))) << 8) | (((omc_u32)((omc_u8)(d))) << 0))

typedef struct omc_bmff_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
} omc_bmff_box;

typedef struct omc_bmff_item_info {
    omc_u32 item_id;
    omc_u16 protection_index;
    omc_u32 item_type;
    int have_type;
    char name[96];
    omc_u16 name_len;
    char content_type[96];
    omc_u16 content_type_len;
    char content_encoding[96];
    omc_u16 content_encoding_len;
    char uri_type[96];
    omc_u16 uri_type_len;
} omc_bmff_item_info;

typedef struct omc_bmff_ispe_prop {
    omc_u32 index;
    omc_u32 width;
    omc_u32 height;
} omc_bmff_ispe_prop;

typedef struct omc_bmff_u8_prop {
    omc_u32 index;
    omc_u8 value;
} omc_bmff_u8_prop;

typedef enum omc_bmff_aux_semantic {
    OMC_BMFF_AUX_UNKNOWN   = 0,
    OMC_BMFF_AUX_ALPHA     = 1,
    OMC_BMFF_AUX_DEPTH     = 2,
    OMC_BMFF_AUX_DISPARITY = 3,
    OMC_BMFF_AUX_MATTE     = 4
} omc_bmff_aux_semantic;

typedef struct omc_bmff_auxc_prop {
    omc_u32 index;
    omc_bmff_aux_semantic semantic;
    char aux_type[96];
    omc_u16 aux_type_len;
    omc_u8 aux_subtype[48];
    omc_u16 aux_subtype_len;
} omc_bmff_auxc_prop;

typedef struct omc_bmff_aux_item_info {
    omc_u32 item_id;
    omc_bmff_aux_semantic semantic;
    char aux_type[96];
    omc_u16 aux_type_len;
    omc_u8 aux_subtype[48];
    omc_u16 aux_subtype_len;
} omc_bmff_aux_item_info;

typedef struct omc_bmff_iref_edge {
    omc_u32 ref_type;
    omc_u32 from_item_id;
    omc_u32 to_item_id;
} omc_bmff_iref_edge;

typedef struct omc_bmff_item_group {
    omc_u32 group_type;
    omc_u32 group_id;
    omc_u32 entity_count;
    omc_u32 entity_id_count;
    int entity_truncated;
    int contains_primary;
    omc_u32 entity_ids[64];
} omc_bmff_item_group;

typedef struct omc_bmff_ipma_assoc {
    omc_u32 item_id;
    omc_u32 property_index;
    omc_u32 property_type;
    omc_u8 essential;
    int have_property_type;
} omc_bmff_ipma_assoc;

typedef struct omc_bmff_prop_type {
    omc_u32 index;
    omc_u32 type;
} omc_bmff_prop_type;

typedef struct omc_bmff_primary_props {
    int have_item_id;
    omc_u32 item_id;
    int have_width_height;
    omc_u32 width;
    omc_u32 height;
    int have_rotation;
    omc_u16 rotation_degrees;
    int have_mirror;
    omc_u8 mirror;
    omc_bmff_iref_edge edges[128];
    omc_u32 edge_count;
    omc_u32 edge_total;
    int edge_truncated;
    omc_bmff_item_group item_groups[64];
    omc_u32 item_group_count;
    omc_u32 item_group_total;
    int item_group_truncated;
    omc_bmff_ipma_assoc ipma_associations[512];
    omc_u32 ipma_association_count;
    omc_u32 ipma_association_total;
    int ipma_truncated;
    omc_u32 auxl_edge_count;
    omc_u32 dimg_edge_count;
    omc_u32 thmb_edge_count;
    omc_u32 cdsc_edge_count;
    omc_u32 primary_auxl_item_ids[32];
    omc_u32 primary_auxl_count;
    omc_bmff_aux_semantic primary_auxl_semantics[32];
    omc_u32 primary_alpha_item_ids[32];
    omc_u32 primary_alpha_count;
    omc_u32 primary_depth_item_ids[32];
    omc_u32 primary_depth_count;
    omc_u32 primary_disparity_item_ids[32];
    omc_u32 primary_disparity_count;
    omc_u32 primary_matte_item_ids[32];
    omc_u32 primary_matte_count;
    omc_u32 primary_dimg_item_ids[32];
    omc_u32 primary_dimg_count;
    omc_u32 primary_thmb_item_ids[32];
    omc_u32 primary_thmb_count;
    omc_u32 primary_cdsc_item_ids[32];
    omc_u32 primary_cdsc_count;
    omc_bmff_aux_item_info aux_items[64];
    omc_u32 aux_item_count;
} omc_bmff_primary_props;

typedef struct omc_bmff_brand_info {
    omc_u32 major_brand;
    omc_u32 minor_version;
    omc_u32 compat_brands[32];
    omc_u32 compat_count;
    int is_heif;
    int is_avif;
    int is_cr3;
} omc_bmff_brand_info;

typedef struct omc_bmff_ctx {
    const omc_u8* bytes;
    omc_size size;
    omc_store* store;
    omc_bmff_opts opts;
    omc_bmff_res res;
    omc_block_id block_id;
    omc_u32 order_in_block;
    int have_block;
    int meta_done;
} omc_bmff_ctx;

typedef enum omc_bmff_item_semantic {
    OMC_BMFF_ITEM_UNKNOWN             = 0,
    OMC_BMFF_ITEM_IMAGE               = 1,
    OMC_BMFF_ITEM_EXIF                = 2,
    OMC_BMFF_ITEM_XMP                 = 3,
    OMC_BMFF_ITEM_JUMBF               = 4,
    OMC_BMFF_ITEM_C2PA                = 5,
    OMC_BMFF_ITEM_ICC_PROFILE         = 6,
    OMC_BMFF_ITEM_AUXILIARY           = 7,
    OMC_BMFF_ITEM_DERIVED             = 8,
    OMC_BMFF_ITEM_THUMBNAIL           = 9,
    OMC_BMFF_ITEM_CONTENT_DESCRIPTION = 10,
    OMC_BMFF_ITEM_URI                 = 11,
    OMC_BMFF_ITEM_JSON                = 12
} omc_bmff_item_semantic;

typedef struct omc_bmff_item_semantic_counts {
    omc_u32 known;
    omc_u32 metadata;
    omc_u32 image;
    omc_u32 exif;
    omc_u32 xmp;
    omc_u32 jumbf;
    omc_u32 c2pa;
    omc_u32 icc_profile;
    omc_u32 auxiliary;
    omc_u32 derived;
    omc_u32 thumbnail;
    omc_u32 content_description;
    omc_u32 uri;
    omc_u32 json;
} omc_bmff_item_semantic_counts;

static void
omc_bmff_res_init(omc_bmff_res* res)
{
    if (res == (omc_bmff_res*)0) {
        return;
    }

    res->status          = OMC_BMFF_OK;
    res->boxes_scanned   = 0U;
    res->item_infos      = 0U;
    res->entries_decoded = 0U;
}

void
omc_bmff_opts_init(omc_bmff_opts* opts)
{
    if (opts == (omc_bmff_opts*)0) {
        return;
    }

    opts->limits.max_boxes      = 1U << 14;
    opts->limits.max_depth      = 16U;
    opts->limits.max_item_infos = 64U;
    opts->limits.max_entries    = 512U;
}

static int
omc_bmff_read_u16be(const omc_u8* bytes, omc_size size, omc_u64 off,
                    omc_u16* out)
{
    omc_u16 v;

    if (bytes == (const omc_u8*)0 || out == (omc_u16*)0) {
        return 0;
    }
    if (off + 2U > (omc_u64)size) {
        return 0;
    }

    v    = (omc_u16)(((omc_u16)bytes[(omc_size)off] << 8)
                  | ((omc_u16)bytes[(omc_size)off + 1U] << 0));
    *out = v;
    return 1;
}

static int
omc_bmff_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 off,
                    omc_u32* out)
{
    omc_u32 v;

    if (bytes == (const omc_u8*)0 || out == (omc_u32*)0) {
        return 0;
    }
    if (off + 4U > (omc_u64)size) {
        return 0;
    }

    v = 0U;
    v |= (omc_u32)bytes[(omc_size)off + 0U] << 24;
    v |= (omc_u32)bytes[(omc_size)off + 1U] << 16;
    v |= (omc_u32)bytes[(omc_size)off + 2U] << 8;
    v |= (omc_u32)bytes[(omc_size)off + 3U] << 0;
    *out = v;
    return 1;
}

static int
omc_bmff_read_u64be(const omc_u8* bytes, omc_size size, omc_u64 off,
                    omc_u64* out)
{
    omc_u64 v;

    if (bytes == (const omc_u8*)0 || out == (omc_u64*)0) {
        return 0;
    }
    if (off + 8U > (omc_u64)size) {
        return 0;
    }

    v = 0U;
    v |= (omc_u64)bytes[(omc_size)off + 0U] << 56;
    v |= (omc_u64)bytes[(omc_size)off + 1U] << 48;
    v |= (omc_u64)bytes[(omc_size)off + 2U] << 40;
    v |= (omc_u64)bytes[(omc_size)off + 3U] << 32;
    v |= (omc_u64)bytes[(omc_size)off + 4U] << 24;
    v |= (omc_u64)bytes[(omc_size)off + 5U] << 16;
    v |= (omc_u64)bytes[(omc_size)off + 6U] << 8;
    v |= (omc_u64)bytes[(omc_size)off + 7U] << 0;
    *out = v;
    return 1;
}

static int
omc_bmff_note_box(omc_bmff_ctx* ctx)
{
    if (ctx == (omc_bmff_ctx*)0) {
        return 0;
    }
    if (ctx->res.boxes_scanned >= ctx->opts.limits.max_boxes) {
        ctx->res.status = OMC_BMFF_LIMIT;
        return 0;
    }
    ctx->res.boxes_scanned += 1U;
    return 1;
}

static int
omc_bmff_parse_box(const omc_u8* bytes, omc_size size, omc_u64 off, omc_u64 end,
                   omc_bmff_box* out_box)
{
    omc_u32 size32;
    omc_u32 type;
    omc_u64 box_size;
    omc_u64 header_size;

    if (out_box == (omc_bmff_box*)0) {
        return 0;
    }
    if (off > end || end > (omc_u64)size) {
        return 0;
    }
    if (off + 8U > end || off + 8U > (omc_u64)size) {
        return 0;
    }
    if (!omc_bmff_read_u32be(bytes, size, off + 0U, &size32)
        || !omc_bmff_read_u32be(bytes, size, off + 4U, &type)) {
        return 0;
    }

    header_size = 8U;
    box_size    = size32;
    if (size32 == 0U) {
        box_size = end - off;
    } else if (size32 == 1U) {
        if (!omc_bmff_read_u64be(bytes, size, off + 8U, &box_size)) {
            return 0;
        }
        header_size = 16U;
    }

    if (box_size < header_size) {
        return 0;
    }
    if (off + box_size > end || off + box_size > (omc_u64)size) {
        return 0;
    }

    out_box->offset      = off;
    out_box->size        = box_size;
    out_box->header_size = header_size;
    out_box->type        = type;
    return 1;
}

static int
omc_bmff_is_container_box(omc_u32 type)
{
    switch (type) {
    case OMC_BMFF_FOURCC('m', 'o', 'o', 'v'):
    case OMC_BMFF_FOURCC('t', 'r', 'a', 'k'):
    case OMC_BMFF_FOURCC('m', 'd', 'i', 'a'):
    case OMC_BMFF_FOURCC('m', 'i', 'n', 'f'):
    case OMC_BMFF_FOURCC('s', 't', 'b', 'l'):
    case OMC_BMFF_FOURCC('e', 'd', 't', 's'):
    case OMC_BMFF_FOURCC('d', 'i', 'n', 'f'):
    case OMC_BMFF_FOURCC('u', 'd', 't', 'a'): return 1;
    default: return 0;
    }
}

static void
omc_bmff_note_brand(omc_u32 brand, omc_bmff_brand_info* info)
{
    if (info == (omc_bmff_brand_info*)0) {
        return;
    }

    if (brand == OMC_BMFF_FOURCC('c', 'r', 'x', ' ')
        || brand == OMC_BMFF_FOURCC('C', 'R', '3', ' ')) {
        info->is_cr3 = 1;
    }
    if (brand == OMC_BMFF_FOURCC('a', 'v', 'i', 'f')
        || brand == OMC_BMFF_FOURCC('a', 'v', 'i', 's')) {
        info->is_avif = 1;
    }
    if (brand == OMC_BMFF_FOURCC('m', 'i', 'f', '1')
        || brand == OMC_BMFF_FOURCC('m', 's', 'f', '1')
        || brand == OMC_BMFF_FOURCC('h', 'e', 'i', 'c')
        || brand == OMC_BMFF_FOURCC('h', 'e', 'i', 'x')
        || brand == OMC_BMFF_FOURCC('h', 'e', 'v', 'c')
        || brand == OMC_BMFF_FOURCC('h', 'e', 'v', 'x')) {
        info->is_heif = 1;
    }
}

static int
omc_bmff_find_ftyp(omc_bmff_ctx* ctx, omc_bmff_box* out_ftyp)
{
    omc_u64 off;

    if (ctx == (omc_bmff_ctx*)0 || out_ftyp == (omc_bmff_box*)0) {
        return 0;
    }

    off = 0U;
    while (off + 8U <= (omc_u64)ctx->size) {
        omc_bmff_box box;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, (omc_u64)ctx->size,
                                &box)) {
            return 0;
        }
        if (box.type == OMC_BMFF_FOURCC('f', 't', 'y', 'p')) {
            *out_ftyp = box;
            return 1;
        }
        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }

    return 0;
}

static int
omc_bmff_parse_ftyp(omc_bmff_ctx* ctx, const omc_bmff_box* ftyp,
                    omc_bmff_brand_info* out_info)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u64 p;
    omc_u32 brand;

    if (ctx == (omc_bmff_ctx*)0 || ftyp == (const omc_bmff_box*)0
        || out_info == (omc_bmff_brand_info*)0) {
        return 0;
    }

    memset(out_info, 0, sizeof(*out_info));
    payload_off = ftyp->offset + ftyp->header_size;
    payload_end = ftyp->offset + ftyp->size;
    if (payload_off + 8U > payload_end) {
        return 0;
    }
    if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, payload_off + 0U,
                             &out_info->major_brand)
        || !omc_bmff_read_u32be(ctx->bytes, ctx->size, payload_off + 4U,
                                &out_info->minor_version)) {
        return 0;
    }

    omc_bmff_note_brand(out_info->major_brand, out_info);
    p = payload_off + 8U;
    while (p + 4U <= payload_end) {
        if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, p, &brand)) {
            return 0;
        }
        omc_bmff_note_brand(brand, out_info);
        if (out_info->compat_count < 32U) {
            out_info->compat_brands[out_info->compat_count] = brand;
            out_info->compat_count += 1U;
        }
        p += 4U;
    }
    return 1;
}

static int
omc_bmff_bytes_ascii(const char* text, omc_u16 len)
{
    omc_u16 i;

    if (text == (const char*)0) {
        return 0;
    }
    for (i = 0U; i < len; ++i) {
        if ((unsigned char)text[i] >= 0x80U) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_prepare_block(omc_bmff_ctx* ctx)
{
    omc_block_info info;
    omc_status st;

    if (ctx == (omc_bmff_ctx*)0) {
        return 0;
    }
    if (ctx->store == (omc_store*)0 || ctx->have_block) {
        return 1;
    }

    memset(&info, 0, sizeof(info));
    st = omc_store_add_block(ctx->store, &info, &ctx->block_id);
    if (st != OMC_STATUS_OK) {
        ctx->res.status = OMC_BMFF_NOMEM;
        return 0;
    }

    ctx->have_block = 1;
    return 1;
}

static int
omc_bmff_emit_entry(omc_bmff_ctx* ctx, const char* field, const omc_val* value)
{
    omc_entry entry;
    omc_byte_ref field_ref;
    omc_status st;

    if (ctx == (omc_bmff_ctx*)0 || field == (const char*)0
        || value == (const omc_val*)0) {
        return 0;
    }
    if (ctx->res.entries_decoded >= ctx->opts.limits.max_entries) {
        ctx->res.status = OMC_BMFF_LIMIT;
        return 0;
    }
    if (ctx->store == (omc_store*)0) {
        ctx->res.entries_decoded += 1U;
        ctx->order_in_block += 1U;
        return 1;
    }
    if (!omc_bmff_prepare_block(ctx)) {
        return 0;
    }

    st = omc_arena_append(&ctx->store->arena, field, strlen(field), &field_ref);
    if (st != OMC_STATUS_OK) {
        ctx->res.status = OMC_BMFF_NOMEM;
        return 0;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_bmff_field(&entry.key, field_ref);
    entry.value                   = *value;
    entry.origin.block            = ctx->block_id;
    entry.origin.order_in_block   = ctx->order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.flags                   = OMC_ENTRY_FLAG_DERIVED;
    st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
    if (st != OMC_STATUS_OK) {
        ctx->res.status = OMC_BMFF_NOMEM;
        return 0;
    }

    ctx->res.entries_decoded += 1U;
    ctx->order_in_block += 1U;
    return 1;
}

static int
omc_bmff_emit_u32_field(omc_bmff_ctx* ctx, const char* field, omc_u32 value)
{
    omc_val val;

    omc_val_make_u32(&val, value);
    return omc_bmff_emit_entry(ctx, field, &val);
}

static int
omc_bmff_emit_u16_field(omc_bmff_ctx* ctx, const char* field, omc_u16 value)
{
    omc_val val;

    omc_val_make_u16(&val, value);
    return omc_bmff_emit_entry(ctx, field, &val);
}

static int
omc_bmff_emit_u64_field(omc_bmff_ctx* ctx, const char* field, omc_u64 value)
{
    omc_val val;

    omc_val_make_u64(&val, value);
    return omc_bmff_emit_entry(ctx, field, &val);
}

static int
omc_bmff_emit_u32_array_field(omc_bmff_ctx* ctx, const char* field,
                              const omc_u32* values, omc_u32 count)
{
    omc_val val;
    omc_byte_ref ref;
    omc_status st;
    omc_size bytes_size;

    if (ctx == (omc_bmff_ctx*)0 || field == (const char*)0
        || values == (const omc_u32*)0 || count == 0U) {
        return 0;
    }
    if (count > ((omc_u32)(~(omc_size)0) / (omc_u32)sizeof(omc_u32))) {
        ctx->res.status = OMC_BMFF_LIMIT;
        return 0;
    }
    if (ctx->res.entries_decoded >= ctx->opts.limits.max_entries) {
        ctx->res.status = OMC_BMFF_LIMIT;
        return 0;
    }
    if (ctx->store == (omc_store*)0) {
        ctx->res.entries_decoded += 1U;
        ctx->order_in_block += 1U;
        return 1;
    }
    if (!omc_bmff_prepare_block(ctx)) {
        return 0;
    }

    bytes_size = (omc_size)count * sizeof(omc_u32);
    st         = omc_arena_append(&ctx->store->arena, values, bytes_size, &ref);
    if (st != OMC_STATUS_OK) {
        ctx->res.status = OMC_BMFF_NOMEM;
        return 0;
    }

    omc_val_init(&val);
    val.kind      = OMC_VAL_ARRAY;
    val.elem_type = OMC_ELEM_U32;
    val.count     = count;
    val.u.ref     = ref;
    return omc_bmff_emit_entry(ctx, field, &val);
}

static int
omc_bmff_emit_u8_field(omc_bmff_ctx* ctx, const char* field, omc_u8 value)
{
    omc_val val;

    omc_val_make_u8(&val, value);
    return omc_bmff_emit_entry(ctx, field, &val);
}

static int
omc_bmff_emit_text_field(omc_bmff_ctx* ctx, const char* field,
                         const char* value, omc_u16 value_len)
{
    omc_val val;
    omc_byte_ref value_ref;
    omc_status st;
    omc_text_encoding enc;

    if (ctx == (omc_bmff_ctx*)0 || field == (const char*)0
        || value == (const char*)0) {
        return 0;
    }
    if (ctx->res.entries_decoded >= ctx->opts.limits.max_entries) {
        ctx->res.status = OMC_BMFF_LIMIT;
        return 0;
    }
    if (ctx->store == (omc_store*)0) {
        ctx->res.entries_decoded += 1U;
        ctx->order_in_block += 1U;
        return 1;
    }
    if (!omc_bmff_prepare_block(ctx)) {
        return 0;
    }

    st = omc_arena_append(&ctx->store->arena, value, value_len, &value_ref);
    if (st != OMC_STATUS_OK) {
        ctx->res.status = OMC_BMFF_NOMEM;
        return 0;
    }

    if (omc_bmff_bytes_ascii(value, value_len)) {
        enc = OMC_TEXT_ASCII;
    } else {
        enc = OMC_TEXT_UTF8;
    }
    omc_val_make_text(&val, value_ref, enc);
    return omc_bmff_emit_entry(ctx, field, &val);
}

static void
omc_bmff_push_primary_item_id(omc_u32* ids, omc_u32* io_count, omc_u32 cap,
                              omc_u32 item_id)
{
    if (ids == (omc_u32*)0 || io_count == (omc_u32*)0) {
        return;
    }
    if (*io_count < cap) {
        ids[*io_count] = item_id;
    }
    if (*io_count != ~(omc_u32)0) {
        *io_count += 1U;
    }
}

static int
omc_bmff_push_unique_item_id(omc_u32* ids, omc_u32* io_count, omc_u32 cap,
                             omc_u32 item_id)
{
    omc_u32 i;
    omc_u32 take_count;

    if (ids == (omc_u32*)0 || io_count == (omc_u32*)0) {
        return 0;
    }

    take_count = *io_count;
    if (take_count > cap) {
        take_count = cap;
    }
    for (i = 0U; i < take_count; ++i) {
        if (ids[i] == item_id) {
            return 1;
        }
    }

    omc_bmff_push_primary_item_id(ids, io_count, cap, item_id);
    return 1;
}

static void
omc_bmff_sort_u32_values(omc_u32* values, omc_u32 count)
{
    omc_u32 i;

    if (values == (omc_u32*)0 || count < 2U) {
        return;
    }

    for (i = 1U; i < count; ++i) {
        omc_u32 key;
        omc_u32 j;

        key = values[i];
        j   = i;
        while (j > 0U && values[j - 1U] > key) {
            values[j] = values[j - 1U];
            j -= 1U;
        }
        values[j] = key;
    }
}

static int
omc_bmff_fourcc_token(omc_u32 type, char out[5])
{
    omc_u32 i;

    if (out == (char*)0) {
        return 0;
    }

    for (i = 0U; i < 4U; ++i) {
        char c;

        c = (char)((type >> ((3U - i) * 8U)) & 0xFFU);
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        } else if (c == ' ') {
            c = '_';
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                     || c == '_')) {
            return 0;
        }
        out[i] = c;
    }
    out[4] = '\0';
    return 1;
}

static int
omc_bmff_fourcc_display_name(omc_u32 type, char out[11], omc_u16* out_len)
{
    static const char hex_digits[] = "0123456789abcdef";
    omc_u32 i;
    int printable;

    if (out == (char*)0 || out_len == (omc_u16*)0) {
        return 0;
    }

    printable = 1;
    for (i = 0U; i < 4U; ++i) {
        char c;

        c      = (char)((type >> ((3U - i) * 8U)) & 0xFFU);
        out[i] = c;
        if (c < 0x20 || c > 0x7E) {
            printable = 0;
        }
    }
    if (printable) {
        out[4]   = '\0';
        *out_len = 4U;
        return 1;
    }

    out[0] = '0';
    out[1] = 'x';
    for (i = 0U; i < 8U; ++i) {
        omc_u32 shift;

        shift       = (7U - i) * 4U;
        out[2U + i] = hex_digits[(type >> shift) & 0x0FU];
    }
    out[10]  = '\0';
    *out_len = 10U;
    return 1;
}

static int
omc_bmff_make_field2(char* out, omc_size cap, const char* prefix,
                     const char* suffix)
{
    omc_size prefix_len;
    omc_size suffix_len;

    if (out == (char*)0 || cap == 0U || prefix == (const char*)0
        || suffix == (const char*)0) {
        return 0;
    }

    prefix_len = strlen(prefix);
    suffix_len = strlen(suffix);
    if (prefix_len + 1U + suffix_len >= cap) {
        return 0;
    }

    memcpy(out, prefix, prefix_len);
    out[prefix_len] = '.';
    memcpy(out + prefix_len + 1U, suffix, suffix_len);
    out[prefix_len + 1U + suffix_len] = '\0';
    return 1;
}

static int
omc_bmff_count_item_edges(const omc_bmff_iref_edge* edges, omc_u32 edge_count,
                          omc_u32 filter_type, int use_filter, omc_u32 item_id,
                          omc_u32* out_in_count, omc_u32* out_out_count)
{
    omc_u32 i;
    omc_u32 in_count;
    omc_u32 out_count;

    if (edges == (const omc_bmff_iref_edge*)0 || out_in_count == (omc_u32*)0
        || out_out_count == (omc_u32*)0) {
        return 0;
    }

    in_count  = 0U;
    out_count = 0U;
    for (i = 0U; i < edge_count; ++i) {
        if (use_filter && edges[i].ref_type != filter_type) {
            continue;
        }
        if (edges[i].from_item_id == item_id) {
            out_count += 1U;
        }
        if (edges[i].to_item_id == item_id) {
            in_count += 1U;
        }
    }

    *out_in_count  = in_count;
    *out_out_count = out_count;
    return 1;
}

static int
omc_bmff_emit_iref_item_summary(omc_bmff_ctx* ctx,
                                const omc_bmff_iref_edge* edges,
                                omc_u32 edge_count, omc_u32 filter_type,
                                int use_filter, const char* prefix,
                                const char* graph_prefix)
{
    omc_u32 item_ids[256];
    omc_u32 from_ids[128];
    omc_u32 to_ids[128];
    omc_u32 item_count;
    omc_u32 from_count;
    omc_u32 to_count;
    omc_u32 match_count;
    omc_u32 i;
    char field[64];

    if (ctx == (omc_bmff_ctx*)0 || edges == (const omc_bmff_iref_edge*)0
        || prefix == (const char*)0) {
        return 0;
    }

    item_count  = 0U;
    from_count  = 0U;
    to_count    = 0U;
    match_count = 0U;
    for (i = 0U; i < edge_count; ++i) {
        if (use_filter && edges[i].ref_type != filter_type) {
            continue;
        }
        match_count += 1U;
        if (!omc_bmff_push_unique_item_id(item_ids, &item_count, 256U,
                                          edges[i].from_item_id)
            || !omc_bmff_push_unique_item_id(item_ids, &item_count, 256U,
                                             edges[i].to_item_id)
            || !omc_bmff_push_unique_item_id(from_ids, &from_count, 128U,
                                             edges[i].from_item_id)
            || !omc_bmff_push_unique_item_id(to_ids, &to_count, 128U,
                                             edges[i].to_item_id)) {
            return 0;
        }
    }

    omc_bmff_sort_u32_values(item_ids, item_count);
    omc_bmff_sort_u32_values(from_ids, from_count);
    omc_bmff_sort_u32_values(to_ids, to_count);

    if (!omc_bmff_make_field2(field, sizeof(field), prefix, "item_count")
        || !omc_bmff_emit_u32_field(ctx, field, item_count)
        || !omc_bmff_make_field2(field, sizeof(field), prefix,
                                 "from_item_unique_count")
        || !omc_bmff_emit_u32_field(ctx, field, from_count)
        || !omc_bmff_make_field2(field, sizeof(field), prefix,
                                 "to_item_unique_count")
        || !omc_bmff_emit_u32_field(ctx, field, to_count)) {
        return 0;
    }

    for (i = 0U; i < item_count; ++i) {
        omc_u32 in_count;
        omc_u32 out_count;

        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "item_id")
            || !omc_bmff_emit_u32_field(ctx, field, item_ids[i])) {
            return 0;
        }
        if (!omc_bmff_count_item_edges(edges, edge_count, filter_type,
                                       use_filter, item_ids[i], &in_count,
                                       &out_count)) {
            return 0;
        }
        if (!omc_bmff_make_field2(field, sizeof(field), prefix,
                                  "item_out_edge_count")
            || !omc_bmff_emit_u32_field(ctx, field, out_count)
            || !omc_bmff_make_field2(field, sizeof(field), prefix,
                                     "item_in_edge_count")
            || !omc_bmff_emit_u32_field(ctx, field, in_count)) {
            return 0;
        }
    }

    if (graph_prefix != (const char*)0) {
        if (!omc_bmff_make_field2(field, sizeof(field), graph_prefix,
                                  "edge_count")
            || !omc_bmff_emit_u32_field(ctx, field, match_count)
            || !omc_bmff_make_field2(field, sizeof(field), graph_prefix,
                                     "from_item_unique_count")
            || !omc_bmff_emit_u32_field(ctx, field, from_count)
            || !omc_bmff_make_field2(field, sizeof(field), graph_prefix,
                                     "to_item_unique_count")
            || !omc_bmff_emit_u32_field(ctx, field, to_count)) {
            return 0;
        }
    }

    return 1;
}

static omc_u8
omc_bmff_ascii_to_lower(omc_u8 c)
{
    if (c >= (omc_u8)'A' && c <= (omc_u8)'Z') {
        return (omc_u8)(c + 0x20U);
    }
    return c;
}

static int
omc_bmff_ascii_ieq(const char* a, omc_u16 a_len, const char* b)
{
    omc_u16 b_len;
    omc_u16 i;

    if (a == (const char*)0 || b == (const char*)0) {
        return 0;
    }

    b_len = (omc_u16)strlen(b);
    if (a_len != b_len) {
        return 0;
    }

    for (i = 0U; i < a_len; ++i) {
        omc_u8 ac;
        omc_u8 bc;

        ac = omc_bmff_ascii_to_lower((omc_u8)a[i]);
        bc = omc_bmff_ascii_to_lower((omc_u8)b[i]);
        if (ac != bc) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_ascii_icontains(const char* hay, omc_u16 hay_len, const char* needle)
{
    omc_u16 needle_len;
    omc_u16 i;

    if (hay == (const char*)0 || needle == (const char*)0) {
        return 0;
    }

    needle_len = (omc_u16)strlen(needle);
    if (needle_len == 0U) {
        return 1;
    }
    if (hay_len < needle_len) {
        return 0;
    }

    for (i = 0U; (omc_u32)i + (omc_u32)needle_len <= (omc_u32)hay_len; ++i) {
        omc_u16 j;
        int match;

        match = 1;
        for (j = 0U; j < needle_len; ++j) {
            omc_u8 hc;
            omc_u8 nc;

            hc = omc_bmff_ascii_to_lower((omc_u8)hay[i + j]);
            nc = omc_bmff_ascii_to_lower((omc_u8)needle[j]);
            if (hc != nc) {
                match = 0;
                break;
            }
        }
        if (match) {
            return 1;
        }
    }
    return 0;
}

static int
omc_bmff_ascii_istarts_with(const char* text, omc_u16 text_len,
                            const char* prefix)
{
    omc_u16 prefix_len;
    omc_u16 i;

    if (text == (const char*)0 || prefix == (const char*)0) {
        return 0;
    }

    prefix_len = (omc_u16)strlen(prefix);
    if (text_len < prefix_len) {
        return 0;
    }

    for (i = 0U; i < prefix_len; ++i) {
        omc_u8 tc;
        omc_u8 pc;

        tc = omc_bmff_ascii_to_lower((omc_u8)text[i]);
        pc = omc_bmff_ascii_to_lower((omc_u8)prefix[i]);
        if (tc != pc) {
            return 0;
        }
    }
    return 1;
}

static const char*
omc_bmff_item_semantic_name(omc_bmff_item_semantic semantic)
{
    switch (semantic) {
    case OMC_BMFF_ITEM_IMAGE: return "image";
    case OMC_BMFF_ITEM_EXIF: return "exif";
    case OMC_BMFF_ITEM_XMP: return "xmp";
    case OMC_BMFF_ITEM_JUMBF: return "jumbf";
    case OMC_BMFF_ITEM_C2PA: return "c2pa";
    case OMC_BMFF_ITEM_ICC_PROFILE: return "icc_profile";
    case OMC_BMFF_ITEM_AUXILIARY: return "auxiliary";
    case OMC_BMFF_ITEM_DERIVED: return "derived";
    case OMC_BMFF_ITEM_THUMBNAIL: return "thumbnail";
    case OMC_BMFF_ITEM_CONTENT_DESCRIPTION: return "content_description";
    case OMC_BMFF_ITEM_URI: return "uri";
    case OMC_BMFF_ITEM_JSON: return "json";
    case OMC_BMFF_ITEM_UNKNOWN:
    default: return "unknown";
    }
}

static int
omc_bmff_item_semantic_is_known(omc_bmff_item_semantic semantic)
{
    return semantic != OMC_BMFF_ITEM_UNKNOWN;
}

static int
omc_bmff_item_semantic_is_metadata(omc_bmff_item_semantic semantic)
{
    switch (semantic) {
    case OMC_BMFF_ITEM_EXIF:
    case OMC_BMFF_ITEM_XMP:
    case OMC_BMFF_ITEM_JUMBF:
    case OMC_BMFF_ITEM_C2PA:
    case OMC_BMFF_ITEM_ICC_PROFILE:
    case OMC_BMFF_ITEM_CONTENT_DESCRIPTION:
    case OMC_BMFF_ITEM_URI:
    case OMC_BMFF_ITEM_JSON: return 1;
    case OMC_BMFF_ITEM_UNKNOWN:
    case OMC_BMFF_ITEM_IMAGE:
    case OMC_BMFF_ITEM_AUXILIARY:
    case OMC_BMFF_ITEM_DERIVED:
    case OMC_BMFF_ITEM_THUMBNAIL:
    default: return 0;
    }
}

static void
omc_bmff_count_item_semantic(omc_bmff_item_semantic semantic,
                             omc_bmff_item_semantic_counts* out)
{
    if (out == (omc_bmff_item_semantic_counts*)0
        || !omc_bmff_item_semantic_is_known(semantic)) {
        return;
    }

    out->known += 1U;
    if (omc_bmff_item_semantic_is_metadata(semantic)) {
        out->metadata += 1U;
    }

    switch (semantic) {
    case OMC_BMFF_ITEM_IMAGE: out->image += 1U; break;
    case OMC_BMFF_ITEM_EXIF: out->exif += 1U; break;
    case OMC_BMFF_ITEM_XMP: out->xmp += 1U; break;
    case OMC_BMFF_ITEM_JUMBF: out->jumbf += 1U; break;
    case OMC_BMFF_ITEM_C2PA: out->c2pa += 1U; break;
    case OMC_BMFF_ITEM_ICC_PROFILE: out->icc_profile += 1U; break;
    case OMC_BMFF_ITEM_AUXILIARY: out->auxiliary += 1U; break;
    case OMC_BMFF_ITEM_DERIVED: out->derived += 1U; break;
    case OMC_BMFF_ITEM_THUMBNAIL: out->thumbnail += 1U; break;
    case OMC_BMFF_ITEM_CONTENT_DESCRIPTION:
        out->content_description += 1U;
        break;
    case OMC_BMFF_ITEM_URI: out->uri += 1U; break;
    case OMC_BMFF_ITEM_JSON: out->json += 1U; break;
    case OMC_BMFF_ITEM_UNKNOWN:
    default: break;
    }
}

static omc_bmff_item_semantic
omc_bmff_classify_item_semantic(const omc_bmff_item_info* info)
{
    if (info == (const omc_bmff_item_info*)0) {
        return OMC_BMFF_ITEM_UNKNOWN;
    }

    if (info->have_type) {
        if (info->item_type == OMC_BMFF_FOURCC('E', 'x', 'i', 'f')) {
            return OMC_BMFF_ITEM_EXIF;
        }
        if (info->item_type == OMC_BMFF_FOURCC('j', 'u', 'm', 'b')) {
            return OMC_BMFF_ITEM_JUMBF;
        }
        if (info->item_type == OMC_BMFF_FOURCC('a', 'u', 'x', 'l')) {
            return OMC_BMFF_ITEM_AUXILIARY;
        }
        if (info->item_type == OMC_BMFF_FOURCC('d', 'e', 'r', 'v')) {
            return OMC_BMFF_ITEM_DERIVED;
        }
        if (info->item_type == OMC_BMFF_FOURCC('t', 'h', 'm', 'b')) {
            return OMC_BMFF_ITEM_THUMBNAIL;
        }
        if (info->item_type == OMC_BMFF_FOURCC('c', 'd', 's', 'c')) {
            return OMC_BMFF_ITEM_CONTENT_DESCRIPTION;
        }
        if (info->item_type == OMC_BMFF_FOURCC('u', 'r', 'i', ' ')) {
            return OMC_BMFF_ITEM_URI;
        }
        if (info->item_type == OMC_BMFF_FOURCC('h', 'v', 'c', '1')
            || info->item_type == OMC_BMFF_FOURCC('a', 'v', '0', '1')
            || info->item_type == OMC_BMFF_FOURCC('j', 'p', 'e', 'g')
            || info->item_type == OMC_BMFF_FOURCC('g', 'r', 'i', 'd')
            || info->item_type == OMC_BMFF_FOURCC('i', 'd', 'e', 'n')) {
            return OMC_BMFF_ITEM_IMAGE;
        }
    }

    if (omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                           "application/c2pa")
        || omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                              "application/c2pa+jumbf")) {
        return OMC_BMFF_ITEM_C2PA;
    }
    if (omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                           "application/jumbf")) {
        return OMC_BMFF_ITEM_JUMBF;
    }
    if (omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                           "application/rdf+xml")
        || omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                              "application/xmp+xml")
        || omc_bmff_ascii_icontains(info->content_type, info->content_type_len,
                                    "xmp")) {
        return OMC_BMFF_ITEM_XMP;
    }
    if (omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                           "application/vnd.iccprofile")
        || omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                              "application/vnd.icc.profile")
        || omc_bmff_ascii_icontains(info->content_type, info->content_type_len,
                                    "icc")) {
        return OMC_BMFF_ITEM_ICC_PROFILE;
    }
    if (omc_bmff_ascii_ieq(info->content_type, info->content_type_len,
                           "application/json")) {
        return OMC_BMFF_ITEM_JSON;
    }
    if (omc_bmff_ascii_istarts_with(info->content_type, info->content_type_len,
                                    "image/")) {
        return OMC_BMFF_ITEM_IMAGE;
    }
    return OMC_BMFF_ITEM_UNKNOWN;
}

static int
omc_bmff_emit_count_field_if_nonzero(omc_bmff_ctx* ctx, const char* field,
                                     omc_u32 count)
{
    if (count == 0U) {
        return 1;
    }
    return omc_bmff_emit_u32_field(ctx, field, count);
}

static int
omc_bmff_emit_item_semantic_counts(omc_bmff_ctx* ctx,
                                   const omc_bmff_item_semantic_counts* counts)
{
    if (ctx == (omc_bmff_ctx*)0
        || counts == (const omc_bmff_item_semantic_counts*)0) {
        return 0;
    }
    if (counts->known == 0U) {
        return 1;
    }

    if (!omc_bmff_emit_u32_field(ctx, "item.semantic_known_count", counts->known)
        || !omc_bmff_emit_count_field_if_nonzero(ctx,
                                                 "item.semantic_metadata_count",
                                                 counts->metadata)
        || !omc_bmff_emit_count_field_if_nonzero(ctx,
                                                 "item.semantic_image_count",
                                                 counts->image)
        || !omc_bmff_emit_count_field_if_nonzero(ctx,
                                                 "item.semantic_exif_count",
                                                 counts->exif)
        || !omc_bmff_emit_count_field_if_nonzero(ctx, "item.semantic_xmp_count",
                                                 counts->xmp)
        || !omc_bmff_emit_count_field_if_nonzero(ctx,
                                                 "item.semantic_jumbf_count",
                                                 counts->jumbf)
        || !omc_bmff_emit_count_field_if_nonzero(ctx,
                                                 "item.semantic_c2pa_count",
                                                 counts->c2pa)
        || !omc_bmff_emit_count_field_if_nonzero(
            ctx, "item.semantic_icc_profile_count", counts->icc_profile)
        || !omc_bmff_emit_count_field_if_nonzero(
            ctx, "item.semantic_auxiliary_count", counts->auxiliary)
        || !omc_bmff_emit_count_field_if_nonzero(ctx,
                                                 "item.semantic_derived_count",
                                                 counts->derived)
        || !omc_bmff_emit_count_field_if_nonzero(
            ctx, "item.semantic_thumbnail_count", counts->thumbnail)
        || !omc_bmff_emit_count_field_if_nonzero(
            ctx, "item.semantic_content_description_count",
            counts->content_description)
        || !omc_bmff_emit_count_field_if_nonzero(ctx, "item.semantic_uri_count",
                                                 counts->uri)
        || !omc_bmff_emit_count_field_if_nonzero(ctx,
                                                 "item.semantic_json_count",
                                                 counts->json)) {
        return 0;
    }
    return 1;
}

static omc_bmff_aux_semantic
omc_bmff_classify_auxc_type(const char* aux_type, omc_u16 aux_type_len)
{
    if (aux_type == (const char*)0 || aux_type_len == 0U) {
        return OMC_BMFF_AUX_UNKNOWN;
    }

    if (omc_bmff_ascii_ieq(aux_type, aux_type_len, "urn:mpeg:hevc:2015:auxid:1")
        || omc_bmff_ascii_icontains(aux_type, aux_type_len, ":aux:alpha")
        || omc_bmff_ascii_ieq(aux_type, aux_type_len,
                              "urn:mpeg:mpegb:cicp:systems:auxiliary:alpha")) {
        return OMC_BMFF_AUX_ALPHA;
    }
    if (omc_bmff_ascii_ieq(aux_type, aux_type_len, "urn:mpeg:hevc:2015:auxid:2")
        || omc_bmff_ascii_icontains(aux_type, aux_type_len, ":aux:depth")
        || omc_bmff_ascii_icontains(aux_type, aux_type_len, "depth")) {
        return OMC_BMFF_AUX_DEPTH;
    }
    if (omc_bmff_ascii_ieq(aux_type, aux_type_len, "urn:mpeg:hevc:2015:auxid:3")
        || omc_bmff_ascii_icontains(aux_type, aux_type_len, ":aux:disparity")
        || omc_bmff_ascii_icontains(aux_type, aux_type_len, "disparity")) {
        return OMC_BMFF_AUX_DISPARITY;
    }
    if (omc_bmff_ascii_icontains(aux_type, aux_type_len, "portraitmatte")
        || omc_bmff_ascii_icontains(aux_type, aux_type_len, ":aux:matte")
        || omc_bmff_ascii_icontains(aux_type, aux_type_len, "matte")) {
        return OMC_BMFF_AUX_MATTE;
    }
    return OMC_BMFF_AUX_UNKNOWN;
}

static const char*
omc_bmff_aux_semantic_name(omc_bmff_aux_semantic semantic)
{
    switch (semantic) {
    case OMC_BMFF_AUX_ALPHA: return "alpha";
    case OMC_BMFF_AUX_DEPTH: return "depth";
    case OMC_BMFF_AUX_DISPARITY: return "disparity";
    case OMC_BMFF_AUX_MATTE: return "matte";
    case OMC_BMFF_AUX_UNKNOWN:
    default: return "unknown";
    }
}

static const char*
omc_bmff_primary_linked_role_for_aux(omc_bmff_aux_semantic semantic)
{
    switch (semantic) {
    case OMC_BMFF_AUX_ALPHA: return "alpha";
    case OMC_BMFF_AUX_DEPTH: return "depth";
    case OMC_BMFF_AUX_DISPARITY: return "disparity";
    case OMC_BMFF_AUX_MATTE: return "matte";
    case OMC_BMFF_AUX_UNKNOWN:
    default: return "auxiliary";
    }
}

typedef enum omc_bmff_aux_subtype_kind {
    OMC_BMFF_AUX_SUBTYPE_NONE    = 0,
    OMC_BMFF_AUX_SUBTYPE_U8      = 1,
    OMC_BMFF_AUX_SUBTYPE_U16BE   = 2,
    OMC_BMFF_AUX_SUBTYPE_U32BE   = 3,
    OMC_BMFF_AUX_SUBTYPE_U64BE   = 4,
    OMC_BMFF_AUX_SUBTYPE_ASCII_Z = 5,
    OMC_BMFF_AUX_SUBTYPE_UUID    = 6,
    OMC_BMFF_AUX_SUBTYPE_HEX     = 7
} omc_bmff_aux_subtype_kind;

static const char*
omc_bmff_aux_subtype_kind_name(omc_bmff_aux_subtype_kind kind)
{
    switch (kind) {
    case OMC_BMFF_AUX_SUBTYPE_U8: return "u8";
    case OMC_BMFF_AUX_SUBTYPE_U16BE: return "u16be";
    case OMC_BMFF_AUX_SUBTYPE_U32BE: return "u32be";
    case OMC_BMFF_AUX_SUBTYPE_U64BE: return "u64be";
    case OMC_BMFF_AUX_SUBTYPE_ASCII_Z: return "ascii_z";
    case OMC_BMFF_AUX_SUBTYPE_UUID: return "uuid";
    case OMC_BMFF_AUX_SUBTYPE_HEX: return "hex";
    case OMC_BMFF_AUX_SUBTYPE_NONE:
    default: return "none";
    }
}

static int
omc_bmff_aux_subtype_is_ascii_z(const omc_u8* bytes, omc_u16 size)
{
    omc_u16 i;

    if (bytes == (const omc_u8*)0 || size < 2U) {
        return 0;
    }
    if (bytes[size - 1U] != 0U) {
        return 0;
    }
    for (i = 0U; i + 1U < size; ++i) {
        omc_u8 c;

        c = bytes[i];
        if (c < 0x20U || c > 0x7EU) {
            return 0;
        }
    }
    return 1;
}

static omc_bmff_aux_subtype_kind
omc_bmff_classify_aux_subtype(const omc_u8* bytes, omc_u16 size)
{
    if (bytes == (const omc_u8*)0 || size == 0U) {
        return OMC_BMFF_AUX_SUBTYPE_NONE;
    }
    if (omc_bmff_aux_subtype_is_ascii_z(bytes, size)) {
        return OMC_BMFF_AUX_SUBTYPE_ASCII_Z;
    }
    if (size == 1U) {
        return OMC_BMFF_AUX_SUBTYPE_U8;
    }
    if (size == 2U) {
        return OMC_BMFF_AUX_SUBTYPE_U16BE;
    }
    if (size == 4U) {
        return OMC_BMFF_AUX_SUBTYPE_U32BE;
    }
    if (size == 8U) {
        return OMC_BMFF_AUX_SUBTYPE_U64BE;
    }
    if (size == 16U) {
        return OMC_BMFF_AUX_SUBTYPE_UUID;
    }
    return OMC_BMFF_AUX_SUBTYPE_HEX;
}

static int
omc_bmff_format_subtype_hex(const omc_u8* bytes, omc_u16 size, char* out,
                            omc_size out_cap)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    omc_u16 i;
    omc_size need;

    if (bytes == (const omc_u8*)0 || out == (char*)0) {
        return 0;
    }

    need = 2U + ((omc_size)size * 2U) + 1U;
    if (out_cap < need) {
        return 0;
    }

    out[0] = '0';
    out[1] = 'x';
    for (i = 0U; i < size; ++i) {
        out[2U + ((omc_size)i * 2U) + 0U] = hex_digits[(bytes[i] >> 4) & 0x0FU];
        out[2U + ((omc_size)i * 2U) + 1U] = hex_digits[bytes[i] & 0x0FU];
    }
    out[need - 1U] = '\0';
    return 1;
}

static int
omc_bmff_format_uuid_text(const omc_u8* bytes, char* out, omc_size out_cap)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    static const omc_u8 dash_pos[] = { 4U, 6U, 8U, 10U };
    omc_u16 i;
    omc_u16 out_pos;
    omc_u16 dash_index;

    if (bytes == (const omc_u8*)0 || out == (char*)0 || out_cap < 37U) {
        return 0;
    }

    out_pos    = 0U;
    dash_index = 0U;
    for (i = 0U; i < 16U; ++i) {
        if (dash_index < 4U && i == dash_pos[dash_index]) {
            out[out_pos] = '-';
            out_pos += 1U;
            dash_index += 1U;
        }
        out[out_pos + 0U] = hex_digits[(bytes[i] >> 4) & 0x0FU];
        out[out_pos + 1U] = hex_digits[bytes[i] & 0x0FU];
        out_pos += 2U;
    }
    out[out_pos] = '\0';
    return 1;
}

static int
omc_bmff_emit_aux_subtype_fields(omc_bmff_ctx* ctx, const char* prefix,
                                 const omc_u8* subtype, omc_u16 subtype_len,
                                 int emit_len)
{
    omc_bmff_aux_subtype_kind kind;
    char field[64];
    char text[128];
    const char* kind_name;

    if (ctx == (omc_bmff_ctx*)0 || prefix == (const char*)0) {
        return 0;
    }
    if (subtype == (const omc_u8*)0 || subtype_len == 0U) {
        return 1;
    }

    kind      = omc_bmff_classify_aux_subtype(subtype, subtype_len);
    kind_name = omc_bmff_aux_subtype_kind_name(kind);
    if ((emit_len
         && (!omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_len")
             || !omc_bmff_emit_u32_field(ctx, field, subtype_len)))
        || !omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_kind")
        || !omc_bmff_emit_text_field(ctx, field, kind_name,
                                     (omc_u16)strlen(kind_name))
        || !omc_bmff_format_subtype_hex(subtype, subtype_len, text, sizeof(text))
        || !omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_hex")
        || !omc_bmff_emit_text_field(ctx, field, text, (omc_u16)strlen(text))) {
        return 0;
    }

    if (kind == OMC_BMFF_AUX_SUBTYPE_U8) {
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_u32")
            || !omc_bmff_emit_u32_field(ctx, field, subtype[0])) {
            return 0;
        }
    } else if (kind == OMC_BMFF_AUX_SUBTYPE_U16BE) {
        omc_u32 value;

        value = ((omc_u32)subtype[0] << 8) | ((omc_u32)subtype[1] << 0);
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_u32")
            || !omc_bmff_emit_u32_field(ctx, field, value)) {
            return 0;
        }
    } else if (kind == OMC_BMFF_AUX_SUBTYPE_U32BE) {
        omc_u32 value;

        value = ((omc_u32)subtype[0] << 24) | ((omc_u32)subtype[1] << 16)
                | ((omc_u32)subtype[2] << 8) | ((omc_u32)subtype[3] << 0);
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_u32")
            || !omc_bmff_emit_u32_field(ctx, field, value)) {
            return 0;
        }
    } else if (kind == OMC_BMFF_AUX_SUBTYPE_U64BE) {
        omc_u64 value;

        value = ((omc_u64)subtype[0] << 56) | ((omc_u64)subtype[1] << 48)
                | ((omc_u64)subtype[2] << 40) | ((omc_u64)subtype[3] << 32)
                | ((omc_u64)subtype[4] << 24) | ((omc_u64)subtype[5] << 16)
                | ((omc_u64)subtype[6] << 8) | ((omc_u64)subtype[7] << 0);
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_u64")
            || !omc_bmff_emit_u64_field(ctx, field, value)) {
            return 0;
        }
    } else if (kind == OMC_BMFF_AUX_SUBTYPE_ASCII_Z) {
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "subtype_text")
            || !omc_bmff_emit_text_field(ctx, field, (const char*)subtype,
                                         (omc_u16)(subtype_len - 1U))) {
            return 0;
        }
    } else if (kind == OMC_BMFF_AUX_SUBTYPE_UUID) {
        if (!omc_bmff_format_uuid_text(subtype, text, sizeof(text))
            || !omc_bmff_make_field2(field, sizeof(field), prefix,
                                     "subtype_text")
            || !omc_bmff_emit_text_field(ctx, field, text, (omc_u16)strlen(text))
            || !omc_bmff_make_field2(field, sizeof(field), prefix,
                                     "subtype_uuid")
            || !omc_bmff_emit_text_field(ctx, field, text,
                                         (omc_u16)strlen(text))) {
            return 0;
        }
    }

    return 1;
}

static omc_u32
omc_bmff_find_primary_auxl_index(const omc_bmff_primary_props* props,
                                 omc_u32 item_id)
{
    omc_u32 i;
    omc_u32 take_count;

    if (props == (const omc_bmff_primary_props*)0) {
        return ~(omc_u32)0;
    }

    take_count = props->primary_auxl_count;
    if (take_count > 32U) {
        take_count = 32U;
    }
    for (i = 0U; i < take_count; ++i) {
        if (props->primary_auxl_item_ids[i] == item_id) {
            return i;
        }
    }
    return ~(omc_u32)0;
}

static omc_u32
omc_bmff_find_aux_item_index(const omc_bmff_primary_props* props,
                             omc_u32 item_id)
{
    omc_u32 i;
    omc_u32 take_count;

    if (props == (const omc_bmff_primary_props*)0) {
        return ~(omc_u32)0;
    }

    take_count = props->aux_item_count;
    if (take_count > 64U) {
        take_count = 64U;
    }
    for (i = 0U; i < take_count; ++i) {
        if (props->aux_items[i].item_id == item_id) {
            return i;
        }
    }
    return ~(omc_u32)0;
}

static omc_u32
omc_bmff_upsert_aux_item(omc_bmff_primary_props* props, omc_u32 item_id)
{
    omc_u32 idx;

    if (props == (omc_bmff_primary_props*)0) {
        return ~(omc_u32)0;
    }

    idx = omc_bmff_find_aux_item_index(props, item_id);
    if (idx != ~(omc_u32)0) {
        return idx;
    }
    if (props->aux_item_count >= 64U) {
        return ~(omc_u32)0;
    }

    idx = props->aux_item_count;
    memset(&props->aux_items[idx], 0, sizeof(props->aux_items[idx]));
    props->aux_items[idx].item_id = item_id;
    props->aux_item_count += 1U;
    return idx;
}

static void
omc_bmff_set_aux_item_semantic(omc_bmff_primary_props* props, omc_u32 item_id,
                               omc_bmff_aux_semantic semantic)
{
    omc_u32 idx;

    if (props == (omc_bmff_primary_props*)0
        || semantic == OMC_BMFF_AUX_UNKNOWN) {
        return;
    }

    idx = omc_bmff_upsert_aux_item(props, item_id);
    if (idx == ~(omc_u32)0 || idx >= 64U) {
        return;
    }
    if (props->aux_items[idx].semantic == OMC_BMFF_AUX_UNKNOWN) {
        props->aux_items[idx].semantic = semantic;
    }
}

static void
omc_bmff_set_aux_item_type(omc_bmff_primary_props* props, omc_u32 item_id,
                           const char* aux_type, omc_u16 aux_type_len)
{
    omc_u32 idx;
    omc_u16 copy_len;

    if (props == (omc_bmff_primary_props*)0 || aux_type == (const char*)0
        || aux_type_len == 0U) {
        return;
    }

    idx = omc_bmff_upsert_aux_item(props, item_id);
    if (idx == ~(omc_u32)0 || idx >= 64U) {
        return;
    }
    if (props->aux_items[idx].aux_type_len != 0U) {
        return;
    }

    copy_len = aux_type_len;
    if (copy_len > (omc_u16)sizeof(props->aux_items[idx].aux_type)) {
        copy_len = (omc_u16)sizeof(props->aux_items[idx].aux_type);
    }
    memcpy(props->aux_items[idx].aux_type, aux_type, copy_len);
    props->aux_items[idx].aux_type_len = copy_len;
}

static void
omc_bmff_set_aux_item_subtype(omc_bmff_primary_props* props, omc_u32 item_id,
                              const omc_u8* aux_subtype,
                              omc_u16 aux_subtype_len)
{
    omc_u32 idx;
    omc_u16 copy_len;

    if (props == (omc_bmff_primary_props*)0 || aux_subtype == (const omc_u8*)0
        || aux_subtype_len == 0U) {
        return;
    }

    idx = omc_bmff_upsert_aux_item(props, item_id);
    if (idx == ~(omc_u32)0 || idx >= 64U) {
        return;
    }
    if (props->aux_items[idx].aux_subtype_len != 0U) {
        return;
    }

    copy_len = aux_subtype_len;
    if (copy_len > (omc_u16)sizeof(props->aux_items[idx].aux_subtype)) {
        copy_len = (omc_u16)sizeof(props->aux_items[idx].aux_subtype);
    }
    memcpy(props->aux_items[idx].aux_subtype, aux_subtype, copy_len);
    props->aux_items[idx].aux_subtype_len = copy_len;
}

static const omc_bmff_aux_item_info*
omc_bmff_find_aux_item(const omc_bmff_primary_props* props, omc_u32 item_id)
{
    omc_u32 idx;

    idx = omc_bmff_find_aux_item_index(props, item_id);
    if (idx == ~(omc_u32)0 || idx >= 64U) {
        return (const omc_bmff_aux_item_info*)0;
    }
    return &props->aux_items[idx];
}

static void
omc_bmff_set_primary_auxl_semantic(omc_bmff_primary_props* props,
                                   omc_u32 item_id,
                                   omc_bmff_aux_semantic semantic)
{
    omc_u32 idx;

    if (props == (omc_bmff_primary_props*)0
        || semantic == OMC_BMFF_AUX_UNKNOWN) {
        return;
    }

    idx = omc_bmff_find_primary_auxl_index(props, item_id);
    if (idx == ~(omc_u32)0 || idx >= 32U) {
        return;
    }
    if (props->primary_auxl_semantics[idx] != OMC_BMFF_AUX_UNKNOWN) {
        return;
    }
    props->primary_auxl_semantics[idx] = semantic;

    if (semantic == OMC_BMFF_AUX_ALPHA) {
        (void)omc_bmff_push_unique_item_id(props->primary_alpha_item_ids,
                                           &props->primary_alpha_count, 32U,
                                           item_id);
    } else if (semantic == OMC_BMFF_AUX_DEPTH) {
        (void)omc_bmff_push_unique_item_id(props->primary_depth_item_ids,
                                           &props->primary_depth_count, 32U,
                                           item_id);
    } else if (semantic == OMC_BMFF_AUX_DISPARITY) {
        (void)omc_bmff_push_unique_item_id(props->primary_disparity_item_ids,
                                           &props->primary_disparity_count, 32U,
                                           item_id);
    } else if (semantic == OMC_BMFF_AUX_MATTE) {
        (void)omc_bmff_push_unique_item_id(props->primary_matte_item_ids,
                                           &props->primary_matte_count, 32U,
                                           item_id);
    }
}

static int
omc_bmff_read_cstr(const omc_u8* bytes, omc_size size, omc_u64* io_off,
                   omc_u64 end, char* out, omc_size out_cap, omc_u16* out_len)
{
    omc_u64 p;
    omc_u16 n;

    if (bytes == (const omc_u8*)0 || io_off == (omc_u64*)0 || out == (char*)0
        || out_len == (omc_u16*)0 || out_cap == 0U) {
        return 0;
    }
    if (*io_off > end || end > (omc_u64)size) {
        return 0;
    }

    p = *io_off;
    while (p < end && bytes[(omc_size)p] != 0U) {
        p += 1U;
    }
    if (p >= end) {
        return 0;
    }

    n = (omc_u16)(p - *io_off);
    if ((omc_size)n > out_cap) {
        n = (omc_u16)out_cap;
    }
    if (n != 0U) {
        memcpy(out, bytes + (omc_size)(*io_off), (omc_size)n);
    }
    *out_len = n;
    *io_off  = p + 1U;
    return 1;
}

static int
omc_bmff_parse_pitm(const omc_u8* bytes, omc_size size,
                    const omc_bmff_box* pitm, omc_u32* out_item_id)
{
    omc_u64 payload_off;
    omc_u64 payload_size;
    omc_u8 version;

    if (bytes == (const omc_u8*)0 || pitm == (const omc_bmff_box*)0
        || out_item_id == (omc_u32*)0) {
        return 0;
    }

    payload_off  = pitm->offset + pitm->header_size;
    payload_size = pitm->size - pitm->header_size;
    if (payload_size < 6U) {
        return 0;
    }

    version = bytes[(omc_size)payload_off];
    if (version == 0U) {
        omc_u16 id16;

        if (!omc_bmff_read_u16be(bytes, size, payload_off + 4U, &id16)) {
            return 0;
        }
        *out_item_id = id16;
        return 1;
    }
    if (version == 1U) {
        return omc_bmff_read_u32be(bytes, size, payload_off + 4U, out_item_id);
    }
    return 0;
}

static int
omc_bmff_parse_infe(const omc_u8* bytes, omc_size size,
                    const omc_bmff_box* infe, omc_bmff_item_info* out_info)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u64 p;
    omc_u8 version;

    if (bytes == (const omc_u8*)0 || infe == (const omc_bmff_box*)0
        || out_info == (omc_bmff_item_info*)0) {
        return 0;
    }

    memset(out_info, 0, sizeof(*out_info));
    payload_off = infe->offset + infe->header_size;
    payload_end = infe->offset + infe->size;
    if (payload_off + 4U > payload_end) {
        return 0;
    }

    version = bytes[(omc_size)payload_off];
    p       = payload_off + 4U;
    if (version <= 1U) {
        omc_u16 item_id16;
        omc_u16 prot16;

        if (!omc_bmff_read_u16be(bytes, size, p + 0U, &item_id16)
            || !omc_bmff_read_u16be(bytes, size, p + 2U, &prot16)) {
            return 0;
        }
        out_info->item_id          = item_id16;
        out_info->protection_index = prot16;
        p += 4U;
        if (!omc_bmff_read_cstr(bytes, size, &p, payload_end, out_info->name,
                                sizeof(out_info->name), &out_info->name_len)) {
            return 0;
        }
        if (!omc_bmff_read_cstr(bytes, size, &p, payload_end,
                                out_info->content_type,
                                sizeof(out_info->content_type),
                                &out_info->content_type_len)) {
            return 0;
        }
        if (!omc_bmff_read_cstr(bytes, size, &p, payload_end,
                                out_info->content_encoding,
                                sizeof(out_info->content_encoding),
                                &out_info->content_encoding_len)) {
            return 0;
        }
        if (out_info->content_type_len != 0U) {
            out_info->have_type = 1;
            out_info->item_type = OMC_BMFF_FOURCC('m', 'i', 'm', 'e');
        }
        return 1;
    }

    if (version == 2U) {
        omc_u16 item_id16;
        omc_u16 prot16;
        omc_u32 item_type;

        if (!omc_bmff_read_u16be(bytes, size, p + 0U, &item_id16)
            || !omc_bmff_read_u16be(bytes, size, p + 2U, &prot16)
            || !omc_bmff_read_u32be(bytes, size, p + 4U, &item_type)) {
            return 0;
        }
        out_info->item_id          = item_id16;
        out_info->protection_index = prot16;
        out_info->have_type        = 1;
        out_info->item_type        = item_type;
        p += 8U;
    } else if (version == 3U) {
        omc_u32 item_id32;
        omc_u16 prot16;
        omc_u32 item_type;

        if (!omc_bmff_read_u32be(bytes, size, p + 0U, &item_id32)
            || !omc_bmff_read_u16be(bytes, size, p + 4U, &prot16)
            || !omc_bmff_read_u32be(bytes, size, p + 6U, &item_type)) {
            return 0;
        }
        out_info->item_id          = item_id32;
        out_info->protection_index = prot16;
        out_info->have_type        = 1;
        out_info->item_type        = item_type;
        p += 10U;
    } else {
        return 0;
    }

    if (!omc_bmff_read_cstr(bytes, size, &p, payload_end, out_info->name,
                            sizeof(out_info->name), &out_info->name_len)) {
        return 0;
    }

    if (out_info->item_type == OMC_BMFF_FOURCC('m', 'i', 'm', 'e')) {
        if (!omc_bmff_read_cstr(bytes, size, &p, payload_end,
                                out_info->content_type,
                                sizeof(out_info->content_type),
                                &out_info->content_type_len)) {
            return 0;
        }
        if (!omc_bmff_read_cstr(bytes, size, &p, payload_end,
                                out_info->content_encoding,
                                sizeof(out_info->content_encoding),
                                &out_info->content_encoding_len)) {
            return 0;
        }
    } else if (out_info->item_type == OMC_BMFF_FOURCC('u', 'r', 'i', ' ')) {
        if (!omc_bmff_read_cstr(bytes, size, &p, payload_end,
                                out_info->uri_type, sizeof(out_info->uri_type),
                                &out_info->uri_type_len)) {
            return 0;
        }
    }

    return 1;
}

static const omc_bmff_item_info*
omc_bmff_find_item_info(const omc_bmff_item_info* items, omc_u32 item_count,
                        omc_u32 item_id)
{
    omc_u32 i;

    if (items == (const omc_bmff_item_info*)0) {
        return (const omc_bmff_item_info*)0;
    }
    for (i = 0U; i < item_count; ++i) {
        if (items[i].item_id == item_id) {
            return &items[i];
        }
    }
    return (const omc_bmff_item_info*)0;
}

static int
omc_bmff_collect_iinf_items(omc_bmff_ctx* ctx, const omc_bmff_box* iinf,
                            omc_bmff_item_info* out_items, omc_u32* out_count)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 version;
    omc_u32 entry_count;
    omc_u64 p;
    omc_u32 seen;
    omc_u32 item_cap;

    if (ctx == (omc_bmff_ctx*)0 || iinf == (const omc_bmff_box*)0
        || out_items == (omc_bmff_item_info*)0 || out_count == (omc_u32*)0) {
        return 0;
    }
    *out_count = 0U;

    payload_off = iinf->offset + iinf->header_size;
    payload_end = iinf->offset + iinf->size;
    if (payload_off + 6U > payload_end) {
        return 0;
    }

    version = ctx->bytes[(omc_size)payload_off];
    p       = payload_off + 4U;
    if (version == 0U) {
        omc_u16 entry_count16;

        if (!omc_bmff_read_u16be(ctx->bytes, ctx->size, p, &entry_count16)) {
            return 0;
        }
        entry_count = entry_count16;
        p += 2U;
    } else {
        if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, p, &entry_count)) {
            return 0;
        }
        p += 4U;
    }

    item_cap = ctx->opts.limits.max_item_infos;
    if (item_cap > 64U) {
        item_cap = 64U;
    }
    seen = 0U;
    while (p + 8U <= payload_end && seen < entry_count) {
        omc_bmff_box child;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, p, payload_end, &child)) {
            return 0;
        }
        if (child.type == OMC_BMFF_FOURCC('i', 'n', 'f', 'e')
            && *out_count < item_cap) {
            if (omc_bmff_parse_infe(ctx->bytes, ctx->size, &child,
                                    &out_items[*out_count])) {
                *out_count += 1U;
            }
        }
        p += child.size;
        if (child.size == 0U) {
            break;
        }
        seen += 1U;
    }

    ctx->res.item_infos = *out_count;
    return 1;
}

static const omc_bmff_ispe_prop*
omc_bmff_find_ispe(const omc_bmff_ispe_prop* props, omc_u32 count,
                   omc_u32 index)
{
    omc_u32 i;

    if (props == (const omc_bmff_ispe_prop*)0) {
        return (const omc_bmff_ispe_prop*)0;
    }
    for (i = 0U; i < count; ++i) {
        if (props[i].index == index) {
            return &props[i];
        }
    }
    return (const omc_bmff_ispe_prop*)0;
}

static const omc_bmff_u8_prop*
omc_bmff_find_u8_prop(const omc_bmff_u8_prop* props, omc_u32 count,
                      omc_u32 index)
{
    omc_u32 i;

    if (props == (const omc_bmff_u8_prop*)0) {
        return (const omc_bmff_u8_prop*)0;
    }
    for (i = 0U; i < count; ++i) {
        if (props[i].index == index) {
            return &props[i];
        }
    }
    return (const omc_bmff_u8_prop*)0;
}

static int
omc_bmff_is_primary_auxl_item(const omc_bmff_primary_props* props,
                              omc_u32 item_id)
{
    return omc_bmff_find_primary_auxl_index(props, item_id) != ~(omc_u32)0;
}

static const omc_bmff_auxc_prop*
omc_bmff_find_auxc_prop(const omc_bmff_auxc_prop* props, omc_u32 count,
                        omc_u32 index)
{
    omc_u32 i;

    if (props == (const omc_bmff_auxc_prop*)0) {
        return (const omc_bmff_auxc_prop*)0;
    }
    for (i = 0U; i < count; ++i) {
        if (props[i].index == index) {
            return &props[i];
        }
    }
    return (const omc_bmff_auxc_prop*)0;
}

static void
omc_bmff_append_prop_type(omc_bmff_prop_type* props, omc_u32* io_count,
                          omc_u32 cap, omc_u32 index, omc_u32 type)
{
    if (props == (omc_bmff_prop_type*)0 || io_count == (omc_u32*)0) {
        return;
    }
    if (*io_count >= cap) {
        return;
    }
    props[*io_count].index = index;
    props[*io_count].type  = type;
    *io_count += 1U;
}

static int
omc_bmff_find_prop_type(const omc_bmff_prop_type* props, omc_u32 count,
                        omc_u32 index, omc_u32* out_type)
{
    omc_u32 i;

    if (props == (const omc_bmff_prop_type*)0 || out_type == (omc_u32*)0) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (props[i].index == index) {
            *out_type = props[i].type;
            return 1;
        }
    }
    return 0;
}

static int
omc_bmff_collect_ipco_props(omc_bmff_ctx* ctx, const omc_bmff_box* ipco,
                            omc_bmff_ispe_prop* out_ispe,
                            omc_u32* out_ispe_count, omc_bmff_u8_prop* out_irot,
                            omc_u32* out_irot_count, omc_bmff_u8_prop* out_imir,
                            omc_u32* out_imir_count,
                            omc_bmff_auxc_prop* out_auxc,
                            omc_u32* out_auxc_count,
                            omc_bmff_prop_type* out_prop_types,
                            omc_u32* out_prop_type_count)
{
    omc_u64 off;
    omc_u64 end;
    omc_u32 prop_index;

    if (ctx == (omc_bmff_ctx*)0 || ipco == (const omc_bmff_box*)0
        || out_ispe == (omc_bmff_ispe_prop*)0 || out_ispe_count == (omc_u32*)0
        || out_irot == (omc_bmff_u8_prop*)0 || out_irot_count == (omc_u32*)0
        || out_imir == (omc_bmff_u8_prop*)0 || out_imir_count == (omc_u32*)0
        || out_auxc == (omc_bmff_auxc_prop*)0 || out_auxc_count == (omc_u32*)0
        || out_prop_types == (omc_bmff_prop_type*)0
        || out_prop_type_count == (omc_u32*)0) {
        return 0;
    }

    *out_ispe_count      = 0U;
    *out_irot_count      = 0U;
    *out_imir_count      = 0U;
    *out_auxc_count      = 0U;
    *out_prop_type_count = 0U;
    off                  = ipco->offset + ipco->header_size;
    end                  = ipco->offset + ipco->size;
    prop_index           = 1U;
    while (off + 8U <= end) {
        omc_bmff_box child;
        omc_u64 payload_off;
        omc_u64 payload_size;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, end, &child)) {
            return 0;
        }
        payload_off  = child.offset + child.header_size;
        payload_size = child.size - child.header_size;
        if (payload_off <= (omc_u64)ctx->size
            && payload_size <= ((omc_u64)ctx->size - payload_off)) {
            if (child.type == OMC_BMFF_FOURCC('i', 's', 'p', 'e')) {
                omc_u32 width;
                omc_u32 height;

                if (payload_size >= 12U
                    && omc_bmff_read_u32be(ctx->bytes, ctx->size,
                                           payload_off + 4U, &width)
                    && omc_bmff_read_u32be(ctx->bytes, ctx->size,
                                           payload_off + 8U, &height)
                    && *out_ispe_count < 64U) {
                    out_ispe[*out_ispe_count].index  = prop_index;
                    out_ispe[*out_ispe_count].width  = width;
                    out_ispe[*out_ispe_count].height = height;
                    *out_ispe_count += 1U;
                    omc_bmff_append_prop_type(
                        out_prop_types, out_prop_type_count, 64U, prop_index,
                        OMC_BMFF_FOURCC('i', 's', 'p', 'e'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('i', 'r', 'o', 't')) {
                if (payload_size >= 1U && *out_irot_count < 64U) {
                    out_irot[*out_irot_count].index = prop_index;
                    out_irot[*out_irot_count].value
                        = (omc_u8)(ctx->bytes[(omc_size)payload_off] & 0x03U);
                    *out_irot_count += 1U;
                    omc_bmff_append_prop_type(
                        out_prop_types, out_prop_type_count, 64U, prop_index,
                        OMC_BMFF_FOURCC('i', 'r', 'o', 't'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('i', 'm', 'i', 'r')) {
                if (payload_size >= 1U && *out_imir_count < 64U) {
                    out_imir[*out_imir_count].index = prop_index;
                    out_imir[*out_imir_count].value
                        = ctx->bytes[(omc_size)payload_off];
                    *out_imir_count += 1U;
                    omc_bmff_append_prop_type(
                        out_prop_types, out_prop_type_count, 64U, prop_index,
                        OMC_BMFF_FOURCC('i', 'm', 'i', 'r'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('c', 'o', 'l', 'r')) {
                if (payload_size >= 4U) {
                    omc_bmff_append_prop_type(
                        out_prop_types, out_prop_type_count, 64U, prop_index,
                        OMC_BMFF_FOURCC('c', 'o', 'l', 'r'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('a', 'u', 'x', 'C')) {
                if (payload_size >= 5U && *out_auxc_count < 64U) {
                    omc_u64 p;
                    omc_u64 e;

                    p = payload_off + 4U;
                    e = payload_off + payload_size;
                    while (p < e && ctx->bytes[(omc_size)p] != 0U) {
                        p += 1U;
                    }
                    if (p < e) {
                        omc_u64 type_off;
                        omc_u64 type_len_u64;

                        type_off     = payload_off + 4U;
                        type_len_u64 = p - type_off;
                        if (type_len_u64 > 0U && type_off <= (omc_u64)ctx->size
                            && type_len_u64
                                   <= ((omc_u64)ctx->size - type_off)) {
                            omc_bmff_auxc_prop* prop;
                            omc_size type_len;

                            prop = &out_auxc[*out_auxc_count];
                            memset(prop, 0, sizeof(*prop));
                            prop->index = prop_index;
                            type_len    = (omc_size)type_len_u64;
                            if (type_len > sizeof(prop->aux_type)) {
                                type_len = sizeof(prop->aux_type);
                            }
                            memcpy(prop->aux_type,
                                   ctx->bytes + (omc_size)type_off, type_len);
                            prop->aux_type_len = (omc_u16)type_len;
                            prop->semantic     = omc_bmff_classify_auxc_type(
                                prop->aux_type, prop->aux_type_len);
                            if (p + 1U < e) {
                                omc_size subtype_len;

                                subtype_len = (omc_size)(e - (p + 1U));
                                if (subtype_len > sizeof(prop->aux_subtype)) {
                                    subtype_len = sizeof(prop->aux_subtype);
                                }
                                memcpy(prop->aux_subtype,
                                       ctx->bytes + (omc_size)(p + 1U),
                                       subtype_len);
                                prop->aux_subtype_len = (omc_u16)subtype_len;
                            }
                            *out_auxc_count += 1U;
                            omc_bmff_append_prop_type(
                                out_prop_types, out_prop_type_count, 64U,
                                prop_index,
                                OMC_BMFF_FOURCC('a', 'u', 'x', 'C'));
                        }
                    }
                }
            } else if (child.type == OMC_BMFF_FOURCC('p', 'a', 's', 'p')) {
                if (payload_size >= 8U) {
                    omc_bmff_append_prop_type(
                        out_prop_types, out_prop_type_count, 64U, prop_index,
                        OMC_BMFF_FOURCC('p', 'a', 's', 'p'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('p', 'i', 'x', 'i')) {
                if (payload_size >= 5U) {
                    omc_u8 channel_count;

                    channel_count = ctx->bytes[(omc_size)(payload_off + 4U)];
                    if (channel_count != 0U
                        && (omc_u64)channel_count <= payload_size - 5U) {
                        omc_bmff_append_prop_type(
                            out_prop_types, out_prop_type_count, 64U,
                            prop_index, OMC_BMFF_FOURCC('p', 'i', 'x', 'i'));
                    }
                }
            } else if (child.type == OMC_BMFF_FOURCC('c', 'l', 'a', 'p')) {
                if (payload_size >= 32U) {
                    omc_bmff_append_prop_type(
                        out_prop_types, out_prop_type_count, 64U, prop_index,
                        OMC_BMFF_FOURCC('c', 'l', 'a', 'p'));
                }
            }
        }

        off += child.size;
        if (child.size == 0U) {
            break;
        }
        if (prop_index == ~(omc_u32)0) {
            break;
        }
        prop_index += 1U;
    }
    return 1;
}

static void
omc_bmff_append_ipma_association(omc_bmff_primary_props* props, omc_u32 item_id,
                                 omc_u32 property_index, omc_u8 essential,
                                 int have_property_type, omc_u32 property_type)
{
    omc_bmff_ipma_assoc* assoc;

    if (props == (omc_bmff_primary_props*)0 || property_index == 0U) {
        return;
    }
    if (props->ipma_association_total != ~(omc_u32)0) {
        props->ipma_association_total += 1U;
    }
    if (props->ipma_association_count >= 512U) {
        props->ipma_truncated = 1;
        return;
    }

    assoc          = &props->ipma_associations[props->ipma_association_count];
    assoc->item_id = item_id;
    assoc->property_index     = property_index;
    assoc->property_type      = property_type;
    assoc->essential          = essential;
    assoc->have_property_type = have_property_type;
    props->ipma_association_count += 1U;
}

static int
omc_bmff_apply_ipma_primary(omc_bmff_ctx* ctx, const omc_bmff_box* ipma,
                            omc_u32 primary_item_id,
                            const omc_bmff_ispe_prop* ispe, omc_u32 ispe_count,
                            const omc_bmff_u8_prop* irot, omc_u32 irot_count,
                            const omc_bmff_u8_prop* imir, omc_u32 imir_count,
                            const omc_bmff_auxc_prop* auxc, omc_u32 auxc_count,
                            const omc_bmff_prop_type* prop_types,
                            omc_u32 prop_type_count,
                            omc_bmff_primary_props* out_props)
{
    omc_u64 payload_off;
    omc_u64 payload_size;
    omc_u64 end;
    omc_u8 version;
    omc_u32 entry_count;
    omc_u64 off;
    omc_u32 i;

    if (ctx == (omc_bmff_ctx*)0 || ipma == (const omc_bmff_box*)0
        || out_props == (omc_bmff_primary_props*)0) {
        return 0;
    }

    payload_off  = ipma->offset + ipma->header_size;
    payload_size = ipma->size - ipma->header_size;
    end          = payload_off + payload_size;
    if (payload_size < 8U) {
        return 0;
    }

    version = ctx->bytes[(omc_size)payload_off];
    if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, payload_off + 4U,
                             &entry_count)) {
        return 0;
    }

    out_props->have_item_id = 1;
    out_props->item_id      = primary_item_id;
    off                     = payload_off + 8U;
    for (i = 0U; i < entry_count; ++i) {
        omc_u32 item_id;
        omc_u8 assoc_count;
        omc_u32 j;

        if (version < 1U) {
            omc_u16 item_id16;

            if (!omc_bmff_read_u16be(ctx->bytes, ctx->size, off, &item_id16)) {
                return 0;
            }
            item_id = item_id16;
            off += 2U;
        } else {
            if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, off, &item_id)) {
                return 0;
            }
            off += 4U;
        }
        if (off + 1U > end) {
            return 0;
        }
        assoc_count = ctx->bytes[(omc_size)off];
        off += 1U;

        for (j = 0U; j < assoc_count; ++j) {
            omc_u32 prop_index;
            omc_u8 essential;

            if (version < 1U) {
                omc_u8 v;

                if (off + 1U > end) {
                    return 0;
                }
                v = ctx->bytes[(omc_size)off];
                off += 1U;
                essential  = (omc_u8)((v & 0x80U) != 0U ? 1U : 0U);
                prop_index = (omc_u32)(v & 0x7FU);
            } else {
                omc_u16 v16;

                if (off + 2U > end
                    || !omc_bmff_read_u16be(ctx->bytes, ctx->size, off, &v16)) {
                    return 0;
                }
                off += 2U;
                essential  = (omc_u8)((v16 & 0x8000U) != 0U ? 1U : 0U);
                prop_index = (omc_u32)(v16 & 0x7FFFU);
            }

            if (prop_index != 0U) {
                const omc_bmff_ispe_prop* ispe_prop;
                const omc_bmff_u8_prop* rot_prop;
                const omc_bmff_u8_prop* mir_prop;
                const omc_bmff_auxc_prop* auxc_prop;
                omc_u32 property_type;
                int have_property_type;
                int is_primary;
                int is_primary_aux;

                property_type = 0U;
                have_property_type
                    = omc_bmff_find_prop_type(prop_types, prop_type_count,
                                              prop_index, &property_type);
                omc_bmff_append_ipma_association(out_props, item_id, prop_index,
                                                 essential, have_property_type,
                                                 property_type);
                ispe_prop = omc_bmff_find_ispe(ispe, ispe_count, prop_index);
                rot_prop  = omc_bmff_find_u8_prop(irot, irot_count, prop_index);
                mir_prop  = omc_bmff_find_u8_prop(imir, imir_count, prop_index);
                auxc_prop = omc_bmff_find_auxc_prop(auxc, auxc_count,
                                                    prop_index);
                is_primary     = (item_id == primary_item_id);
                is_primary_aux = (!is_primary)
                                 && omc_bmff_is_primary_auxl_item(out_props,
                                                                  item_id);

                if (is_primary && ispe_prop != (const omc_bmff_ispe_prop*)0) {
                    out_props->have_width_height = 1;
                    out_props->width             = ispe_prop->width;
                    out_props->height            = ispe_prop->height;
                }
                if (is_primary && rot_prop != (const omc_bmff_u8_prop*)0) {
                    out_props->have_rotation = 1;
                    out_props->rotation_degrees
                        = (omc_u16)((omc_u16)rot_prop->value * 90U);
                }
                if (is_primary && mir_prop != (const omc_bmff_u8_prop*)0) {
                    out_props->have_mirror = 1;
                    out_props->mirror      = mir_prop->value;
                }
                if (auxc_prop != (const omc_bmff_auxc_prop*)0) {
                    omc_bmff_set_aux_item_semantic(out_props, item_id,
                                                   auxc_prop->semantic);
                    if (auxc_prop->aux_type_len != 0U) {
                        omc_bmff_set_aux_item_type(out_props, item_id,
                                                   auxc_prop->aux_type,
                                                   auxc_prop->aux_type_len);
                    }
                    if (auxc_prop->aux_subtype_len != 0U) {
                        omc_bmff_set_aux_item_subtype(
                            out_props, item_id, auxc_prop->aux_subtype,
                            auxc_prop->aux_subtype_len);
                    }
                    if (is_primary_aux) {
                        omc_bmff_set_primary_auxl_semantic(out_props, item_id,
                                                           auxc_prop->semantic);
                    }
                }
            }
        }
    }
    return 1;
}

static void
omc_bmff_note_primary_ref(omc_bmff_primary_props* props, omc_u32 ref_type,
                          omc_u32 to_item_id)
{
    if (props == (omc_bmff_primary_props*)0) {
        return;
    }

    if (ref_type == OMC_BMFF_FOURCC('a', 'u', 'x', 'l')) {
        props->auxl_edge_count += 1U;
        omc_bmff_push_primary_item_id(props->primary_auxl_item_ids,
                                      &props->primary_auxl_count, 32U,
                                      to_item_id);
    } else if (ref_type == OMC_BMFF_FOURCC('d', 'i', 'm', 'g')) {
        props->dimg_edge_count += 1U;
        omc_bmff_push_primary_item_id(props->primary_dimg_item_ids,
                                      &props->primary_dimg_count, 32U,
                                      to_item_id);
    } else if (ref_type == OMC_BMFF_FOURCC('t', 'h', 'm', 'b')) {
        props->thmb_edge_count += 1U;
        omc_bmff_push_primary_item_id(props->primary_thmb_item_ids,
                                      &props->primary_thmb_count, 32U,
                                      to_item_id);
    } else if (ref_type == OMC_BMFF_FOURCC('c', 'd', 's', 'c')) {
        props->cdsc_edge_count += 1U;
        omc_bmff_push_primary_item_id(props->primary_cdsc_item_ids,
                                      &props->primary_cdsc_count, 32U,
                                      to_item_id);
    }
}

static int
omc_bmff_collect_iref_edges(omc_bmff_ctx* ctx, const omc_bmff_box* iref,
                            omc_bmff_primary_props* out_props)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 version;
    omc_u64 off;

    if (ctx == (omc_bmff_ctx*)0 || iref == (const omc_bmff_box*)0
        || out_props == (omc_bmff_primary_props*)0) {
        return 0;
    }

    payload_off = iref->offset + iref->header_size;
    payload_end = iref->offset + iref->size;
    if (payload_off + 4U > payload_end) {
        return 0;
    }

    version = ctx->bytes[(omc_size)payload_off];
    if (version > 1U) {
        return 0;
    }
    off = payload_off + 4U;
    while (off + 8U <= payload_end) {
        omc_bmff_box child;
        omc_u64 p;
        omc_u64 child_end;
        omc_u32 from_item_id;
        omc_u16 ref_count;
        omc_u32 i;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, payload_end,
                                &child)) {
            return 0;
        }

        p         = child.offset + child.header_size;
        child_end = child.offset + child.size;
        if (version == 0U) {
            omc_u16 from16;

            if (p + 2U > child_end
                || !omc_bmff_read_u16be(ctx->bytes, ctx->size, p, &from16)) {
                return 0;
            }
            from_item_id = from16;
            p += 2U;
        } else {
            if (p + 4U > child_end
                || !omc_bmff_read_u32be(ctx->bytes, ctx->size, p,
                                        &from_item_id)) {
                return 0;
            }
            p += 4U;
        }
        if (p + 2U > child_end
            || !omc_bmff_read_u16be(ctx->bytes, ctx->size, p, &ref_count)) {
            return 0;
        }
        p += 2U;

        for (i = 0U; i < ref_count; ++i) {
            omc_u32 to_item_id;

            if (version == 0U) {
                omc_u16 to16;

                if (p + 2U > child_end
                    || !omc_bmff_read_u16be(ctx->bytes, ctx->size, p, &to16)) {
                    return 0;
                }
                to_item_id = to16;
                p += 2U;
            } else {
                if (p + 4U > child_end
                    || !omc_bmff_read_u32be(ctx->bytes, ctx->size, p,
                                            &to_item_id)) {
                    return 0;
                }
                p += 4U;
            }

            if (out_props->edge_total != ~(omc_u32)0) {
                out_props->edge_total += 1U;
            }
            if (out_props->edge_count < 128U) {
                out_props->edges[out_props->edge_count].ref_type = child.type;
                out_props->edges[out_props->edge_count].from_item_id
                    = from_item_id;
                out_props->edges[out_props->edge_count].to_item_id = to_item_id;
                out_props->edge_count += 1U;
            } else {
                out_props->edge_truncated = 1;
            }
            if (out_props->have_item_id && from_item_id == out_props->item_id) {
                omc_bmff_note_primary_ref(out_props, child.type, to_item_id);
            }
        }

        off += child.size;
        if (child.size == 0U) {
            break;
        }
    }
    return 1;
}

static int
omc_bmff_collect_item_groups(omc_bmff_ctx* ctx, const omc_bmff_box* grpl,
                             omc_bmff_primary_props* out_props)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u64 off;

    if (ctx == (omc_bmff_ctx*)0 || grpl == (const omc_bmff_box*)0
        || out_props == (omc_bmff_primary_props*)0) {
        return 0;
    }

    payload_off = grpl->offset + grpl->header_size;
    payload_end = grpl->offset + grpl->size;
    if (payload_off > payload_end || payload_end > (omc_u64)ctx->size) {
        return 0;
    }

    off = payload_off;
    while (off + 8U <= payload_end) {
        omc_bmff_box child;
        omc_u64 child_payload_off;
        omc_u64 child_payload_end;
        omc_u64 p;
        omc_u8 version;
        omc_u32 group_id;
        omc_u32 entity_count;
        omc_u32 i;
        omc_bmff_item_group* group;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, payload_end,
                                &child)) {
            return 0;
        }

        child_payload_off = child.offset + child.header_size;
        child_payload_end = child.offset + child.size;
        if (child_payload_off > child_payload_end
            || child_payload_end > (omc_u64)ctx->size) {
            return 0;
        }
        if (child_payload_off + 12U > child_payload_end) {
            off += child.size;
            if (child.size == 0U) {
                break;
            }
            continue;
        }

        version = ctx->bytes[(omc_size)child_payload_off];
        if (version != 0U) {
            off += child.size;
            if (child.size == 0U) {
                break;
            }
            continue;
        }

        p = child_payload_off + 4U;
        if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, p, &group_id)) {
            return 0;
        }
        p += 4U;
        if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, p, &entity_count)) {
            return 0;
        }
        p += 4U;
        if (entity_count > (1U << 18)
            || (omc_u64)entity_count > ((child_payload_end - p) / 4U)) {
            return 0;
        }

        if (out_props->item_group_total == ~(omc_u32)0) {
            return 0;
        }
        out_props->item_group_total += 1U;

        group = (omc_bmff_item_group*)0;
        if (out_props->item_group_count < 64U) {
            group = &out_props->item_groups[out_props->item_group_count];
            memset(group, 0, sizeof(*group));
            group->group_type   = child.type;
            group->group_id     = group_id;
            group->entity_count = entity_count;
            out_props->item_group_count += 1U;
        } else {
            out_props->item_group_truncated = 1;
        }

        for (i = 0U; i < entity_count; ++i) {
            omc_u32 entity_id;

            if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, p, &entity_id)) {
                return 0;
            }
            p += 4U;

            if (group == (omc_bmff_item_group*)0) {
                continue;
            }
            if (out_props->have_item_id && entity_id == out_props->item_id) {
                group->contains_primary = 1;
            }
            if (group->entity_id_count < 64U) {
                group->entity_ids[group->entity_id_count] = entity_id;
                group->entity_id_count += 1U;
            } else {
                group->entity_truncated = 1;
            }
        }

        off += child.size;
        if (child.size == 0U) {
            break;
        }
    }

    return 1;
}

static int
omc_bmff_emit_item_info_fields(omc_bmff_ctx* ctx,
                               const omc_bmff_item_info* items,
                               omc_u32 item_count)
{
    omc_u32 i;
    omc_bmff_item_semantic_counts semantic_counts;

    if (ctx == (omc_bmff_ctx*)0 || items == (const omc_bmff_item_info*)0) {
        return 0;
    }
    if (item_count == 0U) {
        return 1;
    }
    if (!omc_bmff_emit_u32_field(ctx, "item.info_count", item_count)) {
        return 0;
    }

    memset(&semantic_counts, 0, sizeof(semantic_counts));
    for (i = 0U; i < item_count; ++i) {
        omc_bmff_item_semantic semantic;

        if (!omc_bmff_emit_u32_field(ctx, "item.id", items[i].item_id)
            || !omc_bmff_emit_u16_field(ctx, "item.protection_index",
                                        items[i].protection_index)) {
            return 0;
        }
        if (items[i].have_type) {
            char type_name[11];
            omc_u16 type_name_len;

            if (!omc_bmff_emit_u32_field(ctx, "item.type", items[i].item_type)
                || !omc_bmff_fourcc_display_name(items[i].item_type, type_name,
                                                 &type_name_len)
                || !omc_bmff_emit_text_field(ctx, "item.type_name", type_name,
                                             type_name_len)) {
                return 0;
            }
        }
        if (items[i].name_len != 0U
            && !omc_bmff_emit_text_field(ctx, "item.name", items[i].name,
                                         items[i].name_len)) {
            return 0;
        }
        if (items[i].content_type_len != 0U
            && !omc_bmff_emit_text_field(ctx, "item.content_type",
                                         items[i].content_type,
                                         items[i].content_type_len)) {
            return 0;
        }
        if (items[i].content_encoding_len != 0U
            && !omc_bmff_emit_text_field(ctx, "item.content_encoding",
                                         items[i].content_encoding,
                                         items[i].content_encoding_len)) {
            return 0;
        }
        if (items[i].uri_type_len != 0U
            && !omc_bmff_emit_text_field(ctx, "item.uri_type",
                                         items[i].uri_type,
                                         items[i].uri_type_len)) {
            return 0;
        }

        semantic = omc_bmff_classify_item_semantic(&items[i]);
        omc_bmff_count_item_semantic(semantic, &semantic_counts);
        if (omc_bmff_item_semantic_is_known(semantic)) {
            const char* semantic_name;

            semantic_name = omc_bmff_item_semantic_name(semantic);
            if (!omc_bmff_emit_text_field(ctx, "item.semantic", semantic_name,
                                          (omc_u16)strlen(semantic_name))) {
                return 0;
            }
        }
    }
    if (!omc_bmff_emit_item_semantic_counts(ctx, &semantic_counts)) {
        return 0;
    }
    return 1;
}

static int
omc_bmff_emit_ipma_fields(omc_bmff_ctx* ctx,
                          const omc_bmff_primary_props* props)
{
    omc_u32 i;
    omc_u32 take_count;

    if (ctx == (omc_bmff_ctx*)0) {
        return 0;
    }
    if (props == (const omc_bmff_primary_props*)0
        || props->ipma_association_total == 0U) {
        return 1;
    }

    if (!omc_bmff_emit_u32_field(ctx, "ipma.association_count",
                                 props->ipma_association_total)) {
        return 0;
    }
    if (props->ipma_truncated
        && !omc_bmff_emit_u8_field(ctx, "ipma.association_truncated", 1U)) {
        return 0;
    }

    take_count = props->ipma_association_count;
    if (take_count > 512U) {
        take_count = 512U;
    }
    for (i = 0U; i < take_count; ++i) {
        const omc_bmff_ipma_assoc* assoc;

        assoc = &props->ipma_associations[i];
        if (!omc_bmff_emit_u32_field(ctx, "ipma.item_id", assoc->item_id)
            || !omc_bmff_emit_u32_field(ctx, "ipma.property_index",
                                        assoc->property_index)
            || !omc_bmff_emit_u8_field(ctx, "ipma.essential",
                                       assoc->essential)) {
            return 0;
        }
        if (assoc->have_property_type) {
            char type_name[11];
            omc_u16 type_name_len;

            if (!omc_bmff_emit_u32_field(ctx, "ipma.property_type",
                                         assoc->property_type)
                || !omc_bmff_fourcc_display_name(assoc->property_type,
                                                 type_name, &type_name_len)
                || !omc_bmff_emit_text_field(ctx, "ipma.property_type_name",
                                             type_name, type_name_len)) {
                return 0;
            }
        }
    }
    return 1;
}

static int
omc_bmff_emit_item_group_type_summary(omc_bmff_ctx* ctx, omc_u32 group_type,
                                      const char* group_token,
                                      const omc_bmff_item_group* groups,
                                      omc_u32 group_count)
{
    char prefix[32];
    char field[64];
    omc_u32 i;
    omc_u32 type_count;

    if (ctx == (omc_bmff_ctx*)0 || group_token == (const char*)0
        || groups == (const omc_bmff_item_group*)0) {
        return 0;
    }
    if (!omc_bmff_make_field2(prefix, sizeof(prefix), "item_group",
                              group_token)) {
        return 0;
    }

    type_count = 0U;
    for (i = 0U; i < group_count; ++i) {
        if (groups[i].group_type == group_type) {
            type_count += 1U;
        }
    }
    if (type_count == 0U) {
        return 1;
    }

    if (!omc_bmff_make_field2(field, sizeof(field), prefix, "count")
        || !omc_bmff_emit_u32_field(ctx, field, type_count)) {
        return 0;
    }

    for (i = 0U; i < group_count; ++i) {
        const omc_bmff_item_group* group;
        omc_u32 j;

        group = &groups[i];
        if (group->group_type != group_type) {
            continue;
        }
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "id")
            || !omc_bmff_emit_u32_field(ctx, field, group->group_id)
            || !omc_bmff_make_field2(field, sizeof(field), prefix,
                                     "entity_count")
            || !omc_bmff_emit_u32_field(ctx, field, group->entity_count)) {
            return 0;
        }
        for (j = 0U; j < group->entity_id_count && j < 64U; ++j) {
            if (!omc_bmff_make_field2(field, sizeof(field), prefix, "entity_id")
                || !omc_bmff_emit_u32_field(ctx, field, group->entity_ids[j])) {
                return 0;
            }
        }
        if (group->entity_truncated
            && (!omc_bmff_make_field2(field, sizeof(field), prefix,
                                      "entity_truncated")
                || !omc_bmff_emit_u8_field(ctx, field, 1U))) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_emit_item_group_fields(omc_bmff_ctx* ctx,
                                const omc_bmff_primary_props* props)
{
    omc_u32 dynamic_types[32];
    char dynamic_tokens[32][5];
    omc_u32 dynamic_count;
    omc_u32 primary_group_count;
    omc_u32 take_count;
    omc_u32 i;

    if (ctx == (omc_bmff_ctx*)0) {
        return 0;
    }
    if (props == (const omc_bmff_primary_props*)0
        || props->item_group_total == 0U) {
        return 1;
    }

    if (!omc_bmff_emit_u32_field(ctx, "item_group.count",
                                 props->item_group_total)) {
        return 0;
    }
    if (props->item_group_truncated
        && !omc_bmff_emit_u8_field(ctx, "item_group.truncated", 1U)) {
        return 0;
    }

    dynamic_count       = 0U;
    primary_group_count = 0U;
    take_count          = props->item_group_count;
    if (take_count > 64U) {
        take_count = 64U;
    }
    for (i = 0U; i < take_count; ++i) {
        const omc_bmff_item_group* group;
        char type_name[11];
        omc_u16 type_name_len;
        omc_u32 j;
        char token[5];

        group = &props->item_groups[i];
        if (!omc_bmff_emit_u32_field(ctx, "item_group.type", group->group_type)
            || !omc_bmff_fourcc_display_name(group->group_type, type_name,
                                             &type_name_len)
            || !omc_bmff_emit_text_field(ctx, "item_group.type_name", type_name,
                                         type_name_len)
            || !omc_bmff_emit_u32_field(ctx, "item_group.id", group->group_id)
            || !omc_bmff_emit_u32_field(ctx, "item_group.entity_count",
                                        group->entity_count)) {
            return 0;
        }
        for (j = 0U; j < group->entity_id_count && j < 64U; ++j) {
            if (!omc_bmff_emit_u32_field(ctx, "item_group.entity_id",
                                         group->entity_ids[j])) {
                return 0;
            }
        }
        if (group->entity_truncated
            && !omc_bmff_emit_u8_field(ctx, "item_group.entity_truncated", 1U)) {
            return 0;
        }

        if (omc_bmff_fourcc_token(group->group_type, token)) {
            omc_u32 k;
            int found;

            found = 0;
            for (k = 0U; k < dynamic_count; ++k) {
                if (dynamic_types[k] == group->group_type) {
                    found = 1;
                    break;
                }
            }
            if (!found && dynamic_count < 32U) {
                dynamic_types[dynamic_count] = group->group_type;
                memcpy(dynamic_tokens[dynamic_count], token, 5U);
                dynamic_count += 1U;
            }
        }

        if (props->have_item_id && group->contains_primary) {
            primary_group_count += 1U;
        }
    }

    for (i = 0U; i < dynamic_count; ++i) {
        if (!omc_bmff_emit_item_group_type_summary(ctx, dynamic_types[i],
                                                   dynamic_tokens[i],
                                                   props->item_groups,
                                                   take_count)) {
            return 0;
        }
    }

    if (!props->have_item_id || primary_group_count == 0U) {
        return 1;
    }
    if (!omc_bmff_emit_u32_field(ctx, "primary.item_group_count",
                                 primary_group_count)) {
        return 0;
    }
    for (i = 0U; i < take_count; ++i) {
        const omc_bmff_item_group* group;
        char type_name[11];
        omc_u16 type_name_len;
        omc_u32 j;

        group = &props->item_groups[i];
        if (!group->contains_primary) {
            continue;
        }
        if (!omc_bmff_emit_u32_field(ctx, "primary.item_group_type",
                                     group->group_type)
            || !omc_bmff_fourcc_display_name(group->group_type, type_name,
                                             &type_name_len)
            || !omc_bmff_emit_text_field(ctx, "primary.item_group_type_name",
                                         type_name, type_name_len)
            || !omc_bmff_emit_u32_field(ctx, "primary.item_group_id",
                                        group->group_id)
            || !omc_bmff_emit_u32_field(ctx, "primary.item_group_entity_count",
                                        group->entity_count)) {
            return 0;
        }
        for (j = 0U; j < group->entity_id_count && j < 64U; ++j) {
            if (!omc_bmff_emit_u32_field(ctx, "primary.item_group_entity_id",
                                         group->entity_ids[j])) {
                return 0;
            }
        }
        if (group->entity_truncated
            && !omc_bmff_emit_u8_field(ctx,
                                       "primary.item_group_entity_truncated",
                                       1U)) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_emit_primary_rel_ids(omc_bmff_ctx* ctx, const char* field,
                              const omc_u32* item_ids, omc_u32 count,
                              omc_u32 cap)
{
    omc_u32 i;
    omc_u32 take_count;

    if (ctx == (omc_bmff_ctx*)0 || field == (const char*)0
        || item_ids == (const omc_u32*)0) {
        return 0;
    }

    take_count = count;
    if (take_count > cap) {
        take_count = cap;
    }
    for (i = 0U; i < take_count; ++i) {
        if (!omc_bmff_emit_u32_field(ctx, field, item_ids[i])) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_emit_iref_fields(omc_bmff_ctx* ctx,
                          const omc_bmff_primary_props* props)
{
    omc_u32 i;
    omc_u32 take_count;
    omc_u32 group_types[128];
    omc_u32 group_count;

    if (ctx == (omc_bmff_ctx*)0) {
        return 0;
    }
    if (props == (const omc_bmff_primary_props*)0 || props->edge_total == 0U) {
        return 1;
    }

    if (!omc_bmff_emit_u32_field(ctx, "iref.edge_count", props->edge_total)) {
        return 0;
    }
    if (props->edge_truncated
        && !omc_bmff_emit_u8_field(ctx, "iref.edge_truncated", 1U)) {
        return 0;
    }

    take_count = props->edge_count;
    if (take_count > 128U) {
        take_count = 128U;
    }
    group_count = 0U;
    for (i = 0U; i < take_count; ++i) {
        const omc_bmff_iref_edge* edge;
        char token[5];
        char field[32];
        const omc_bmff_aux_item_info* aux_item;

        edge = &props->edges[i];
        if (!omc_bmff_emit_u32_field(ctx, "iref.ref_type", edge->ref_type)
            || !omc_bmff_emit_u32_field(ctx, "iref.from_item_id",
                                        edge->from_item_id)
            || !omc_bmff_emit_u32_field(ctx, "iref.to_item_id",
                                        edge->to_item_id)) {
            return 0;
        }

        if (omc_bmff_fourcc_token(edge->ref_type, token)) {
            if (!omc_bmff_emit_text_field(ctx, "iref.ref_type_name", token, 4U)
                || !omc_bmff_make_field2(field, sizeof(field), "iref", token)
                || !omc_bmff_make_field2(field, sizeof(field), field,
                                         "from_item_id")
                || !omc_bmff_emit_u32_field(ctx, field, edge->from_item_id)
                || !omc_bmff_make_field2(field, sizeof(field), "iref", token)
                || !omc_bmff_make_field2(field, sizeof(field), field,
                                         "to_item_id")
                || !omc_bmff_emit_u32_field(ctx, field, edge->to_item_id)
                || !omc_bmff_push_unique_item_id(group_types, &group_count,
                                                 128U, edge->ref_type)) {
                return 0;
            }
        }

        if (edge->ref_type != OMC_BMFF_FOURCC('a', 'u', 'x', 'l')) {
            continue;
        }

        aux_item = omc_bmff_find_aux_item(props, edge->to_item_id);
        if (!omc_bmff_emit_text_field(
                ctx, "iref.auxl.semantic",
                omc_bmff_aux_semantic_name(
                    aux_item != (const omc_bmff_aux_item_info*)0
                        ? aux_item->semantic
                        : OMC_BMFF_AUX_UNKNOWN),
                (omc_u16)strlen(omc_bmff_aux_semantic_name(
                    aux_item != (const omc_bmff_aux_item_info*)0
                        ? aux_item->semantic
                        : OMC_BMFF_AUX_UNKNOWN)))) {
            return 0;
        }
        if (aux_item == (const omc_bmff_aux_item_info*)0) {
            continue;
        }
        if (aux_item->aux_type_len != 0U
            && !omc_bmff_emit_text_field(ctx, "iref.auxl.type",
                                         aux_item->aux_type,
                                         aux_item->aux_type_len)) {
            return 0;
        }
        if (!omc_bmff_emit_aux_subtype_fields(ctx, "iref.auxl",
                                              aux_item->aux_subtype,
                                              aux_item->aux_subtype_len, 0)) {
            return 0;
        }
    }

    if (props->edge_truncated) {
        return 1;
    }

    if (!omc_bmff_emit_iref_item_summary(ctx, props->edges, take_count, 0U, 0,
                                         "iref", (const char*)0)) {
        return 0;
    }

    for (i = 0U; i < group_count; ++i) {
        char token[5];
        char prefix[16];
        char graph_prefix[24];
        char edge_field[32];
        omc_u32 j;
        omc_u32 group_edge_count;

        if (!omc_bmff_fourcc_token(group_types[i], token)) {
            continue;
        }
        if (!omc_bmff_make_field2(prefix, sizeof(prefix), "iref", token)
            || !omc_bmff_make_field2(graph_prefix, sizeof(graph_prefix),
                                     "iref.graph", token)) {
            continue;
        }

        group_edge_count = 0U;
        for (j = 0U; j < take_count; ++j) {
            if (props->edges[j].ref_type == group_types[i]) {
                group_edge_count += 1U;
            }
        }
        if (!omc_bmff_make_field2(edge_field, sizeof(edge_field), prefix,
                                  "edge_count")
            || !omc_bmff_emit_u32_field(ctx, edge_field, group_edge_count)) {
            return 0;
        }
        if (!omc_bmff_emit_iref_item_summary(ctx, props->edges, take_count,
                                             group_types[i], 1, prefix,
                                             graph_prefix)) {
            return 0;
        }
    }

    return 1;
}

static int
omc_bmff_emit_aux_fields(omc_bmff_ctx* ctx, const omc_bmff_primary_props* props)
{
    omc_u32 i;
    omc_u32 take_count;
    omc_u32 alpha_count;
    omc_u32 depth_count;
    omc_u32 disparity_count;
    omc_u32 matte_count;

    if (ctx == (omc_bmff_ctx*)0) {
        return 0;
    }
    if (props == (const omc_bmff_primary_props*)0
        || props->aux_item_count == 0U) {
        return 1;
    }

    take_count = props->aux_item_count;
    if (take_count > 64U) {
        take_count = 64U;
    }
    alpha_count     = 0U;
    depth_count     = 0U;
    disparity_count = 0U;
    matte_count     = 0U;
    for (i = 0U; i < take_count; ++i) {
        if (props->aux_items[i].semantic == OMC_BMFF_AUX_ALPHA) {
            alpha_count += 1U;
        } else if (props->aux_items[i].semantic == OMC_BMFF_AUX_DEPTH) {
            depth_count += 1U;
        } else if (props->aux_items[i].semantic == OMC_BMFF_AUX_DISPARITY) {
            disparity_count += 1U;
        } else if (props->aux_items[i].semantic == OMC_BMFF_AUX_MATTE) {
            matte_count += 1U;
        }
    }
    if (!omc_bmff_emit_u32_field(ctx, "aux.item_count", take_count)) {
        return 0;
    }
    if (alpha_count != 0U
        && !omc_bmff_emit_u32_field(ctx, "aux.alpha_count", alpha_count)) {
        return 0;
    }
    if (depth_count != 0U
        && !omc_bmff_emit_u32_field(ctx, "aux.depth_count", depth_count)) {
        return 0;
    }
    if (disparity_count != 0U
        && !omc_bmff_emit_u32_field(ctx, "aux.disparity_count",
                                    disparity_count)) {
        return 0;
    }
    if (matte_count != 0U
        && !omc_bmff_emit_u32_field(ctx, "aux.matte_count", matte_count)) {
        return 0;
    }
    for (i = 0U; i < take_count; ++i) {
        if (!omc_bmff_emit_u32_field(ctx, "aux.item_id",
                                     props->aux_items[i].item_id)
            || !omc_bmff_emit_text_field(
                ctx, "aux.semantic",
                omc_bmff_aux_semantic_name(props->aux_items[i].semantic),
                (omc_u16)strlen(omc_bmff_aux_semantic_name(
                    props->aux_items[i].semantic)))) {
            return 0;
        }
        if (props->aux_items[i].aux_type_len != 0U
            && !omc_bmff_emit_text_field(ctx, "aux.type",
                                         props->aux_items[i].aux_type,
                                         props->aux_items[i].aux_type_len)) {
            return 0;
        }
        if (!omc_bmff_emit_aux_subtype_fields(
                ctx, "aux", props->aux_items[i].aux_subtype,
                props->aux_items[i].aux_subtype_len, 1)) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_emit_primary_linked_fields(omc_bmff_ctx* ctx,
                                    const omc_bmff_item_info* items,
                                    omc_u32 item_count,
                                    const omc_bmff_primary_props* props)
{
    omc_u32 item_ids[128];
    const char* roles[128];
    const omc_bmff_item_info* info;
    omc_u32 count;
    omc_u32 i;

    if (ctx == (omc_bmff_ctx*)0 || props == (const omc_bmff_primary_props*)0) {
        return 0;
    }

    count = 0U;
    for (i = 0U; i < props->primary_auxl_count && i < 32U; ++i) {
        omc_u32 j;
        int found;
        const char* role_name;

        role_name = omc_bmff_primary_linked_role_for_aux(
            props->primary_auxl_semantics[i]);
        found = 0;
        for (j = 0U; j < count; ++j) {
            if (item_ids[j] == props->primary_auxl_item_ids[i]
                && strcmp(roles[j], role_name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && count < 128U) {
            item_ids[count] = props->primary_auxl_item_ids[i];
            roles[count]    = role_name;
            count += 1U;
        }
    }
    for (i = 0U; i < props->primary_dimg_count && i < 32U; ++i) {
        omc_u32 j;
        int found;

        found = 0;
        for (j = 0U; j < count; ++j) {
            if (item_ids[j] == props->primary_dimg_item_ids[i]
                && strcmp(roles[j], "derived") == 0) {
                found = 1;
                break;
            }
        }
        if (!found && count < 128U) {
            item_ids[count] = props->primary_dimg_item_ids[i];
            roles[count]    = "derived";
            count += 1U;
        }
    }
    for (i = 0U; i < props->primary_thmb_count && i < 32U; ++i) {
        omc_u32 j;
        int found;

        found = 0;
        for (j = 0U; j < count; ++j) {
            if (item_ids[j] == props->primary_thmb_item_ids[i]
                && strcmp(roles[j], "thumbnail") == 0) {
                found = 1;
                break;
            }
        }
        if (!found && count < 128U) {
            item_ids[count] = props->primary_thmb_item_ids[i];
            roles[count]    = "thumbnail";
            count += 1U;
        }
    }
    for (i = 0U; i < props->primary_cdsc_count && i < 32U; ++i) {
        omc_u32 j;
        int found;

        found = 0;
        for (j = 0U; j < count; ++j) {
            if (item_ids[j] == props->primary_cdsc_item_ids[i]
                && strcmp(roles[j], "content_description") == 0) {
                found = 1;
                break;
            }
        }
        if (!found && count < 128U) {
            item_ids[count] = props->primary_cdsc_item_ids[i];
            roles[count]    = "content_description";
            count += 1U;
        }
    }

    if (count == 0U) {
        return 1;
    }

    if (!omc_bmff_emit_u32_field(ctx, "primary.linked_item_role_count", count)) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        info = omc_bmff_find_item_info(items, item_count, item_ids[i]);
        if (!omc_bmff_emit_u32_field(ctx, "primary.linked_item_id",
                                     item_ids[i])) {
            return 0;
        }
        if (info != (const omc_bmff_item_info*)0) {
            omc_bmff_item_semantic semantic;

            if (info->have_type) {
                char type_name[11];
                omc_u16 type_name_len;

                if (!omc_bmff_emit_u32_field(ctx, "primary.linked_item_type",
                                             info->item_type)
                    || !omc_bmff_fourcc_display_name(info->item_type, type_name,
                                                     &type_name_len)
                    || !omc_bmff_emit_text_field(ctx,
                                                 "primary.linked_item_type_name",
                                                 type_name, type_name_len)) {
                    return 0;
                }
            }
            if (info->name_len != 0U
                && !omc_bmff_emit_text_field(ctx, "primary.linked_item_name",
                                             info->name, info->name_len)) {
                return 0;
            }
            semantic = omc_bmff_classify_item_semantic(info);
            if (omc_bmff_item_semantic_is_known(semantic)) {
                const char* semantic_name;

                semantic_name = omc_bmff_item_semantic_name(semantic);
                if (!omc_bmff_emit_text_field(ctx,
                                              "primary.linked_item_semantic",
                                              semantic_name,
                                              (omc_u16)strlen(semantic_name))) {
                    return 0;
                }
            }
        }
        if (!omc_bmff_emit_text_field(ctx, "primary.linked_item_role", roles[i],
                                      (omc_u16)strlen(roles[i]))) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_emit_primary_fields(omc_bmff_ctx* ctx, const omc_bmff_item_info* items,
                             omc_u32 item_count, omc_u32 primary_item_id,
                             const omc_bmff_primary_props* props)
{
    const omc_bmff_item_info* primary;
    omc_u32 i;

    if (ctx == (omc_bmff_ctx*)0) {
        return 0;
    }
    if (!omc_bmff_emit_u32_field(ctx, "meta.primary_item_id", primary_item_id)) {
        return 0;
    }

    primary = omc_bmff_find_item_info(items, item_count, primary_item_id);
    if (primary != (const omc_bmff_item_info*)0) {
        if (!omc_bmff_emit_u16_field(ctx, "primary.protection_index",
                                     primary->protection_index)) {
            return 0;
        }
        if (primary->have_type) {
            char type_name[11];
            omc_u16 type_name_len;

            if (!omc_bmff_emit_u32_field(ctx, "primary.item_type",
                                         primary->item_type)
                || !omc_bmff_fourcc_display_name(primary->item_type, type_name,
                                                 &type_name_len)
                || !omc_bmff_emit_text_field(ctx, "primary.item_type_name",
                                             type_name, type_name_len)) {
                return 0;
            }
        }
        if (primary->name_len != 0U
            && !omc_bmff_emit_text_field(ctx, "primary.item_name",
                                         primary->name, primary->name_len)) {
            return 0;
        }
        if (primary->content_type_len != 0U
            && !omc_bmff_emit_text_field(ctx, "primary.content_type",
                                         primary->content_type,
                                         primary->content_type_len)) {
            return 0;
        }
        if (primary->content_encoding_len != 0U
            && !omc_bmff_emit_text_field(ctx, "primary.content_encoding",
                                         primary->content_encoding,
                                         primary->content_encoding_len)) {
            return 0;
        }
        if (primary->uri_type_len != 0U
            && !omc_bmff_emit_text_field(ctx, "primary.uri_type",
                                         primary->uri_type,
                                         primary->uri_type_len)) {
            return 0;
        }
        {
            omc_bmff_item_semantic primary_semantic;

            primary_semantic = omc_bmff_classify_item_semantic(primary);
            if (omc_bmff_item_semantic_is_known(primary_semantic)) {
                const char* semantic_name;

                semantic_name = omc_bmff_item_semantic_name(primary_semantic);
                if (!omc_bmff_emit_text_field(ctx, "primary.item_semantic",
                                              semantic_name,
                                              (omc_u16)strlen(semantic_name))) {
                    return 0;
                }
            }
        }
    }

    if (props != (const omc_bmff_primary_props*)0) {
        omc_u32 primary_auxl_take;

        if (props->have_width_height) {
            if (!omc_bmff_emit_u32_field(ctx, "primary.width", props->width)
                || !omc_bmff_emit_u32_field(ctx, "primary.height",
                                            props->height)) {
                return 0;
            }
        }
        if (props->have_rotation
            && !omc_bmff_emit_u16_field(ctx, "primary.rotation_degrees",
                                        props->rotation_degrees)) {
            return 0;
        }
        if (props->have_mirror
            && !omc_bmff_emit_u8_field(ctx, "primary.mirror", props->mirror)) {
            return 0;
        }
        if (!omc_bmff_emit_primary_rel_ids(ctx, "primary.auxl_item_id",
                                           props->primary_auxl_item_ids,
                                           props->primary_auxl_count, 32U)
            || !omc_bmff_emit_primary_rel_ids(ctx, "primary.dimg_item_id",
                                              props->primary_dimg_item_ids,
                                              props->primary_dimg_count, 32U)
            || !omc_bmff_emit_primary_rel_ids(ctx, "primary.thmb_item_id",
                                              props->primary_thmb_item_ids,
                                              props->primary_thmb_count, 32U)
            || !omc_bmff_emit_primary_rel_ids(ctx, "primary.cdsc_item_id",
                                              props->primary_cdsc_item_ids,
                                              props->primary_cdsc_count, 32U)) {
            return 0;
        }

        primary_auxl_take = props->primary_auxl_count;
        if (primary_auxl_take > 32U) {
            primary_auxl_take = 32U;
        }
        for (i = 0U; i < primary_auxl_take; ++i) {
            const char* semantic_name;

            semantic_name = omc_bmff_aux_semantic_name(
                props->primary_auxl_semantics[i]);
            if (!omc_bmff_emit_text_field(ctx, "primary.auxl_semantic",
                                          semantic_name,
                                          (omc_u16)strlen(semantic_name))) {
                return 0;
            }
        }
        if (!omc_bmff_emit_primary_linked_fields(ctx, items, item_count,
                                                 props)) {
            return 0;
        }
        if (!omc_bmff_emit_primary_rel_ids(ctx, "primary.alpha_item_id",
                                           props->primary_alpha_item_ids,
                                           props->primary_alpha_count, 32U)
            || !omc_bmff_emit_primary_rel_ids(ctx, "primary.depth_item_id",
                                              props->primary_depth_item_ids,
                                              props->primary_depth_count, 32U)
            || !omc_bmff_emit_primary_rel_ids(ctx, "primary.disparity_item_id",
                                              props->primary_disparity_item_ids,
                                              props->primary_disparity_count,
                                              32U)
            || !omc_bmff_emit_primary_rel_ids(ctx, "primary.matte_item_id",
                                              props->primary_matte_item_ids,
                                              props->primary_matte_count,
                                              32U)) {
            return 0;
        }
        if (props->primary_auxl_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.auxl_count",
                                        props->primary_auxl_count)) {
            return 0;
        }
        if (props->primary_alpha_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.alpha_count",
                                        props->primary_alpha_count)) {
            return 0;
        }
        if (props->primary_depth_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.depth_count",
                                        props->primary_depth_count)) {
            return 0;
        }
        if (props->primary_dimg_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.dimg_count",
                                        props->primary_dimg_count)) {
            return 0;
        }
        if (props->primary_thmb_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.thmb_count",
                                        props->primary_thmb_count)) {
            return 0;
        }
        if (props->primary_cdsc_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.cdsc_count",
                                        props->primary_cdsc_count)) {
            return 0;
        }
        if (props->primary_disparity_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.disparity_count",
                                        props->primary_disparity_count)) {
            return 0;
        }
        if (props->primary_matte_count != 0U
            && !omc_bmff_emit_u32_field(ctx, "primary.matte_count",
                                        props->primary_matte_count)) {
            return 0;
        }
    }
    return 1;
}

static int
omc_bmff_decode_meta_box(omc_bmff_ctx* ctx, const omc_bmff_box* meta)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u64 p;
    omc_bmff_box iinf;
    omc_bmff_box pitm;
    omc_bmff_box iprp;
    omc_bmff_box iref;
    omc_bmff_box grpl;
    int has_iinf;
    int has_pitm;
    int has_iprp;
    int has_iref;
    int has_grpl;
    omc_bmff_item_info items[64];
    omc_bmff_primary_props primary_props;
    omc_u32 item_count;
    omc_u32 primary_item_id;

    if (ctx == (omc_bmff_ctx*)0 || meta == (const omc_bmff_box*)0) {
        return 0;
    }

    payload_off = meta->offset + meta->header_size;
    payload_end = meta->offset + meta->size;
    if (payload_off + 4U > payload_end) {
        return 0;
    }

    memset(&iinf, 0, sizeof(iinf));
    memset(&pitm, 0, sizeof(pitm));
    memset(&iprp, 0, sizeof(iprp));
    memset(&iref, 0, sizeof(iref));
    memset(&grpl, 0, sizeof(grpl));
    memset(&primary_props, 0, sizeof(primary_props));
    has_iinf = 0;
    has_pitm = 0;
    has_iprp = 0;
    has_iref = 0;
    has_grpl = 0;
    p        = payload_off + 4U;
    while (p + 8U <= payload_end) {
        omc_bmff_box child;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, p, payload_end, &child)) {
            return 0;
        }
        if (child.type == OMC_BMFF_FOURCC('i', 'i', 'n', 'f')) {
            iinf     = child;
            has_iinf = 1;
        } else if (child.type == OMC_BMFF_FOURCC('p', 'i', 't', 'm')) {
            pitm     = child;
            has_pitm = 1;
        } else if (child.type == OMC_BMFF_FOURCC('i', 'p', 'r', 'p')) {
            iprp     = child;
            has_iprp = 1;
        } else if (child.type == OMC_BMFF_FOURCC('i', 'r', 'e', 'f')) {
            iref     = child;
            has_iref = 1;
        } else if (child.type == OMC_BMFF_FOURCC('g', 'r', 'p', 'l')) {
            grpl     = child;
            has_grpl = 1;
        }
        p += child.size;
        if (child.size == 0U) {
            break;
        }
    }

    item_count = 0U;
    if (has_iinf) {
        if (!omc_bmff_collect_iinf_items(ctx, &iinf, items, &item_count)) {
            return 0;
        }
    }
    if (has_pitm) {
        omc_u64 iprp_off;
        omc_u64 iprp_end;
        omc_bmff_box ipco;
        omc_bmff_box ipma;
        int has_ipco;
        int has_ipma;

        if (!omc_bmff_parse_pitm(ctx->bytes, ctx->size, &pitm,
                                 &primary_item_id)) {
            return 0;
        }
        primary_props.have_item_id = 1;
        primary_props.item_id      = primary_item_id;
        if (has_iref
            && !omc_bmff_collect_iref_edges(ctx, &iref, &primary_props)) {
            return 0;
        }
        memset(&ipco, 0, sizeof(ipco));
        memset(&ipma, 0, sizeof(ipma));
        has_ipco = 0;
        has_ipma = 0;
        if (has_iprp) {
            iprp_off = iprp.offset + iprp.header_size;
            iprp_end = iprp.offset + iprp.size;
            if (iprp_off > iprp_end || iprp_end > (omc_u64)ctx->size) {
                return 0;
            }
            while (iprp_off + 8U <= iprp_end) {
                omc_bmff_box child;

                if (!omc_bmff_note_box(ctx)) {
                    return 0;
                }
                if (!omc_bmff_parse_box(ctx->bytes, ctx->size, iprp_off,
                                        iprp_end, &child)) {
                    return 0;
                }
                if (child.type == OMC_BMFF_FOURCC('i', 'p', 'c', 'o')) {
                    ipco     = child;
                    has_ipco = 1;
                } else if (child.type == OMC_BMFF_FOURCC('i', 'p', 'm', 'a')) {
                    ipma     = child;
                    has_ipma = 1;
                }
                iprp_off += child.size;
                if (child.size == 0U) {
                    break;
                }
            }
        }
        if (has_ipma) {
            omc_bmff_ispe_prop ispe[64];
            omc_bmff_u8_prop irot[64];
            omc_bmff_u8_prop imir[64];
            omc_bmff_auxc_prop auxc[64];
            omc_bmff_prop_type prop_types[64];
            omc_u32 ispe_count;
            omc_u32 irot_count;
            omc_u32 imir_count;
            omc_u32 auxc_count;
            omc_u32 prop_type_count;

            ispe_count      = 0U;
            irot_count      = 0U;
            imir_count      = 0U;
            auxc_count      = 0U;
            prop_type_count = 0U;
            if (has_ipco
                && !omc_bmff_collect_ipco_props(ctx, &ipco, ispe, &ispe_count,
                                                irot, &irot_count, imir,
                                                &imir_count, auxc, &auxc_count,
                                                prop_types, &prop_type_count)) {
                return 0;
            }
            if (!omc_bmff_apply_ipma_primary(ctx, &ipma, primary_item_id, ispe,
                                             ispe_count, irot, irot_count, imir,
                                             imir_count, auxc, auxc_count,
                                             prop_types, prop_type_count,
                                             &primary_props)) {
                return 0;
            }
        }
    }

    if (has_grpl && !omc_bmff_collect_item_groups(ctx, &grpl, &primary_props)) {
        return 0;
    }
    if ((has_iinf && !omc_bmff_emit_item_info_fields(ctx, items, item_count))
        || !omc_bmff_emit_ipma_fields(ctx, &primary_props)
        || !omc_bmff_emit_item_group_fields(ctx, &primary_props)) {
        return 0;
    }
    if (has_pitm) {
        if (!omc_bmff_emit_primary_fields(ctx, items, item_count,
                                          primary_item_id, &primary_props)
            || !omc_bmff_emit_iref_fields(ctx, &primary_props)
            || !omc_bmff_emit_aux_fields(ctx, &primary_props)) {
            return 0;
        }
    }

    ctx->meta_done = 1;
    return 1;
}

static void
omc_bmff_scan_for_meta(omc_bmff_ctx* ctx, omc_u64 off, omc_u64 end,
                       omc_u32 depth)
{
    if (ctx == (omc_bmff_ctx*)0 || ctx->meta_done
        || ctx->res.status != OMC_BMFF_OK) {
        return;
    }
    if (depth > ctx->opts.limits.max_depth) {
        ctx->res.status = OMC_BMFF_LIMIT;
        return;
    }

    while (off + 8U <= end && !ctx->meta_done
           && ctx->res.status == OMC_BMFF_OK) {
        omc_bmff_box box;

        if (!omc_bmff_note_box(ctx)) {
            return;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, end, &box)) {
            ctx->res.status = OMC_BMFF_MALFORMED;
            return;
        }

        if (box.type == OMC_BMFF_FOURCC('m', 'e', 't', 'a')) {
            if (!omc_bmff_decode_meta_box(ctx, &box)) {
                if (ctx->res.status == OMC_BMFF_OK) {
                    ctx->res.status = OMC_BMFF_MALFORMED;
                }
                return;
            }
        } else if (omc_bmff_is_container_box(box.type)) {
            omc_u64 child_off;
            omc_u64 child_end;

            child_off = box.offset + box.header_size;
            child_end = box.offset + box.size;
            if (child_off < child_end && child_end <= (omc_u64)ctx->size) {
                omc_bmff_scan_for_meta(ctx, child_off, child_end, depth + 1U);
            }
        }

        off += box.size;
        if (box.size == 0U) {
            break;
        }
    }
}

static omc_bmff_res
omc_bmff_run(const omc_u8* file_bytes, omc_size file_size, omc_store* store,
             const omc_bmff_opts* opts)
{
    omc_bmff_ctx ctx;
    omc_bmff_opts local_opts;
    omc_bmff_box ftyp;
    omc_bmff_brand_info brand;

    memset(&ctx, 0, sizeof(ctx));
    omc_bmff_res_init(&ctx.res);
    omc_bmff_opts_init(&local_opts);
    if (opts != (const omc_bmff_opts*)0) {
        local_opts = *opts;
    }
    ctx.bytes    = file_bytes;
    ctx.size     = file_size;
    ctx.store    = store;
    ctx.opts     = local_opts;
    ctx.block_id = OMC_INVALID_BLOCK_ID;

    if (file_bytes == (const omc_u8*)0) {
        ctx.res.status = OMC_BMFF_MALFORMED;
        return ctx.res;
    }

    if (!omc_bmff_find_ftyp(&ctx, &ftyp)) {
        if (ctx.res.status == OMC_BMFF_OK) {
            ctx.res.boxes_scanned = 0U;
        }
        return ctx.res;
    }
    if (!omc_bmff_parse_ftyp(&ctx, &ftyp, &brand)) {
        if (ctx.res.status == OMC_BMFF_OK) {
            ctx.res.status = OMC_BMFF_MALFORMED;
        }
        return ctx.res;
    }
    if (!brand.is_heif && !brand.is_avif && !brand.is_cr3) {
        ctx.res.status          = OMC_BMFF_OK;
        ctx.res.entries_decoded = 0U;
        return ctx.res;
    }

    {
        char brand_name[11];
        omc_u16 brand_name_len;

        if (!omc_bmff_fourcc_display_name(brand.major_brand, brand_name,
                                          &brand_name_len)
            || !omc_bmff_emit_u32_field(&ctx, "ftyp.major_brand",
                                        brand.major_brand)
            || !omc_bmff_emit_text_field(&ctx, "ftyp.major_brand_name",
                                         brand_name, brand_name_len)
            || !omc_bmff_emit_u32_field(&ctx, "ftyp.minor_version",
                                        brand.minor_version)
            || !omc_bmff_emit_u32_field(&ctx, "ftyp.compat_brand_count",
                                        brand.compat_count)) {
            return ctx.res;
        }
    }
    if (brand.compat_count != 0U
        && !omc_bmff_emit_u32_array_field(&ctx, "ftyp.compat_brands",
                                          brand.compat_brands,
                                          brand.compat_count)) {
        return ctx.res;
    }
    if (brand.compat_count != 0U) {
        omc_u32 i;

        for (i = 0U; i < brand.compat_count; ++i) {
            char compat_name[11];
            omc_u16 compat_name_len;

            if (!omc_bmff_fourcc_display_name(brand.compat_brands[i],
                                              compat_name, &compat_name_len)
                || !omc_bmff_emit_text_field(&ctx, "ftyp.compat_brand_name",
                                             compat_name, compat_name_len)) {
                return ctx.res;
            }
        }
    }

    omc_bmff_scan_for_meta(&ctx, 0U, (omc_u64)file_size, 0U);
    return ctx.res;
}

omc_bmff_res
omc_bmff_dec(const omc_u8* file_bytes, omc_size file_size, omc_store* store,
             const omc_bmff_opts* opts)
{
    return omc_bmff_run(file_bytes, file_size, store, opts);
}

omc_bmff_res
omc_bmff_meas(const omc_u8* file_bytes, omc_size file_size,
              const omc_bmff_opts* opts)
{
    return omc_bmff_run(file_bytes, file_size, (omc_store*)0, opts);
}
