#include "read/omc_input.h"
#include "omc/omc_bmff.h"

#include <stdio.h>
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
    const omc_bmff_item_info *items;
    omc_u32 item_count;
    int have_idat;
    omc_u64 idat_offset;
    omc_u64 idat_end;
    omc_u64 iloc_end;
    omc_u8 iloc_sizes[5];
    omc_u32 location_count;
    omc_u32 location_total;
    omc_u32 location_ids[128];
    omc_u64 location_offsets[128];
    omc_bmff_box dinf;
    omc_bmff_box ipco;
    int have_ipco_summary;
    omc_u32 property_count;
    omc_u32 known_property_count;
    omc_u32 property_type_counts[9];
    int have_item_id;
    omc_u32 item_id;
    int have_width_height;
    omc_u32 width;
    omc_u32 height;
    int have_rotation;
    omc_u16 rotation_degrees;
    int have_mirror;
    omc_u8 mirror;
    omc_bmff_iref_edge edges[512];
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
    omc_u32 primary_auxl_item_ids[128];
    omc_u32 primary_auxl_count;
    omc_bmff_aux_semantic primary_auxl_semantics[128];
    omc_u32 primary_alpha_item_ids[128];
    omc_u32 primary_alpha_count;
    omc_u32 primary_depth_item_ids[128];
    omc_u32 primary_depth_count;
    omc_u32 primary_disparity_item_ids[128];
    omc_u32 primary_disparity_count;
    omc_u32 primary_matte_item_ids[128];
    omc_u32 primary_matte_count;
    omc_u32 primary_dimg_item_ids[128];
    omc_u32 primary_dimg_count;
    omc_u32 primary_dimg_source_count;
    omc_u32 primary_dimg_source_item_ids[128];
    omc_u32 primary_thmb_item_ids[128];
    omc_u32 primary_thmb_count;
    omc_u32 primary_cdsc_item_ids[128];
    omc_u32 primary_cdsc_count;
    omc_bmff_aux_item_info aux_items[256];
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
    omc_input* bytes;
    omc_u64 size;
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
    opts->limits.max_item_infos = 256U;
    opts->limits.max_entries    = 512U;
}

static int
omc_bmff_read_u16be(omc_input* bytes, omc_u64 size, omc_u64 off,
                    omc_u16* out)
{
    omc_u64 value;
    if (out == NULL || off > size || 2U > size - off ||
        !omc_input_number(bytes, off, 2U, 0, &value)) return 0;
    *out = (omc_u16)value;
    return 1;
}

static int
omc_bmff_read_u32be(omc_input* bytes, omc_u64 size, omc_u64 off,
                    omc_u32* out)
{
    omc_u64 value;
    if (out == NULL || off > size || 4U > size - off ||
        !omc_input_number(bytes, off, 4U, 0, &value)) return 0;
    *out = (omc_u32)value;
    return 1;
}

static int
omc_bmff_read_u64be(omc_input* bytes, omc_u64 size, omc_u64 off,
                    omc_u64* out)
{
    omc_u64 value;
    if (out == NULL || off > size || 8U > size - off ||
        !omc_input_number(bytes, off, 8U, 0, &value)) return 0;
    *out = (omc_u64)value;
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
omc_bmff_parse_box(omc_input* bytes, omc_u64 size, omc_u64 off, omc_u64 end,
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
    entry.origin.wire_count = value->kind == OMC_VAL_SCALAR
                                  ? 1U
                                  : (value->kind == OMC_VAL_ARRAY ? value->count : 0U);
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

static int omc_bmff_emit_utf8_field(omc_bmff_ctx *ctx, const char *field,
                                    const char *value, omc_u32 value_len)
{
    omc_val val;
    omc_byte_ref value_ref;
    omc_status st;
    omc_text_encoding enc;

    if (ctx == (omc_bmff_ctx *)0 || field == (const char *)0 ||
        value == (const char *)0) {
        return 0;
    }
    if (ctx->res.entries_decoded >= ctx->opts.limits.max_entries) {
        ctx->res.status = OMC_BMFF_LIMIT;
        return 0;
    }
    if (ctx->store == (omc_store *)0) {
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

    enc = OMC_TEXT_UTF8;
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
        if (info->item_type == OMC_BMFF_FOURCC('d', 'e', 'r', 'v') ||
            info->item_type == OMC_BMFF_FOURCC('g', 'r', 'i', 'd') ||
            info->item_type == OMC_BMFF_FOURCC('i', 'o', 'v', 'l') ||
            info->item_type == OMC_BMFF_FOURCC('i', 'd', 'e', 'n')) {
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
        if (info->item_type == OMC_BMFF_FOURCC('h', 'v', 'c', '1') ||
            info->item_type == OMC_BMFF_FOURCC('a', 'v', '0', '1') ||
            info->item_type == OMC_BMFF_FOURCC('j', 'p', 'e', 'g') ||
            info->item_type == OMC_BMFF_FOURCC('t', 'i', 'l', 'i')) {
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
    if (take_count > 128U) {
        take_count = 128U;
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
    if (props->aux_item_count >= 256U) {
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
    if (idx == ~(omc_u32)0 || idx >= 256U) {
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
    if (idx == ~(omc_u32)0 || idx >= 256U) {
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
    if (idx == ~(omc_u32)0 || idx >= 256U) {
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
    if (idx == ~(omc_u32)0 || idx >= 256U) {
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
    if (idx == ~(omc_u32)0 || idx >= 128U) {
        return;
    }
    if (props->primary_auxl_semantics[idx] != OMC_BMFF_AUX_UNKNOWN) {
        return;
    }
    props->primary_auxl_semantics[idx] = semantic;

    if (semantic == OMC_BMFF_AUX_ALPHA) {
        (void)omc_bmff_push_unique_item_id(props->primary_alpha_item_ids,
                                           &props->primary_alpha_count, 128U, item_id);
    } else if (semantic == OMC_BMFF_AUX_DEPTH) {
        (void)omc_bmff_push_unique_item_id(props->primary_depth_item_ids,
                                           &props->primary_depth_count, 128U, item_id);
    } else if (semantic == OMC_BMFF_AUX_DISPARITY) {
        (void)omc_bmff_push_unique_item_id(props->primary_disparity_item_ids,
                                           &props->primary_disparity_count, 128U,
                                           item_id);
    } else if (semantic == OMC_BMFF_AUX_MATTE) {
        (void)omc_bmff_push_unique_item_id(props->primary_matte_item_ids,
                                           &props->primary_matte_count, 128U, item_id);
    }
}

static int
omc_bmff_read_cstr(omc_input* bytes, omc_u64 size, omc_u64* io_off,
                   omc_u64 end, char* out, omc_size out_cap, omc_u16* out_len)
{
    omc_u64 p;
    omc_u16 n;

    if (bytes == NULL || io_off == (omc_u64*)0 || out == (char*)0
        || out_len == (omc_u16*)0 || out_cap == 0U) {
        return 0;
    }
    if (*io_off > end || end > (omc_u64)size) {
        return 0;
    }

    p = *io_off;
    while (p < end && omc_input_byte(bytes, p) != 0U) {
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
        if (!omc_input_read(bytes, *io_off, out, (omc_size)n)) return 0;
    }
    *out_len = n;
    *io_off  = p + 1U;
    return 1;
}

static int
omc_bmff_parse_pitm(omc_input* bytes, omc_u64 size,
                    const omc_bmff_box* pitm, omc_u32* out_item_id)
{
    omc_u64 payload_off;
    omc_u64 payload_size;
    omc_u8 version;

    if (bytes == NULL || pitm == (const omc_bmff_box*)0
        || out_item_id == (omc_u32*)0) {
        return 0;
    }

    payload_off  = pitm->offset + pitm->header_size;
    payload_size = pitm->size - pitm->header_size;
    if (payload_size < 6U) {
        return 0;
    }

    version = omc_input_byte(bytes, payload_off);
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
omc_bmff_parse_infe(omc_input* bytes, omc_u64 size,
                    const omc_bmff_box* infe, omc_bmff_item_info* out_info)
{
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u64 p;
    omc_u8 version;

    if (bytes == NULL || infe == (const omc_bmff_box*)0
        || out_info == (omc_bmff_item_info*)0) {
        return 0;
    }

    memset(out_info, 0, sizeof(*out_info));
    payload_off = infe->offset + infe->header_size;
    payload_end = infe->offset + infe->size;
    if (payload_off + 4U > payload_end) {
        return 0;
    }

    version = omc_input_byte(bytes, payload_off);
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

    version = omc_input_byte(ctx->bytes, payload_off);
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
    if (item_cap > 256U) {
        item_cap = 256U;
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

static const char *const omc_bmff_property_tokens[9] = {
    "ispe", "irot", "imir", "colr", "auxC", "pasp", "pixi", "clap", "tilC"};

static int omc_bmff_property_kind(omc_u32 type)
{
    int i;
    for (i = 0; i < 9; ++i) {
        const char *t;
        t = omc_bmff_property_tokens[i];
        if (type == OMC_BMFF_FOURCC(t[0], t[1], t[2], t[3]))
            return i;
    }
    return -1;
}

static int omc_bmff_emit_property_summaries(omc_bmff_ctx *ctx,
                                            const omc_bmff_primary_props *props)
{
    omc_u32 associations[9];
    omc_u32 primary[9];
    omc_u32 essential[9];
    omc_u32 i;
    char prefix[24];
    char field[64];
    memset(associations, 0, sizeof(associations));
    memset(primary, 0, sizeof(primary));
    memset(essential, 0, sizeof(essential));
    if (props->have_ipco_summary) {
        if (!omc_bmff_emit_u32_field(ctx, "ipco.property_count",
                                     props->property_count) ||
            !omc_bmff_emit_u32_field(ctx, "ipco.known_property_count",
                                     props->known_property_count) ||
            !omc_bmff_emit_u32_field(ctx, "ipco.unknown_property_count",
                                     props->property_count -
                                         props->known_property_count))
            return 0;
        for (i = 0U; i < 9U; ++i) {
            if (!props->property_type_counts[i])
                continue;
            if (!omc_bmff_make_field2(field, sizeof(field), "ipco",
                                      omc_bmff_property_tokens[i]))
                return 0;
            strcat(field, "_count");
            if (!omc_bmff_emit_u32_field(ctx, field, props->property_type_counts[i]))
                return 0;
        }
    }
    for (i = 0U; i < props->ipma_association_count; ++i) {
        const omc_bmff_ipma_assoc *a;
        int k;
        a = &props->ipma_associations[i];
        k = a->have_property_type ? omc_bmff_property_kind(a->property_type) : -1;
        if (k < 0)
            continue;
        ++associations[k];
        if (props->have_item_id && a->item_id == props->item_id)
            ++primary[k];
        if (a->essential)
            ++essential[k];
    }
    for (i = 0U; i < 9U; ++i) {
        if (!associations[i])
            continue;
        if (!omc_bmff_make_field2(prefix, sizeof(prefix), "ipma",
                                  omc_bmff_property_tokens[i]) ||
            !omc_bmff_make_field2(field, sizeof(field), prefix, "association_count") ||
            !omc_bmff_emit_u32_field(ctx, field, associations[i]))
            return 0;
        if (primary[i] && (!omc_bmff_make_field2(field, sizeof(field), prefix,
                                                 "primary_association_count") ||
                           !omc_bmff_emit_u32_field(ctx, field, primary[i])))
            return 0;
        if (essential[i] &&
            (!omc_bmff_make_field2(field, sizeof(field), prefix, "essential_count") ||
             !omc_bmff_emit_u32_field(ctx, field, essential[i])))
            return 0;
    }
    return 1;
}

static int omc_bmff_collect_ipco_props(
    omc_bmff_ctx *ctx, const omc_bmff_box *ipco, omc_bmff_ispe_prop *out_ispe,
    omc_u32 *out_ispe_count, omc_bmff_u8_prop *out_irot, omc_u32 *out_irot_count,
    omc_bmff_u8_prop *out_imir, omc_u32 *out_imir_count, omc_bmff_auxc_prop *out_auxc,
    omc_u32 *out_auxc_count, omc_bmff_prop_type *out_prop_types,
    omc_u32 *out_prop_type_count, omc_bmff_primary_props *summary)
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

    summary->ipco = *ipco;
    summary->have_ipco_summary = 1;
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
        int kind;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, end, &child)) {
            return 0;
        }
        ++summary->property_count;
        kind = omc_bmff_property_kind(child.type);
        if (kind >= 0) {
            ++summary->known_property_count;
            ++summary->property_type_counts[kind];
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
                    omc_bmff_append_prop_type(out_prop_types, out_prop_type_count, 256U,
                                              prop_index,
                                              OMC_BMFF_FOURCC('i', 's', 'p', 'e'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('i', 'r', 'o', 't')) {
                if (payload_size >= 1U && *out_irot_count < 64U) {
                    out_irot[*out_irot_count].index = prop_index;
                    out_irot[*out_irot_count].value
                        = (omc_u8)(omc_input_byte(ctx->bytes, payload_off) & 0x03U);
                    *out_irot_count += 1U;
                    omc_bmff_append_prop_type(out_prop_types, out_prop_type_count, 256U,
                                              prop_index,
                                              OMC_BMFF_FOURCC('i', 'r', 'o', 't'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('i', 'm', 'i', 'r')) {
                if (payload_size >= 1U && *out_imir_count < 64U) {
                    out_imir[*out_imir_count].index = prop_index;
                    out_imir[*out_imir_count].value
                        = omc_input_byte(ctx->bytes, payload_off);
                    *out_imir_count += 1U;
                    omc_bmff_append_prop_type(out_prop_types, out_prop_type_count, 256U,
                                              prop_index,
                                              OMC_BMFF_FOURCC('i', 'm', 'i', 'r'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('c', 'o', 'l', 'r')) {
                if (payload_size >= 4U) {
                    omc_bmff_append_prop_type(out_prop_types, out_prop_type_count, 256U,
                                              prop_index,
                                              OMC_BMFF_FOURCC('c', 'o', 'l', 'r'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('a', 'u', 'x', 'C')) {
                if (payload_size >= 5U && *out_auxc_count < 64U) {
                    omc_u64 p;
                    omc_u64 e;

                    p = payload_off + 4U;
                    e = payload_off + payload_size;
                    while (p < e && omc_input_byte(ctx->bytes, p) != 0U) {
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
                            (void)omc_input_read(ctx->bytes, type_off, prop->aux_type, type_len);
                            prop->aux_type_len = (omc_u16)type_len;
                            prop->semantic     = omc_bmff_classify_auxc_type(
                                prop->aux_type, prop->aux_type_len);
                            if (p + 1U < e) {
                                omc_size subtype_len;

                                subtype_len = (omc_size)(e - (p + 1U));
                                if (subtype_len > sizeof(prop->aux_subtype)) {
                                    subtype_len = sizeof(prop->aux_subtype);
                                }
                                (void)omc_input_read(ctx->bytes, p + 1U, prop->aux_subtype, subtype_len);
                                prop->aux_subtype_len = (omc_u16)subtype_len;
                            }
                            *out_auxc_count += 1U;
                            omc_bmff_append_prop_type(
                                out_prop_types, out_prop_type_count, 256U, prop_index,
                                OMC_BMFF_FOURCC('a', 'u', 'x', 'C'));
                        }
                    }
                }
            } else if (child.type == OMC_BMFF_FOURCC('p', 'a', 's', 'p')) {
                if (payload_size >= 8U) {
                    omc_bmff_append_prop_type(out_prop_types, out_prop_type_count, 256U,
                                              prop_index,
                                              OMC_BMFF_FOURCC('p', 'a', 's', 'p'));
                }
            } else if (child.type == OMC_BMFF_FOURCC('p', 'i', 'x', 'i')) {
                if (payload_size >= 5U) {
                    omc_u8 channel_count;

                    channel_count = omc_input_byte(ctx->bytes, (payload_off + 4U));
                    if (channel_count != 0U
                        && (omc_u64)channel_count <= payload_size - 5U) {
                        omc_bmff_append_prop_type(out_prop_types, out_prop_type_count,
                                                  256U, prop_index,
                                                  OMC_BMFF_FOURCC('p', 'i', 'x', 'i'));
                    }
                }
            } else if (child.type == OMC_BMFF_FOURCC('t', 'i', 'l', 'C')) {
                omc_bmff_append_prop_type(out_prop_types, out_prop_type_count, 256U,
                                          prop_index, child.type);
            } else if (child.type == OMC_BMFF_FOURCC('c', 'l', 'a', 'p')) {
                if (payload_size >= 32U) {
                    omc_bmff_append_prop_type(out_prop_types, out_prop_type_count, 256U,
                                              prop_index,
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

    version = omc_input_byte(ctx->bytes, payload_off);
    if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, payload_off + 4U,
                             &entry_count)) {
        return 0;
    }

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
        assoc_count = omc_input_byte(ctx->bytes, off);
        off += 1U;

        for (j = 0U; j < assoc_count; ++j) {
            omc_u32 prop_index;
            omc_u8 essential;

            if (version < 1U) {
                omc_u8 v;

                if (off + 1U > end) {
                    return 0;
                }
                v = omc_input_byte(ctx->bytes, off);
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
                                      &props->primary_auxl_count, 128U, to_item_id);
    } else if (ref_type == OMC_BMFF_FOURCC('d', 'i', 'm', 'g')) {
        props->dimg_edge_count += 1U;
        omc_bmff_push_primary_item_id(props->primary_dimg_item_ids,
                                      &props->primary_dimg_count, 128U, to_item_id);
    } else if (ref_type == OMC_BMFF_FOURCC('t', 'h', 'm', 'b')) {
        props->thmb_edge_count += 1U;
        omc_bmff_push_primary_item_id(props->primary_thmb_item_ids,
                                      &props->primary_thmb_count, 128U, to_item_id);
    } else if (ref_type == OMC_BMFF_FOURCC('c', 'd', 's', 'c')) {
        props->cdsc_edge_count += 1U;
        omc_bmff_push_primary_item_id(props->primary_cdsc_item_ids,
                                      &props->primary_cdsc_count, 128U, to_item_id);
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

    version = omc_input_byte(ctx->bytes, payload_off);
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
            if (out_props->edge_count < 512U) {
                out_props->edges[out_props->edge_count].ref_type = child.type;
                out_props->edges[out_props->edge_count].from_item_id
                    = from_item_id;
                out_props->edges[out_props->edge_count].to_item_id = to_item_id;
                out_props->edge_count += 1U;
            } else {
                out_props->edge_truncated = 1;
            }
            if (out_props->have_item_id && to_item_id == out_props->item_id)
                omc_bmff_note_primary_ref(out_props, child.type, from_item_id);
            if (out_props->have_item_id && from_item_id == out_props->item_id &&
                child.type == OMC_BMFF_FOURCC('d', 'i', 'm', 'g'))
                omc_bmff_push_primary_item_id(out_props->primary_dimg_source_item_ids,
                                              &out_props->primary_dimg_source_count,
                                              128U, to_item_id);
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

        version = omc_input_byte(ctx->bytes, child_payload_off);
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

static const char *omc_bmff_group_semantic(omc_u32 type)
{
    if (type == OMC_BMFF_FOURCC('a', 'l', 't', 'r'))
        return "alternatives";
    if (type == OMC_BMFF_FOURCC('s', 't', 'e', 'r'))
        return "stereo_pair";
    if (type == OMC_BMFF_FOURCC('p', 'y', 'm', 'd'))
        return "image_pyramid";
    return "unclassified";
}

static const char *omc_bmff_group_role(omc_u32 type, omc_u32 index)
{
    if (type == OMC_BMFF_FOURCC('a', 'l', 't', 'r'))
        return "alternative";
    if (type == OMC_BMFF_FOURCC('s', 't', 'e', 'r'))
        return !index ? "left_image" : index == 1U ? "right_image" : "stereo_member";
    if (type == OMC_BMFF_FOURCC('p', 'y', 'm', 'd'))
        return "pyramid_layer";
    return "member";
}

static int omc_bmff_emit_group_role(omc_bmff_ctx *ctx, const char *index_key,
                                    const char *role_key,
                                    const omc_bmff_item_group *group, omc_u32 index)
{
    const char *role;
    role = omc_bmff_group_role(group->group_type, index);
    return omc_bmff_emit_u32_field(ctx, index_key, index) &&
           omc_bmff_emit_text_field(ctx, role_key, role, (omc_u16)strlen(role));
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
            char role_key[64];
            if (!omc_bmff_make_field2(field, sizeof(field), prefix, "entity_index") ||
                !omc_bmff_make_field2(role_key, sizeof(role_key), prefix,
                                      "entity_role") ||
                !omc_bmff_emit_group_role(ctx, field, role_key, group, j))
                return 0;
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
        if (!omc_bmff_emit_text_field(
                ctx, "item_group.semantic", omc_bmff_group_semantic(group->group_type),
                (omc_u16)strlen(omc_bmff_group_semantic(group->group_type))))
            return 0;
        for (j = 0U; j < group->entity_id_count && j < 64U; ++j) {
            if (!omc_bmff_emit_group_role(ctx, "item_group.entity_index",
                                          "item_group.entity_role", group, j))
                return 0;
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
        if (!omc_bmff_emit_text_field(
                ctx, "primary.item_group_semantic",
                omc_bmff_group_semantic(group->group_type),
                (omc_u16)strlen(omc_bmff_group_semantic(group->group_type))))
            return 0;
        for (j = 0U; j < group->entity_id_count && j < 64U; ++j) {
            if (!omc_bmff_emit_group_role(ctx, "primary.item_group_entity_index",
                                          "primary.item_group_entity_role", group, j))
                return 0;
            if (group->entity_ids[j] == props->item_id &&
                !omc_bmff_emit_group_role(ctx,
                                          "primary.item_group_primary_entity_index",
                                          "primary.item_group_primary_role", group, j))
                return 0;
            if (!omc_bmff_emit_u32_field(ctx, "primary.item_group_entity_id",
                                         group->entity_ids[j])) {
                return 0;
            }
        }
        if (group->entity_count > 0U &&
            !omc_bmff_emit_u32_field(ctx, "primary.item_group_other_entity_count",
                                     group->entity_count - 1U))
            return 0;
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
    omc_u32 group_types[512];
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
    if (take_count > 512U) {
        take_count = 512U;
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
            if (!omc_bmff_emit_text_field(ctx, "iref.ref_type_name", token, 4U) ||
                !omc_bmff_make_field2(field, sizeof(field), "iref", token) ||
                !omc_bmff_make_field2(field, sizeof(field), field, "from_item_id") ||
                !omc_bmff_emit_u32_field(ctx, field, edge->from_item_id) ||
                !omc_bmff_make_field2(field, sizeof(field), "iref", token) ||
                !omc_bmff_make_field2(field, sizeof(field), field, "to_item_id") ||
                !omc_bmff_emit_u32_field(ctx, field, edge->to_item_id) ||
                !omc_bmff_push_unique_item_id(group_types, &group_count, 512U,
                                              edge->ref_type)) {
                return 0;
            }
        }

        if (edge->ref_type != OMC_BMFF_FOURCC('a', 'u', 'x', 'l')) {
            const char *from_role;
            const char *to_role;
            const char *from_id;
            const char *to_id;
            from_role = (const char *)0;
            to_role = (const char *)0;
            from_id = (const char *)0;
            to_id = (const char *)0;
            if (edge->ref_type == OMC_BMFF_FOURCC('d', 'i', 'm', 'g')) {
                from_role = "derived_image";
                to_role = "source_image";
                from_id = "derived_item_id";
                to_id = "source_item_id";
            } else if (edge->ref_type == OMC_BMFF_FOURCC('t', 'h', 'm', 'b')) {
                from_role = "thumbnail_image";
                to_role = "master_image";
                from_id = "thumbnail_item_id";
                to_id = "master_item_id";
            } else if (edge->ref_type == OMC_BMFF_FOURCC('c', 'd', 's', 'c')) {
                from_role = "descriptive_item";
                to_role = "described_item";
                from_id = "descriptive_item_id";
                to_id = "described_item_id";
            }
            if (from_role != (const char *)0) {
                char prefix[12];
                if (!omc_bmff_make_field2(prefix, sizeof(prefix), "iref", token) ||
                    !omc_bmff_make_field2(field, sizeof(field), prefix, "from_role") ||
                    !omc_bmff_emit_text_field(ctx, field, from_role,
                                              (omc_u16)strlen(from_role)) ||
                    !omc_bmff_make_field2(field, sizeof(field), prefix, "to_role") ||
                    !omc_bmff_emit_text_field(ctx, field, to_role,
                                              (omc_u16)strlen(to_role)) ||
                    !omc_bmff_make_field2(field, sizeof(field), prefix, from_id) ||
                    !omc_bmff_emit_u32_field(ctx, field, edge->from_item_id) ||
                    !omc_bmff_make_field2(field, sizeof(field), prefix, to_id) ||
                    !omc_bmff_emit_u32_field(ctx, field, edge->to_item_id))
                    return 0;
            }
            continue;
        }

        if (!omc_bmff_emit_text_field(ctx, "iref.auxl.from_role", "auxiliary_image",
                                      15U) ||
            !omc_bmff_emit_text_field(ctx, "iref.auxl.to_role", "master_image", 12U) ||
            !omc_bmff_emit_u32_field(ctx, "iref.auxl.auxiliary_item_id",
                                     edge->from_item_id) ||
            !omc_bmff_emit_u32_field(ctx, "iref.auxl.master_item_id", edge->to_item_id))
            return 0;
        aux_item = omc_bmff_find_aux_item(props, edge->from_item_id);
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

static omc_bmff_item_semantic omc_bmff_scene_semantic(const omc_bmff_primary_props *p,
                                                      const omc_bmff_item_info *item);

static int omc_bmff_emit_count(omc_bmff_ctx *ctx, const char *key, omc_u32 count);

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
    for (i = 0U; i < props->primary_auxl_count && i < 128U; ++i) {
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
    for (i = 0U; i < props->primary_dimg_count && i < 128U; ++i) {
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
    for (i = 0U; i < props->primary_thmb_count && i < 128U; ++i) {
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
    for (i = 0U; i < props->primary_cdsc_count && i < 128U; ++i) {
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
    {
        static const char *const role_names[8] = {
            "auxiliary", "alpha",   "depth",     "disparity",
            "matte",     "derived", "thumbnail", "content_description"};
        static const char *const role_fields[8] = {
            "auxiliary", "alpha",         "depth",     "disparity",
            "matte",     "derived_image", "thumbnail", "content_description"};
        omc_u32 unique[128];
        omc_u32 unique_count;
        omc_u32 k;
        omc_bmff_item_semantic_counts counts;
        omc_u32 image_count;
        omc_u32 bound_count;
        char field[80];
        unique_count = 0U;
        memset(&counts, 0, sizeof(counts));
        for (i = 0U; i < count; ++i)
            if (!omc_bmff_push_unique_item_id(unique, &unique_count, 128U, item_ids[i]))
                return 0;
        if (!omc_bmff_emit_u32_field(ctx, "primary.sidecar_count", unique_count) ||
            !omc_bmff_emit_u32_field(ctx, "primary.scene_primary_item_count", 1U) ||
            !omc_bmff_emit_u32_field(ctx, "primary.scene_linked_item_count",
                                     unique_count) ||
            !omc_bmff_emit_u32_field(ctx, "primary.scene_node_count",
                                     1U + unique_count) ||
            !omc_bmff_emit_u32_field(ctx, "primary.scene_edge_count", count))
            return 0;
        for (k = 0U; k < 8U; ++k) {
            omc_u32 role_count;
            role_count = 0U;
            /* Roles were already deduplicated by (item_id, role). */
            for (i = 0U; i < count; ++i)
                if (strcmp(roles[i], role_names[k]) == 0)
                    ++role_count;
            strcpy(field, "primary.scene_");
            strcat(field, role_fields[k]);
            strcat(field, "_node_count");
            if (!omc_bmff_emit_count(ctx, field, role_count))
                return 0;
            strcpy(field, "primary.scene_");
            strcat(field, role_fields[k]);
            strcat(field, "_edge_count");
            if (!omc_bmff_emit_count(ctx, field, role_count))
                return 0;
        }
        for (i = 0U; i < unique_count; ++i) {
            info = omc_bmff_find_item_info(items, item_count, unique[i]);
            if (info != (const omc_bmff_item_info *)0)
                omc_bmff_count_item_semantic(omc_bmff_scene_semantic(props, info),
                                             &counts);
        }
        image_count =
            counts.image + counts.auxiliary + counts.derived + counts.thumbnail;
        bound_count = counts.jumbf + counts.c2pa;
        if (counts.metadata &&
            (!omc_bmff_emit_u32_field(ctx, "primary.scene_metadata_node_count",
                                      counts.metadata) ||
             !omc_bmff_emit_u8_field(ctx, "primary.has_metadata_sidecar", 1U) ||
             !omc_bmff_emit_u32_field(ctx, "primary.metadata_sidecar_count",
                                      counts.metadata)))
            return 0;
        if (image_count &&
            (!omc_bmff_emit_u32_field(ctx, "primary.scene_image_node_count",
                                      image_count) ||
             !omc_bmff_emit_u8_field(ctx, "primary.has_image_sidecar", 1U) ||
             !omc_bmff_emit_u32_field(ctx, "primary.image_sidecar_count", image_count)))
            return 0;
        if (bound_count &&
            (!omc_bmff_emit_u32_field(
                 ctx, "primary.scene_content_bound_metadata_node_count", bound_count) ||
             !omc_bmff_emit_u8_field(ctx, "primary.has_content_bound_metadata_sidecar",
                                     1U) ||
             !omc_bmff_emit_u32_field(
                 ctx, "primary.content_bound_metadata_sidecar_count", bound_count)))
            return 0;
        if (bound_count &&
            !omc_bmff_emit_text_field(ctx, "primary.content_bound_metadata_policy",
                                      "requires_target_rewrite", 23U))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.exif_sidecar_count", counts.exif))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.xmp_sidecar_count", counts.xmp))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.jumbf_sidecar_count", counts.jumbf))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.c2pa_sidecar_count", counts.c2pa))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.icc_profile_sidecar_count",
                                 counts.icc_profile))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.auxiliary_sidecar_count",
                                 counts.auxiliary))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.derived_sidecar_count", counts.derived))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.thumbnail_sidecar_count",
                                 counts.thumbnail))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.content_description_sidecar_count",
                                 counts.content_description))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.uri_sidecar_count", counts.uri))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.json_sidecar_count", counts.json))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_known_count",
                                 counts.known))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_metadata_count",
                                 counts.metadata))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_image_count",
                                 counts.image))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_exif_count",
                                 counts.exif))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_xmp_count",
                                 counts.xmp))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_jumbf_count",
                                 counts.jumbf))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_c2pa_count",
                                 counts.c2pa))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_icc_profile_count",
                                 counts.icc_profile))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_auxiliary_count",
                                 counts.auxiliary))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_derived_count",
                                 counts.derived))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_thumbnail_count",
                                 counts.thumbnail))
            return 0;
        if (!omc_bmff_emit_count(
                ctx, "primary.linked_item_semantic_content_description_count",
                counts.content_description))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_uri_count",
                                 counts.uri))
            return 0;
        if (!omc_bmff_emit_count(ctx, "primary.linked_item_semantic_json_count",
                                 counts.json))
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
            if (omc_bmff_item_semantic_is_metadata(primary_semantic) &&
                !omc_bmff_emit_u8_field(ctx, "primary.metadata_carrier", 1U))
                return 0;
            if (primary_semantic == OMC_BMFF_ITEM_C2PA &&
                !omc_bmff_emit_u8_field(ctx, "primary.c2pa_carrier", 1U))
                return 0;
            if (primary_semantic == OMC_BMFF_ITEM_JUMBF &&
                !omc_bmff_emit_u8_field(ctx, "primary.jumbf_carrier", 1U))
                return 0;
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
                                           props->primary_auxl_count, 128U) ||
            !omc_bmff_emit_primary_rel_ids(ctx, "primary.dimg_item_id",
                                           props->primary_dimg_item_ids,
                                           props->primary_dimg_count, 128U) ||
            !omc_bmff_emit_primary_rel_ids(ctx, "primary.thmb_item_id",
                                           props->primary_thmb_item_ids,
                                           props->primary_thmb_count, 128U) ||
            !omc_bmff_emit_primary_rel_ids(ctx, "primary.cdsc_item_id",
                                           props->primary_cdsc_item_ids,
                                           props->primary_cdsc_count, 128U)) {
            return 0;
        }

        primary_auxl_take = props->primary_auxl_count;
        if (primary_auxl_take > 128U) {
            primary_auxl_take = 128U;
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
                                           props->primary_alpha_count, 128U) ||
            !omc_bmff_emit_primary_rel_ids(ctx, "primary.depth_item_id",
                                           props->primary_depth_item_ids,
                                           props->primary_depth_count, 128U) ||
            !omc_bmff_emit_primary_rel_ids(ctx, "primary.disparity_item_id",
                                           props->primary_disparity_item_ids,
                                           props->primary_disparity_count, 128U) ||
            !omc_bmff_emit_primary_rel_ids(ctx, "primary.matte_item_id",
                                           props->primary_matte_item_ids,
                                           props->primary_matte_count, 128U)) {
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

/* Scene projection uses bounded node arrays. Component statistics are computed
 * one component at a time to avoid a second large array on embedded stacks. */
typedef struct omc_bmff_scene_node {
    omc_u32 id;
    omc_u32 parent;
    omc_bmff_item_semantic semantic;
} omc_bmff_scene_node;

typedef struct omc_bmff_scene_stats {
    omc_u32 node_count;
    omc_u32 image_node_count;
    omc_u32 metadata_node_count;
    omc_u32 content_bound_metadata_node_count;
    omc_u32 edge_count;
    omc_bmff_item_semantic_counts semantic_counts;
    omc_u32 auxiliary_edge_count;
    omc_u32 alpha_edge_count;
    omc_u32 depth_edge_count;
    omc_u32 disparity_edge_count;
    omc_u32 matte_edge_count;
    omc_u32 derived_image_edge_count;
    omc_u32 thumbnail_edge_count;
    omc_u32 content_description_edge_count;
    omc_u32 other_edge_count;
    int contains_primary;
} omc_bmff_scene_stats;

static omc_bmff_item_semantic omc_bmff_relation_semantic(omc_u32 type)
{
    switch (type) {
    case OMC_BMFF_FOURCC('a', 'u', 'x', 'l'):
        return OMC_BMFF_ITEM_AUXILIARY;
    case OMC_BMFF_FOURCC('d', 'i', 'm', 'g'):
        return OMC_BMFF_ITEM_DERIVED;
    case OMC_BMFF_FOURCC('t', 'h', 'm', 'b'):
        return OMC_BMFF_ITEM_THUMBNAIL;
    case OMC_BMFF_FOURCC('c', 'd', 's', 'c'):
        return OMC_BMFF_ITEM_CONTENT_DESCRIPTION;
    default:
        return OMC_BMFF_ITEM_UNKNOWN;
    }
}

static omc_bmff_item_semantic omc_bmff_scene_semantic(const omc_bmff_primary_props *p,
                                                      const omc_bmff_item_info *item)
{
    omc_bmff_item_semantic base;
    omc_u32 i;
    base = omc_bmff_classify_item_semantic(item);
    if (omc_bmff_item_semantic_is_metadata(base))
        return base;
    for (i = 0U; i < p->edge_count; ++i) {
        omc_bmff_item_semantic relation;
        if (p->edges[i].from_item_id != item->item_id)
            continue;
        relation = omc_bmff_relation_semantic(p->edges[i].ref_type);
        if (relation != OMC_BMFF_ITEM_UNKNOWN)
            return relation;
    }
    return base;
}

static omc_u32 omc_bmff_scene_find(const omc_bmff_scene_node *nodes, omc_u32 count,
                                   omc_u32 id)
{
    omc_u32 i;
    for (i = 0U; i < count; ++i)
        if (nodes[i].id == id)
            return i;
    return ~(omc_u32)0;
}

static void omc_bmff_scene_push(omc_bmff_scene_node *nodes, omc_u32 *count, omc_u32 id,
                                omc_bmff_item_semantic semantic)
{
    omc_u32 i;
    if (!id)
        return;
    i = omc_bmff_scene_find(nodes, *count, id);
    if (i != ~(omc_u32)0) {
        if (nodes[i].semantic == OMC_BMFF_ITEM_UNKNOWN)
            nodes[i].semantic = semantic;
    } else if (*count < 512U) {
        nodes[*count].id = id;
        nodes[*count].semantic = semantic;
        nodes[*count].parent = *count;
        ++*count;
    }
}

static omc_u32 omc_bmff_scene_root(omc_bmff_scene_node *nodes, omc_u32 i)
{
    omc_u32 root;
    root = i;
    while (nodes[root].parent != root)
        root = nodes[root].parent;
    while (nodes[i].parent != i) {
        omc_u32 next;
        next = nodes[i].parent;
        nodes[i].parent = root;
        i = next;
    }
    return root;
}

static int omc_bmff_scene_image(omc_bmff_item_semantic semantic)
{
    return semantic == OMC_BMFF_ITEM_IMAGE || semantic == OMC_BMFF_ITEM_AUXILIARY ||
           semantic == OMC_BMFF_ITEM_DERIVED || semantic == OMC_BMFF_ITEM_THUMBNAIL;
}

static int omc_bmff_scene_content_bound(omc_bmff_item_semantic semantic)
{
    return semantic == OMC_BMFF_ITEM_JUMBF || semantic == OMC_BMFF_ITEM_C2PA;
}

static void omc_bmff_scene_stats_for(omc_bmff_scene_node *nodes, omc_u32 count,
                                     omc_u32 root, const omc_bmff_primary_props *p,
                                     omc_bmff_scene_stats *out)
{
    omc_u32 i;
    memset(out, 0, sizeof(*out));
    for (i = 0U; i < count; ++i) {
        omc_bmff_item_semantic semantic;
        if (omc_bmff_scene_root(nodes, i) != root)
            continue;
        ++out->node_count;
        semantic = nodes[i].semantic;
        omc_bmff_count_item_semantic(semantic, &out->semantic_counts);
        if (omc_bmff_scene_image(semantic))
            ++out->image_node_count;
        if (omc_bmff_item_semantic_is_metadata(semantic))
            ++out->metadata_node_count;
        if (omc_bmff_scene_content_bound(semantic))
            ++out->content_bound_metadata_node_count;
        if (p->have_item_id && nodes[i].id == p->item_id)
            out->contains_primary = 1;
    }
    for (i = 0U; i < p->edge_count; ++i) {
        const omc_bmff_iref_edge *e;
        omc_u32 from;
        e = &p->edges[i];
        from = omc_bmff_scene_find(nodes, count, e->from_item_id);
        if (from == ~(omc_u32)0 || omc_bmff_scene_root(nodes, from) != root)
            continue;
        ++out->edge_count;
        switch (e->ref_type) {
        case OMC_BMFF_FOURCC('a', 'u', 'x', 'l'): {
            const omc_bmff_aux_item_info *aux;
            ++out->auxiliary_edge_count;
            aux = omc_bmff_find_aux_item(p, e->from_item_id);
            if (aux != (const omc_bmff_aux_item_info *)0) {
                switch (aux->semantic) {
                case OMC_BMFF_AUX_ALPHA:
                    ++out->alpha_edge_count;
                    break;
                case OMC_BMFF_AUX_DEPTH:
                    ++out->depth_edge_count;
                    break;
                case OMC_BMFF_AUX_DISPARITY:
                    ++out->disparity_edge_count;
                    break;
                case OMC_BMFF_AUX_MATTE:
                    ++out->matte_edge_count;
                    break;
                default:
                    break;
                }
            }
            break;
        }
        case OMC_BMFF_FOURCC('d', 'i', 'm', 'g'):
            ++out->derived_image_edge_count;
            break;
        case OMC_BMFF_FOURCC('t', 'h', 'm', 'b'):
            ++out->thumbnail_edge_count;
            break;
        case OMC_BMFF_FOURCC('c', 'd', 's', 'c'):
            ++out->content_description_edge_count;
            break;
        default:
            ++out->other_edge_count;
            break;
        }
    }
}

static int omc_bmff_emit_count(omc_bmff_ctx *ctx, const char *key, omc_u32 count)
{
    return !count || omc_bmff_emit_u32_field(ctx, key, count);
}

static int omc_bmff_emit_scene_component(omc_bmff_ctx *ctx,
                                         const omc_bmff_scene_stats *component,
                                         omc_u32 index)
{
    const char *role;
    role = component->contains_primary      ? "primary_scene"
           : component->image_node_count    ? "image_scene"
           : component->metadata_node_count ? "metadata_only"
                                            : "unclassified";
    if (!omc_bmff_emit_u32_field(ctx, "scene.component.index", index) ||
        !omc_bmff_emit_text_field(ctx, "scene.component.role", role,
                                  (omc_u16)strlen(role)))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "scene.component.node_count",
                                 component->node_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.known_node_count",
                             component->semantic_counts.known))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.unknown_node_count",
                             component->node_count - component->semantic_counts.known))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "scene.component.image_node_count",
                                 component->image_node_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "scene.component.metadata_node_count",
                                 component->metadata_node_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx,
                                 "scene.component.content_bound_metadata_node_count",
                                 component->content_bound_metadata_node_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.exif_node_count",
                             component->semantic_counts.exif))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.xmp_node_count",
                             component->semantic_counts.xmp))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.jumbf_node_count",
                             component->semantic_counts.jumbf))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.c2pa_node_count",
                             component->semantic_counts.c2pa))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.icc_profile_node_count",
                             component->semantic_counts.icc_profile))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.auxiliary_node_count",
                             component->semantic_counts.auxiliary))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.thumbnail_node_count",
                             component->semantic_counts.thumbnail))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.uri_node_count",
                             component->semantic_counts.uri))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.json_node_count",
                             component->semantic_counts.json))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.derived_image_node_count",
                             component->semantic_counts.derived))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.content_description_node_count",
                             component->semantic_counts.content_description))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.derived_image_edge_count",
                             component->derived_image_edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.content_description_edge_count",
                             component->content_description_edge_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "scene.component.edge_count",
                                 component->edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.auxiliary_edge_count",
                             component->auxiliary_edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.alpha_edge_count",
                             component->alpha_edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.depth_edge_count",
                             component->depth_edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.disparity_edge_count",
                             component->disparity_edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.matte_edge_count",
                             component->matte_edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.thumbnail_edge_count",
                             component->thumbnail_edge_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "scene.component.other_edge_count",
                             component->other_edge_count))
        return 0;
    if (!omc_bmff_emit_u8_field(ctx, "scene.component.contains_primary",
                                (omc_u8)component->contains_primary))
        return 0;
    if (!omc_bmff_emit_u8_field(
            ctx, "scene.component.isolated",
            component->node_count == 1U && component->edge_count == 0U ? 1U : 0U))
        return 0;
    if (!omc_bmff_emit_u8_field(ctx, "scene.component.has_content_bound_metadata",
                                (component->content_bound_metadata_node_count != 0U)))
        return 0;
    if (!omc_bmff_emit_u8_field(ctx, "scene.component.multi_image_candidate",
                                (component->image_node_count > 1U)))
        return 0;
    if (component->content_bound_metadata_node_count &&
        !omc_bmff_emit_text_field(ctx, "scene.component.metadata_policy",
                                  "requires_target_rewrite",
                                  (omc_u16)strlen("requires_target_rewrite")))
        return 0;
    if (component->image_node_count > 1U &&
        !omc_bmff_emit_text_field(ctx, "scene.component.multi_image_policy",
                                  "requires_target_rewrite",
                                  (omc_u16)strlen("requires_target_rewrite")))
        return 0;
    return 1;
}

static int omc_bmff_emit_scene(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                               const omc_bmff_item_info *items, omc_u32 item_count)
{
    omc_bmff_scene_node nodes[512];
    omc_u32 roots[512];
    omc_bmff_item_semantic_counts counts;
    omc_bmff_scene_stats component;
    omc_bmff_scene_stats primary;
    omc_u32 node_count;
    omc_u32 component_count;
    omc_u32 image_components;
    omc_u32 multi_image_components;
    omc_u32 bound_components;
    omc_u32 isolated_images;
    omc_u32 i;
    omc_u32 c;
    omc_u32 images;
    omc_u32 bound;
    if (!item_count)
        return 1;
    memset(&counts, 0, sizeof(counts));
    memset(&primary, 0, sizeof(primary));
    node_count = 0U;
    component_count = 0U;
    image_components = 0U;
    multi_image_components = 0U;
    bound_components = 0U;
    isolated_images = 0U;
    for (i = 0U; i < item_count; ++i)
        omc_bmff_count_item_semantic(omc_bmff_scene_semantic(p, &items[i]), &counts);
    images = counts.image + counts.auxiliary + counts.derived + counts.thumbnail;
    bound = counts.jumbf + counts.c2pa;
    if (!omc_bmff_emit_u32_field(ctx, "scene.item_count", item_count) ||
        !omc_bmff_emit_count(ctx, "scene.known_item_count", counts.known) ||
        !omc_bmff_emit_count(ctx, "scene.image_node_count", images) ||
        !omc_bmff_emit_count(ctx, "scene.metadata_node_count", counts.metadata) ||
        !omc_bmff_emit_count(ctx, "scene.content_bound_metadata_node_count", bound) ||
        !omc_bmff_emit_count(ctx, "scene.auxiliary_node_count", counts.auxiliary) ||
        !omc_bmff_emit_count(ctx, "scene.derived_image_node_count", counts.derived) ||
        !omc_bmff_emit_count(ctx, "scene.thumbnail_node_count", counts.thumbnail) ||
        !omc_bmff_emit_count(ctx, "scene.content_description_node_count",
                             counts.content_description) ||
        !omc_bmff_emit_count(ctx, "scene.edge_count", p->edge_total) ||
        !omc_bmff_emit_count(ctx, "scene.item_group_count", p->item_group_total))
        return 0;
    if (bound &&
        (!omc_bmff_emit_u8_field(ctx, "scene.has_content_bound_metadata", 1U) ||
         !omc_bmff_emit_text_field(ctx, "scene.content_bound_metadata_policy",
                                   "requires_target_rewrite", 23U)))
        return 0;
    if (images > 1U &&
        (!omc_bmff_emit_u8_field(ctx, "scene.multi_image_candidate", 1U) ||
         !omc_bmff_emit_text_field(ctx, "scene.multi_image_policy",
                                   "requires_target_rewrite", 23U)))
        return 0;
    if (p->have_item_id)
        omc_bmff_scene_push(nodes, &node_count, p->item_id, OMC_BMFF_ITEM_UNKNOWN);
    for (i = 0U; i < item_count; ++i)
        omc_bmff_scene_push(nodes, &node_count, items[i].item_id,
                            omc_bmff_scene_semantic(p, &items[i]));
    for (i = 0U; i < p->edge_count; ++i) {
        omc_bmff_scene_push(nodes, &node_count, p->edges[i].from_item_id,
                            omc_bmff_relation_semantic(p->edges[i].ref_type));
        omc_bmff_scene_push(nodes, &node_count, p->edges[i].to_item_id,
                            OMC_BMFF_ITEM_UNKNOWN);
    }
    if (!node_count)
        return 1;
    for (i = 0U; i < p->edge_count; ++i) {
        omc_u32 a;
        omc_u32 b;
        a = omc_bmff_scene_find(nodes, node_count, p->edges[i].from_item_id);
        b = omc_bmff_scene_find(nodes, node_count, p->edges[i].to_item_id);
        if (a == ~(omc_u32)0 || b == ~(omc_u32)0)
            continue;
        a = omc_bmff_scene_root(nodes, a);
        b = omc_bmff_scene_root(nodes, b);
        if (a < b)
            nodes[b].parent = a;
        else
            nodes[a].parent = b;
    }
    for (i = 0U; i < node_count; ++i) {
        omc_u32 root;
        root = omc_bmff_scene_root(nodes, i);
        for (c = 0U; c < component_count; ++c)
            if (roots[c] == root)
                break;
        if (c < component_count)
            continue;
        roots[component_count++] = root;
        omc_bmff_scene_stats_for(nodes, node_count, root, p, &component);
        if (component.image_node_count)
            ++image_components;
        if (component.image_node_count > 1U)
            ++multi_image_components;
        if (component.content_bound_metadata_node_count)
            ++bound_components;
        if (component.node_count == 1U && component.image_node_count == 1U &&
            !component.edge_count)
            ++isolated_images;
        if (component.contains_primary)
            primary = component;
    }
    if (!omc_bmff_emit_u32_field(ctx, "scene.graph_node_count", node_count) ||
        !omc_bmff_emit_u32_field(ctx, "scene.graph_component_count", component_count) ||
        !omc_bmff_emit_count(ctx, "scene.graph_image_component_count",
                             image_components) ||
        !omc_bmff_emit_count(ctx, "scene.graph_multi_image_component_count",
                             multi_image_components) ||
        !omc_bmff_emit_count(ctx, "scene.graph_content_bound_metadata_component_count",
                             bound_components) ||
        !omc_bmff_emit_count(ctx, "scene.graph_isolated_image_node_count",
                             isolated_images) ||
        !omc_bmff_emit_count(ctx, "scene.graph_observed_edge_count", p->edge_count))
        return 0;
    if (p->edge_truncated &&
        !omc_bmff_emit_u8_field(ctx, "scene.graph_component_truncated", 1U))
        return 0;
    for (c = 0U; c < component_count; ++c) {
        omc_bmff_scene_stats_for(nodes, node_count, roots[c], p, &component);
        if (!omc_bmff_emit_scene_component(ctx, &component, c))
            return 0;
        for (i = 0U; i < node_count; ++i) {
            if (nodes[i].parent == roots[c] &&
                !omc_bmff_emit_u32_field(ctx, "scene.component.item_id", nodes[i].id))
                return 0;
        }
    }
    if (!primary.contains_primary)
        return 1;
    if (!omc_bmff_emit_u32_field(ctx, "scene.primary_graph_component_node_count",
                                 primary.node_count) ||
        !omc_bmff_emit_count(ctx, "scene.primary_graph_component_image_node_count",
                             primary.image_node_count) ||
        !omc_bmff_emit_count(ctx, "scene.primary_graph_component_metadata_node_count",
                             primary.metadata_node_count) ||
        !omc_bmff_emit_count(
            ctx, "scene.primary_graph_component_content_bound_metadata_node_count",
            primary.content_bound_metadata_node_count) ||
        !omc_bmff_emit_count(ctx, "scene.primary_graph_component_edge_count",
                             primary.edge_count))
        return 0;
    if (primary.content_bound_metadata_node_count &&
        (!omc_bmff_emit_u8_field(
             ctx, "scene.primary_graph_component_has_content_bound_metadata", 1U) ||
         !omc_bmff_emit_text_field(ctx, "scene.primary_graph_component_metadata_policy",
                                   "requires_target_rewrite", 23U)))
        return 0;
    if (primary.image_node_count > 1U &&
        (!omc_bmff_emit_u8_field(
             ctx, "scene.primary_graph_component_multi_image_candidate", 1U) ||
         !omc_bmff_emit_text_field(ctx,
                                   "scene.primary_graph_component_multi_image_policy",
                                   "requires_target_rewrite", 23U)))
        return 0;
    return 1;
}

static int omc_bmff_emit_i32_field(omc_bmff_ctx *ctx, const char *key, omc_s32 value)
{
    omc_val v;
    omc_val_make_i32(&v, value);
    return omc_bmff_emit_entry(ctx, key, &v);
}

static int omc_bmff_emit_primary_extra(omc_bmff_ctx *ctx,
                                       const omc_bmff_primary_props *p)
{
    omc_bmff_box selected[9];
    omc_u32 ordinal[9];
    omc_u32 index;
    omc_u32 i;
    omc_u64 off;
    omc_u64 end;
    char text[48];
    if (!p->have_item_id)
        return 1;
    if (!omc_bmff_emit_count(ctx, "primary.auxiliary_image_count",
                             p->primary_auxl_count) ||
        !omc_bmff_emit_primary_rel_ids(ctx, "primary.auxiliary_image_item_id",
                                       p->primary_auxl_item_ids, p->primary_auxl_count,
                                       128U))
        return 0;
    if (!omc_bmff_emit_count(ctx, "primary.derived_image_count",
                             p->primary_dimg_count) ||
        !omc_bmff_emit_primary_rel_ids(ctx, "primary.derived_image_item_id",
                                       p->primary_dimg_item_ids, p->primary_dimg_count,
                                       128U))
        return 0;
    if (!omc_bmff_emit_count(ctx, "primary.thumbnail_image_count",
                             p->primary_thmb_count) ||
        !omc_bmff_emit_primary_rel_ids(ctx, "primary.thumbnail_image_item_id",
                                       p->primary_thmb_item_ids, p->primary_thmb_count,
                                       128U))
        return 0;
    if (!omc_bmff_emit_count(ctx, "primary.descriptive_item_count",
                             p->primary_cdsc_count) ||
        !omc_bmff_emit_primary_rel_ids(ctx, "primary.descriptive_item_id",
                                       p->primary_cdsc_item_ids, p->primary_cdsc_count,
                                       128U))
        return 0;
    if (!omc_bmff_emit_count(ctx, "primary.source_image_count",
                             p->primary_dimg_source_count) ||
        !omc_bmff_emit_primary_rel_ids(ctx, "primary.source_image_item_id",
                                       p->primary_dimg_source_item_ids,
                                       p->primary_dimg_source_count, 128U))
        return 0;
    if (!omc_bmff_emit_count(ctx, "primary.dimg_source_count",
                             p->primary_dimg_source_count) ||
        !omc_bmff_emit_primary_rel_ids(ctx, "primary.dimg_source_item_id",
                                       p->primary_dimg_source_item_ids,
                                       p->primary_dimg_source_count, 128U))
        return 0;

    if (p->have_width_height || p->have_rotation || p->have_mirror) {
        int swapped;
        /* Decimal values are bounded (rotation <= 270, mirror <= 255). */
        if (p->have_rotation && p->have_mirror)
            sprintf(text, "rotate_%u_mirror_%u", (unsigned)p->rotation_degrees,
                    (unsigned)p->mirror);
        else if (p->have_rotation)
            sprintf(text, "rotate_%u", (unsigned)p->rotation_degrees);
        else if (p->have_mirror)
            sprintf(text, "mirror_%u", (unsigned)p->mirror);
        else
            strcpy(text, "identity");
        if (!omc_bmff_emit_text_field(ctx, "primary.transform_summary", text,
                                      (omc_u16)strlen(text)))
            return 0;
        swapped = p->have_rotation &&
                  (p->rotation_degrees == 90U || p->rotation_degrees == 270U);
        if (p->have_width_height &&
            (!omc_bmff_emit_u32_field(ctx, "primary.display_width",
                                      swapped ? p->height : p->width) ||
             !omc_bmff_emit_u32_field(ctx, "primary.display_height",
                                      swapped ? p->width : p->height) ||
             !omc_bmff_emit_u8_field(ctx, "primary.display_dimensions_swapped",
                                     (omc_u8)swapped)))
            return 0;
    }
    if (!p->have_ipco_summary)
        return 1;
    memset(selected, 0, sizeof(selected));
    for (i = 0U; i < 9U; ++i)
        ordinal[i] = ~(omc_u32)0;
    off = p->ipco.offset + p->ipco.header_size;
    end = p->ipco.offset + p->ipco.size;
    index = 1U;
    while (off + 8U <= end) {
        omc_bmff_box box;
        int kind;
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, end, &box))
            return 0;
        kind = omc_bmff_property_kind(box.type);
        if (kind >= 0) {
            for (i = 0U; i < p->ipma_association_count && i < ordinal[kind]; ++i) {
                const omc_bmff_ipma_assoc *a;
                a = &p->ipma_associations[i];
                if (a->item_id == p->item_id && a->property_index == index &&
                    a->have_property_type) {
                    selected[kind] = box;
                    ordinal[kind] = i;
                    break;
                }
            }
        }
        off += box.size;
        ++index;
    }
    if (selected[5].size) {
        omc_u32 a;
        omc_u32 b;
        off = selected[5].offset + selected[5].header_size;
        if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, off, &a) ||
            !omc_bmff_read_u32be(ctx->bytes, ctx->size, off + 4U, &b) ||
            !omc_bmff_emit_u32_field(ctx, "primary.pixel_aspect_h_spacing", a) ||
            !omc_bmff_emit_u32_field(ctx, "primary.pixel_aspect_v_spacing", b))
            return 0;
    }
    if (selected[6].size) {
        omc_u8 channels;
        off = selected[6].offset + selected[6].header_size;
        channels = omc_input_byte(ctx->bytes, off + 4U);
        if (channels > 16U)
            channels = 16U;
        if (!omc_bmff_emit_u8_field(ctx, "primary.pixel_depth_channel_count", channels))
            return 0;
        for (i = 0U; i < channels; ++i)
            if (!omc_bmff_emit_u8_field(ctx, "primary.pixel_depth_bits_per_channel",
                                        omc_input_byte(ctx->bytes, off + 5U + i)))
                return 0;
    }
    if (selected[7].size) {
        static const char *const fields[8] = {
            "width_n",     "width_d",     "height_n",   "height_d",
            "horiz_off_n", "horiz_off_d", "vert_off_n", "vert_off_d"};
        off = selected[7].offset + selected[7].header_size;
        for (i = 0U; i < 8U; ++i) {
            omc_u32 raw;
            omc_s32 value;
            if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, off + i * 4U, &raw))
                return 0;
            value = raw <= 0x7FFFFFFFU ? (omc_s32)raw : -1 - (omc_s32)(~raw);
            strcpy(text, "primary.clean_aperture_");
            strcat(text, fields[i]);
            if (!omc_bmff_emit_i32_field(ctx, text, value))
                return 0;
        }
    }
    if (selected[3].size) {
        omc_u32 type;
        omc_u64 size;
        off = selected[3].offset + selected[3].header_size;
        size = selected[3].size - selected[3].header_size;
        if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, off, &type))
            return 0;
        for (i = 0U; i < 4U; ++i)
            text[i] = (char)((type >> ((3U - i) * 8U)) & 255U);
        text[4] = '\0';
        for (i = 0U; i < 4U; ++i)
            if ((unsigned char)text[i] < 32U || (unsigned char)text[i] > 126U)
                break;
        if (i != 4U)
            sprintf(text, "0x%08lx", (unsigned long)type);
        if (!omc_bmff_emit_u32_field(ctx, "primary.color_type", type) ||
            !omc_bmff_emit_text_field(ctx, "primary.color_type_name", text,
                                      (omc_u16)strlen(text)))
            return 0;
        if ((type == OMC_BMFF_FOURCC('n', 'c', 'l', 'x') && size >= 11U) ||
            (type == OMC_BMFF_FOURCC('n', 'c', 'l', 'c') && size >= 10U)) {
            static const char *const keys[3] = {"primary.nclx_colour_primaries",
                                                "primary.nclx_transfer_characteristics",
                                                "primary.nclx_matrix_coefficients"};
            for (i = 0U; i < 3U; ++i) {
                omc_u16 value;
                if (!omc_bmff_read_u16be(ctx->bytes, ctx->size, off + 4U + 2U * i,
                                         &value) ||
                    !omc_bmff_emit_u16_field(ctx, keys[i], value))
                    return 0;
            }
            if (type == OMC_BMFF_FOURCC('n', 'c', 'l', 'x') &&
                !omc_bmff_emit_u8_field(
                    ctx, "primary.nclx_full_range_flag",
                    (omc_u8)(omc_input_byte(ctx->bytes, off + 10U) >> 7U)))
                return 0;
        } else if ((type == OMC_BMFF_FOURCC('r', 'I', 'C', 'C') ||
                    type == OMC_BMFF_FOURCC('p', 'r', 'o', 'f')) &&
                   size > 4U) {
            size -= 4U;
            if (!omc_bmff_emit_u32_field(ctx, "primary.color_profile_bytes",
                                         size > 0xFFFFFFFFU ? 0xFFFFFFFFU
                                                            : (omc_u32)size))
                return 0;
        }
    }
    return 1;
}

typedef struct omc_bmff_extent {
    omc_u64 index;
    omc_u64 offset;
    omc_u64 length;
} omc_bmff_extent;

typedef struct omc_bmff_location {
    omc_u32 item_id;
    omc_u16 construction_method;
    omc_u16 data_reference_index;
    omc_u64 base_offset;
    omc_u32 extent_count;
    omc_u32 extent_record_count;
    omc_u64 total_extent_bytes;
    int extent_truncated;
    int length_overflow;
    omc_bmff_extent extents[16];
} omc_bmff_location;

static int omc_bmff_uint(omc_bmff_ctx *ctx, omc_u64 *off, omc_u64 end, omc_u8 bytes,
                         omc_u64 *value)
{
    omc_u8 i;
    omc_u64 v;
    if (bytes > 8U || *off > end || bytes > end - *off)
        return 0;
    v = 0U;
    for (i = 0U; i < bytes; ++i)
        v = (v << 8U) | omc_input_byte(ctx->bytes, *off + i);
    *off += bytes;
    *value = v;
    return 1;
}

static int omc_bmff_read_location(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                  omc_u64 *off, omc_bmff_location *loc)
{
    omc_u64 v;
    omc_u32 i;
    memset(loc, 0, sizeof(*loc));
    if (!omc_bmff_uint(ctx, off, p->iloc_end, p->iloc_sizes[0] < 2U ? 2U : 4U, &v))
        return 0;
    loc->item_id = (omc_u32)v;
    if (p->iloc_sizes[0] > 0U) {
        if (!omc_bmff_uint(ctx, off, p->iloc_end, 2U, &v))
            return 0;
        loc->construction_method = (omc_u16)(v & 15U);
    }
    if (!omc_bmff_uint(ctx, off, p->iloc_end, 2U, &v))
        return 0;
    loc->data_reference_index = (omc_u16)v;
    if (!omc_bmff_uint(ctx, off, p->iloc_end, p->iloc_sizes[3], &loc->base_offset) ||
        !omc_bmff_uint(ctx, off, p->iloc_end, 2U, &v))
        return 0;
    if (v > (1U << 14))
        return 0;
    loc->extent_count = (omc_u32)v;
    for (i = 0U; i < loc->extent_count; ++i) {
        omc_bmff_extent extent;
        if (!omc_bmff_uint(ctx, off, p->iloc_end, p->iloc_sizes[4], &extent.index) ||
            !omc_bmff_uint(ctx, off, p->iloc_end, p->iloc_sizes[1], &extent.offset) ||
            !omc_bmff_uint(ctx, off, p->iloc_end, p->iloc_sizes[2], &extent.length))
            return 0;
        if (extent.length > ~(omc_u64)0 - loc->total_extent_bytes)
            loc->length_overflow = 1;
        else
            loc->total_extent_bytes += extent.length;
        if (loc->extent_record_count < 16U)
            loc->extents[loc->extent_record_count++] = extent;
        else
            loc->extent_truncated = 1;
    }
    return 1;
}

static int omc_bmff_location_for(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                 omc_u32 id, omc_bmff_location *loc)
{
    omc_u32 i;
    for (i = 0U; i < p->location_count; ++i) {
        if (p->location_ids[i] == id) {
            omc_u64 off;
            off = p->location_offsets[i];
            return omc_bmff_read_location(ctx, p, &off, loc);
        }
    }
    return 0;
}

static int omc_bmff_collect_locations(omc_bmff_ctx *ctx, const omc_bmff_box *box,
                                      omc_bmff_primary_props *p)
{
    omc_u64 off;
    omc_u64 count;
    omc_u64 v;
    omc_u32 i;
    off = box->offset + box->header_size;
    p->iloc_end = box->offset + box->size;
    if (!omc_bmff_uint(ctx, &off, p->iloc_end, 4U, &v))
        return 0;
    p->iloc_sizes[0] = (omc_u8)(v >> 24U);
    if (p->iloc_sizes[0] > 2U || !omc_bmff_uint(ctx, &off, p->iloc_end, 2U, &v))
        return 0;
    p->iloc_sizes[1] = (omc_u8)((v >> 12U) & 15U);
    p->iloc_sizes[2] = (omc_u8)((v >> 8U) & 15U);
    p->iloc_sizes[3] = (omc_u8)((v >> 4U) & 15U);
    p->iloc_sizes[4] = p->iloc_sizes[0] ? (omc_u8)(v & 15U) : 0U;
    for (i = 1U; i < 5U; ++i)
        if (p->iloc_sizes[i] > 8U)
            return 0;
    if (!omc_bmff_uint(ctx, &off, p->iloc_end, p->iloc_sizes[0] < 2U ? 2U : 4U,
                       &count) ||
        count > (1U << 18))
        return 0;
    for (i = 0U; i < (omc_u32)count; ++i) {
        omc_bmff_location loc;
        omc_u64 start;
        start = off;
        if (!omc_bmff_read_location(ctx, p, &off, &loc))
            return 0;
        ++p->location_total;
        if (p->location_count < 128U) {
            p->location_offsets[p->location_count] = start;
            p->location_ids[p->location_count++] = loc.item_id;
        }
    }
    return 1;
}

static int omc_bmff_emit_location_row(omc_bmff_ctx *ctx, const char *prefix,
                                      const omc_bmff_location *loc)
{
    char field[64];
    const char *method;
    omc_u32 i;
    method = loc->construction_method == 0U   ? "file_offset"
             : loc->construction_method == 1U ? "idat_offset"
             : loc->construction_method == 2U ? "item_offset"
                                              : "reserved";
    if ((!omc_bmff_make_field2(field, sizeof(field), prefix, "item_id") ||
         !omc_bmff_emit_u32_field(ctx, field, loc->item_id)))
        return 0;
    if ((!omc_bmff_make_field2(field, sizeof(field), prefix, "construction_method") ||
         !omc_bmff_emit_u16_field(ctx, field, loc->construction_method)))
        return 0;
    if ((!omc_bmff_make_field2(field, sizeof(field), prefix,
                               "construction_method_name") ||
         !omc_bmff_emit_text_field(ctx, field, method, (omc_u16)strlen(method))))
        return 0;
    if ((!omc_bmff_make_field2(field, sizeof(field), prefix, "data_reference_index") ||
         !omc_bmff_emit_u16_field(ctx, field, loc->data_reference_index)))
        return 0;
    if ((!omc_bmff_make_field2(field, sizeof(field), prefix, "base_offset") ||
         !omc_bmff_emit_u64_field(ctx, field, loc->base_offset)))
        return 0;
    if ((!omc_bmff_make_field2(field, sizeof(field), prefix, "extent_count") ||
         !omc_bmff_emit_u32_field(ctx, field, loc->extent_count)))
        return 0;
    if ((!omc_bmff_make_field2(field, sizeof(field), prefix, "total_extent_bytes") ||
         !omc_bmff_emit_u64_field(ctx, field, loc->total_extent_bytes)))
        return 0;
    if (loc->length_overflow &&
        (!omc_bmff_make_field2(field, sizeof(field), prefix, "length_overflow") ||
         !omc_bmff_emit_u8_field(ctx, field, 1U)))
        return 0;
    if (loc->extent_truncated &&
        (!omc_bmff_make_field2(field, sizeof(field), prefix, "extent_truncated") ||
         !omc_bmff_emit_u8_field(ctx, field, 1U)))
        return 0;
    for (i = 0U; i < loc->extent_record_count; ++i) {
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "extent_index") ||
            !omc_bmff_emit_u64_field(ctx, field, loc->extents[i].index))
            return 0;
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "extent_offset") ||
            !omc_bmff_emit_u64_field(ctx, field, loc->extents[i].offset))
            return 0;
        if (!omc_bmff_make_field2(field, sizeof(field), prefix, "extent_length") ||
            !omc_bmff_emit_u64_field(ctx, field, loc->extents[i].length))
            return 0;
    }
    return 1;
}

static int omc_bmff_emit_locations(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p)
{
    static const char *const size_fields[5] = {"version", "offset_size", "length_size",
                                               "base_offset_size", "index_size"};
    omc_u32 i;
    omc_u32 idat_count;
    omc_bmff_location loc;
    char field[64];
    if (p->have_idat &&
        !omc_bmff_emit_u64_field(ctx, "idat.bytes", p->idat_end - p->idat_offset))
        return 0;
    if (!p->location_total)
        return 1;
    if (!omc_bmff_emit_u32_field(ctx, "item_location.count", p->location_total))
        return 0;
    if (p->location_count < p->location_total &&
        !omc_bmff_emit_u8_field(ctx, "item_location.truncated", 1U))
        return 0;
    for (i = 0U; i < 5U; ++i) {
        if (!omc_bmff_make_field2(field, sizeof(field), "item_location",
                                  size_fields[i]) ||
            !omc_bmff_emit_u8_field(ctx, field, p->iloc_sizes[i]))
            return 0;
    }
    idat_count = 0U;
    for (i = 0U; i < p->location_count; ++i) {
        omc_u64 off;
        off = p->location_offsets[i];
        if (!omc_bmff_read_location(ctx, p, &off, &loc) ||
            !omc_bmff_emit_location_row(ctx, "item_location", &loc))
            return 0;
        if (loc.construction_method == 1U)
            ++idat_count;
    }
    if (!omc_bmff_emit_count(ctx, "item_location.idat_item_count", idat_count))
        return 0;
    if (p->have_item_id && omc_bmff_location_for(ctx, p, p->item_id, &loc))
        return omc_bmff_emit_location_row(ctx, "primary.item_location", &loc);
    return 1;
}

typedef struct omc_bmff_cursor {
    omc_bmff_ctx *ctx;
    const omc_bmff_primary_props *props;
    omc_u32 item_id;
    omc_u16 allowed_reference;
    omc_u64 position;
    omc_u64 size;
    omc_u32 reference_depth;
} omc_bmff_cursor;

static int omc_bmff_resolve(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                            omc_u32 id, omc_u16 allowed_reference, omc_u32 *path,
                            omc_u32 depth, omc_u32 *steps, omc_u64 position,
                            omc_u8 *byte, omc_u64 *size, omc_u32 *max_depth)
{
    omc_bmff_location loc;
    omc_u64 total;
    omc_u32 i;
    int found;
    if (depth >= 16U || !*steps)
        return 0;
    --*steps;
    for (i = 0U; i < depth; ++i)
        if (path[i] == id)
            return 0;
    path[depth] = id;
    if (depth > *max_depth)
        *max_depth = depth;
    if (!omc_bmff_location_for(ctx, p, id, &loc) ||
        (loc.data_reference_index &&
         (depth || loc.data_reference_index != allowed_reference)) ||
        loc.extent_truncated || loc.length_overflow || !loc.extent_count ||
        loc.construction_method > 2U)
        return 0;
    total = 0U;
    found = 0;
    for (i = 0U; i < loc.extent_record_count; ++i) {
        omc_u64 source_size;
        omc_u64 source_offset;
        omc_u64 length;
        omc_u32 source_id;
        source_id = 0U;
        if (loc.construction_method == 0U)
            source_size = ctx->size;
        else if (loc.construction_method == 1U) {
            if (!p->have_idat || p->idat_end > ctx->size)
                return 0;
            source_size = p->idat_end - p->idat_offset;
        } else {
            omc_u64 reference;
            omc_u32 j;
            omc_u64 seen;
            if (p->edge_truncated)
                return 0;
            reference = p->iloc_sizes[4]               ? loc.extents[i].index
                        : loc.extent_record_count > 1U ? (omc_u64)i + 1U
                                                       : 1U;
            seen = 0U;
            for (j = 0U; j < p->edge_count; ++j) {
                if (p->edges[j].ref_type == OMC_BMFF_FOURCC('i', 'l', 'o', 'c') &&
                    p->edges[j].from_item_id == id) {
                    if (++seen == reference) {
                        source_id = p->edges[j].to_item_id;
                        break;
                    }
                }
            }
            if (j == p->edge_count ||
                !omc_bmff_resolve(ctx, p, source_id, allowed_reference, path,
                                  depth + 1U, steps, 0U, (omc_u8 *)0, &source_size,
                                  max_depth))
                return 0;
        }
        if (loc.extents[i].offset > ~(omc_u64)0 - loc.base_offset)
            return 0;
        source_offset = loc.base_offset + loc.extents[i].offset;
        if (source_offset > source_size)
            return 0;
        length = loc.extents[i].length;
        if (!length) {
            if (loc.extent_record_count != 1U)
                return 0;
            length = source_size - source_offset;
        } else if (length > source_size - source_offset)
            return 0;
        if (length > ~(omc_u64)0 - total)
            return 0;
        if (byte != (omc_u8 *)0 && !found && position >= total &&
            position - total < length) {
            source_offset += position - total;
            if (loc.construction_method == 2U) {
                omc_u64 ignored;
                if (!omc_bmff_resolve(ctx, p, source_id, allowed_reference, path,
                                      depth + 1U, steps, source_offset, byte, &ignored,
                                      max_depth))
                    return 0;
            } else {
                if (loc.construction_method == 1U)
                    source_offset += p->idat_offset;
                *byte = omc_input_byte(ctx->bytes, source_offset);
            }
            found = 1;
        }
        total += length;
    }
    *size = total;
    return byte == (omc_u8 *)0 || found;
}

static int omc_bmff_cursor_init_ref(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                    omc_u32 id, omc_u16 allowed_reference,
                                    omc_bmff_cursor *cursor)
{
    omc_u32 path[16];
    omc_u32 steps;
    memset(cursor, 0, sizeof(*cursor));
    cursor->ctx = ctx;
    cursor->props = p;
    cursor->item_id = id;
    cursor->allowed_reference = allowed_reference;
    steps = 4096U;
    return omc_bmff_resolve(ctx, p, id, cursor->allowed_reference, path, 0U, &steps, 0U,
                            (omc_u8 *)0, &cursor->size, &cursor->reference_depth);
}

static int omc_bmff_cursor_uint(omc_bmff_cursor *c, omc_u8 bytes, omc_u32 *out)
{
    omc_u32 i;
    omc_u32 value;
    value = 0U;
    for (i = 0U; i < bytes; ++i) {
        omc_u32 path[16];
        omc_u32 steps;
        omc_u64 size;
        omc_u8 byte;
        if (c->position >= c->size)
            return 0;
        steps = 4096U;
        if (!omc_bmff_resolve(c->ctx, c->props, c->item_id, c->allowed_reference, path,
                              0U, &steps, c->position, &byte, &size,
                              &c->reference_depth))
            return 0;
        ++c->position;
        value = (value << 8U) | byte;
    }
    *out = value;
    return 1;
}

static int omc_bmff_cursor_uint64(omc_bmff_cursor *cursor, omc_u8 bytes, omc_u64 *out)
{
    omc_u8 i;
    omc_u64 value;
    if (bytes > 8U)
        return 0;
    value = 0U;
    for (i = 0U; i < bytes; ++i) {
        omc_u32 byte;
        if (!omc_bmff_cursor_uint(cursor, 1U, &byte))
            return 0;
        value = (value << 8U) | byte;
    }
    *out = value;
    return 1;
}

typedef struct omc_bmff_descriptor {
    int available;
    int have_header;
    int valid;
    omc_u32 reference_depth;
    omc_u8 version;
    omc_u8 flags;
    omc_u16 rows;
    omc_u16 columns;
    omc_u32 output_width;
    omc_u32 output_height;
    omc_u16 background[4];
    omc_s32 offset_x[512];
    omc_s32 offset_y[512];
} omc_bmff_descriptor;

static omc_bmff_descriptor omc_bmff_parse_descriptor(omc_bmff_ctx *ctx,
                                                     const omc_bmff_primary_props *p,
                                                     omc_u32 id, int overlay,
                                                     omc_u32 count)
{
    omc_bmff_descriptor out;
    omc_bmff_cursor cursor;
    omc_u32 value;
    omc_u32 i;
    omc_u8 bits;
    memset(&out, 0, sizeof(out));
    if (!omc_bmff_cursor_init_ref(ctx, p, id, 0U, &cursor))
        return out;
    out.available = 1;
    out.reference_depth = cursor.reference_depth;
    if (overlay && (!count || count > 512U))
        return out;
    if (!omc_bmff_cursor_uint(&cursor, 1U, &value))
        return out;
    out.version = (omc_u8)value;
    if (!omc_bmff_cursor_uint(&cursor, 1U, &value))
        return out;
    out.flags = (omc_u8)value;
    out.have_header = 1;
    if (out.version)
        return out;
    bits = (out.flags & 1U) ? 4U : 2U;
    if (overlay) {
        for (i = 0U; i < 4U; ++i) {
            if (!omc_bmff_cursor_uint(&cursor, 2U, &value))
                return out;
            out.background[i] = (omc_u16)value;
        }
    } else {
        if (!omc_bmff_cursor_uint(&cursor, 1U, &value))
            return out;
        out.rows = (omc_u16)(value + 1U);
        if (!omc_bmff_cursor_uint(&cursor, 1U, &value))
            return out;
        out.columns = (omc_u16)(value + 1U);
    }
    if (!omc_bmff_cursor_uint(&cursor, bits, &out.output_width) ||
        !omc_bmff_cursor_uint(&cursor, bits, &out.output_height))
        return out;
    if (overlay) {
        for (i = 0U; i < count * 2U; ++i) {
            omc_s32 signed_value;
            if (!omc_bmff_cursor_uint(&cursor, bits, &value))
                return out;
            if (bits == 2U)
                signed_value =
                    value <= 32767U ? (omc_s32)value : (omc_s32)value - 65536;
            else
                signed_value =
                    value <= 0x7FFFFFFFU ? (omc_s32)value : -1 - (omc_s32)(~value);
            if (i & 1U)
                out.offset_y[i / 2U] = signed_value;
            else
                out.offset_x[i / 2U] = signed_value;
        }
    }
    out.valid = out.output_width != 0U && out.output_height != 0U;
    return out;
}

typedef struct omc_bmff_derived_graph {
    int cycle;
    int self_reference;
    int depth_exceeded;
    int references_truncated;
    omc_u32 missing_source_count;
    omc_u32 max_depth;
    omc_u32 path[64];
    omc_u32 visited[256];
    omc_u32 path_count;
    omc_u32 visited_count;
} omc_bmff_derived_graph;

static void omc_bmff_derived_visit(const omc_bmff_primary_props *p, omc_u32 id,
                                   omc_bmff_derived_graph *out)
{
    omc_u32 i;
    if (out->cycle || out->depth_exceeded)
        return;
    for (i = 0U; i < out->path_count; ++i)
        if (out->path[i] == id) {
            out->cycle = 1;
            return;
        }
    for (i = 0U; i < out->visited_count; ++i)
        if (out->visited[i] == id)
            return;
    if (out->path_count >= 64U || out->visited_count >= 256U) {
        out->depth_exceeded = 1;
        return;
    }
    out->path[out->path_count++] = id;
    if (out->max_depth < out->path_count - 1U)
        out->max_depth = out->path_count - 1U;
    for (i = 0U; i < p->edge_count; ++i) {
        const omc_bmff_iref_edge *e;
        e = &p->edges[i];
        if (e->ref_type != OMC_BMFF_FOURCC('d', 'i', 'm', 'g') || e->from_item_id != id)
            continue;
        if (e->to_item_id == id)
            out->self_reference = 1;
        if (omc_bmff_find_item_info(p->items, p->item_count, e->to_item_id) ==
            (const omc_bmff_item_info *)0)
            ++out->missing_source_count;
        else
            omc_bmff_derived_visit(p, e->to_item_id, out);
    }
    --out->path_count;
    if (out->visited_count < 256U)
        out->visited[out->visited_count++] = id;
}

static omc_bmff_derived_graph
omc_bmff_validate_derived_graph(const omc_bmff_primary_props *p, omc_u32 id)
{
    omc_bmff_derived_graph out;
    memset(&out, 0, sizeof(out));
    out.references_truncated = p->edge_truncated;
    omc_bmff_derived_visit(p, id, &out);
    return out;
}

static int omc_bmff_derived_graph_valid(const omc_bmff_derived_graph *g)
{
    return !g->cycle && !g->self_reference && !g->depth_exceeded &&
           !g->references_truncated && !g->missing_source_count;
}

static const char *omc_bmff_derived_construction(omc_u32 type)
{
    switch (type) {
    case OMC_BMFF_FOURCC('g', 'r', 'i', 'd'):
        return "grid";
    case OMC_BMFF_FOURCC('i', 'o', 'v', 'l'):
        return "overlay";
    case OMC_BMFF_FOURCC('i', 'd', 'e', 'n'):
        return "identity";
    default:
        return "";
    }
}

static omc_u32 omc_bmff_count_sources(const omc_bmff_primary_props *p, omc_u32 id)
{
    omc_u32 i;
    omc_u32 count;
    count = 0U;
    for (i = 0U; i < p->edge_count; ++i)
        if (p->edges[i].ref_type == OMC_BMFF_FOURCC('d', 'i', 'm', 'g') &&
            p->edges[i].from_item_id == id)
            ++count;
    return count;
}

static int omc_bmff_get_source(const omc_bmff_primary_props *p, omc_u32 id,
                               omc_u32 index, omc_u32 *source)
{
    omc_u32 i;
    for (i = 0U; i < p->edge_count; ++i) {
        if (p->edges[i].ref_type == OMC_BMFF_FOURCC('d', 'i', 'm', 'g') &&
            p->edges[i].from_item_id == id) {
            if (!index) {
                *source = p->edges[i].to_item_id;
                return 1;
            }
            --index;
        }
    }
    return 0;
}

static int omc_bmff_emit_fourcc(omc_bmff_ctx *ctx, const char *key, omc_u32 value)
{
    char text[11];
    omc_u16 length;
    if (!omc_bmff_fourcc_display_name(value, text, &length))
        return 0;
    return omc_bmff_emit_text_field(ctx, key, text, length);
}

static int omc_bmff_emit_derived(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p)
{
    omc_u32 item_count;
    omc_u32 grid_count;
    omc_u32 overlay_count;
    omc_u32 identity_count;
    omc_u32 graph_invalid_count;
    omc_u32 graph_cycle_count;
    omc_u32 graph_missing_source_count;
    omc_u32 graph_self_reference_count;
    omc_u32 graph_depth_exceeded_count;
    omc_u32 graph_reference_truncated_count;
    item_count = 0U;
    grid_count = 0U;
    overlay_count = 0U;
    identity_count = 0U;
    {
        omc_u32 i;
        i = 0U;
        for (; i < p->item_count; ++i) {
            const omc_bmff_item_info *info;
            info = &p->items[i];
            if ((!info->have_type) ||
                (!(*omc_bmff_derived_construction(info->item_type)))) {
                continue;
            }
            item_count += 1U;
            if (info->item_type == OMC_BMFF_FOURCC('g', 'r', 'i', 'd')) {
                grid_count += 1U;
            } else if (info->item_type == OMC_BMFF_FOURCC('i', 'o', 'v', 'l')) {
                overlay_count += 1U;
            } else {
                identity_count += 1U;
            }
        }
    }
    if (item_count == 0U) {
        return 1;
    }
    if (!omc_bmff_emit_u32_field(ctx, "derived_image.count", item_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.grid_count", grid_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.overlay_count", overlay_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.identity_count", identity_count))
        return 0;
    graph_invalid_count = 0U;
    graph_cycle_count = 0U;
    graph_missing_source_count = 0U;
    graph_self_reference_count = 0U;
    graph_depth_exceeded_count = 0U;
    graph_reference_truncated_count = 0U;
    {
        omc_u32 i;
        i = 0U;
        for (; i < p->item_count; ++i) {
            const omc_bmff_item_info *info;
            const char *construction;
            omc_u32 source_count;
            omc_bmff_derived_graph graph;
            int graph_valid;
            int descriptor_required;
            int descriptor_available;
            int descriptor_valid;
            int source_count_valid;
            int construction_valid;
            omc_u32 output_width;
            omc_u32 output_height;
            omc_u16 grid_rows;
            omc_u16 grid_columns;
            omc_u32 descriptor_reference_depth;
            info = &p->items[i];
            if (!info->have_type) {
                continue;
            }
            construction = omc_bmff_derived_construction(info->item_type);
            if (!(*construction)) {
                continue;
            }
            source_count = omc_bmff_count_sources(p, info->item_id);
            graph = omc_bmff_validate_derived_graph(p, info->item_id);
            graph_valid = omc_bmff_derived_graph_valid(&graph);
            if (!graph_valid) {
                graph_invalid_count += 1U;
            }
            if (graph.cycle) {
                graph_cycle_count += 1U;
            }
            if (graph.missing_source_count != 0U) {
                graph_missing_source_count += graph.missing_source_count;
            }
            if (graph.self_reference) {
                graph_self_reference_count += 1U;
            }
            if (graph.depth_exceeded) {
                graph_depth_exceeded_count += 1U;
            }
            if (graph.references_truncated) {
                graph_reference_truncated_count += 1U;
            }
            if (!omc_bmff_emit_u32_field(ctx, "derived_image.item_id", info->item_id))
                return 0;
            if (!omc_bmff_emit_u32_field(ctx, "derived_image.type", info->item_type))
                return 0;
            if (!omc_bmff_emit_fourcc(ctx, "derived_image.type_name", info->item_type))
                return 0;
            if (!omc_bmff_emit_text_field(ctx, "derived_image.construction",
                                          construction, (omc_u16)strlen(construction)))
                return 0;
            if (!omc_bmff_emit_u32_field(ctx, "derived_image.source_count",
                                         source_count))
                return 0;
            if (!omc_bmff_emit_u32_field(ctx, "derived_image.graph_max_depth",
                                         graph.max_depth))
                return 0;
            if (!omc_bmff_emit_u32_field(ctx,
                                         "derived_image.graph_missing_source_count",
                                         graph.missing_source_count))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.graph_cycle",
                                        (graph.cycle) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.graph_self_reference",
                                        (graph.self_reference) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.graph_depth_exceeded",
                                        (graph.depth_exceeded) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.graph_references_truncated",
                                        (graph.references_truncated) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.graph_valid",
                                        (graph_valid) ? (1U) : (0U)))
                return 0;
            {
                omc_u32 source_i;
                source_i = 0U;
                for (; source_i < source_count; ++source_i) {
                    omc_u32 source_id;
                    source_id = 0U;
                    if (!omc_bmff_get_source(p, info->item_id, source_i, &source_id)) {
                        continue;
                    }
                    if (!omc_bmff_emit_u32_field(ctx, "derived_image.source_index",
                                                 source_i))
                        return 0;
                    if (!omc_bmff_emit_u32_field(ctx, "derived_image.source_item_id",
                                                 source_id))
                        return 0;
                }
            }
            descriptor_required = 1;
            descriptor_available = 0;
            descriptor_valid = 0;
            source_count_valid = 0;
            construction_valid = 0;
            output_width = 0U;
            output_height = 0U;
            grid_rows = 0U;
            grid_columns = 0U;
            descriptor_reference_depth = 0U;
            if (info->item_type == OMC_BMFF_FOURCC('g', 'r', 'i', 'd')) {
                omc_bmff_descriptor grid;
                omc_u32 expected_sources;
                grid = omc_bmff_parse_descriptor(ctx, p, info->item_id, 0, 0U);
                descriptor_available = grid.available;
                descriptor_valid = grid.valid;
                descriptor_reference_depth = grid.reference_depth;
                expected_sources = ((omc_u32)grid.rows) * ((omc_u32)grid.columns);
                source_count_valid = grid.valid && (source_count == expected_sources);
                construction_valid = descriptor_valid && source_count_valid;
                output_width = grid.output_width;
                output_height = grid.output_height;
                grid_rows = grid.rows;
                grid_columns = grid.columns;
                if (!omc_bmff_emit_u32_field(ctx, "derived_image.grid.item_id",
                                             info->item_id))
                    return 0;
                if (grid.have_header) {
                    if (!omc_bmff_emit_u8_field(
                            ctx, "derived_image.grid.descriptor_version", grid.version))
                        return 0;
                    if (!omc_bmff_emit_u8_field(
                            ctx, "derived_image.grid.descriptor_flags", grid.flags))
                        return 0;
                    if (!omc_bmff_emit_u8_field(ctx, "derived_image.grid.field_bits",
                                                ((grid.flags & 1U) != 0U) ? (32U)
                                                                          : (16U)))
                        return 0;
                }
                if (grid.valid) {
                    if (!omc_bmff_emit_u16_field(ctx, "derived_image.grid.rows",
                                                 grid.rows))
                        return 0;
                    if (!omc_bmff_emit_u16_field(ctx, "derived_image.grid.columns",
                                                 grid.columns))
                        return 0;
                    if (!omc_bmff_emit_u32_field(ctx, "derived_image.grid.output_width",
                                                 grid.output_width))
                        return 0;
                    if (!omc_bmff_emit_u32_field(ctx,
                                                 "derived_image.grid.output_height",
                                                 grid.output_height))
                        return 0;
                    if (!omc_bmff_emit_u32_field(
                            ctx, "derived_image.grid.expected_source_count",
                            expected_sources))
                        return 0;
                    {
                        omc_u32 source_i;
                        source_i = 0U;
                        for (; source_i < source_count; ++source_i) {
                            omc_u32 source_id;
                            int source_in_grid;
                            source_id = 0U;
                            if (!omc_bmff_get_source(p, info->item_id, source_i,
                                                     &source_id)) {
                                continue;
                            }
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "derived_image.grid.source_index", source_i))
                                return 0;
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "derived_image.grid.source_item_id",
                                    source_id))
                                return 0;
                            source_in_grid = source_i < expected_sources;
                            if (!omc_bmff_emit_u8_field(
                                    ctx, "derived_image.grid.source_in_grid",
                                    (source_in_grid) ? (1U) : (0U)))
                                return 0;
                            if (!source_in_grid) {
                                continue;
                            }
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "derived_image.grid.source_row",
                                    source_i / grid.columns))
                                return 0;
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "derived_image.grid.source_column",
                                    source_i % grid.columns))
                                return 0;
                        }
                    }
                }
            } else if (info->item_type == OMC_BMFF_FOURCC('i', 'o', 'v', 'l')) {
                omc_bmff_descriptor overlay;
                overlay =
                    omc_bmff_parse_descriptor(ctx, p, info->item_id, 1, source_count);
                descriptor_available = overlay.available;
                descriptor_valid = overlay.valid;
                descriptor_reference_depth = overlay.reference_depth;
                source_count_valid = source_count > 0U;
                construction_valid = descriptor_valid && source_count_valid;
                output_width = overlay.output_width;
                output_height = overlay.output_height;
                if (!omc_bmff_emit_u32_field(ctx, "derived_image.overlay.item_id",
                                             info->item_id))
                    return 0;
                if (overlay.have_header) {
                    if (!omc_bmff_emit_u8_field(
                            ctx, "derived_image.overlay.descriptor_version",
                            overlay.version))
                        return 0;
                    if (!omc_bmff_emit_u8_field(
                            ctx, "derived_image.overlay.descriptor_flags",
                            overlay.flags))
                        return 0;
                    if (!omc_bmff_emit_u8_field(ctx, "derived_image.overlay.field_bits",
                                                ((overlay.flags & 1U) != 0U) ? (32U)
                                                                             : (16U)))
                        return 0;
                }
                if (overlay.valid) {
                    static const char *kBackgroundFields[4] = {
                        "derived_image.overlay.background_r",
                        "derived_image.overlay.background_g",
                        "derived_image.overlay.background_b",
                        "derived_image.overlay.background_a"};
                    if (!omc_bmff_emit_u32_field(ctx,
                                                 "derived_image.overlay.output_width",
                                                 overlay.output_width))
                        return 0;
                    if (!omc_bmff_emit_u32_field(ctx,
                                                 "derived_image.overlay.output_height",
                                                 overlay.output_height))
                        return 0;
                    {
                        omc_u32 channel;
                        channel = 0U;
                        for (; channel < 4U; ++channel) {
                            if (!omc_bmff_emit_u16_field(ctx,
                                                         kBackgroundFields[channel],
                                                         overlay.background[channel]))
                                return 0;
                        }
                    }
                    {
                        omc_u32 source_i;
                        source_i = 0U;
                        for (; source_i < source_count; ++source_i) {
                            omc_u32 source_id;
                            source_id = 0U;
                            if (!omc_bmff_get_source(p, info->item_id, source_i,
                                                     &source_id)) {
                                continue;
                            }
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "derived_image.overlay.source_index",
                                    source_i))
                                return 0;
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "derived_image.overlay.source_item_id",
                                    source_id))
                                return 0;
                            if (!omc_bmff_emit_i32_field(
                                    ctx, "derived_image.overlay.offset_x",
                                    overlay.offset_x[source_i]))
                                return 0;
                            if (!omc_bmff_emit_i32_field(
                                    ctx, "derived_image.overlay.offset_y",
                                    overlay.offset_y[source_i]))
                                return 0;
                        }
                    }
                }
            } else {
                omc_u32 source_id;
                int have_source;
                descriptor_required = 0;
                descriptor_valid = 1;
                source_count_valid = source_count == 1U;
                source_id = 0U;
                have_source = omc_bmff_get_source(p, info->item_id, 0U, &source_id);
                construction_valid =
                    (source_count_valid && have_source) && (source_id != info->item_id);
                if (!omc_bmff_emit_u32_field(ctx, "derived_image.identity.item_id",
                                             info->item_id))
                    return 0;
                if (have_source) {
                    if (!omc_bmff_emit_u32_field(
                            ctx, "derived_image.identity.source_item_id", source_id))
                        return 0;
                }
            }
            construction_valid = construction_valid && graph_valid;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.descriptor_required",
                                        (descriptor_required) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.descriptor_available",
                                        (descriptor_available) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.descriptor_valid",
                                        (descriptor_valid) ? (1U) : (0U)))
                return 0;
            if (descriptor_available) {
                if (!omc_bmff_emit_u32_field(ctx,
                                             "derived_image.descriptor_reference_depth",
                                             descriptor_reference_depth))
                    return 0;
            }
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.source_count_valid",
                                        (source_count_valid) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "derived_image.construction_valid",
                                        (construction_valid) ? (1U) : (0U)))
                return 0;
            if (p->have_item_id && (info->item_id == p->item_id)) {
                if (!omc_bmff_emit_text_field(ctx, "primary.derived_construction",
                                              construction,
                                              (omc_u16)strlen(construction)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "primary.derived_descriptor_valid",
                                            (descriptor_valid) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "primary.derived_source_count_valid",
                                            (source_count_valid) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "primary.derived_construction_valid",
                                            (construction_valid) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "primary.derived_graph_valid",
                                            (graph_valid) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "primary.derived_graph_cycle",
                                            (graph.cycle) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "primary.derived_graph_self_reference",
                                            (graph.self_reference) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "primary.derived_graph_depth_exceeded",
                                            (graph.depth_exceeded) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(
                        ctx, "primary.derived_graph_references_truncated",
                        (graph.references_truncated) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u32_field(ctx, "primary.derived_graph_max_depth",
                                             graph.max_depth))
                    return 0;
                if (!omc_bmff_emit_u32_field(
                        ctx, "primary.derived_graph_missing_source_count",
                        graph.missing_source_count))
                    return 0;
                if (descriptor_valid &&
                    (info->item_type != OMC_BMFF_FOURCC('i', 'd', 'e', 'n'))) {
                    if (!omc_bmff_emit_u32_field(ctx, "primary.derived_output_width",
                                                 output_width))
                        return 0;
                    if (!omc_bmff_emit_u32_field(ctx, "primary.derived_output_height",
                                                 output_height))
                        return 0;
                }
                if (info->item_type == OMC_BMFF_FOURCC('g', 'r', 'i', 'd')) {
                    if (descriptor_valid) {
                        if (!omc_bmff_emit_u16_field(ctx, "primary.derived_grid_rows",
                                                     grid_rows))
                            return 0;
                        if (!omc_bmff_emit_u16_field(
                                ctx, "primary.derived_grid_columns", grid_columns))
                            return 0;
                    }
                }
            }
        }
    }
    if (!omc_bmff_emit_count(ctx, "derived_image.graph_invalid_count",
                             graph_invalid_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.graph_cycle_count", graph_cycle_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.graph_missing_source_count_total",
                             graph_missing_source_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.graph_self_reference_count",
                             graph_self_reference_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.graph_depth_exceeded_count",
                             graph_depth_exceeded_count))
        return 0;
    if (!omc_bmff_emit_count(ctx, "derived_image.graph_references_truncated_count",
                             graph_reference_truncated_count))
        return 0;
    return 1;
}

typedef struct omc_bmff_tile_association {
    omc_u32 property_index;
    omc_u32 property_type;
    omc_u8 essential;
    int have_property_type;
} omc_bmff_tile_association;

typedef struct omc_bmff_tilc {
    omc_u32 index;
    omc_u32 flags;
    omc_u32 tile_width;
    omc_u32 tile_height;
    omc_u32 conditional_payload_bytes;
    omc_u64 conditional_payload_offset;
    omc_u64 conditional_payload_end;
    omc_u32 dimension_sizes[8];
    omc_u8 version;
    omc_u8 extra_dimension_count;
    omc_u8 stored_dimension_count;
    int have_header;
    int have_core;
    int core_valid;
    int dimensions_truncated;
} omc_bmff_tilc;

typedef struct omc_bmff_tile_url {
    char bytes[512];
    omc_u32 stored_bytes;
    omc_u32 total_bytes;
    int truncated;
} omc_bmff_tile_url;

typedef struct omc_bmff_data_ref {
    omc_u32 index;
    omc_u32 type;
    omc_u32 flags;
    omc_u64 input_item_count;
    omc_u64 tile_offset_table_start;
    omc_u32 tile_offset_table_size;
    omc_u64 tile_id_start;
    omc_u16 directory_id_start;
    omc_u16 directory_id_end;
    omc_u8 version;
    omc_u8 offset_field_bytes;
    omc_u8 size_field_bytes;
    omc_u8 input_item_count_field_bytes;
    int have_header;
    int syntax_valid;
    int sequential_order;
    int external_tiles_urls;
    int directory_id_present;
    omc_bmff_tile_url base_url;
    omc_bmff_tile_url url_extension;
    omc_bmff_tile_url tile_request_template;
} omc_bmff_data_ref;

typedef struct omc_bmff_tile_row {
    omc_u64 tile_index;
    omc_u64 start_offset;
    omc_u64 size;
    int have_size;
    int empty;
} omc_bmff_tile_row;

typedef struct omc_bmff_tiled_state {
    omc_u32 item_id;
    omc_u32 configuration_count;
    omc_u32 ispe_count;
    omc_u32 tile_columns;
    omc_u32 tile_rows;
    omc_u64 expected_tile_count;
    const omc_bmff_tilc *configuration;
    const omc_bmff_ispe_prop *ispe;
    const omc_bmff_location *location;
    const omc_bmff_data_ref *data_reference;
    omc_u8 configuration_essential;
    omc_u32 tile_item_type;
    omc_u32 tipa_flags;
    omc_u32 tile_property_association_count;
    omc_u32 stored_tile_property_association_count;
    omc_u64 logical_item_data_size;
    omc_u64 expected_offset_table_size;
    omc_u64 empty_tile_count;
    omc_u32 stored_offset_row_count;
    omc_u8 tipa_version;
    omc_bmff_tile_association tile_property_associations[64];
    omc_bmff_tile_row offset_rows[64];
    int configuration_valid;
    int tile_count_overflow;
    int have_tile_grid;
    int layout_valid;
    int data_reference_present;
    int data_reference_valid;
    int input_item_count_matches;
    int conditional_payload_valid;
    int tipa_present;
    int tipa_valid;
    int tile_property_associations_truncated;
    int offset_table_present;
    int offset_table_valid;
    int offset_table_validation_truncated;
    int tile_offsets_valid;
    int tile_sizes_validated;
    int complete_configuration_valid;
    omc_bmff_tilc config_storage;
    omc_bmff_ispe_prop ispe_storage;
    omc_bmff_location location_storage;
    omc_bmff_data_ref reference_storage;
} omc_bmff_tiled_state;

static int omc_bmff_property_box(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                 omc_u32 index, omc_bmff_box *box)
{
    omc_u64 off;
    omc_u64 end;
    omc_u32 i;
    if (!p->have_ipco_summary || !index)
        return 0;
    off = p->ipco.offset + p->ipco.header_size;
    end = p->ipco.offset + p->ipco.size;
    for (i = 1U; off + 8U <= end; ++i) {
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, end, box))
            return 0;
        if (i == index)
            return 1;
        off += box->size;
    }
    return 0;
}

static void omc_bmff_parse_tilc(omc_bmff_ctx *ctx, const omc_bmff_box *box,
                                omc_bmff_tilc *out)
{
    omc_u64 off;
    omc_u64 size;
    omc_u32 header;
    omc_u32 i;
    int nonzero;
    off = box->offset + box->header_size;
    size = box->size - box->header_size;
    if (size >= 4U && omc_bmff_read_u32be(ctx->bytes, ctx->size, off, &header)) {
        out->have_header = 1;
        out->version = (omc_u8)(header >> 24U);
        out->flags = header & 0xFFFFFFU;
    }
    if (size < 13U)
        return;
    if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, off + 4U, &out->tile_width) ||
        !omc_bmff_read_u32be(ctx->bytes, ctx->size, off + 8U, &out->tile_height))
        return;
    out->extra_dimension_count = omc_input_byte(ctx->bytes, off + 12U);
    out->dimensions_truncated = out->extra_dimension_count > 8U;
    if (13U + (omc_u32)out->extra_dimension_count * 4U > size) {
        out->dimensions_truncated = 1;
        return;
    }
    out->have_core = 1;
    out->stored_dimension_count =
        out->extra_dimension_count > 8U ? 8U : out->extra_dimension_count;
    nonzero = 1;
    for (i = 0U; i < out->extra_dimension_count; ++i) {
        omc_u32 dimension;
        if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, off + 13U + i * 4U,
                                 &dimension)) {
            out->have_core = 0;
            return;
        }
        if (!dimension)
            nonzero = 0;
        if (i < out->stored_dimension_count)
            out->dimension_sizes[i] = dimension;
    }
    out->conditional_payload_offset =
        off + 13U + (omc_u32)out->extra_dimension_count * 4U;
    out->conditional_payload_end = off + size;
    size = out->conditional_payload_end - out->conditional_payload_offset;
    out->conditional_payload_bytes = size > 0xFFFFFFFFU ? 0xFFFFFFFFU : (omc_u32)size;
    out->core_valid = !out->version && !out->flags && out->tile_width &&
                      out->tile_height && !out->dimensions_truncated && nonzero;
}

static int omc_bmff_tile_url_read(omc_bmff_ctx *ctx, omc_u64 *off, omc_u64 end,
                                  omc_bmff_tile_url *out)
{
    omc_u64 start;
    omc_u32 cp;
    omc_u32 minimum;
    omc_u8 remaining;
    start = *off;
    cp = 0U;
    minimum = 0U;
    remaining = 0U;
    memset(out, 0, sizeof(*out));
    while (*off < end) {
        omc_u8 c;
        c = omc_input_byte(ctx->bytes, *off);
        if (!c) {
            if (remaining)
                goto invalid;
            ++*off;
            out->truncated = out->stored_bytes != out->total_bytes;
            return 1;
        }
        if (*off - start >= (1U << 20))
            goto invalid;
        if (remaining) {
            if ((c & 0xC0U) != 0x80U)
                goto invalid;
            cp = (cp << 6U) | (c & 0x3FU);
            if (!--remaining &&
                (cp < minimum || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)))
                goto invalid;
        } else if (c >= 0x80U) {
            if (c >= 0xC2U && c <= 0xDFU) {
                cp = c & 31U;
                remaining = 1U;
                minimum = 0x80U;
            } else if (c >= 0xE0U && c <= 0xEFU) {
                cp = c & 15U;
                remaining = 2U;
                minimum = 0x800U;
            } else if (c >= 0xF0U && c <= 0xF4U) {
                cp = c & 7U;
                remaining = 3U;
                minimum = 0x10000U;
            } else
                goto invalid;
        }
        if (out->stored_bytes < 512U)
            out->bytes[out->stored_bytes++] = (char)c;
        ++out->total_bytes;
        ++*off;
    }
invalid:
    memset(out, 0, sizeof(*out));
    return 0;
}

static int omc_bmff_parse_deti(omc_bmff_ctx *ctx, const omc_bmff_box *box,
                               omc_bmff_data_ref *out)
{
    static const omc_u8 offsets[4] = {4U, 5U, 6U, 8U};
    static const omc_u8 sizes[4] = {0U, 3U, 4U, 8U};
    static const omc_u8 counts[4] = {1U, 2U, 4U, 8U};
    omc_u64 off;
    omc_u64 end;
    omc_u64 v;
    off = box->offset + box->header_size;
    end = box->offset + box->size;
    if (!omc_bmff_uint(ctx, &off, end, 4U, &v))
        return 0;
    out->have_header = 1;
    out->version = (omc_u8)(v >> 24U);
    out->flags = (omc_u32)(v & 0xFFFFFFU);
    if (out->version || out->flags > 255U)
        return 0;
    out->offset_field_bytes = offsets[out->flags & 3U];
    out->size_field_bytes = sizes[(out->flags >> 2U) & 3U];
    out->input_item_count_field_bytes = counts[(out->flags >> 5U) & 3U];
    out->sequential_order = (out->flags & 16U) != 0U;
    out->external_tiles_urls = (out->flags & 128U) != 0U;
    if (!omc_bmff_uint(ctx, &off, end, out->input_item_count_field_bytes,
                       &out->input_item_count))
        return 0;
    if (out->external_tiles_urls) {
        if (!omc_bmff_uint(ctx, &off, end, 1U, &v) || v > 1U)
            return 0;
        out->directory_id_present = (int)v;
        if (out->directory_id_present) {
            if (!omc_bmff_uint(ctx, &off, end, 2U, &v))
                return 0;
            out->directory_id_start = (omc_u16)v;
            if (!omc_bmff_uint(ctx, &off, end, 2U, &v))
                return 0;
            out->directory_id_end = (omc_u16)v;
        }
        if (!omc_bmff_uint(ctx, &off, end, 8U, &out->tile_id_start) ||
            !omc_bmff_tile_url_read(ctx, &off, end, &out->base_url) ||
            !omc_bmff_tile_url_read(ctx, &off, end, &out->url_extension) ||
            !omc_bmff_tile_url_read(ctx, &off, end, &out->tile_request_template))
            return 0;
    } else {
        if (!omc_bmff_uint(ctx, &off, end, out->offset_field_bytes,
                           &out->tile_offset_table_start) ||
            !omc_bmff_uint(ctx, &off, end, 4U, &v))
            return 0;
        out->tile_offset_table_size = (omc_u32)v;
    }
    out->syntax_valid = off == end;
    return out->syntax_valid;
}

static int omc_bmff_find_data_ref(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                  omc_u16 index, omc_bmff_data_ref *out, int *valid)
{
    omc_u64 off;
    omc_u64 end;
    omc_bmff_box dref;
    omc_u32 header;
    omc_u32 count;
    omc_u32 i;
    int have;
    *valid = 0;
    if (!p->dinf.size || !index || index > 128U)
        return 0;
    off = p->dinf.offset + p->dinf.header_size;
    end = p->dinf.offset + p->dinf.size;
    have = 0;
    while (off + 8U <= end) {
        omc_bmff_box box;
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, end, &box))
            return 0;
        if (box.type == OMC_BMFF_FOURCC('d', 'r', 'e', 'f')) {
            if (have)
                return 0;
            dref = box;
            have = 1;
        }
        off += box.size;
    }
    if (off != end || !have)
        return 0;
    off = dref.offset + dref.header_size;
    end = dref.offset + dref.size;
    if (end - off < 8U || !omc_bmff_read_u32be(ctx->bytes, ctx->size, off, &header) ||
        header || !omc_bmff_read_u32be(ctx->bytes, ctx->size, off + 4U, &count) ||
        count > (1U << 16))
        return 0;
    off += 8U;
    have = 0;
    for (i = 0U; i < count; ++i) {
        omc_bmff_box box;
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, off, end, &box))
            return have;
        if (i + 1U == index) {
            memset(out, 0, sizeof(*out));
            out->index = index;
            out->type = box.type;
            if (box.type == OMC_BMFF_FOURCC('d', 'e', 't', 'i'))
                (void)omc_bmff_parse_deti(ctx, &box, out);
            else
                out->syntax_valid = 1;
            have = 1;
        }
        off += box.size;
    }
    *valid = off == end;
    return have;
}

static int omc_bmff_checked_add(omc_u64 a, omc_u64 b, omc_u64 *out)
{
    if (b > ~(omc_u64)0 - a)
        return 0;
    *out = a + b;
    return 1;
}
static int omc_bmff_checked_mul(omc_u64 a, omc_u64 b, omc_u64 *out)
{
    if (a && b > ~(omc_u64)0 / a)
        return 0;
    *out = a * b;
    return 1;
}

static int omc_bmff_tile_conditional(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                     omc_bmff_tiled_state *state)
{
    const omc_bmff_tilc *config;
    const omc_bmff_data_ref *data_ref;
    omc_u64 p_offset;
    omc_bmff_box tipa;
    omc_u64 payload_off;
    omc_u64 payload_end;
    omc_u8 association_bytes;
    omc_u64 expected_payload_size;
    omc_u64 association_offset;
    int associations_valid;
    if (((!state) || (!state->configuration)) || (!state->data_reference)) {
        return 0;
    }
    config = state->configuration;
    data_ref = state->data_reference;
    if ((config->conditional_payload_offset > config->conditional_payload_end) ||
        (config->conditional_payload_end > ctx->size)) {
        return 0;
    }
    if (data_ref->external_tiles_urls) {
        state->conditional_payload_valid =
            config->conditional_payload_offset == config->conditional_payload_end;
        return state->conditional_payload_valid;
    }
    p_offset = config->conditional_payload_offset;
    if (config->conditional_payload_end - p_offset < 4U)
        return 0;
    if (!omc_bmff_read_u32be(ctx->bytes, ctx->size, p_offset, &state->tile_item_type)) {
        return 0;
    }
    p_offset += 4U;
    memset(&tipa, 0, sizeof(tipa));
    if (((!omc_bmff_parse_box(ctx->bytes, ctx->size, p_offset,
                              config->conditional_payload_end, &tipa)) ||
         (tipa.type != OMC_BMFF_FOURCC('t', 'i', 'p', 'a'))) ||
        ((tipa.offset + tipa.size) != config->conditional_payload_end)) {
        return 0;
    }
    state->tipa_present = 1;
    payload_off = tipa.offset + tipa.header_size;
    payload_end = tipa.offset + tipa.size;
    if ((payload_off + 5U) > payload_end) {
        return 0;
    }
    state->tipa_version = omc_input_byte(ctx->bytes, payload_off);
    state->tipa_flags =
        ((((omc_u32)omc_input_byte(ctx->bytes, payload_off + 1U)) << 16U) |
         (((omc_u32)omc_input_byte(ctx->bytes, payload_off + 2U)) << 8U)) |
        ((omc_u32)omc_input_byte(ctx->bytes, payload_off + 3U));
    if ((state->tipa_version != 0U) || (state->tipa_flags > 1U)) {
        return 0;
    }
    state->tile_property_association_count =
        omc_input_byte(ctx->bytes, payload_off + 4U);
    association_bytes = (state->tipa_flags == 0U) ? (1U) : (2U);
    expected_payload_size = 0U;
    if (((!omc_bmff_checked_mul(state->tile_property_association_count,
                                association_bytes, &expected_payload_size)) ||
         (!omc_bmff_checked_add(expected_payload_size, 5U, &expected_payload_size))) ||
        (expected_payload_size != (payload_end - payload_off))) {
        return 0;
    }
    association_offset = payload_off + 5U;
    associations_valid = 1;
    {
        omc_u32 i;
        i = 0U;
        for (; i < state->tile_property_association_count; ++i) {
            omc_u16 raw;
            omc_u16 essential_mask;
            omc_u16 index_mask;
            omc_bmff_tile_association association;
            raw = 0U;
            if (association_bytes == 1U) {
                raw = omc_input_byte(ctx->bytes, association_offset);
            } else if (!omc_bmff_read_u16be(ctx->bytes, ctx->size, association_offset,
                                            &raw)) {
                return 0;
            }
            association_offset += association_bytes;
            essential_mask = (association_bytes == 1U) ? (0x80U) : (0x8000U);
            index_mask = (association_bytes == 1U) ? (0x7FU) : (0x7FFFU);
            memset(&association, 0, sizeof(association));
            association.essential = ((raw & essential_mask) != 0U) ? (1U) : (0U);
            association.property_index = raw & index_mask;
            if (((association.property_index == 0U) && (association.essential != 0U)) ||
                (association.property_index > p->property_count)) {
                associations_valid = 0;
            }
            {
                omc_bmff_box property;
                if ((association.property_index <= 256U) &&
                    omc_bmff_property_box(ctx, p, association.property_index,
                                          &property)) {
                    association.property_type = property.type;
                    association.have_property_type = 1;
                }
            }
            if (state->stored_tile_property_association_count < 64U) {
                state->tile_property_associations
                    [state->stored_tile_property_association_count] = association;
                state->stored_tile_property_association_count += 1U;
            } else {
                state->tile_property_associations_truncated = 1;
            }
        }
    }
    state->tipa_valid = associations_valid;
    state->conditional_payload_valid = state->tipa_valid;
    return state->conditional_payload_valid;
}

static int omc_bmff_tile_offsets(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                 omc_bmff_tiled_state *state)
{
    const omc_bmff_data_ref *data_ref;
    omc_u64 record_bytes;
    omc_bmff_cursor cursor;
    omc_u64 table_end;
    int offsets_valid;
    int have_previous_offset;
    omc_u64 previous_offset;
    omc_u32 previous_stored_row;
    if ((((!state) || (!state->location)) || (!state->data_reference)) ||
        state->data_reference->external_tiles_urls) {
        return 0;
    }
    data_ref = state->data_reference;
    state->offset_table_present = 1;
    if (state->location->construction_method != 0U) {
        return 0;
    }
    record_bytes = 0U;
    if (((!omc_bmff_checked_add(data_ref->offset_field_bytes,
                                data_ref->size_field_bytes, &record_bytes)) ||
         (!omc_bmff_checked_mul(state->expected_tile_count, record_bytes,
                                &state->expected_offset_table_size))) ||
        (state->expected_offset_table_size != data_ref->tile_offset_table_size)) {
        return 0;
    }
    if (state->expected_tile_count > (1U << 18U)) {
        state->offset_table_validation_truncated = 1;
        return 0;
    }
    memset(&cursor, 0, sizeof(cursor));
    if (!omc_bmff_cursor_init_ref(ctx, p, state->item_id,
                                  state->location->data_reference_index, &cursor)) {
        return 0;
    }
    state->logical_item_data_size = cursor.size;
    table_end = 0U;
    if ((!omc_bmff_checked_add(data_ref->tile_offset_table_start,
                               data_ref->tile_offset_table_size, &table_end)) ||
        (table_end > cursor.size)) {
        return 0;
    }
    cursor.position = data_ref->tile_offset_table_start;
    offsets_valid = 1;
    have_previous_offset = 0;
    previous_offset = 0U;
    previous_stored_row = 0xFFFFFFFFU;
    {
        omc_u64 i;
        i = 0U;
        for (; i < state->expected_tile_count; ++i) {
            omc_u64 start_offset;
            omc_u64 tile_size;
            int empty;
            int offset_in_range;
            start_offset = 0U;
            tile_size = 0U;
            if ((!omc_bmff_cursor_uint64(&cursor, data_ref->offset_field_bytes,
                                         &start_offset)) ||
                ((data_ref->size_field_bytes != 0U) &&
                 (!omc_bmff_cursor_uint64(&cursor, data_ref->size_field_bytes,
                                          &tile_size)))) {
                return 0;
            }
            empty = start_offset == 0xFFFFFFFFU;
            offset_in_range = (!empty) && (start_offset <= cursor.size);
            if (empty) {
                state->empty_tile_count += 1U;
            } else if (start_offset > cursor.size) {
                offsets_valid = 0;
            } else if (data_ref->size_field_bytes != 0U) {
                if (tile_size > (cursor.size - start_offset)) {
                    offsets_valid = 0;
                }
            } else if (data_ref->sequential_order) {
                if (have_previous_offset && (start_offset <= previous_offset)) {
                    offsets_valid = 0;
                }
                if (have_previous_offset && (previous_stored_row != 0xFFFFFFFFU)) {
                    omc_bmff_tile_row *previous;
                    previous = &state->offset_rows[previous_stored_row];
                    if (start_offset > previous_offset) {
                        previous->size = start_offset - previous_offset;
                        previous->have_size = 1;
                    }
                }
                have_previous_offset = 1;
                previous_offset = start_offset;
            }
            if (state->stored_offset_row_count < 64U) {
                omc_bmff_tile_row *row;
                row = &state->offset_rows[state->stored_offset_row_count];
                row->tile_index = i;
                row->start_offset = start_offset;
                row->size = tile_size;
                row->have_size = data_ref->size_field_bytes != 0U;
                row->empty = empty;
                if (((data_ref->size_field_bytes == 0U) &&
                     data_ref->sequential_order) &&
                    offset_in_range) {
                    previous_stored_row = state->stored_offset_row_count;
                }
                state->stored_offset_row_count += 1U;
            } else if (((data_ref->size_field_bytes == 0U) &&
                        data_ref->sequential_order) &&
                       offset_in_range) {
                previous_stored_row = 0xFFFFFFFFU;
            }
        }
    }
    if ((((data_ref->size_field_bytes == 0U) && data_ref->sequential_order) &&
         have_previous_offset) &&
        (previous_stored_row != 0xFFFFFFFFU)) {
        omc_bmff_tile_row *last;
        last = &state->offset_rows[previous_stored_row];
        last->size = cursor.size - previous_offset;
        last->have_size = 1;
    }
    state->tile_offsets_valid = offsets_valid;
    state->tile_sizes_validated =
        offsets_valid &&
        ((data_ref->size_field_bytes != 0U) || data_ref->sequential_order);
    state->offset_table_valid = offsets_valid && (cursor.position == table_end);
    return state->offset_table_valid;
}

static void omc_bmff_tile_state(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p,
                                omc_u32 item_id, int complete,
                                omc_bmff_tiled_state *out)
{
    omc_u32 i;
    omc_u64 count;
    int references_valid;
    memset(out, 0, sizeof(*out));
    out->item_id = item_id;
    for (i = 0U; i < p->ipma_association_count; ++i) {
        const omc_bmff_ipma_assoc *a;
        omc_bmff_box box;
        a = &p->ipma_associations[i];
        if (a->item_id != item_id || !a->have_property_type)
            continue;
        if (a->property_type == OMC_BMFF_FOURCC('t', 'i', 'l', 'C')) {
            ++out->configuration_count;
            if (!out->configuration &&
                omc_bmff_property_box(ctx, p, a->property_index, &box)) {
                out->config_storage.index = a->property_index;
                omc_bmff_parse_tilc(ctx, &box, &out->config_storage);
                out->configuration = &out->config_storage;
                out->configuration_essential = a->essential;
            }
        } else if (a->property_type == OMC_BMFF_FOURCC('i', 's', 'p', 'e')) {
            ++out->ispe_count;
            if (!out->ispe && omc_bmff_property_box(ctx, p, a->property_index, &box)) {
                out->ispe_storage.index = a->property_index;
                if (omc_bmff_read_u32be(ctx->bytes, ctx->size,
                                        box.offset + box.header_size + 4U,
                                        &out->ispe_storage.width) &&
                    omc_bmff_read_u32be(ctx->bytes, ctx->size,
                                        box.offset + box.header_size + 8U,
                                        &out->ispe_storage.height))
                    out->ispe = &out->ispe_storage;
            }
        }
    }
    out->configuration_valid = out->configuration_count == 1U && out->configuration &&
                               out->configuration->core_valid;
    if (omc_bmff_location_for(ctx, p, item_id, &out->location_storage)) {
        out->location = &out->location_storage;
        if (out->location->data_reference_index) {
            out->data_reference_present = 1;
            if (omc_bmff_find_data_ref(ctx, p, out->location->data_reference_index,
                                       &out->reference_storage, &references_valid)) {
                out->data_reference = &out->reference_storage;
                out->data_reference_valid =
                    references_valid &&
                    out->data_reference->type == OMC_BMFF_FOURCC('d', 'e', 't', 'i') &&
                    out->data_reference->syntax_valid;
            }
        }
    }
    if (!out->configuration_valid || out->ispe_count != 1U || !out->ispe ||
        !out->ispe->width || !out->ispe->height)
        return;
    out->tile_columns =
        out->ispe->width / out->configuration->tile_width +
        (out->ispe->width % out->configuration->tile_width != 0U ? 1U : 0U);
    out->tile_rows =
        out->ispe->height / out->configuration->tile_height +
        (out->ispe->height % out->configuration->tile_height != 0U ? 1U : 0U);
    if (!omc_bmff_checked_mul(out->tile_columns, out->tile_rows, &count)) {
        out->tile_count_overflow = 1;
        return;
    }
    for (i = 0U; i < out->configuration->stored_dimension_count; ++i)
        if (!omc_bmff_checked_mul(count, out->configuration->dimension_sizes[i],
                                  &count)) {
            out->tile_count_overflow = 1;
            return;
        }
    out->expected_tile_count = count;
    out->have_tile_grid = 1;
    out->layout_valid = 1;
    if (!complete || !out->data_reference_valid)
        return;
    out->input_item_count_matches = out->data_reference->input_item_count == count;
    if (!out->input_item_count_matches || !omc_bmff_tile_conditional(ctx, p, out))
        return;
    if (out->data_reference->external_tiles_urls)
        out->complete_configuration_valid = 1;
    else
        out->complete_configuration_valid = omc_bmff_tile_offsets(ctx, p, out);
}

static int omc_bmff_emit_tiled(omc_bmff_ctx *ctx, const omc_bmff_primary_props *p)
{
    omc_u32 state_count;
    omc_u32 configuration_valid_count;
    omc_u32 layout_valid_count;
    omc_u32 complete_valid_count;
    state_count = 0U;
    configuration_valid_count = 0U;
    layout_valid_count = 0U;
    complete_valid_count = 0U;
    {
        omc_u32 i;
        i = 0U;
        for (; i < p->item_count; ++i) {
            const omc_bmff_item_info *item;
            omc_bmff_tiled_state state;
            item = &p->items[i];
            if ((!item->have_type) ||
                (item->item_type != OMC_BMFF_FOURCC('t', 'i', 'l', 'i'))) {
                continue;
            }
            omc_bmff_tile_state(ctx, p, item->item_id, 0, &state);
            if (state.configuration_valid) {
                configuration_valid_count += 1U;
            }
            if (state.layout_valid) {
                layout_valid_count += 1U;
            }
            state_count += 1U;
        }
    }
    if (state_count == 0U) {
        return 1;
    }
    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.count", state_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.configuration_valid_count",
                                 configuration_valid_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.configuration_invalid_count",
                                 state_count - configuration_valid_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.layout_valid_count",
                                 layout_valid_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.layout_invalid_count",
                                 state_count - layout_valid_count))
        return 0;
    {
        omc_u32 i;
        i = 0U;
        for (; i < p->item_count; ++i) {
            const omc_bmff_item_info *item;
            omc_bmff_tiled_state state;
            item = &p->items[i];
            if ((!item->have_type) ||
                (item->item_type != OMC_BMFF_FOURCC('t', 'i', 'l', 'i'))) {
                continue;
            }
            omc_bmff_tile_state(ctx, p, item->item_id, 1, &state);
            if (state.complete_configuration_valid) {
                complete_valid_count += 1U;
            }
            if (!omc_bmff_emit_u32_field(ctx, "tiled_image.item_id", state.item_id))
                return 0;
            if (!omc_bmff_emit_u32_field(ctx, "tiled_image.configuration_count",
                                         state.configuration_count))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.configuration_present",
                                        (state.configuration_count != 0U) ? (1U)
                                                                          : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.configuration_unique",
                                        (state.configuration_count == 1U) ? (1U)
                                                                          : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.configuration_valid",
                                        (state.configuration_valid) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u32_field(ctx, "tiled_image.ispe_count",
                                         state.ispe_count))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.ispe_present",
                                        (state.ispe_count != 0U) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.ispe_unique",
                                        (state.ispe_count == 1U) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.layout_valid",
                                        (state.layout_valid) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.complete_configuration_valid",
                                        (state.complete_configuration_valid) ? (1U)
                                                                             : (0U)))
                return 0;
            if (state.configuration_count != 0U) {
                if (!omc_bmff_emit_text_field(ctx, "tiled_image.configuration", "tiled",
                                              (omc_u16)strlen("tiled")))
                    return 0;
            }
            if (state.configuration) {
                const omc_bmff_tilc *config;
                config = state.configuration;
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.property_index",
                                             config->index))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.property_essential",
                                            state.configuration_essential))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx,
                                            "tiled_image.configuration_header_present",
                                            (config->have_header) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx,
                                            "tiled_image.configuration_core_present",
                                            (config->have_core) ? (1U) : (0U)))
                    return 0;
                if (config->have_header) {
                    if (!omc_bmff_emit_u8_field(
                            ctx, "tiled_image.configuration_version", config->version))
                        return 0;
                    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.configuration_flags",
                                                 config->flags))
                        return 0;
                }
                if (config->have_core) {
                    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.tile_width",
                                                 config->tile_width))
                        return 0;
                    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.tile_height",
                                                 config->tile_height))
                        return 0;
                    if (!omc_bmff_emit_u8_field(ctx,
                                                "tiled_image.extra_dimension_count",
                                                config->extra_dimension_count))
                        return 0;
                    {
                        omc_u8 j;
                        j = 0U;
                        for (; j < config->stored_dimension_count; ++j) {
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "tiled_image.dimension_index", j))
                                return 0;
                            if (!omc_bmff_emit_u32_field(ctx,
                                                         "tiled_image.dimension_size",
                                                         config->dimension_sizes[j]))
                                return 0;
                        }
                    }
                    if (!omc_bmff_emit_u32_field(
                            ctx, "tiled_image.conditional_payload_bytes",
                            config->conditional_payload_bytes))
                        return 0;
                }
                if (config->dimensions_truncated) {
                    if (!omc_bmff_emit_u8_field(ctx, "tiled_image.dimensions_truncated",
                                                1U))
                        return 0;
                }
            }
            if (state.ispe) {
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.output_width",
                                             state.ispe->width))
                    return 0;
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.output_height",
                                             state.ispe->height))
                    return 0;
            }
            if (state.have_tile_grid) {
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.tile_columns",
                                             state.tile_columns))
                    return 0;
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.tile_rows",
                                             state.tile_rows))
                    return 0;
                if (!omc_bmff_emit_u64_field(ctx, "tiled_image.expected_tile_count",
                                             state.expected_tile_count))
                    return 0;
            }
            if (state.tile_count_overflow) {
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.tile_count_overflow", 1U))
                    return 0;
            }
            if (state.location) {
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.data_reference_index",
                                             state.location->data_reference_index))
                    return 0;
            }
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.data_reference_present",
                                        (state.data_reference_present) ? (1U) : (0U)))
                return 0;
            if (!omc_bmff_emit_u8_field(ctx, "tiled_image.data_reference_valid",
                                        (state.data_reference_valid) ? (1U) : (0U)))
                return 0;
            if (state.data_reference) {
                const omc_bmff_data_ref *data_ref;
                data_ref = state.data_reference;
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.data_reference_type",
                                             data_ref->type))
                    return 0;
                if (!omc_bmff_emit_fourcc(ctx, "tiled_image.data_reference_type_name",
                                          data_ref->type))
                    return 0;
                if (data_ref->have_header) {
                    if (!omc_bmff_emit_u8_field(ctx,
                                                "tiled_image.data_reference_version",
                                                data_ref->version))
                        return 0;
                    if (!omc_bmff_emit_u32_field(
                            ctx, "tiled_image.data_reference_flags", data_ref->flags))
                        return 0;
                }
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.external_tiles",
                                            (data_ref->external_tiles_urls) ? (1U)
                                                                            : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.sequential_order",
                                            (data_ref->sequential_order) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.offset_field_bytes",
                                            data_ref->offset_field_bytes))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.size_field_bytes",
                                            data_ref->size_field_bytes))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx,
                                            "tiled_image.input_item_count_field_bytes",
                                            data_ref->input_item_count_field_bytes))
                    return 0;
                if (!omc_bmff_emit_u64_field(ctx, "tiled_image.input_item_count",
                                             data_ref->input_item_count))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.input_item_count_matches",
                                            (state.input_item_count_matches) ? (1U)
                                                                             : (0U)))
                    return 0;
                if (data_ref->external_tiles_urls) {
                    const omc_bmff_tile_url *urls[3];
                    const char *byte_fields[3] = {
                        "tiled_image.base_url_bytes", "tiled_image.url_extension_bytes",
                        "tiled_image.tile_request_template_bytes"};
                    const char *text_fields[3] = {"tiled_image.base_url",
                                                  "tiled_image.url_extension",
                                                  "tiled_image.tile_request_template"};
                    const char *truncated_fields[3] = {
                        "tiled_image.base_url_truncated",
                        "tiled_image.url_extension_truncated",
                        "tiled_image.tile_request_template_truncated"};
                    urls[0] = &data_ref->base_url;
                    urls[1] = &data_ref->url_extension;
                    urls[2] = &data_ref->tile_request_template;
                    if (!omc_bmff_emit_u8_field(
                            ctx, "tiled_image.directory_id_present",
                            (data_ref->directory_id_present) ? (1U) : (0U)))
                        return 0;
                    if (data_ref->directory_id_present) {
                        if (!omc_bmff_emit_u16_field(ctx,
                                                     "tiled_image.directory_id_start",
                                                     data_ref->directory_id_start))
                            return 0;
                        if (!omc_bmff_emit_u16_field(ctx,
                                                     "tiled_image.directory_id_end",
                                                     data_ref->directory_id_end))
                            return 0;
                    }
                    if (!omc_bmff_emit_u64_field(ctx, "tiled_image.tile_id_start",
                                                 data_ref->tile_id_start))
                        return 0;
                    {
                        omc_u32 j;
                        j = 0U;
                        for (; j < 3U; ++j) {
                            if (!omc_bmff_emit_u32_field(ctx, byte_fields[j],
                                                         urls[j]->total_bytes))
                                return 0;
                            if (!urls[j]->truncated) {
                                if (!omc_bmff_emit_utf8_field(ctx, text_fields[j],
                                                              urls[j]->bytes,
                                                              urls[j]->stored_bytes))
                                    return 0;
                            } else {
                                if (!omc_bmff_emit_u8_field(ctx, truncated_fields[j],
                                                            1U))
                                    return 0;
                            }
                        }
                    }
                }
            }
            if (state.data_reference_valid) {
                if (!omc_bmff_emit_u8_field(
                        ctx, "tiled_image.conditional_payload_valid",
                        (state.conditional_payload_valid) ? (1U) : (0U)))
                    return 0;
            }
            if (state.tipa_present) {
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.tile_item_type",
                                             state.tile_item_type))
                    return 0;
                if (!omc_bmff_emit_fourcc(ctx, "tiled_image.tile_item_type_name",
                                          state.tile_item_type))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.tipa_version",
                                            state.tipa_version))
                    return 0;
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.tipa_flags",
                                             state.tipa_flags))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.tipa_valid",
                                            (state.tipa_valid) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u32_field(
                        ctx, "tiled_image.tile_property_association_count",
                        state.tile_property_association_count))
                    return 0;
                {
                    omc_u32 j;
                    j = 0U;
                    for (; j < state.stored_tile_property_association_count; ++j) {
                        const omc_bmff_tile_association *association;
                        association = &state.tile_property_associations[j];
                        if (!omc_bmff_emit_u32_field(ctx,
                                                     "tiled_image.tile_property_index",
                                                     association->property_index))
                            return 0;
                        if (!omc_bmff_emit_u8_field(
                                ctx, "tiled_image.tile_property_essential",
                                association->essential))
                            return 0;
                        if (association->have_property_type) {
                            if (!omc_bmff_emit_u32_field(
                                    ctx, "tiled_image.tile_property_type",
                                    association->property_type))
                                return 0;
                            if (!omc_bmff_emit_fourcc(
                                    ctx, "tiled_image.tile_property_type_name",
                                    association->property_type))
                                return 0;
                        }
                    }
                }
                if (state.tile_property_associations_truncated) {
                    if (!omc_bmff_emit_u8_field(
                            ctx, "tiled_image.tile_property_associations_truncated",
                            1U))
                        return 0;
                }
            }
            if (state.offset_table_present && state.data_reference) {
                const omc_bmff_data_ref *data_ref;
                data_ref = state.data_reference;
                if (!omc_bmff_emit_u64_field(ctx, "tiled_image.offset_table_start",
                                             data_ref->tile_offset_table_start))
                    return 0;
                if (!omc_bmff_emit_u32_field(ctx, "tiled_image.offset_table_size",
                                             data_ref->tile_offset_table_size))
                    return 0;
                if (!omc_bmff_emit_u64_field(ctx,
                                             "tiled_image.expected_offset_table_size",
                                             state.expected_offset_table_size))
                    return 0;
                if (!omc_bmff_emit_u64_field(ctx, "tiled_image.logical_item_data_size",
                                             state.logical_item_data_size))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.offset_table_valid",
                                            (state.offset_table_valid) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.tile_offsets_valid",
                                            (state.tile_offsets_valid) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u8_field(ctx, "tiled_image.tile_sizes_validated",
                                            (state.tile_sizes_validated) ? (1U) : (0U)))
                    return 0;
                if (!omc_bmff_emit_u64_field(ctx, "tiled_image.empty_tile_count",
                                             state.empty_tile_count))
                    return 0;
                {
                    omc_u32 j;
                    j = 0U;
                    for (; j < state.stored_offset_row_count; ++j) {
                        const omc_bmff_tile_row *row;
                        row = &state.offset_rows[j];
                        if (!omc_bmff_emit_u64_field(
                                ctx, "tiled_image.offset.tile_index", row->tile_index))
                            return 0;
                        if (!omc_bmff_emit_u64_field(ctx, "tiled_image.offset.start",
                                                     row->start_offset))
                            return 0;
                        if (!omc_bmff_emit_u8_field(ctx, "tiled_image.offset.empty",
                                                    (row->empty) ? (1U) : (0U)))
                            return 0;
                        if (row->have_size) {
                            if (!omc_bmff_emit_u64_field(ctx, "tiled_image.offset.size",
                                                         row->size))
                                return 0;
                        }
                    }
                }
                if (state.offset_table_validation_truncated) {
                    if (!omc_bmff_emit_u8_field(
                            ctx, "tiled_image.offset_table_validation_truncated", 1U))
                        return 0;
                }
            }
        }
    }
    if (!omc_bmff_emit_u32_field(ctx, "tiled_image.complete_configuration_valid_count",
                                 complete_valid_count))
        return 0;
    if (!omc_bmff_emit_u32_field(ctx,
                                 "tiled_image.complete_configuration_invalid_count",
                                 state_count - complete_valid_count))
        return 0;
    return 1;
}

static int omc_bmff_decode_meta_box(omc_bmff_ctx *ctx, const omc_bmff_box *meta)
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
    omc_bmff_item_info items[256];
    omc_bmff_primary_props primary_props;
    omc_u32 item_count;
    omc_u32 primary_item_id;

    if (ctx == (omc_bmff_ctx *)0 || meta == (const omc_bmff_box *)0) {
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
    p = payload_off + 4U;
    while (p + 8U <= payload_end) {
        omc_bmff_box child;

        if (!omc_bmff_note_box(ctx)) {
            return 0;
        }
        if (!omc_bmff_parse_box(ctx->bytes, ctx->size, p, payload_end, &child)) {
            return 0;
        }
        if (child.type == OMC_BMFF_FOURCC('i', 'i', 'n', 'f')) {
            iinf = child;
            has_iinf = 1;
        } else if (child.type == OMC_BMFF_FOURCC('p', 'i', 't', 'm')) {
            pitm = child;
            has_pitm = 1;
        } else if (child.type == OMC_BMFF_FOURCC('i', 'p', 'r', 'p')) {
            iprp = child;
            has_iprp = 1;
        } else if (child.type == OMC_BMFF_FOURCC('i', 'r', 'e', 'f')) {
            iref = child;
            has_iref = 1;
        } else if (child.type == OMC_BMFF_FOURCC('d', 'i', 'n', 'f')) {
            primary_props.dinf = child;
        } else if (child.type == OMC_BMFF_FOURCC('i', 'l', 'o', 'c')) {
            if (!omc_bmff_collect_locations(ctx, &child, &primary_props))
                return 0;
        } else if (child.type == OMC_BMFF_FOURCC('i', 'd', 'a', 't')) {
            primary_props.have_idat = 1;
            primary_props.idat_offset = child.offset + child.header_size;
            primary_props.idat_end = child.offset + child.size;
        } else if (child.type == OMC_BMFF_FOURCC('g', 'r', 'p', 'l')) {
            grpl = child;
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
    primary_item_id = 0U;
    {
        omc_u64 iprp_off;
        omc_u64 iprp_end;
        omc_bmff_box ipco;
        omc_bmff_box ipma;
        int has_ipco;
        int has_ipma;

        if (has_pitm) {
            if (!omc_bmff_parse_pitm(ctx->bytes, ctx->size, &pitm, &primary_item_id)) {
                return 0;
            }
            primary_props.have_item_id = 1;
            primary_props.item_id = primary_item_id;
        }
        if (has_iref && !omc_bmff_collect_iref_edges(ctx, &iref, &primary_props)) {
            return 0;
        }
        memset(&ipco, 0, sizeof(ipco));
        memset(&ipma, 0, sizeof(ipma));
        has_ipco = 0;
        has_ipma = 0;
        if (has_iprp && has_pitm) {
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
        if (has_ipco || has_ipma) {
            omc_bmff_ispe_prop ispe[64];
            omc_bmff_u8_prop irot[64];
            omc_bmff_u8_prop imir[64];
            omc_bmff_auxc_prop auxc[64];
            omc_bmff_prop_type prop_types[256];
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
            if (has_ipco &&
                !omc_bmff_collect_ipco_props(
                    ctx, &ipco, ispe, &ispe_count, irot, &irot_count, imir, &imir_count,
                    auxc, &auxc_count, prop_types, &prop_type_count, &primary_props)) {
                return 0;
            }
            if (has_ipma && !omc_bmff_apply_ipma_primary(
                                ctx, &ipma, primary_item_id, ispe, ispe_count, irot,
                                irot_count, imir, imir_count, auxc, auxc_count,
                                prop_types, prop_type_count, &primary_props)) {
                return 0;
            }
        }
    }

    if (has_grpl && !omc_bmff_collect_item_groups(ctx, &grpl, &primary_props)) {
        return 0;
    }
    if ((has_iinf && !omc_bmff_emit_item_info_fields(ctx, items, item_count)) ||
        !omc_bmff_emit_property_summaries(ctx, &primary_props) ||
        !omc_bmff_emit_iref_fields(ctx, &primary_props) ||
        !omc_bmff_emit_ipma_fields(ctx, &primary_props) ||
        !omc_bmff_emit_item_group_fields(ctx, &primary_props)) {
        return 0;
    }
    if (has_pitm) {
        if (!omc_bmff_emit_primary_fields(ctx, items, item_count,
                                          primary_item_id, &primary_props)
            || !omc_bmff_emit_aux_fields(ctx, &primary_props)) {
            return 0;
        }
    }

    primary_props.items = items;
    primary_props.item_count = item_count;
    if (!omc_bmff_emit_derived(ctx, &primary_props))
        return 0;
    if (!omc_bmff_emit_tiled(ctx, &primary_props))
        return 0;
    if (!omc_bmff_emit_locations(ctx, &primary_props))
        return 0;
    if (!omc_bmff_emit_primary_extra(ctx, &primary_props))
        return 0;
    if (!omc_bmff_emit_scene(ctx, &primary_props, items, item_count))
        return 0;
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
omc_bmff_run(omc_input* file_bytes, omc_u64 file_size, omc_store* store,
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

    if (file_bytes == NULL) {
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
omc_bmff_dec(const omc_u8* bytes, omc_size size, omc_store* store,
              const omc_bmff_opts* opts)
{
    omc_input input;
    omc_input_memory(&input, bytes, size);
    return omc_bmff_run(bytes == NULL ? NULL : &input, size, store, opts);
}
omc_bmff_res
omc_bmff_meas(const omc_u8* bytes, omc_size size, const omc_bmff_opts* opts)
{
    return omc_bmff_dec(bytes, size, NULL, opts);
}
omc_bmff_res
omc_bmff_dec_source(const omc_source_range* range, omc_store* store,
                     omc_source_state* state, const omc_source_limits* limits,
                     const omc_bmff_opts* opts)
{
    omc_bmff_res res;
    omc_input input;
    omc_bmff_res_init(&res);
    if (!omc_source_range_valid(range) || state == NULL || state->code != OMC_SOURCE_OK) {
        res.status = OMC_BMFF_MALFORMED; return res;
    }
    memset(&input, 0, sizeof(input));
    input.range = *range;
    input.state = state;
    input.limits = limits;
    res = omc_bmff_run(&input, range->size, store, opts);
    if (!omc_input_ok(&input)) res.status = OMC_BMFF_MALFORMED;
    return res;
}
