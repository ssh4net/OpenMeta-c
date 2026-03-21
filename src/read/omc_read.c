#include "omc/omc_read.h"

#include "read/omc_ciff.h"

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
    res->bmff.status = OMC_BMFF_OK;
    res->bmff.boxes_scanned = 0U;
    res->bmff.item_infos = 0U;
    res->bmff.entries_decoded = 0U;
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
    res->jumbf.status = OMC_JUMBF_OK;
    res->jumbf.boxes_decoded = 0U;
    res->jumbf.cbor_items = 0U;
    res->jumbf.entries_decoded = 0U;
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
omc_read_merge_bmff(omc_bmff_res* dst, omc_bmff_res src)
{
    dst->boxes_scanned += src.boxes_scanned;
    dst->item_infos += src.item_infos;
    dst->entries_decoded += src.entries_decoded;
    if (dst->status == OMC_BMFF_NOMEM || dst->status == OMC_BMFF_LIMIT) {
        return;
    }
    if (src.status == OMC_BMFF_NOMEM || src.status == OMC_BMFF_LIMIT) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_BMFF_MALFORMED) {
        return;
    }
    if (src.status == OMC_BMFF_MALFORMED) {
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

static void
omc_read_merge_jumbf(omc_jumbf_res* dst, omc_jumbf_res src)
{
    dst->boxes_decoded += src.boxes_decoded;
    dst->cbor_items += src.cbor_items;
    dst->entries_decoded += src.entries_decoded;
    if (dst->status == OMC_JUMBF_NOMEM || dst->status == OMC_JUMBF_LIMIT) {
        return;
    }
    if (src.status == OMC_JUMBF_NOMEM || src.status == OMC_JUMBF_LIMIT) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_JUMBF_MALFORMED) {
        return;
    }
    if (src.status == OMC_JUMBF_MALFORMED) {
        dst->status = src.status;
        return;
    }
    if (dst->status == OMC_JUMBF_UNSUPPORTED) {
        return;
    }
    if (src.status == OMC_JUMBF_UNSUPPORTED) {
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

static void
omc_read_clear_casio_simple_context(omc_store* store, omc_size entry_start)
{
    omc_size i;

    if (store == (omc_store*)0 || entry_start >= store->entry_count) {
        return;
    }
    for (i = entry_start; i < store->entry_count; ++i) {
        omc_entry* entry;

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_CONTEXTUAL_NAME) == 0U
            || entry->origin.name_context_kind
                   != OMC_ENTRY_NAME_CTX_CASIO_TYPE2_LEGACY) {
            continue;
        }
        entry->flags &= (omc_entry_flags)~OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
        entry->origin.name_context_variant = 0U;
    }
}

static void
omc_read_clear_pentax_simple_context(omc_store* store, omc_size entry_start)
{
    omc_size i;

    if (store == (omc_store*)0 || entry_start >= store->entry_count) {
        return;
    }
    for (i = entry_start; i < store->entry_count; ++i) {
        omc_entry* entry;

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_CONTEXTUAL_NAME) == 0U
            || entry->origin.name_context_kind
                   != OMC_ENTRY_NAME_CTX_PENTAX_MAIN_0062) {
            continue;
        }
        entry->flags &= (omc_entry_flags)~OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
        entry->origin.name_context_variant = 0U;
    }
}

static void
omc_read_clear_ricoh_simple_context(omc_store* store, omc_size entry_start)
{
    omc_size i;

    if (store == (omc_store*)0 || entry_start >= store->entry_count) {
        return;
    }
    for (i = entry_start; i < store->entry_count; ++i) {
        omc_entry* entry;

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_CONTEXTUAL_NAME) == 0U
            || entry->origin.name_context_kind
                   != OMC_ENTRY_NAME_CTX_RICOH_MAIN_COMPAT) {
            continue;
        }
        entry->flags &= (omc_entry_flags)~OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
        entry->origin.name_context_variant = 0U;
    }
}

static void
omc_read_remap_ricoh_padded_type2_ifd(omc_store* store, omc_size entry_start)
{
    static const char type2_ifd[] = "mk_ricoh_type2_0";
    static const char main_ifd[] = "mk_ricoh0";
    omc_size i;
    int have_padded_type2;
    omc_byte_ref main_ifd_ref;

    if (store == (omc_store*)0 || entry_start >= store->entry_count) {
        return;
    }

    have_padded_type2 = 0;
    for (i = entry_start; i < store->entry_count; ++i) {
        omc_entry* entry;
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_view.size != (sizeof(type2_ifd) - 1U)
            || memcmp(ifd_view.data, type2_ifd, sizeof(type2_ifd) - 1U) != 0) {
            continue;
        }
        if (entry->key.u.exif_tag.tag == 0x0104U) {
            have_padded_type2 = 1;
            break;
        }
    }

    if (!have_padded_type2
        || omc_arena_append(&store->arena, main_ifd, sizeof(main_ifd) - 1U,
                            &main_ifd_ref)
               != OMC_STATUS_OK) {
        return;
    }

    for (i = entry_start; i < store->entry_count; ++i) {
        omc_entry* entry;
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_view.size != (sizeof(type2_ifd) - 1U)
            || memcmp(ifd_view.data, type2_ifd, sizeof(type2_ifd) - 1U) != 0) {
            continue;
        }
        entry->key.u.exif_tag.ifd = main_ifd_ref;
    }
}

static int
omc_read_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 off,
                    omc_u32* out_value)
{
    omc_u32 value;

    if (bytes == (const omc_u8*)0 || out_value == (omc_u32*)0) {
        return 0;
    }
    if (off + 4U > (omc_u64)size) {
        return 0;
    }

    value = 0U;
    value |= (omc_u32)bytes[(omc_size)off + 0U] << 24;
    value |= (omc_u32)bytes[(omc_size)off + 1U] << 16;
    value |= (omc_u32)bytes[(omc_size)off + 2U] << 8;
    value |= (omc_u32)bytes[(omc_size)off + 3U] << 0;
    *out_value = value;
    return 1;
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
omc_read_has_nul(const omc_u8* bytes, omc_size size)
{
    omc_size i;

    if (bytes == (const omc_u8*)0) {
        return 0;
    }
    for (i = 0U; i < size; ++i) {
        if (bytes[i] == 0U) {
            return 1;
        }
    }
    return 0;
}

static int
omc_read_bytes_ascii(const omc_u8* bytes, omc_size size)
{
    omc_size i;

    if (bytes == (const omc_u8*)0) {
        return 0;
    }
    for (i = 0U; i < size; ++i) {
        if (bytes[i] >= 0x80U) {
            return 0;
        }
    }
    return 1;
}

static int
omc_read_bytes_valid_utf8(const omc_u8* bytes, omc_size size)
{
    omc_size i;

    if (bytes == (const omc_u8*)0) {
        return 0;
    }

    i = 0U;
    while (i < size) {
        omc_u8 c0;
        omc_u32 cp;
        omc_size needed;
        omc_u32 min_cp;
        omc_size j;

        c0 = bytes[i];
        if (c0 < 0x80U) {
            i += 1U;
            continue;
        }
        if ((c0 & 0xE0U) == 0xC0U) {
            needed = 1U;
            cp = c0 & 0x1FU;
            min_cp = 0x80U;
        } else if ((c0 & 0xF0U) == 0xE0U) {
            needed = 2U;
            cp = c0 & 0x0FU;
            min_cp = 0x800U;
        } else if ((c0 & 0xF8U) == 0xF0U) {
            needed = 3U;
            cp = c0 & 0x07U;
            min_cp = 0x10000U;
        } else {
            return 0;
        }
        if (i + needed >= size) {
            return 0;
        }
        for (j = 1U; j <= needed; ++j) {
            omc_u8 cx;

            cx = bytes[i + j];
            if ((cx & 0xC0U) != 0x80U) {
                return 0;
            }
            cp = (cp << 6) | (omc_u32)(cx & 0x3FU);
        }
        if (cp < min_cp || cp > 0x10FFFFU) {
            return 0;
        }
        if (cp >= 0xD800U && cp <= 0xDFFFU) {
            return 0;
        }
        i += needed + 1U;
    }
    return 1;
}

static int
omc_read_emit_comment_entry(omc_store* store, omc_block_id block_id,
                            const omc_u8* bytes, omc_size size)
{
    omc_entry entry;
    omc_byte_ref value_ref;
    omc_status st;

    if (store == (omc_store*)0 || bytes == (const omc_u8*)0) {
        return 0;
    }

    st = omc_arena_append(&store->arena, bytes, size, &value_ref);
    if (st != OMC_STATUS_OK) {
        return 0;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_comment(&entry.key);
    if (!omc_read_has_nul(bytes, size) && omc_read_bytes_ascii(bytes, size)) {
        omc_val_make_text(&entry.value, value_ref, OMC_TEXT_ASCII);
    } else if (!omc_read_has_nul(bytes, size)
               && omc_read_bytes_valid_utf8(bytes, size)) {
        omc_val_make_text(&entry.value, value_ref, OMC_TEXT_UTF8);
    } else {
        omc_val_make_bytes(&entry.value, value_ref);
    }
    entry.origin.block = block_id;
    entry.origin.order_in_block = 0U;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    return omc_store_add_entry(store, &entry, (omc_entry_id*)0)
           == OMC_STATUS_OK;
}

static int
omc_read_emit_png_text_entry(omc_store* store, omc_block_id block_id,
                             omc_u32* io_order, const omc_u8* keyword,
                             omc_size keyword_size, const char* field,
                             const omc_u8* text, omc_size text_size,
                             omc_text_encoding enc)
{
    omc_entry entry;
    omc_byte_ref keyword_ref;
    omc_byte_ref field_ref;
    omc_byte_ref value_ref;
    omc_status st;

    if (store == (omc_store*)0 || io_order == (omc_u32*)0
        || keyword == (const omc_u8*)0 || field == (const char*)0
        || text == (const omc_u8*)0) {
        return 0;
    }

    st = omc_arena_append(&store->arena, keyword, keyword_size, &keyword_ref);
    if (st != OMC_STATUS_OK) {
        return 0;
    }
    st = omc_arena_append(&store->arena, field, strlen(field), &field_ref);
    if (st != OMC_STATUS_OK) {
        return 0;
    }
    st = omc_arena_append(&store->arena, text, text_size, &value_ref);
    if (st != OMC_STATUS_OK) {
        return 0;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_png_text(&entry.key, keyword_ref, field_ref);
    omc_val_make_text(&entry.value, value_ref, enc);
    entry.origin.block = block_id;
    entry.origin.order_in_block = *io_order;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    if (omc_store_add_entry(store, &entry, (omc_entry_id*)0)
        != OMC_STATUS_OK) {
        return 0;
    }

    *io_order += 1U;
    return 1;
}

static omc_u32
omc_read_write_u32_decimal(char* out, omc_u32 value)
{
    char digits[16];
    omc_u32 count;
    omc_u32 i;

    count = 0U;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    for (i = 0U; i < count; ++i) {
        out[i] = digits[count - 1U - i];
    }
    return count;
}

static int
omc_read_make_subifd_name(const char* vendor_prefix, const char* subtable,
                          omc_u32 index, char* out, omc_size out_cap)
{
    omc_size prefix_len;
    omc_size sub_len;
    omc_u32 digits;

    if (vendor_prefix == (const char*)0 || subtable == (const char*)0
        || out == (char*)0 || out_cap == 0U) {
        return 0;
    }

    prefix_len = strlen(vendor_prefix);
    sub_len = strlen(subtable);
    if (prefix_len == 0U || sub_len == 0U
        || out_cap < (prefix_len + 1U + sub_len + 2U)) {
        return 0;
    }

    memcpy(out, vendor_prefix, prefix_len);
    out[prefix_len] = '_';
    memcpy(out + prefix_len + 1U, subtable, sub_len);
    out[prefix_len + 1U + sub_len] = '_';
    digits = omc_read_write_u32_decimal(
        out + prefix_len + 1U + sub_len + 1U, index);
    if (prefix_len + 1U + sub_len + 1U + digits + 1U > out_cap) {
        return 0;
    }
    out[prefix_len + 1U + sub_len + 1U + digits] = '\0';
    return 1;
}

static int
omc_read_emit_exif_entry(omc_store* store, omc_block_id block_id,
                         omc_u32* io_order, const char* ifd_name,
                         omc_u16 tag, const omc_val* value)
{
    omc_entry entry;
    omc_byte_ref ifd_ref;
    omc_status st;

    if (store == (omc_store*)0 || io_order == (omc_u32*)0
        || ifd_name == (const char*)0 || value == (const omc_val*)0) {
        return 0;
    }

    st = omc_arena_append(&store->arena, ifd_name, strlen(ifd_name), &ifd_ref);
    if (st != OMC_STATUS_OK) {
        return 0;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, tag);
    entry.value = *value;
    entry.origin.block = block_id;
    entry.origin.order_in_block = *io_order;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = value->count;
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;
    if (omc_store_add_entry(store, &entry, (omc_entry_id*)0)
        != OMC_STATUS_OK) {
        return 0;
    }

    *io_order += 1U;
    return 1;
}

static int
omc_read_make_fixed_ascii_text(omc_store* store, const omc_u8* bytes,
                               omc_size size, omc_val* out_value)
{
    omc_size trimmed;
    omc_byte_ref ref;
    omc_status st;

    if (store == (omc_store*)0 || out_value == (omc_val*)0
        || bytes == (const omc_u8*)0) {
        return 0;
    }

    trimmed = 0U;
    while (trimmed < size && bytes[trimmed] != 0U) {
        trimmed += 1U;
    }
    st = omc_arena_append(&store->arena, bytes, trimmed, &ref);
    if (st != OMC_STATUS_OK) {
        return 0;
    }
    omc_val_make_text(out_value, ref, OMC_TEXT_ASCII);
    return 1;
}

static int
omc_read_make_casio_qvci_datetime(omc_store* store, const omc_u8* bytes,
                                  omc_size size, omc_val* out_value)
{
    char buf[32];
    omc_byte_ref ref;
    omc_status st;
    omc_size n;

    if (store == (omc_store*)0 || out_value == (omc_val*)0
        || bytes == (const omc_u8*)0) {
        return 0;
    }

    n = 0U;
    while (n < size && (n + 1U) < sizeof(buf) && bytes[n] != 0U) {
        char c;

        c = (char)bytes[n];
        if (c == '.') {
            c = ':';
        }
        buf[n] = c;
        n += 1U;
    }
    if (n >= 11U && buf[10] == ':') {
        buf[10] = ' ';
    }

    st = omc_arena_append(&store->arena, buf, n, &ref);
    if (st != OMC_STATUS_OK) {
        return 0;
    }
    omc_val_make_text(out_value, ref, OMC_TEXT_ASCII);
    return 1;
}

static void
omc_read_decode_casio_qvci(omc_const_bytes block_view, omc_store* store,
                           omc_block_id block_id, omc_u32* io_index,
                           const omc_exif_opts* exif_opts,
                           omc_exif_res* exif_res)
{
    char ifd_name[64];
    omc_u32 order;
    omc_val value;

    if (store == (omc_store*)0 || io_index == (omc_u32*)0
        || exif_opts == (const omc_exif_opts*)0
        || exif_res == (omc_exif_res*)0
        || block_view.data == (const omc_u8*)0 || block_view.size < 4U
        || memcmp(block_view.data, "QVCI", 4U) != 0) {
        return;
    }
    if (!omc_read_make_subifd_name("mk_casio", "qvci", *io_index, ifd_name,
                                   sizeof(ifd_name))) {
        exif_res->status = OMC_EXIF_LIMIT;
        return;
    }
    *io_index += 1U;
    order = 0U;

    if (block_view.size > 0x002CU) {
        if ((exif_res->entries_decoded + 1U) > exif_opts->limits.max_total_entries) {
            exif_res->status = OMC_EXIF_LIMIT;
            return;
        }
        omc_val_make_u8(&value, block_view.data[0x002CU]);
        if (!omc_read_emit_exif_entry(store, block_id, &order, ifd_name,
                                      0x002CU, &value)) {
            exif_res->status = OMC_EXIF_NOMEM;
            return;
        }
        exif_res->entries_decoded += 1U;
    }
    if (block_view.size > 0x0037U) {
        if ((exif_res->entries_decoded + 1U) > exif_opts->limits.max_total_entries) {
            exif_res->status = OMC_EXIF_LIMIT;
            return;
        }
        omc_val_make_u8(&value, block_view.data[0x0037U]);
        if (!omc_read_emit_exif_entry(store, block_id, &order, ifd_name,
                                      0x0037U, &value)) {
            exif_res->status = OMC_EXIF_NOMEM;
            return;
        }
        exif_res->entries_decoded += 1U;
    }
    if (block_view.size >= 0x004DU + 20U) {
        if ((exif_res->entries_decoded + 1U) > exif_opts->limits.max_total_entries) {
            exif_res->status = OMC_EXIF_LIMIT;
            return;
        }
        if (!omc_read_make_casio_qvci_datetime(store,
                                               block_view.data + 0x004DU,
                                               20U, &value)
            || !omc_read_emit_exif_entry(store, block_id, &order, ifd_name,
                                         0x004DU, &value)) {
            exif_res->status = OMC_EXIF_NOMEM;
            return;
        }
        exif_res->entries_decoded += 1U;
    }
    if (block_view.size >= 0x0062U + 7U) {
        if ((exif_res->entries_decoded + 1U) > exif_opts->limits.max_total_entries) {
            exif_res->status = OMC_EXIF_LIMIT;
            return;
        }
        if (!omc_read_make_fixed_ascii_text(store, block_view.data + 0x0062U,
                                            7U, &value)
            || !omc_read_emit_exif_entry(store, block_id, &order, ifd_name,
                                         0x0062U, &value)) {
            exif_res->status = OMC_EXIF_NOMEM;
            return;
        }
        exif_res->entries_decoded += 1U;
    }
    if (block_view.size >= 0x0072U + 9U) {
        if ((exif_res->entries_decoded + 1U) > exif_opts->limits.max_total_entries) {
            exif_res->status = OMC_EXIF_LIMIT;
            return;
        }
        if (!omc_read_make_fixed_ascii_text(store, block_view.data + 0x0072U,
                                            9U, &value)
            || !omc_read_emit_exif_entry(store, block_id, &order, ifd_name,
                                         0x0072U, &value)) {
            exif_res->status = OMC_EXIF_NOMEM;
            return;
        }
        exif_res->entries_decoded += 1U;
    }
    if (block_view.size >= 0x007CU + 9U) {
        if ((exif_res->entries_decoded + 1U) > exif_opts->limits.max_total_entries) {
            exif_res->status = OMC_EXIF_LIMIT;
            return;
        }
        if (!omc_read_make_fixed_ascii_text(store, block_view.data + 0x007CU,
                                            9U, &value)
            || !omc_read_emit_exif_entry(store, block_id, &order, ifd_name,
                                         0x007CU, &value)) {
            exif_res->status = OMC_EXIF_NOMEM;
            return;
        }
        exif_res->entries_decoded += 1U;
    }
}

static void
omc_read_decode_png_text(const omc_u8* file_bytes, omc_size file_size,
                         const omc_blk_ref* block, omc_const_bytes block_view,
                         int payload_decompressed, omc_store* store,
                         omc_block_id block_id)
{
    omc_u32 data_size;
    omc_u32 type;
    const omc_u8* data;
    omc_u32 order;
    omc_size keyword_end;

    if (file_bytes == (const omc_u8*)0 || block == (const omc_blk_ref*)0
        || store == (omc_store*)0) {
        return;
    }
    if (block->format != OMC_SCAN_FMT_PNG || block->kind != OMC_BLK_TEXT) {
        return;
    }
    if (block->outer_offset > (omc_u64)file_size
        || block->outer_size > ((omc_u64)file_size - block->outer_offset)
        || block->outer_size < 12U) {
        return;
    }
    if (!omc_read_read_u32be(file_bytes, file_size, block->outer_offset + 0U,
                             &data_size)
        || !omc_read_read_u32be(file_bytes, file_size, block->outer_offset + 4U,
                                &type)) {
        return;
    }
    if ((omc_u64)data_size + 12U != block->outer_size) {
        return;
    }

    data = file_bytes + (omc_size)block->outer_offset + 8U;
    order = 0U;
    if (type == OMC_FOURCC('t', 'E', 'X', 't')) {
        const omc_u8* text;
        omc_size text_size;

        keyword_end = 0U;
        while (keyword_end < data_size && data[keyword_end] != 0U) {
            keyword_end += 1U;
        }
        if (keyword_end >= data_size) {
            return;
        }
        text = data + keyword_end + 1U;
        text_size = (omc_size)data_size - (keyword_end + 1U);
        (void)omc_read_emit_png_text_entry(store, block_id, &order, data,
                                           keyword_end, "text", text,
                                           text_size, OMC_TEXT_UNKNOWN);
        return;
    }

    if (type == OMC_FOURCC('z', 'T', 'X', 't')) {
        keyword_end = 0U;
        while (keyword_end < data_size && data[keyword_end] != 0U) {
            keyword_end += 1U;
        }
        if (keyword_end + 2U > data_size || !payload_decompressed) {
            return;
        }
        (void)omc_read_emit_png_text_entry(store, block_id, &order, data,
                                           keyword_end, "text",
                                           block_view.data, block_view.size,
                                           OMC_TEXT_UNKNOWN);
        return;
    }

    if (type == OMC_FOURCC('i', 'T', 'X', 't')) {
        omc_size p;
        omc_size lang_end;
        omc_size translated_start;
        omc_size translated_end;
        int compressed;

        keyword_end = 0U;
        while (keyword_end < data_size && data[keyword_end] != 0U) {
            keyword_end += 1U;
        }
        if (keyword_end + 3U > data_size) {
            return;
        }

        compressed = data[keyword_end + 1U] != 0U;
        if (compressed && !payload_decompressed) {
            return;
        }

        p = keyword_end + 3U;
        lang_end = p;
        while (lang_end < data_size && data[lang_end] != 0U) {
            lang_end += 1U;
        }
        if (lang_end >= data_size) {
            return;
        }

        translated_start = lang_end + 1U;
        translated_end = translated_start;
        while (translated_end < data_size && data[translated_end] != 0U) {
            translated_end += 1U;
        }
        if (translated_end >= data_size) {
            return;
        }

        if (lang_end > p) {
            (void)omc_read_emit_png_text_entry(store, block_id, &order, data,
                                               keyword_end, "language",
                                               data + p, lang_end - p,
                                               OMC_TEXT_ASCII);
        }
        if (translated_end > translated_start) {
            (void)omc_read_emit_png_text_entry(store, block_id, &order, data,
                                               keyword_end,
                                               "translated_keyword",
                                               data + translated_start,
                                               translated_end
                                                   - translated_start,
                                               OMC_TEXT_UTF8);
        }
        (void)omc_read_emit_png_text_entry(store, block_id, &order, data,
                                           keyword_end, "text",
                                           block_view.data, block_view.size,
                                           OMC_TEXT_UTF8);
    }
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
        && block->chunking != OMC_BLK_CHUNK_JPEG_APP2_SEQ
        && block->chunking != OMC_BLK_CHUNK_GIF_SUB) {
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

    omc_bmff_opts_init(&opts->bmff);
    omc_exif_opts_init(&opts->exif);
    omc_icc_opts_init(&opts->icc);
    omc_iptc_opts_init(&opts->iptc);
    omc_irb_opts_init(&opts->irb);
    omc_jumbf_opts_init(&opts->jumbf);
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
    omc_u32 casio_qvci_index;
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
        res.bmff.status = OMC_BMFF_MALFORMED;
        res.exif.status = OMC_EXIF_MALFORMED;
        res.icc.status = OMC_ICC_MALFORMED;
        res.iptc.status = OMC_IPTC_MALFORMED;
        res.irb.status = OMC_IRB_MALFORMED;
        res.jumbf.status = OMC_JUMBF_MALFORMED;
        res.xmp.status = OMC_XMP_MALFORMED;
        return res;
    }

    entries_before = store->entry_count;
    casio_qvci_index = 0U;
    res.scan = omc_scan_auto(file_bytes, file_size, out_blocks, block_cap);
    omc_read_merge_bmff(&res.bmff,
                        omc_bmff_dec(file_bytes, file_size, store,
                                     &use_opts->bmff));

    for (i = 0U; i < res.scan.written; ++i) {
        const omc_blk_ref* block;
        omc_block_id block_id;

        block = &out_blocks[i];
        if (!omc_read_store_block(store, block, &block_id)) {
            res.exif.status = OMC_EXIF_NOMEM;
            res.icc.status = OMC_ICC_NOMEM;
            res.iptc.status = OMC_IPTC_NOMEM;
            res.irb.status = OMC_IRB_NOMEM;
            res.jumbf.status = OMC_JUMBF_NOMEM;
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
                omc_read_clear_casio_simple_context(store, entry_start);
                omc_read_clear_pentax_simple_context(store, entry_start);
                omc_read_clear_ricoh_simple_context(store, entry_start);
                omc_read_remap_ricoh_padded_type2_ifd(store, entry_start);
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
                omc_read_clear_casio_simple_context(store, entry_start);
                omc_read_clear_pentax_simple_context(store, entry_start);
                omc_read_clear_ricoh_simple_context(store, entry_start);
                omc_read_remap_ricoh_padded_type2_ifd(store, entry_start);
            }
        } else if (block->kind == OMC_BLK_CIFF) {
            omc_exif_res ciff_res;

            ciff_res = omc_ciff_dec(file_bytes, file_size, store, block_id,
                                    &use_opts->exif);
            omc_read_merge_exif(&res.exif, ciff_res);
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
        } else if (block->kind == OMC_BLK_MAKERNOTE) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            if (block->format == OMC_SCAN_FMT_JPEG
                && block->aux_u32 == OMC_FOURCC('Q', 'V', 'C', 'I')) {
                omc_read_decode_casio_qvci(block_view, store, block_id,
                                           &casio_qvci_index,
                                           &use_opts->exif, &res.exif);
            }
        } else if (block->kind == OMC_BLK_JUMBF) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;
            omc_jumbf_res jumbf_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            jumbf_res = omc_jumbf_dec(block_view.data, block_view.size, store,
                                      block_id, OMC_ENTRY_FLAG_NONE,
                                      &use_opts->jumbf);
            omc_read_merge_jumbf(&res.jumbf, jumbf_res);
        } else if (block->kind == OMC_BLK_COMP_METADATA) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            if (block->aux_u32 == OMC_FOURCC('x', 'm', 'l', ' ')) {
                omc_xmp_res xmp_res;

                xmp_res = omc_xmp_dec(block_view.data, block_view.size, store,
                                      block_id, OMC_ENTRY_FLAG_NONE,
                                      &use_opts->xmp);
                omc_read_merge_xmp(&res.xmp, xmp_res);
            } else if (block->aux_u32 == OMC_FOURCC('j', 'u', 'm', 'b')
                       || block->aux_u32 == OMC_FOURCC('c', '2', 'p', 'a')) {
                omc_jumbf_res jumbf_res;

                jumbf_res = omc_jumbf_dec(block_view.data, block_view.size,
                                          store, block_id,
                                          OMC_ENTRY_FLAG_NONE,
                                          &use_opts->jumbf);
                omc_read_merge_jumbf(&res.jumbf, jumbf_res);
            } else if (block->aux_u32 == OMC_FOURCC('E', 'x', 'i', 'f')) {
                omc_exif_res exif_res;
                omc_size entry_start;

                entry_start = store->entry_count;
                exif_res = omc_exif_dec(block_view.data, block_view.size,
                                        store, block_id, out_ifds, ifd_cap,
                                        &use_opts->exif);
                omc_read_merge_exif(&res.exif, exif_res);
                omc_read_clear_casio_simple_context(store, entry_start);
                omc_read_clear_pentax_simple_context(store, entry_start);
                omc_read_clear_ricoh_simple_context(store, entry_start);
                omc_read_remap_ricoh_padded_type2_ifd(store, entry_start);
            }
        } else if (block->kind == OMC_BLK_COMMENT) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            (void)omc_read_emit_comment_entry(store, block_id, block_view.data,
                                              block_view.size);
        } else if (block->kind == OMC_BLK_TEXT) {
            omc_const_bytes block_view;
            omc_pay_res pay_res;

            if (!omc_read_block_view(file_bytes, file_size, out_blocks,
                                     res.scan.written, i, payload, payload_cap,
                                     payload_scratch_indices,
                                     payload_scratch_cap, &use_opts->pay,
                                     &block_view, &pay_res)) {
                omc_read_merge_pay(&res.pay, pay_res);
                continue;
            }

            omc_read_merge_pay(&res.pay, pay_res);
            omc_read_decode_png_text(file_bytes, file_size, block, block_view,
                                     1, store, block_id);
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
