#include "omc/omc_exif.h"
#include "read/omc_exif_internal.h"
#include "read/omc_input.h"

#include "read/omc_canon_tables.h"
#include "read/omc_sony_models.h"
#include <stdlib.h>
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
    omc_u64 size;
    const omc_source_range* source;
    omc_source_state* input;
    const omc_source_limits* io_limits;
    omc_exif_source_workspace workspace;
    omc_exif_source_res* source_result;
    omc_u64 raw_source_offset;
    omc_u8 inline_value[8];
    omc_u64 visited_offsets[OMC_EXIF_TASK_CAP];
    omc_u8 visited_masks[OMC_EXIF_TASK_CAP];
    omc_u32 visited_count;
    omc_s64 value_base;
    int source_single_ifd;
    const char* source_ifd_token;
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
    omc_u64 source_offset;
} omc_exif_geotiff_tag_ref;

static int omc_exif_decode_nikon_preview_aliases(omc_exif_ctx *ctx);

static void
omc_exif_maybe_mark_contextual_name(const omc_exif_ctx* ctx, omc_entry* entry);

static int
omc_exif_entry_ifd_equals(const omc_store* store, const omc_entry* entry,
                          const char* ifd_name);

static void
omc_exif_mark_last_contextual_name(omc_exif_ctx* ctx,
                                   omc_entry_name_ctx_kind kind,
                                   omc_u8 variant);

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
omc_exif_write_u32(omc_exif_cfg cfg, omc_u8* bytes, omc_size size,
                   omc_u64 offset, omc_u32 value)
{
    if (bytes == (omc_u8*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }
    if (cfg.little_endian) {
        bytes[(omc_size)offset + 0U] = (omc_u8)(value & 0xFFU);
        bytes[(omc_size)offset + 1U] = (omc_u8)((value >> 8) & 0xFFU);
        bytes[(omc_size)offset + 2U] = (omc_u8)((value >> 16) & 0xFFU);
        bytes[(omc_size)offset + 3U] = (omc_u8)((value >> 24) & 0xFFU);
    } else {
        bytes[(omc_size)offset + 0U] = (omc_u8)((value >> 24) & 0xFFU);
        bytes[(omc_size)offset + 1U] = (omc_u8)((value >> 16) & 0xFFU);
        bytes[(omc_size)offset + 2U] = (omc_u8)((value >> 8) & 0xFFU);
        bytes[(omc_size)offset + 3U] = (omc_u8)(value & 0xFFU);
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

/* Structural values are copied to a small local buffer; resolving a tag
 * value never invalidates a directory entry or another structural field. */
static int
omc_exif_input_read(const omc_exif_ctx* ctx, omc_u64 offset,
                     omc_u8* out, omc_size size)
{
    if (offset > ctx->size || size > ctx->size - offset)
        return 0;
    if (ctx->source != NULL)
        return omc_source_read(ctx->source, offset, out, size, ctx->input,
                                 ctx->io_limits) == OMC_SOURCE_OK;
    if (size != 0U)
        memcpy(out, ctx->bytes + (omc_size)offset, size);
    return 1;
}
static int
omc_exif_input_u16(const omc_exif_ctx* ctx, omc_u64 offset, omc_u16* value)
{
    omc_u8 bytes[2];
    return omc_exif_input_read(ctx, offset, bytes, sizeof(bytes)) &&
           omc_exif_read_u16(ctx->cfg, bytes, sizeof(bytes), 0U, value);
}
static int
omc_exif_input_u32(const omc_exif_ctx* ctx, omc_u64 offset, omc_u32* value)
{
    omc_u8 bytes[4];
    return omc_exif_input_read(ctx, offset, bytes, sizeof(bytes)) &&
           omc_exif_read_u32(ctx->cfg, bytes, sizeof(bytes), 0U, value);
}
static int
omc_exif_input_u64(const omc_exif_ctx* ctx, omc_u64 offset, omc_u64* value)
{
    omc_u8 bytes[8];
    return omc_exif_input_read(ctx, offset, bytes, sizeof(bytes)) &&
           omc_exif_read_u64(ctx->cfg, bytes, sizeof(bytes), 0U, value);
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
omc_exif_resolve_raw(omc_exif_ctx* ctx, omc_u64 entry_value_off,
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

    value_offset = entry_value_off;
    if (total_size > ctx->cfg.inline_size) {
        if (!ctx->cfg.big_tiff) {
            omc_u32 off32;
            if (!omc_exif_input_u32(ctx, entry_value_off, &off32))
                return 0;
            value_offset = off32;
        } else if (!omc_exif_input_u64(ctx, entry_value_off, &value_offset)) {
            return 0;
        }
    }
    if (ctx->source != NULL && total_size > ctx->cfg.inline_size) {
        if (ctx->value_base < 0) {
            omc_u64 delta = (omc_u64)(-(ctx->value_base + 1)) + 1U;
            if (value_offset < delta) return 0;
            value_offset -= delta;
        } else {
            if (value_offset > ~(omc_u64)0 - (omc_u64)ctx->value_base) return 0;
            value_offset += (omc_u64)ctx->value_base;
        }
    }
    if (value_offset > ctx->size || total_size > ctx->size - value_offset)
        return 0;
    ctx->raw_source_offset = value_offset;
    if (ctx->source != NULL) {
        omc_u8* destination;
        if (total_size > ctx->opts.limits.max_value_bytes) {
            *out_ptr = NULL;
            *out_size = total_size;
            return 1;
        }
        destination = ctx->inline_value;
        if (total_size > ctx->cfg.inline_size) {
            if (total_size > ctx->workspace.value_capacity) {
                if (total_size > ctx->source_result->value_scratch_needed)
                    ctx->source_result->value_scratch_needed = total_size;
                *out_ptr = NULL;
                *out_size = total_size;
                return 1;
            }
            destination = ctx->workspace.value;
            if (total_size > ctx->source_result->value_scratch_used)
                ctx->source_result->value_scratch_used = (omc_size)total_size;
        }
        if (!omc_exif_input_read(ctx, value_offset, destination,
                                  (omc_size)total_size))
            return 0;
        *out_ptr = destination;
    } else {
        *out_ptr = ctx->bytes + (omc_size)value_offset;
    }
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

static int
omc_exif_ascii_contains_nocase(const omc_u8* text, omc_u32 text_size,
                               const char* needle)
{
    omc_u32 needle_size;
    omc_u32 i;

    if (text == (const omc_u8*)0 || needle == (const char*)0) {
        return 0;
    }

    needle_size = (omc_u32)strlen(needle);
    if (needle_size == 0U) {
        return 1;
    }
    if (text_size < needle_size) {
        return 0;
    }

    for (i = 0U; i + needle_size <= text_size; ++i) {
        omc_u32 j;
        int matched;

        matched = 1;
        for (j = 0U; j < needle_size; ++j) {
            if (omc_exif_ascii_tolower((int)text[i + j])
                != omc_exif_ascii_tolower((int)(unsigned char)needle[j])) {
                matched = 0;
                break;
            }
        }
        if (matched) {
            return 1;
        }
    }
    return 0;
}

static int
omc_exif_ascii_equals_nocase(const omc_u8* text, omc_u32 text_size,
                             const char* expected)
{
    omc_u32 i;

    if (text == (const omc_u8*)0 || expected == (const char*)0) {
        return 0;
    }

    for (i = 0U; expected[i] != '\0'; ++i) {
        if (i >= text_size) {
            return 0;
        }
        if (omc_exif_ascii_tolower((int)text[i])
            != omc_exif_ascii_tolower((int)(unsigned char)expected[i])) {
            return 0;
        }
    }
    return i == text_size;
}

static omc_u32
omc_exif_trim_nul_size(const omc_u8* text, omc_u32 text_size)
{
    omc_u32 i;

    if (text == (const omc_u8*)0) {
        return 0U;
    }
    for (i = 0U; i < text_size; ++i) {
        if (text[i] == 0U) {
            return i;
        }
    }
    return text_size;
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
    omc_u32 n;

    if (out_text == (const omc_u8**)0 || out_size == (omc_u32*)0) {
        return 0;
    }

    entry = omc_exif_find_first_entry(store, ifd_name, tag);
    if (entry == (const omc_entry*)0
        || (entry->value.kind != OMC_VAL_TEXT
            && entry->value.kind != OMC_VAL_BYTES)) {
        return 0;
    }

    view = omc_arena_view(&store->arena, entry->value.u.ref);
    if (view.size > (omc_size)(~(omc_u32)0)) {
        return 0;
    }
    if (entry->value.kind == OMC_VAL_BYTES) {
        n = (omc_u32)view.size;
        while (n != 0U && view.data[n - 1U] == 0U) {
            --n;
        }
        while (n != 0U && view.data[n - 1U] == (omc_u8)' ') {
            --n;
        }
        if (n == 0U) {
            return 0;
        }
        *out_text = view.data;
        *out_size = n;
        return 1;
    }

    *out_text = view.data;
    *out_size = (omc_u32)view.size;
    return 1;
}

static int
omc_exif_find_outer_ifd0_ascii(const omc_exif_ctx* ctx, omc_u16 tag,
                               const omc_u8** out_text, omc_u32* out_size)
{
    omc_u32 ifd0_off32;
    omc_u64 ifd0_off;
    omc_u16 entry_count;
    omc_u32 i;

    if (out_text == (const omc_u8**)0 || out_size == (omc_u32*)0) {
        return 0;
    }
    if (ctx == (const omc_exif_ctx*)0 || ctx->bytes == (const omc_u8*)0) {
        return 0;
    }
    if (ctx->cfg.big_tiff || ctx->size < 8U) {
        return 0;
    }
    if (!omc_exif_input_u32(ctx, 4U, &ifd0_off32)) {
        return 0;
    }

    ifd0_off = (omc_u64)ifd0_off32;
    if (!omc_exif_input_u16(ctx, ifd0_off,
                           &entry_count)) {
        return 0;
    }

    for (i = 0U; i < (omc_u32)entry_count; ++i) {
        omc_u64 entry_off;
        omc_u16 entry_tag;
        omc_u16 type16;
        omc_u32 count32;
        omc_u32 text_off32;
        omc_u32 text_size;

        entry_off = ifd0_off + 2U + ((omc_u64)i * 12U);
        if (!omc_exif_input_u16(ctx, entry_off,
                               &entry_tag)
            || !omc_exif_input_u16(ctx, entry_off + 2U, &type16)
            || !omc_exif_input_u32(ctx, entry_off + 4U, &count32)) {
            return 0;
        }
        if (entry_tag != tag || type16 != 2U || count32 == 0U) {
            continue;
        }

        if (count32 <= 4U) {
            text_off32 = (omc_u32)(entry_off + 8U);
        } else if (!omc_exif_input_u32(ctx, entry_off + 8U, &text_off32)) {
            return 0;
        }

        if ((omc_u64)text_off32 >= ctx->size) {
            return 0;
        }
        if ((omc_u64)count32 > (ctx->size - (omc_u64)text_off32)) {
            return 0;
        }

        text_size = count32;
        while (text_size != 0U && ctx->bytes[text_off32 + text_size - 1U] == 0U) {
            --text_size;
        }
        while (text_size != 0U
               && ctx->bytes[text_off32 + text_size - 1U] == (omc_u8)' ') {
            --text_size;
        }
        if (text_size == 0U) {
            return 0;
        }

        *out_text = ctx->bytes + text_off32;
        *out_size = text_size;
        return 1;
    }

    return 0;
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

static int
omc_exif_read_u16be_raw(const omc_u8* bytes, omc_u64 size, omc_u64 offset,
                        omc_u16* out_value)
{
    if (bytes == (const omc_u8*)0 || out_value == (omc_u16*)0) {
        return 0;
    }
    if (offset > size || (size - offset) < 2U) {
        return 0;
    }

    *out_value = (omc_u16)((((omc_u16)bytes[(omc_size)offset + 0U]) << 8)
                           | ((omc_u16)bytes[(omc_size)offset + 1U]));
    return 1;
}

static int
omc_exif_read_u32be_raw(const omc_u8* bytes, omc_u64 size, omc_u64 offset,
                        omc_u32* out_value)
{
    if (bytes == (const omc_u8*)0 || out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > size || (size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    return 1;
}

static int
omc_exif_read_u8_raw(const omc_u8* bytes, omc_u64 size, omc_u64 offset,
                     omc_u8* out_value)
{
    if (bytes == (const omc_u8*)0 || out_value == (omc_u8*)0) {
        return 0;
    }
    if (offset >= size) {
        return 0;
    }

    *out_value = bytes[(omc_size)offset];
    return 1;
}

static omc_s8
omc_exif_u8_to_i8(omc_u8 value)
{
    if (value <= 0x7FU) {
        return (omc_s8)value;
    }
    return (omc_s8)((omc_s16)value - 256);
}

static int
omc_exif_entry_raw_view(const omc_store* store, const omc_entry* entry,
                        omc_const_bytes* out_view)
{
    if (out_view == (omc_const_bytes*)0) {
        return 0;
    }

    out_view->data = (const omc_u8*)0;
    out_view->size = 0U;
    if (store == (const omc_store*)0 || entry == (const omc_entry*)0) {
        return 0;
    }

    if (entry->value.kind == OMC_VAL_BYTES || entry->value.kind == OMC_VAL_TEXT
        || entry->value.kind == OMC_VAL_ARRAY) {
        *out_view = omc_arena_view(&store->arena, entry->value.u.ref);
        return 1;
    }
    return 0;
}

static omc_exif_status
omc_exif_entry_raw_copy_view(const omc_store* store, const omc_entry* entry,
                             omc_u8** out_copy, omc_const_bytes* out_view)
{
    omc_const_bytes view;
    omc_u8* copy;

    if (out_copy == (omc_u8**)0 || out_view == (omc_const_bytes*)0) {
        return OMC_EXIF_MALFORMED;
    }

    *out_copy = (omc_u8*)0;
    out_view->data = (const omc_u8*)0;
    out_view->size = 0U;
    if (!omc_exif_entry_raw_view(store, entry, &view) || view.size == 0U) {
        return OMC_EXIF_OK;
    }

    copy = (omc_u8*)malloc(view.size);
    if (copy == (omc_u8*)0) {
        return OMC_EXIF_NOMEM;
    }

    memcpy(copy, view.data, view.size);
    *out_copy = copy;
    out_view->data = copy;
    out_view->size = view.size;
    return OMC_EXIF_OK;
}

static omc_u32
omc_exif_write_u32_decimal(char* out, omc_u32 value)
{
    char tmp[16];
    omc_u32 n;
    omc_u32 i;

    n = 0U;
    do {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && n < (omc_u32)sizeof(tmp));

    for (i = 0U; i < n; ++i) {
        out[i] = tmp[n - 1U - i];
    }
    return n;
}

static int
omc_exif_make_subifd_name(const char* vendor_prefix, const char* subtable,
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
    if (prefix_len == 0U || sub_len == 0U) {
        return 0;
    }
    if (out_cap < (prefix_len + 1U + sub_len + 2U)) {
        return 0;
    }

    memcpy(out, vendor_prefix, prefix_len);
    out[prefix_len] = '_';
    memcpy(out + prefix_len + 1U, subtable, sub_len);
    out[prefix_len + 1U + sub_len] = '_';
    digits = omc_exif_write_u32_decimal(out + prefix_len + 1U + sub_len + 1U,
                                        index);
    if (prefix_len + 1U + sub_len + 1U + digits + 1U > out_cap) {
        return 0;
    }
    out[prefix_len + 1U + sub_len + 1U + digits] = '\0';
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
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;

    omc_exif_maybe_mark_contextual_name(ctx, &entry);

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
omc_exif_emit_derived_exif_u32(omc_exif_ctx* ctx, const char* ifd_name,
                               omc_u16 tag, omc_u32 order_in_block,
                               omc_u32 value32)
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
    omc_val_make_u32(&value, value32);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_derived_exif_urational(omc_exif_ctx* ctx, const char* ifd_name,
                                     omc_u16 tag, omc_u32 order_in_block,
                                     omc_u32 numer, omc_u32 denom)
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
    omc_val_init(&value);
    value.kind = OMC_VAL_SCALAR;
    value.elem_type = OMC_ELEM_URATIONAL;
    value.count = 1U;
    value.u.ur.numer = numer;
    value.u.ur.denom = denom;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_derived_exif_i8(omc_exif_ctx* ctx, const char* ifd_name,
                              omc_u16 tag, omc_u32 order_in_block,
                              omc_s8 value8)
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
    omc_val_make_i64(&value, value8);
    value.elem_type = OMC_ELEM_I8;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_derived_exif_i16(omc_exif_ctx* ctx, const char* ifd_name,
                               omc_u16 tag, omc_u32 order_in_block,
                               omc_s16 value16)
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
    omc_val_make_i64(&value, value16);
    value.elem_type = OMC_ELEM_I16;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_derived_exif_i32(omc_exif_ctx* ctx, const char* ifd_name,
                               omc_u16 tag, omc_u32 order_in_block,
                               omc_s32 value32)
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
    omc_val_make_i64(&value, value32);
    value.elem_type = OMC_ELEM_I32;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block, 1U);
}

static omc_exif_status
omc_exif_emit_derived_exif_f32_bits(omc_exif_ctx* ctx, const char* ifd_name,
                                    omc_u16 tag, omc_u32 order_in_block,
                                    omc_u32 bits)
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
    omc_val_make_f32_bits(&value, bits);
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

    status = omc_exif_store_ref(ctx, text, text_size, &text_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_key_make_exif_tag(&key, ifd_ref, tag);
    omc_val_make_text(&value, text_ref, OMC_TEXT_ASCII);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      text_size);
}

static omc_u32
omc_exif_trim_ascii_span(const omc_u8* text, omc_u32 text_size,
                         omc_u32* out_start)
{
    omc_u32 start;
    omc_u32 end;

    if (out_start == (omc_u32*)0) {
        return 0U;
    }
    *out_start = 0U;
    if (text == (const omc_u8*)0) {
        return 0U;
    }

    start = 0U;
    end = text_size;
    while (start < end
           && (text[start] == 0U || text[start] == (omc_u8)' '
               || text[start] == (omc_u8)'\t'
               || text[start] == (omc_u8)'\r'
               || text[start] == (omc_u8)'\n')) {
        ++start;
    }
    while (end > start
           && (text[end - 1U] == 0U || text[end - 1U] == (omc_u8)' '
               || text[end - 1U] == (omc_u8)'\t'
               || text[end - 1U] == (omc_u8)'\r'
               || text[end - 1U] == (omc_u8)'\n')) {
        --end;
    }
    *out_start = start;
    return end - start;
}

static void
omc_exif_write_two_digits(omc_u8 value, omc_u8* out)
{
    out[0] = (omc_u8)('0' + ((value / 10U) % 10U));
    out[1] = (omc_u8)('0' + (value % 10U));
}

static omc_exif_status
omc_exif_emit_derived_exif_bytes(omc_exif_ctx* ctx, const char* ifd_name,
                                 omc_u16 tag, omc_u32 order_in_block,
                                 const omc_u8* raw, omc_u32 raw_size)
{
    omc_key key;
    omc_val value;
    omc_byte_ref ifd_ref;
    omc_byte_ref raw_ref;
    omc_exif_status status;

    if (ifd_name == (const char*)0 || raw == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        omc_key_init(&key);
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          raw_size);
    }

    status = omc_exif_store_ref(ctx, raw, raw_size, &raw_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_key_make_exif_tag(&key, ifd_ref, tag);
    omc_val_make_bytes(&value, raw_ref);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      raw_size);
}

static omc_exif_status
omc_exif_emit_derived_exif_bytes_ref(omc_exif_ctx* ctx, const char* ifd_name,
                                     omc_u16 tag, omc_u32 order_in_block,
                                     omc_byte_ref raw_ref)
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
                                          raw_ref.size);
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_key_make_exif_tag(&key, ifd_ref, tag);
    omc_val_make_bytes(&value, raw_ref);
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      raw_ref.size);
}

static omc_exif_status
omc_exif_emit_derived_exif_array_copy(omc_exif_ctx* ctx, const char* ifd_name,
                                      omc_u16 tag, omc_u32 order_in_block,
                                      omc_elem_type elem_type,
                                      const omc_u8* raw, omc_u32 raw_size,
                                      omc_u32 count)
{
    omc_key key;
    omc_val value;
    omc_byte_ref ifd_ref;
    omc_byte_ref raw_ref;
    omc_exif_status status;

    if (ifd_name == (const char*)0 || raw == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        omc_key_init(&key);
        omc_val_init(&value);
        return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                          count);
    }

    status = omc_exif_store_ref(ctx, raw, raw_size, &raw_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    omc_key_make_exif_tag(&key, ifd_ref, tag);
    omc_val_init(&value);
    value.kind = OMC_VAL_ARRAY;
    value.elem_type = elem_type;
    value.count = count;
    value.u.ref = raw_ref;
    return omc_exif_add_derived_entry(ctx, &key, &value, order_in_block,
                                      count);
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

    if (ctx->source_ifd_token != NULL)
        return omc_exif_store_cstr_len(ctx, ctx->source_ifd_token,
                                        strlen(ctx->source_ifd_token), out_ref);
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
    omc_u8 signature[2];

    if (ctx->size < 8U) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }

    if (!omc_exif_input_read(ctx, 0U, signature, 2U)) {
        ctx->res.status = OMC_EXIF_MALFORMED;
        return 0;
    }
    if (signature[0] == (omc_u8)'I' && signature[1] == (omc_u8)'I') {
        ctx->cfg.little_endian = 1;
    } else if (signature[0] == (omc_u8)'M'
               && signature[1] == (omc_u8)'M') {
        ctx->cfg.little_endian = 0;
    } else {
        ctx->res.status = OMC_EXIF_UNSUPPORTED;
        return 0;
    }

    if (!omc_exif_input_u16(ctx, 2U, &version)) {
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

        if (!omc_exif_input_u32(ctx, 4U,
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
        if (!omc_exif_input_u16(ctx, 4U,
                               &off_size)
            || !omc_exif_input_u16(ctx, 6U,
                                  &reserved)
            || !omc_exif_input_u64(ctx, 8U,
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
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;
    entry.flags = flags;

    total_size = 0U;
    if ((flags & OMC_ENTRY_FLAG_UNREADABLE) == 0U &&
        (!omc_exif_elem_size(type, &elem_size) ||
         !omc_exif_mul_u64((omc_u64)elem_size, count, &total_size))) {
        return OMC_EXIF_LIMIT;
    }

    if ((flags & OMC_ENTRY_FLAG_UNREADABLE) != 0U ||
        (ctx->source != NULL && raw == NULL) ||
        ((type == 2U || type == 129U || type == 7U || count != 1U) &&
         total_size > ctx->opts.limits.max_value_bytes)) {
        if ((flags & OMC_ENTRY_FLAG_UNREADABLE) == 0U)
            entry.flags |= OMC_ENTRY_FLAG_TRUNCATED;
        entry.value.kind = OMC_VAL_EMPTY;
        omc_exif_maybe_mark_contextual_name(ctx, &entry);
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

    if (type == 7U || count != 1U || type == 2U || type == 129U) {
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
        entry.value.byte_order = ctx->cfg.little_endian
                                    ? OMC_BYTE_ORDER_LITTLE
                                    : OMC_BYTE_ORDER_BIG;
        omc_exif_maybe_mark_contextual_name(ctx, &entry);
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

    omc_exif_maybe_mark_contextual_name(ctx, &entry);
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

static omc_exif_status
omc_exif_emit_derived_exif_u16_array_le(omc_exif_ctx* ctx,
                                        const char* ifd_name, omc_u16 tag,
                                        omc_u32 order_in_block,
                                        const omc_u16* values,
                                        omc_u32 count);

typedef enum omc_exif_mn_vendor {
    OMC_EXIF_MN_UNKNOWN = 0,
    OMC_EXIF_MN_FUJI = 1,
    OMC_EXIF_MN_NIKON = 2,
    OMC_EXIF_MN_CANON = 3,
    OMC_EXIF_MN_SONY = 4,
    OMC_EXIF_MN_APPLE = 5,
    OMC_EXIF_MN_KODAK = 6,
    OMC_EXIF_MN_FLIR = 7,
    OMC_EXIF_MN_HP = 8,
    OMC_EXIF_MN_NINTENDO = 9,
    OMC_EXIF_MN_RECONYX = 10,
    OMC_EXIF_MN_CASIO = 11,
    OMC_EXIF_MN_PENTAX = 12,
    OMC_EXIF_MN_RICOH = 13,
    OMC_EXIF_MN_OLYMPUS = 14,
    OMC_EXIF_MN_PANASONIC = 15,
    OMC_EXIF_MN_SAMSUNG = 16,
    OMC_EXIF_MN_MINOLTA = 17,
    OMC_EXIF_MN_MOTOROLA = 18,
    OMC_EXIF_MN_SIGMA = 19,
    OMC_EXIF_MN_PHASEONE = 20
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
omc_exif_set_sony_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_sony";
    opts->tokens.subifd_prefix = "mk_sony_subifd";
    opts->tokens.exif_ifd_token = "mk_sony_exififd";
    opts->tokens.gps_ifd_token = "mk_sony_gpsifd";
    opts->tokens.interop_ifd_token = "mk_sony_interopifd";
}

static void
omc_exif_set_sigma_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_sigma";
    opts->tokens.subifd_prefix = "mk_sigma_subifd";
    opts->tokens.exif_ifd_token = "mk_sigma_exififd";
    opts->tokens.gps_ifd_token = "mk_sigma_gpsifd";
    opts->tokens.interop_ifd_token = "mk_sigma_interopifd";
}

static void
omc_exif_set_samsung_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_samsung";
    opts->tokens.subifd_prefix = "mk_samsung_subifd";
    opts->tokens.exif_ifd_token = "mk_samsung_exififd";
    opts->tokens.gps_ifd_token = "mk_samsung_gpsifd";
    opts->tokens.interop_ifd_token = "mk_samsung_interopifd";
}

static void
omc_exif_set_apple_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_apple";
    opts->tokens.subifd_prefix = "mk_apple_subifd";
    opts->tokens.exif_ifd_token = "mk_apple_exififd";
    opts->tokens.gps_ifd_token = "mk_apple_gpsifd";
    opts->tokens.interop_ifd_token = "mk_apple_interopifd";
}

static void
omc_exif_set_flir_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_flir";
    opts->tokens.subifd_prefix = "mk_flir_subifd";
    opts->tokens.exif_ifd_token = "mk_flir_exififd";
    opts->tokens.gps_ifd_token = "mk_flir_gpsifd";
    opts->tokens.interop_ifd_token = "mk_flir_interopifd";
}

static void
omc_exif_set_nintendo_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_nintendo";
    opts->tokens.subifd_prefix = "mk_nintendo_subifd";
    opts->tokens.exif_ifd_token = "mk_nintendo_exififd";
    opts->tokens.gps_ifd_token = "mk_nintendo_gpsifd";
    opts->tokens.interop_ifd_token = "mk_nintendo_interopifd";
}

static void
omc_exif_set_casio_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_casio_type2_";
    opts->tokens.subifd_prefix = "mk_casio_type2_subifd_";
    opts->tokens.exif_ifd_token = "mk_casio_type2_exififd";
    opts->tokens.gps_ifd_token = "mk_casio_type2_gpsifd";
    opts->tokens.interop_ifd_token = "mk_casio_type2_interopifd";
}

static void
omc_exif_set_pentax_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_pentax";
    opts->tokens.subifd_prefix = "mk_pentax_subifd";
    opts->tokens.exif_ifd_token = "mk_pentax_exififd";
    opts->tokens.gps_ifd_token = "mk_pentax_gpsifd";
    opts->tokens.interop_ifd_token = "mk_pentax_interopifd";
}

static void
omc_exif_set_pentax_type2_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_pentax_type2_";
    opts->tokens.subifd_prefix = "mk_pentax_type2_subifd_";
    opts->tokens.exif_ifd_token = "mk_pentax_type2_exififd";
    opts->tokens.gps_ifd_token = "mk_pentax_type2_gpsifd";
    opts->tokens.interop_ifd_token = "mk_pentax_type2_interopifd";
}

static void
omc_exif_set_ricoh_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_ricoh";
    opts->tokens.subifd_prefix = "mk_ricoh_subifd";
    opts->tokens.exif_ifd_token = "mk_ricoh_exififd";
    opts->tokens.gps_ifd_token = "mk_ricoh_gpsifd";
    opts->tokens.interop_ifd_token = "mk_ricoh_interopifd";
}

static void
omc_exif_set_ricoh_type2_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_ricoh_type2_";
    opts->tokens.subifd_prefix = "mk_ricoh_type2_subifd_";
    opts->tokens.exif_ifd_token = "mk_ricoh_type2_exififd";
    opts->tokens.gps_ifd_token = "mk_ricoh_type2_gpsifd";
    opts->tokens.interop_ifd_token = "mk_ricoh_type2_interopifd";
}

static void
omc_exif_set_panasonic_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_panasonic";
    opts->tokens.subifd_prefix = "mk_panasonic_subifd";
    opts->tokens.exif_ifd_token = "mk_panasonic_exififd";
    opts->tokens.gps_ifd_token = "mk_panasonic_gpsifd";
    opts->tokens.interop_ifd_token = "mk_panasonic_interopifd";
}

static void
omc_exif_set_minolta_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_minolta";
    opts->tokens.subifd_prefix = "mk_minolta_subifd";
    opts->tokens.exif_ifd_token = "mk_minolta_exififd";
    opts->tokens.gps_ifd_token = "mk_minolta_gpsifd";
    opts->tokens.interop_ifd_token = "mk_minolta_interopifd";
}

static void
omc_exif_set_motorola_tokens(omc_exif_opts* opts)
{
    if (opts == (omc_exif_opts*)0) {
        return;
    }

    opts->tokens.ifd_prefix = "mk_motorola";
    opts->tokens.subifd_prefix = "mk_motorola_subifd";
    opts->tokens.exif_ifd_token = "mk_motorola_exififd";
    opts->tokens.gps_ifd_token = "mk_motorola_gpsifd";
    opts->tokens.interop_ifd_token = "mk_motorola_interopifd";
}

static void
omc_exif_init_child_cfg(omc_exif_ctx* child, const omc_exif_ctx* parent,
                        const omc_u8* bytes, omc_size size,
                        const omc_exif_opts* opts, omc_exif_cfg cfg)
{
    memset(child, 0, sizeof(*child));
    child->bytes = bytes;
    child->size = size;
    if (bytes == NULL && parent->source != NULL) {
        child->size = parent->size;
        child->source = parent->source;
        child->input = parent->input;
        child->io_limits = parent->io_limits;
        child->workspace = parent->workspace;
        child->source_result = parent->source_result;
        child->value_base = parent->value_base;
    }
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

    if (child.res.status != OMC_EXIF_LIMIT && child.res.status != OMC_EXIF_NOMEM)
        (void)omc_exif_decode_nikon_preview_aliases(&child);
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

    if ((bytes == NULL && parent->source == NULL) || opts == NULL) {
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

static int omc_exif_decode_ifd_blob_offsets(omc_exif_ctx *parent, const omc_u8 *bytes,
                                            omc_u64 size, omc_u64 ifd_off,
                                            const omc_exif_opts *opts, omc_exif_cfg cfg,
                                            omc_u64 min_tail_bytes, omc_s64 value_base)
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
        || min_tail_bytes > ((size - entry_table_off) - table_bytes)) {
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
            if (value_base < 0) {
                omc_u64 delta = (omc_u64)(-(value_base + 1)) + 1U;
                value_off = off32 >= delta ? off32 - delta : ~(omc_u64)0;
            } else {
                value_off = (omc_u64)value_base + off32;
            }
        }

        raw = value_off <= size && raw_size <= size - value_off
                  ? child.bytes + (omc_size)value_off
                  : NULL;

        emit_pointer = 1;
        if ((tag == 0x8769U || tag == 0x8825U || tag == 0xA005U
             || tag == 0x014AU)
            && !child.opts.include_pointer_tags) {
            emit_pointer = 0;
        }

        if (!child.measure_only && emit_pointer) {
            estatus = omc_exif_add_entry(
                &child, &token_ref, tag, type, count, raw, raw_size, i,
                raw != NULL ? OMC_ENTRY_FLAG_NONE : OMC_ENTRY_FLAG_UNREADABLE);
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

static int omc_exif_decode_ifd_blob_loose_cfg_tail(
    omc_exif_ctx *parent, const omc_u8 *bytes, omc_u64 size, omc_u64 ifd_off,
    const omc_exif_opts *opts, omc_exif_cfg cfg, omc_u64 min_tail_bytes)
{
    return omc_exif_decode_ifd_blob_offsets(parent, bytes, size, ifd_off, opts, cfg,
                                            min_tail_bytes, 0);
}

static int
omc_exif_decode_ifd_blob_loose_cfg(omc_exif_ctx* parent, const omc_u8* bytes,
                                   omc_u64 size, omc_u64 ifd_off,
                                   const omc_exif_opts* opts,
                                   omc_exif_cfg cfg)
{
    return omc_exif_decode_ifd_blob_loose_cfg_tail(parent, bytes, size,
                                                   ifd_off, opts, cfg, 4U);
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
omc_exif_decode_ifd_blob_loose_named_cfg_tail(omc_exif_ctx* parent,
                                              const omc_u8* bytes,
                                              omc_u64 size,
                                              omc_u64 ifd_off,
                                              const omc_exif_opts* opts,
                                              omc_exif_cfg cfg,
                                              const char* ifd_name,
                                              omc_u64 min_tail_bytes)
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

    if (bytes == (const omc_u8*)0 || opts == (const omc_exif_opts*)0
        || ifd_name == (const char*)0) {
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
        || min_tail_bytes > ((size - entry_table_off) - table_bytes)) {
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
        tok_status = omc_exif_store_cstr_len(&child, ifd_name, strlen(ifd_name),
                                             &token_ref);
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
omc_exif_decode_ifd_blob_loose_named_cfg(omc_exif_ctx* parent,
                                         const omc_u8* bytes, omc_u64 size,
                                         omc_u64 ifd_off,
                                         const omc_exif_opts* opts,
                                         omc_exif_cfg cfg,
                                         const char* ifd_name)
{
    return omc_exif_decode_ifd_blob_loose_named_cfg_tail(parent, bytes, size,
                                                         ifd_off, opts, cfg,
                                                         ifd_name, 4U);
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

static int
omc_exif_pentax_ifd_has_type2_signature(omc_exif_cfg cfg, const omc_u8* raw,
                                        omc_u64 raw_size, omc_u64 ifd_off)
{
    omc_u16 entry_count16;
    omc_u64 entries_off;
    omc_u64 table_bytes;
    omc_u32 i;

    if (raw == (const omc_u8*)0) {
        return 0;
    }
    if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, ifd_off,
                           &entry_count16)
        || entry_count16 == 0U) {
        return 0;
    }

    entries_off = ifd_off + 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)
        || entries_off > raw_size || table_bytes > (raw_size - entries_off)) {
        return 0;
    }

    for (i = 0U; i < (omc_u32)entry_count16; ++i) {
        omc_u16 tag16;

        if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size,
                               entries_off + ((omc_u64)i * 12U), &tag16)) {
            return 0;
        }
        if (tag16 == 0x1000U || tag16 == 0x1001U) {
            return 1;
        }
    }
    return 0;
}

static int
omc_exif_pentax_makernote_has_type2_signature(const omc_u8* raw,
                                              omc_u64 raw_size)
{
    omc_exif_cfg cfg;

    if (raw == (const omc_u8*)0) {
        return 0;
    }

    if (raw_size >= 8U && memcmp(raw, "AOC\0", 4U) == 0) {
        cfg = omc_exif_make_classic_cfg(1);
        cfg.little_endian = (raw[4U] == (omc_u8)'I'
                             && raw[5U] == (omc_u8)'I');
        return omc_exif_pentax_ifd_has_type2_signature(cfg, raw, raw_size, 6U);
    }

    cfg = omc_exif_make_classic_cfg(1);
    if (omc_exif_pentax_ifd_has_type2_signature(cfg, raw, raw_size, 0U)) {
        return 1;
    }
    cfg.little_endian = 0;
    return omc_exif_pentax_ifd_has_type2_signature(cfg, raw, raw_size, 0U);
}

static int
omc_exif_pentax_model_uses_type2(const omc_u8* model_text, omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model_text, model_size,
                                        "PENTAX Optio 330")
           || omc_exif_ascii_equals_nocase(model_text, model_size,
                                           "PENTAX Optio 430")
           || omc_exif_ascii_equals_nocase(model_text, model_size,
                                           "Optio 330")
           || omc_exif_ascii_equals_nocase(model_text, model_size,
                                           "Optio 430");
}

static int
omc_exif_pentax_model_uses_type2_store(const omc_exif_ctx* ctx)
{
    const omc_u8* model_text;
    omc_u32 model_size;

    model_text = (const omc_u8*)0;
    model_size = 0U;
    if (ctx == (const omc_exif_ctx*)0) {
        return 0;
    }
    if (!omc_exif_find_first_text(ctx->store, "ifd0", 0x0110U, &model_text,
                                  &model_size)
        && !omc_exif_find_outer_ifd0_ascii(ctx, 0x0110U, &model_text,
                                           &model_size)) {
        return 0;
    }
    return omc_exif_pentax_model_uses_type2(model_text, model_size);
}

static int
omc_exif_motorola_model_prefers_placeholder(const omc_u8* model_text,
                                            omc_u32 model_size)
{
    return omc_exif_ascii_starts_with_nocase(model_text, model_size, "XT");
}

static int
omc_exif_sigma_main_0033_prefers_placeholder(const omc_u8* model_text,
                                             omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model_text, model_size,
                                        "SIGMA DP1 Merrill")
           || omc_exif_ascii_equals_nocase(model_text, model_size,
                                           "SIGMA dp2 Quattro");
}

static int
omc_exif_sigma_main_fp_l_prefers_placeholder(const omc_u8* model_text,
                                             omc_u32 model_size, omc_u16 tag)
{
    if (!omc_exif_ascii_equals_nocase(model_text, model_size, "SIGMA fp L")) {
        return 0;
    }

    switch (tag) {
    case 0x0026U:
    case 0x0027U:
    case 0x002AU:
    case 0x002BU:
    case 0x0048U:
    case 0x0056U:
        return 1;
    default:
        return 0;
    }
}

static int
omc_exif_sigma_main_sd_quattro_h_prefers_placeholder(const omc_u8* model_text,
                                                     omc_u32 model_size,
                                                     omc_u16 tag)
{
    if (!omc_exif_ascii_equals_nocase(model_text, model_size,
                                      "sd Quattro H")) {
        return 0;
    }
    return tag == 0x0034U || tag == 0x004BU;
}

typedef struct omc_exif_pentax_subdir_candidate {
    omc_u16 tag;
    int scalar_u8_valid;
    omc_u8 scalar_u8;
    omc_byte_ref raw_ref;
} omc_exif_pentax_subdir_candidate;

static int
omc_exif_pentax_has_detected_faces(const omc_store* store,
                                   const omc_exif_pentax_subdir_candidate* cands,
                                   omc_u32 cand_count)
{
    omc_u32 i;

    if (store == (const omc_store*)0
        || cands == (const omc_exif_pentax_subdir_candidate*)0) {
        return 0;
    }

    for (i = 0U; i < cand_count; ++i) {
        const omc_exif_pentax_subdir_candidate* cand;

        cand = &cands[i];
        if (cand->tag != 0x0060U) {
            continue;
        }
        if (cand->scalar_u8_valid) {
            return cand->scalar_u8 != 0U;
        }
        if (cand->raw_ref.size != 0U) {
            omc_const_bytes raw;

            raw = omc_arena_view(&store->arena, cand->raw_ref);
            if (raw.size != 0U) {
                return raw.data[0U] != 0U;
            }
        }
    }
    return 0;
}

static int
omc_exif_decode_pentax_u8_table_ref(omc_exif_ctx* ctx, const char* ifd_name,
                                    omc_byte_ref raw_ref)
{
    omc_const_bytes raw;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || ifd_name == (const char*)0) {
        return 1;
    }

    raw = omc_arena_view(&ctx->store->arena, raw_ref);
    if (raw.size == 0U) {
        return 1;
    }
    if (ctx->opts.limits.max_entries_per_ifd != 0U
        && raw.size > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }

    for (i = 0U; i < (omc_u32)raw.size; ++i) {
        omc_exif_status status;

        raw = omc_arena_view(&ctx->store->arena, raw_ref);
        if (i >= (omc_u32)raw.size) {
            break;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, (omc_u16)i, i,
                                               raw.data[i]);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    return 1;
}

static int
omc_exif_decode_pentax_binary_subdirs(omc_exif_ctx* ctx, const char* mk_ifd0)
{
    omc_exif_pentax_subdir_candidate cands[16];
    omc_u32 cand_count;
    omc_size i;
    int have_detected_faces;
    omc_u32 idx_srinfo;
    omc_u32 idx_faceinfo;
    omc_u32 idx_aeinfo2;
    omc_u32 idx_shotinfo;
    omc_u32 idx_facepos;
    omc_u32 idx_facesize;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || mk_ifd0 == (const char*)0) {
        return 1;
    }

    cand_count = 0U;
    for (i = 0U; i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_exif_pentax_subdir_candidate* cand;

        entry = &ctx->store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || !omc_exif_entry_ifd_equals(ctx->store, entry, mk_ifd0)) {
            continue;
        }

        switch (entry->key.u.exif_tag.tag) {
        case 0x005CU:
        case 0x0060U:
        case 0x0206U:
        case 0x0226U:
        case 0x0227U:
        case 0x0228U:
            break;
        default:
            continue;
        }

        if (cand_count >= (omc_u32)(sizeof(cands) / sizeof(cands[0]))) {
            break;
        }

        cand = &cands[cand_count];
        memset(cand, 0, sizeof(*cand));
        cand->tag = entry->key.u.exif_tag.tag;
        if (entry->value.kind == OMC_VAL_SCALAR
            && entry->value.elem_type == OMC_ELEM_U8
            && cand->tag == 0x0060U) {
            cand->scalar_u8_valid = 1;
            cand->scalar_u8 = (omc_u8)entry->value.u.u64;
        } else if (entry->value.kind == OMC_VAL_BYTES
                   || entry->value.kind == OMC_VAL_ARRAY) {
            cand->raw_ref = entry->value.u.ref;
        } else {
            continue;
        }
        cand_count += 1U;
    }

    if (cand_count == 0U) {
        return 1;
    }

    have_detected_faces = omc_exif_pentax_has_detected_faces(ctx->store, cands,
                                                             cand_count);
    idx_srinfo = 0U;
    idx_faceinfo = 0U;
    idx_aeinfo2 = 0U;
    idx_shotinfo = 0U;
    idx_facepos = 0U;
    idx_facesize = 0U;

    for (i = 0U; i < cand_count; ++i) {
        const omc_exif_pentax_subdir_candidate* cand;
        char ifd_name[64];

        cand = &cands[i];
        if (cand->tag == 0x0060U && cand->scalar_u8_valid) {
            omc_exif_status status;

            if (!omc_exif_make_subifd_name("mk_pentax", "faceinfo",
                                           idx_faceinfo++, ifd_name,
                                           sizeof(ifd_name))) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0000U, 0U,
                                                   cand->scalar_u8);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            continue;
        }

        if (cand->raw_ref.size == 0U) {
            continue;
        }

        switch (cand->tag) {
        case 0x005CU: {
            omc_const_bytes raw;
            const char* subtable;

            raw = omc_arena_view(&ctx->store->arena, cand->raw_ref);
            subtable = (raw.size == 4U) ? "srinfo" : "srinfo2";
            if (!omc_exif_make_subifd_name("mk_pentax", subtable,
                                           idx_srinfo++, ifd_name,
                                           sizeof(ifd_name))) {
                continue;
            }
            break;
        }
        case 0x0060U:
            if (!omc_exif_make_subifd_name("mk_pentax", "faceinfo",
                                           idx_faceinfo++, ifd_name,
                                           sizeof(ifd_name))) {
                continue;
            }
            break;
        case 0x0206U:
            if (!omc_exif_make_subifd_name("mk_pentax", "aeinfo2",
                                           idx_aeinfo2++, ifd_name,
                                           sizeof(ifd_name))) {
                continue;
            }
            break;
        case 0x0226U:
            if (!omc_exif_make_subifd_name("mk_pentax", "shotinfo",
                                           idx_shotinfo++, ifd_name,
                                           sizeof(ifd_name))) {
                continue;
            }
            break;
        case 0x0227U:
            if (!have_detected_faces
                || !omc_exif_make_subifd_name("mk_pentax", "facepos",
                                              idx_facepos++, ifd_name,
                                              sizeof(ifd_name))) {
                continue;
            }
            break;
        case 0x0228U:
            if (!have_detected_faces
                || !omc_exif_make_subifd_name("mk_pentax", "facesize",
                                              idx_facesize++, ifd_name,
                                              sizeof(ifd_name))) {
                continue;
            }
            break;
        default:
            continue;
        }

        if (!omc_exif_decode_pentax_u8_table_ref(ctx, ifd_name,
                                                 cand->raw_ref)) {
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_panasonic_read_u16_endian(const omc_u8* raw, omc_u64 raw_size,
                                   omc_u64 off, int little_endian,
                                   omc_u16* out_value)
{
    return omc_exif_read_u16(omc_exif_make_classic_cfg(little_endian), raw,
                             (omc_size)raw_size, off, out_value);
}

static int
omc_exif_panasonic_read_u32_endian(const omc_u8* raw, omc_u64 raw_size,
                                   omc_u64 off, int little_endian,
                                   omc_u32* out_value)
{
    return omc_exif_read_u32(omc_exif_make_classic_cfg(little_endian), raw,
                             (omc_size)raw_size, off, out_value);
}

static omc_exif_status
omc_exif_panasonic_emit_trimmed_ascii(omc_exif_ctx* ctx, const char* ifd_name,
                                      omc_u16 tag, omc_u32 order_in_block,
                                      const omc_u8* raw, omc_u32 raw_size)
{
    omc_u32 start;
    omc_u32 trimmed;

    trimmed = omc_exif_trim_ascii_span(raw, raw_size, &start);
    if (trimmed == 0U) {
        return OMC_EXIF_OK;
    }
    return omc_exif_emit_derived_exif_text(ctx, ifd_name, tag, order_in_block,
                                           raw + start, trimmed);
}

static int
omc_exif_panasonic_datetime_text(const omc_u8* raw, omc_u64 raw_size,
                                 char* out_buf, omc_u32* out_size)
{
    char digits[16];
    omc_u32 i;
    omc_u32 n;

    if (out_size == (omc_u32*)0) {
        return 0;
    }
    *out_size = 0U;
    if (raw == (const omc_u8*)0 || out_buf == (char*)0 || raw_size < 8U
        || raw[0U] == 0U) {
        return 0;
    }

    n = 0U;
    for (i = 0U; i < 8U; ++i) {
        omc_u8 hi;
        omc_u8 lo;

        hi = (omc_u8)((raw[i] >> 4U) & 0x0FU);
        lo = (omc_u8)((raw[i] >> 0U) & 0x0FU);
        if (hi > 9U || lo > 9U) {
            return 0;
        }
        digits[n++] = (char)('0' + hi);
        digits[n++] = (char)('0' + lo);
    }

    out_buf[0] = digits[0];
    out_buf[1] = digits[1];
    out_buf[2] = digits[2];
    out_buf[3] = digits[3];
    out_buf[4] = ':';
    out_buf[5] = digits[4];
    out_buf[6] = digits[5];
    out_buf[7] = ':';
    out_buf[8] = digits[6];
    out_buf[9] = digits[7];
    out_buf[10] = ' ';
    out_buf[11] = digits[8];
    out_buf[12] = digits[9];
    out_buf[13] = ':';
    out_buf[14] = digits[10];
    out_buf[15] = digits[11];
    out_buf[16] = ':';
    out_buf[17] = digits[12];
    out_buf[18] = digits[13];
    out_buf[19] = '.';
    out_buf[20] = digits[14];
    out_buf[21] = digits[15];
    *out_size = 22U;
    return 1;
}

static int
omc_exif_decode_panasonic_facedetinfo_ref(omc_exif_ctx* ctx,
                                          omc_byte_ref raw_ref,
                                          omc_u32 index,
                                          int little_endian)
{
    static const omc_u16 k_tags[5] = { 0x0001U, 0x0005U, 0x0009U, 0x000DU,
                                       0x0011U };
    char ifd_name[64];
    omc_exif_status status;
    omc_u16 faces;
    omc_u32 face_n;
    omc_u32 i;
    omc_u32 order;
    omc_const_bytes raw;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }
    raw = omc_arena_view(&ctx->store->arena, raw_ref);
    if (raw.size < 2U
        || !omc_exif_make_subifd_name("mk_panasonic", "facedetinfo", index,
                                      ifd_name, sizeof(ifd_name))
        || !omc_exif_panasonic_read_u16_endian(raw.data, raw.size, 0U,
                                               little_endian, &faces)) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0000U, 0U, faces);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    face_n = (faces < 5U) ? (omc_u32)faces : 5U;
    order = 1U;
    for (i = 0U; i < face_n; ++i) {
        omc_u16 pos[4];
        omc_u32 j;
        omc_u64 off;

        raw = omc_arena_view(&ctx->store->arena, raw_ref);
        off = (omc_u64)k_tags[i] * 2U;
        if (off + 8U > raw.size) {
            continue;
        }
        for (j = 0U; j < 4U; ++j) {
            if (!omc_exif_panasonic_read_u16_endian(
                    raw.data, raw.size, off + ((omc_u64)j * 2U),
                    little_endian, &pos[j])) {
                break;
            }
        }
        if (j != 4U) {
            continue;
        }
        status = omc_exif_emit_derived_exif_u16_array_le(
            ctx, ifd_name, k_tags[i], order++, pos, 4U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_panasonic_facerecinfo_ref(omc_exif_ctx* ctx,
                                          omc_byte_ref raw_ref,
                                          omc_u32 index,
                                          int little_endian)
{
    char ifd_name[64];
    omc_exif_status status;
    omc_u16 faces;
    omc_u32 face_n;
    omc_u32 i;
    omc_u32 order;
    omc_const_bytes raw;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }
    raw = omc_arena_view(&ctx->store->arena, raw_ref);
    if (raw.size < 2U
        || !omc_exif_make_subifd_name("mk_panasonic", "facerecinfo", index,
                                      ifd_name, sizeof(ifd_name))
        || !omc_exif_panasonic_read_u16_endian(raw.data, raw.size, 0U,
                                               little_endian, &faces)) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0000U, 0U, faces);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    face_n = (faces < 3U) ? (omc_u32)faces : 3U;
    order = 1U;
    for (i = 0U; i < face_n; ++i) {
        omc_u64 name_off;
        omc_u64 pos_off;
        omc_u64 age_off;
        omc_u16 pos[4];
        omc_u8 text_raw[20];
        omc_u32 j;

        name_off = 4U + ((omc_u64)i * 48U);
        pos_off = 24U + ((omc_u64)i * 48U);
        age_off = 32U + ((omc_u64)i * 48U);

        raw = omc_arena_view(&ctx->store->arena, raw_ref);
        if (name_off + 20U <= raw.size) {
            memcpy(text_raw, raw.data + (omc_size)name_off, sizeof(text_raw));
            status = omc_exif_panasonic_emit_trimmed_ascii(
                ctx, ifd_name, (omc_u16)name_off, order++,
                text_raw, (omc_u32)sizeof(text_raw));
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }

        raw = omc_arena_view(&ctx->store->arena, raw_ref);
        if (pos_off + 8U <= raw.size) {
            for (j = 0U; j < 4U; ++j) {
                if (!omc_exif_panasonic_read_u16_endian(
                        raw.data, raw.size, pos_off + ((omc_u64)j * 2U),
                        little_endian, &pos[j])) {
                    break;
                }
            }
            if (j == 4U) {
                status = omc_exif_emit_derived_exif_u16_array_le(
                    ctx, ifd_name, (omc_u16)pos_off, order++, pos, 4U);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
        }

        raw = omc_arena_view(&ctx->store->arena, raw_ref);
        if (age_off + 20U <= raw.size) {
            memcpy(text_raw, raw.data + (omc_size)age_off, sizeof(text_raw));
            status = omc_exif_panasonic_emit_trimmed_ascii(
                ctx, ifd_name, (omc_u16)age_off, order++,
                text_raw, (omc_u32)sizeof(text_raw));
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
    }

    return 1;
}

static int
omc_exif_decode_panasonic_timeinfo_ref(omc_exif_ctx* ctx, omc_byte_ref raw_ref,
                                       omc_u32 index, int little_endian)
{
    char ifd_name[64];
    omc_exif_status status;
    char dt_text[32];
    omc_u32 dt_size;
    omc_const_bytes raw;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }
    raw = omc_arena_view(&ctx->store->arena, raw_ref);
    if (raw.size == 0U
        || !omc_exif_make_subifd_name("mk_panasonic", "timeinfo", index,
                                      ifd_name, sizeof(ifd_name))) {
        return 1;
    }

    if (omc_exif_panasonic_datetime_text(raw.data, raw.size, dt_text,
                                         &dt_size)) {
        status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U, 0U,
                                                 (const omc_u8*)dt_text,
                                                 dt_size);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    raw = omc_arena_view(&ctx->store->arena, raw_ref);
    if (raw.size >= 20U) {
        omc_u32 shot;

        if (omc_exif_panasonic_read_u32_endian(raw.data, raw.size, 16U,
                                               little_endian, &shot)) {
            status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0010U,
                                                    1U, shot);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
    }

    return 1;
}

static int
omc_exif_decode_panasonic_type2(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size, int little_endian)
{
    char ifd_name[64];
    omc_exif_status status;
    omc_u16 gain;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 4U
        || !omc_exif_make_subifd_name("mk_panasonic", "type2", 0U,
                                      ifd_name, sizeof(ifd_name))) {
        return 0;
    }

    for (i = 0U; i < 4U; ++i) {
        if (raw[i] < 0x20U || raw[i] > 0x7EU) {
            return 0;
        }
    }

    status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U, 0U, raw,
                                             4U);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    if (raw_size >= 8U
        && omc_exif_panasonic_read_u16_endian(raw, raw_size, 6U,
                                              little_endian, &gain)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0003U, 1U,
                                                gain);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_panasonic_candidate(const omc_u8* bytes, omc_u64 size,
                             omc_u64 ifd_off, omc_u64 maker_note_end,
                             omc_exif_cfg cfg, omc_u32 max_entries_per_ifd,
                             omc_u16* out_entry_count)
{
    omc_u16 entry_count16;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;

    if (bytes == (const omc_u8*)0 || out_entry_count == (omc_u16*)0
        || ifd_off >= size || maker_note_end > size
        || !omc_exif_read_u16(cfg, bytes, (omc_size)size, ifd_off,
                              &entry_count16)
        || entry_count16 == 0U
        || entry_count16 > max_entries_per_ifd) {
        return 0;
    }

    entry_table_off = ifd_off + 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)) {
        return 0;
    }
    if (entry_table_off > maker_note_end
        || table_bytes > (maker_note_end - entry_table_off)) {
        return 0;
    }

    *out_entry_count = entry_count16;
    return 1;
}

static int
omc_exif_decode_panasonic_binary_subdirs(omc_exif_ctx* ctx,
                                         omc_size entry_start,
                                         int little_endian)
{
    omc_u32 idx_facedet;
    omc_u32 idx_facerec;
    omc_u32 idx_time;
    omc_size entry_count;
    omc_size i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || entry_start >= ctx->store->entry_count) {
        return 1;
    }

    idx_facedet = 0U;
    idx_facerec = 0U;
    idx_time = 0U;
    entry_count = ctx->store->entry_count;

    for (i = entry_start; i < entry_count; ++i) {
        const omc_entry* entry;

        entry = &ctx->store->entries[i];
        if (!omc_exif_entry_ifd_equals(ctx->store, entry, "mk_panasonic0")
            || (entry->value.kind != OMC_VAL_BYTES
                && entry->value.kind != OMC_VAL_ARRAY)) {
            continue;
        }

        if (entry->key.u.exif_tag.tag == 0x004EU) {
            if (!omc_exif_decode_panasonic_facedetinfo_ref(
                    ctx, entry->value.u.ref, idx_facedet++, little_endian)) {
                return 0;
            }
        } else if (entry->key.u.exif_tag.tag == 0x0061U) {
            if (!omc_exif_decode_panasonic_facerecinfo_ref(
                    ctx, entry->value.u.ref, idx_facerec++, little_endian)) {
                return 0;
            }
        } else if (entry->key.u.exif_tag.tag == 0x2003U) {
            if (!omc_exif_decode_panasonic_timeinfo_ref(
                    ctx, entry->value.u.ref, idx_time++, little_endian)) {
                return 0;
            }
        }
    }

    return 1;
}

static int
omc_exif_decode_panasonic_makernote(omc_exif_ctx* ctx, omc_u64 maker_note_off,
                                    const omc_u8* raw, omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_u64 maker_note_end;
    omc_u64 scan_end;
    omc_u64 abs_off;
    omc_u64 best_off;
    omc_exif_cfg best_cfg;
    int found;
    omc_u32 endian_i;
    omc_size entry_start;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size == 0U) {
        return 1;
    }

    maker_note_end = maker_note_off + raw_size;
    if (maker_note_end > (omc_u64)ctx->size) {
        maker_note_end = (omc_u64)ctx->size;
    }
    scan_end = maker_note_off + ((raw_size < 512U) ? raw_size : 512U);
    if (scan_end > maker_note_end) {
        scan_end = maker_note_end;
    }

    found = 0;
    best_off = 0U;
    memset(&best_cfg, 0, sizeof(best_cfg));
    abs_off = maker_note_off;
    for (endian_i = 0U; endian_i < 2U; ++endian_i) {
        omc_exif_cfg cfg;
        omc_u16 count16;

        cfg = omc_exif_make_classic_cfg(endian_i == 0U);
        if (!omc_exif_panasonic_candidate(ctx->bytes, (omc_u64)ctx->size,
                                          abs_off, maker_note_end, cfg,
                                          ctx->opts.limits.max_entries_per_ifd,
                                          &count16)) {
            continue;
        }
        found = 1;
        best_off = abs_off;
        best_cfg = cfg;
        break;
    }

    for (abs_off = maker_note_off + 2U;
         !found && abs_off + 2U <= scan_end; abs_off += 2U) {
        for (endian_i = 0U; endian_i < 2U; ++endian_i) {
            omc_exif_cfg cfg;
            omc_u16 count16;

            cfg = omc_exif_make_classic_cfg(endian_i == 0U);
            if (!omc_exif_panasonic_candidate(ctx->bytes, (omc_u64)ctx->size,
                                              abs_off, maker_note_end, cfg,
                                              ctx->opts.limits
                                                  .max_entries_per_ifd,
                                              &count16)) {
                continue;
            }
            found = 1;
            best_off = abs_off;
            best_cfg = cfg;
            break;
        }
    }

    if (!found) {
        return omc_exif_decode_panasonic_type2(ctx, raw, raw_size,
                                               ctx->cfg.little_endian);
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_panasonic_tokens(&mn_opts);

    entry_start = (ctx->store != (omc_store*)0) ? ctx->store->entry_count : 0U;
    if (!omc_exif_decode_ifd_blob_loose_cfg_tail(ctx, ctx->bytes,
                                                 (omc_u64)ctx->size, best_off,
                                                 &mn_opts, best_cfg, 0U)) {
        return 0;
    }
    return omc_exif_decode_panasonic_binary_subdirs(ctx, entry_start,
                                                    best_cfg.little_endian);
}

static const char*
omc_exif_olympus_main_subtable_name(omc_u16 tag)
{
    switch (tag) {
    case 0x2010U: return "equipment";
    case 0x2020U: return "camerasettings";
    case 0x2030U: return "rawdevelopment";
    case 0x2031U: return "rawdevelopment2";
    case 0x2040U: return "imageprocessing";
    case 0x2050U: return "focusinfo";
    case 0x2100U:
    case 0x2200U:
    case 0x2300U:
    case 0x2400U:
    case 0x2500U:
    case 0x2600U:
    case 0x2700U:
    case 0x2800U:
    case 0x2900U:
        return "fetags";
    case 0x3000U: return "rawinfo";
    case 0x4000U: return "main";
    case 0x5000U: return "unknowninfo";
    default: break;
    }
    return (const char*)0;
}

static const char*
omc_exif_olympus_camerasettings_subtable_name(omc_u16 tag)
{
    switch (tag) {
    case 0x030AU: return "aftargetinfo";
    case 0x030BU: return "subjectdetectinfo";
    default: break;
    }
    return (const char*)0;
}

static int
omc_exif_decode_olympus_named_ifd(omc_exif_ctx* ctx, const omc_u8* bytes,
                                  omc_u64 size, omc_u64 ifd_off,
                                  omc_exif_cfg cfg,
                                  const char* ifd_name)
{
    omc_exif_opts mn_opts;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0) {
        return 0;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;

    return omc_exif_decode_ifd_blob_loose_named_cfg(ctx, bytes, size, ifd_off,
                                                    &mn_opts, cfg, ifd_name);
}

static int
omc_exif_decode_olympus_camerasettings_nested(omc_exif_ctx* ctx,
                                              const omc_u8* bytes,
                                              omc_u64 size, omc_u64 ifd_off,
                                              omc_exif_cfg cfg)
{
    omc_u16 entry_count16;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || bytes == (const omc_u8*)0
        || ifd_off >= size
        || !omc_exif_read_u16(cfg, bytes, (omc_size)size, ifd_off,
                              &entry_count16)
        || entry_count16 == 0U
        || entry_count16 > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }

    entry_table_off = ifd_off + 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)
        || entry_table_off > size || table_bytes > (size - entry_table_off)) {
        return 1;
    }

    for (i = 0U; i < (omc_u32)entry_count16; ++i) {
        omc_u64 eoff;
        omc_u16 tag;
        omc_u16 type;
        omc_u32 count32;
        omc_u32 sub_ifd_off32;
        const char* subtable;
        char ifd_name[96];

        eoff = entry_table_off + ((omc_u64)i * 12U);
        if (!omc_exif_read_u16(cfg, bytes, (omc_size)size, eoff + 0U, &tag)
            || !omc_exif_read_u16(cfg, bytes, (omc_size)size, eoff + 2U,
                                  &type)
            || !omc_exif_read_u32(cfg, bytes, (omc_size)size, eoff + 4U,
                                  &count32)
            || !omc_exif_read_u32(cfg, bytes, (omc_size)size, eoff + 8U,
                                  &sub_ifd_off32)) {
            continue;
        }

        subtable = omc_exif_olympus_camerasettings_subtable_name(tag);
        if (subtable == (const char*)0 || count32 != 1U
            || (type != 4U && type != 13U)
            || sub_ifd_off32 >= (omc_u32)size
            || !omc_exif_make_subifd_name("mk_olympus", subtable, 0U,
                                          ifd_name, sizeof(ifd_name))) {
            continue;
        }

        if (!omc_exif_decode_olympus_named_ifd(ctx, bytes, size,
                                               (omc_u64)sub_ifd_off32, cfg,
                                               ifd_name)) {
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_olympus_main_subifds(omc_exif_ctx* ctx, const omc_u8* bytes,
                                     omc_u64 size, omc_u64 ifd_off,
                                     omc_exif_cfg cfg)
{
    omc_u16 entry_count16;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u32 idx_fetags;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || bytes == (const omc_u8*)0
        || ifd_off >= size
        || !omc_exif_read_u16(cfg, bytes, (omc_size)size, ifd_off,
                              &entry_count16)
        || entry_count16 == 0U
        || entry_count16 > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }

    entry_table_off = ifd_off + 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)
        || entry_table_off > size || table_bytes > (size - entry_table_off)) {
        return 1;
    }

    idx_fetags = 0U;
    for (i = 0U; i < (omc_u32)entry_count16; ++i) {
        omc_u64 eoff;
        omc_u16 tag;
        omc_u16 type;
        omc_u32 count32;
        omc_u32 value_or_off32;
        omc_u64 sub_ifd_off;
        omc_u32 elem_size32;
        omc_u64 value_bytes;
        const char* subtable;
        omc_u32 sub_idx;
        char ifd_name[96];

        eoff = entry_table_off + ((omc_u64)i * 12U);
        if (!omc_exif_read_u16(cfg, bytes, (omc_size)size, eoff + 0U, &tag)
            || !omc_exif_read_u16(cfg, bytes, (omc_size)size, eoff + 2U,
                                  &type)
            || !omc_exif_read_u32(cfg, bytes, (omc_size)size, eoff + 4U,
                                  &count32)
            || !omc_exif_read_u32(cfg, bytes, (omc_size)size, eoff + 8U,
                                  &value_or_off32)) {
            continue;
        }

        subtable = omc_exif_olympus_main_subtable_name(tag);
        if (subtable == (const char*)0) {
            continue;
        }

        sub_ifd_off = ~(omc_u64)0;
        if ((type == 4U || type == 13U) && count32 == 1U) {
            sub_ifd_off = (omc_u64)value_or_off32;
        } else if (omc_exif_elem_size(type, &elem_size32)
                   && omc_exif_mul_u64((omc_u64)elem_size32,
                                       (omc_u64)count32,
                                       &value_bytes)
                   && value_bytes > 4U) {
            sub_ifd_off = (omc_u64)value_or_off32;
        }
        if (sub_ifd_off >= size) {
            continue;
        }

        sub_idx = 0U;
        if (strcmp(subtable, "fetags") == 0) {
            sub_idx = idx_fetags++;
        }
        if (!omc_exif_make_subifd_name("mk_olympus", subtable, sub_idx,
                                       ifd_name, sizeof(ifd_name))) {
            continue;
        }
        if (!omc_exif_decode_olympus_named_ifd(ctx, bytes, size, sub_ifd_off,
                                               cfg, ifd_name)) {
            return 0;
        }
        if (strcmp(subtable, "camerasettings") == 0
            && !omc_exif_decode_olympus_camerasettings_nested(ctx, bytes,
                                                              size,
                                                              sub_ifd_off, cfg)) {
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_olympus_makernote(omc_exif_ctx* ctx, omc_u64 maker_note_off,
                                  const omc_u8* raw, omc_u64 raw_size)
{
    omc_exif_cfg cfg;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 8U) {
        return 1;
    }

    if (raw_size >= 16U && memcmp(raw, "OM SYSTEM", 9U) == 0) {
        if (raw[12U] == (omc_u8)'I' && raw[13U] == (omc_u8)'I') {
            cfg = omc_exif_make_classic_cfg(1);
        } else if (raw[12U] == (omc_u8)'M' && raw[13U] == (omc_u8)'M') {
            cfg = omc_exif_make_classic_cfg(0);
        } else {
            return 0;
        }
        if (!omc_exif_decode_olympus_named_ifd(ctx, raw, raw_size, 16U, cfg,
                                               "mk_olympus0")) {
            return 0;
        }
        return omc_exif_decode_olympus_main_subifds(ctx, raw, raw_size, 16U,
                                                    cfg);
    }

    if (raw_size >= 16U && memcmp(raw, "OLYMPUS\0", 8U) == 0) {
        if (raw[8U] == (omc_u8)'I' && raw[9U] == (omc_u8)'I') {
            cfg = omc_exif_make_classic_cfg(1);
        } else if (raw[8U] == (omc_u8)'M' && raw[9U] == (omc_u8)'M') {
            cfg = omc_exif_make_classic_cfg(0);
        } else {
            return 0;
        }
        if (!omc_exif_decode_olympus_named_ifd(ctx, raw, raw_size, 12U, cfg,
                                               "mk_olympus0")) {
            return 0;
        }
        return omc_exif_decode_olympus_main_subifds(ctx, raw, raw_size, 12U,
                                                    cfg);
    }

    if (raw_size >= 8U
        && (memcmp(raw, "OLYMP\0", 6U) == 0 || memcmp(raw, "EPSON\0", 6U) == 0
            || memcmp(raw, "MINOL\0", 6U) == 0
            || memcmp(raw, "CAMER\0", 6U) == 0)) {
        cfg = ctx->cfg;
        if (!omc_exif_decode_olympus_named_ifd(ctx, ctx->bytes,
                                               (omc_u64)ctx->size,
                                               maker_note_off + 8U, cfg,
                                               "mk_olympus0")) {
            return 0;
        }
        return omc_exif_decode_olympus_main_subifds(ctx, ctx->bytes,
                                                    (omc_u64)ctx->size,
                                                    maker_note_off + 8U, cfg);
    }

    return 1;
}

static omc_exif_status
omc_exif_emit_other_exif_u32(omc_exif_ctx* ctx, const char* ifd_name,
                             omc_u16 tag, omc_u32 order_in_block,
                             omc_u32 value32)
{
    omc_entry entry;
    omc_status st;
    omc_byte_ref ifd_ref;
    omc_val value;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        return OMC_EXIF_OK;
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, tag);
    omc_val_make_u32(&value, value32);
    entry.value = value;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = 1U;
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;
    entry.flags = OMC_ENTRY_FLAG_NONE;
    omc_exif_maybe_mark_contextual_name(ctx, &entry);
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
omc_exif_emit_other_exif_text(omc_exif_ctx* ctx, const char* ifd_name,
                              omc_u16 tag, omc_u32 order_in_block,
                              const omc_u8* text, omc_u32 text_size)
{
    omc_entry entry;
    omc_status st;
    omc_byte_ref ifd_ref;
    omc_byte_ref text_ref;
    omc_val value;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0
        || text == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        return OMC_EXIF_OK;
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    status = omc_exif_store_ref(ctx, text, text_size, &text_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, tag);
    omc_val_make_text(&value, text_ref, OMC_TEXT_ASCII);
    entry.value = value;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = text_size;
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;
    entry.flags = OMC_ENTRY_FLAG_NONE;
    omc_exif_maybe_mark_contextual_name(ctx, &entry);
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
omc_exif_emit_other_exif_bytes(omc_exif_ctx* ctx, const char* ifd_name,
                               omc_u16 tag, omc_u32 order_in_block,
                               const omc_u8* raw, omc_u32 raw_size)
{
    omc_entry entry;
    omc_status st;
    omc_byte_ref ifd_ref;
    omc_byte_ref raw_ref;
    omc_val value;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0
        || raw == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        return OMC_EXIF_OK;
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    status = omc_exif_store_ref(ctx, raw, raw_size, &raw_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, tag);
    omc_val_make_bytes(&value, raw_ref);
    entry.value = value;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = raw_size;
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;
    entry.flags = OMC_ENTRY_FLAG_NONE;
    omc_exif_maybe_mark_contextual_name(ctx, &entry);
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
omc_exif_emit_other_exif_empty(omc_exif_ctx* ctx, const char* ifd_name,
                               omc_u16 tag, omc_u32 order_in_block,
                               omc_u16 wire_type, omc_u32 wire_count,
                               omc_entry_flags flags)
{
    omc_entry entry;
    omc_status st;
    omc_byte_ref ifd_ref;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        return OMC_EXIF_OK;
    }

    status = omc_exif_store_cstr_len(ctx, ifd_name, strlen(ifd_name), &ifd_ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, tag);
    entry.value.kind = OMC_VAL_EMPTY;
    entry.value.elem_type = OMC_ELEM_U8;
    entry.value.text_encoding = OMC_TEXT_UNKNOWN;
    entry.value.count = 0U;
    entry.value.u.u64 = 0U;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = wire_type;
    entry.origin.wire_count = wire_count;
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;
    entry.flags = flags;
    omc_exif_maybe_mark_contextual_name(ctx, &entry);
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
omc_exif_samsung_is_dec_digit(omc_u8 c)
{
    return c >= (omc_u8)'0' && c <= (omc_u8)'9';
}

static int
omc_exif_decode_samsung_compat_tag0(omc_exif_ctx* ctx, const omc_u8* raw,
                                    omc_u64 raw_size, const char* ifd_name)
{
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || ifd_name == (const char*)0 || raw_size < 4U) {
        return 1;
    }
    if (ctx->res.entries_decoded >= ctx->opts.limits.max_total_entries) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, 0U, 0x0000U);
        return 0;
    }

    if (raw_size >= 14U && omc_exif_samsung_is_dec_digit(raw[10U])
        && omc_exif_samsung_is_dec_digit(raw[11U])
        && omc_exif_samsung_is_dec_digit(raw[12U])
        && omc_exif_samsung_is_dec_digit(raw[13U])) {
        status = omc_exif_emit_other_exif_text(ctx, ifd_name, 0x0000U, 0U,
                                               raw + 10U, 4U);
    } else {
        status = omc_exif_emit_other_exif_bytes(ctx, ifd_name, 0x0000U, 0U,
                                                raw, 4U);
    }
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    ctx->res.entries_decoded += 1U;
    return 1;
}

static int
omc_exif_decode_samsung_ifd(omc_exif_ctx* ctx, const omc_u8* raw,
                            omc_u64 raw_size, omc_u64 ifd_off,
                            const char* ifd_name)
{
    omc_exif_ctx child;
    omc_byte_ref token_ref;
    omc_exif_status tok_status;
    omc_u32 entry_count32;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u64 next_off_pos;
    omc_u64 base;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || ifd_name == (const char*)0 || raw_size > (omc_u64)(~(omc_size)0)) {
        return 1;
    }
    if (ifd_off > raw_size || raw_size - ifd_off < 4U) {
        omc_exif_update_status(&ctx->res, OMC_EXIF_MALFORMED);
        return 0;
    }
    if (!omc_exif_read_u32le_raw(raw, raw_size, ifd_off, &entry_count32)
        || entry_count32 == 0U) {
        return 1;
    }

    omc_exif_init_child_cfg(&child, ctx, raw, (omc_size)raw_size, &ctx->opts,
                            omc_exif_make_classic_cfg(1));
    if (entry_count32 > child.opts.limits.max_entries_per_ifd) {
        omc_exif_mark_limit(&child, OMC_EXIF_LIM_MAX_ENTRIES_IFD, ifd_off,
                            0U);
        omc_exif_merge_makernote_child(ctx, &child);
        return 0;
    }

    entry_table_off = ifd_off + 4U;
    if (!omc_exif_mul_u64((omc_u64)entry_count32, 12U, &table_bytes)) {
        omc_exif_update_status(&ctx->res, OMC_EXIF_LIMIT);
        return 0;
    }
    if (entry_table_off > raw_size || table_bytes > (raw_size - entry_table_off)
        || 4U > ((raw_size - entry_table_off) - table_bytes)) {
        omc_exif_update_status(&ctx->res, OMC_EXIF_MALFORMED);
        return 0;
    }

    next_off_pos = entry_table_off + table_bytes;
    base = next_off_pos + 4U;
    memset(&token_ref, 0, sizeof(token_ref));
    if (!child.measure_only) {
        tok_status = omc_exif_store_cstr_len(&child, ifd_name, strlen(ifd_name),
                                             &token_ref);
        if (tok_status != OMC_EXIF_OK) {
            omc_exif_update_status(&child.res, tok_status);
            omc_exif_merge_makernote_child(ctx, &child);
            return 0;
        }
    }

    for (i = 0U; i < entry_count32; ++i) {
        omc_u64 entry_off;
        omc_u16 tag16;
        omc_u16 type16;
        omc_u32 count32;
        omc_u32 value32;
        omc_u32 elem_size;
        omc_u64 value_size;
        omc_u64 value_off;
        omc_exif_status status;

        entry_off = entry_table_off + ((omc_u64)i * 12U);
        if (!omc_exif_read_u16le_raw(raw, raw_size, entry_off + 0U, &tag16)
            || !omc_exif_read_u16le_raw(raw, raw_size, entry_off + 2U, &type16)
            || !omc_exif_read_u32le_raw(raw, raw_size, entry_off + 4U,
                                        &count32)
            || !omc_exif_elem_size(type16, &elem_size)
            || !omc_exif_mul_u64((omc_u64)elem_size, (omc_u64)count32,
                                 &value_size)) {
            omc_exif_update_status(&child.res, OMC_EXIF_MALFORMED);
            omc_exif_merge_makernote_child(ctx, &child);
            return 0;
        }

        if (value_size <= 4U) {
            value_off = entry_off + 8U;
        } else {
            if (!omc_exif_read_u32le_raw(raw, raw_size, entry_off + 8U,
                                         &value32)) {
                omc_exif_update_status(&child.res, OMC_EXIF_MALFORMED);
                omc_exif_merge_makernote_child(ctx, &child);
                return 0;
            }
            value_off = base + (omc_u64)value32;
        }

        if (value_off > raw_size || value_size > (raw_size - value_off)) {
            omc_exif_update_status(&child.res, OMC_EXIF_MALFORMED);
            omc_exif_merge_makernote_child(ctx, &child);
            return 0;
        }
        if (child.res.entries_decoded >= child.opts.limits.max_total_entries) {
            omc_exif_mark_limit(&child, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, ifd_off,
                                tag16);
            omc_exif_merge_makernote_child(ctx, &child);
            return 0;
        }

        if (!child.measure_only) {
            status = omc_exif_add_entry(&child, &token_ref, tag16, type16,
                                        (omc_u64)count32,
                                        raw + (omc_size)value_off, value_size,
                                        i, OMC_ENTRY_FLAG_NONE);
            if (status != OMC_EXIF_OK) {
                if (status == OMC_EXIF_LIMIT) {
                    omc_exif_mark_limit(&child, OMC_EXIF_LIM_VALUE_COUNT,
                                        ifd_off, tag16);
                } else {
                    omc_exif_update_status(&child.res, status);
                }
                omc_exif_merge_makernote_child(ctx, &child);
                return 0;
            }
        }
        child.res.entries_decoded += 1U;
    }

    omc_exif_merge_makernote_child(ctx, &child);
    return 1;
}

static int
omc_exif_decode_samsung_picturewizard(omc_exif_ctx* ctx,
                                      const char* type2_ifd_name,
                                      int little_endian)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    char ifd_name[64];
    omc_u16 values[5];
    omc_u32 i;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || type2_ifd_name == (const char*)0) {
        return 1;
    }
    if (!omc_exif_make_subifd_name("mk_samsung", "picturewizard", 0U,
                                   ifd_name, sizeof(ifd_name))) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, type2_ifd_name, 0x0021U);
    if (!omc_exif_entry_raw_view(ctx->store, entry, &raw) || raw.size < 10U) {
        return 1;
    }
    if (entry->value.kind != OMC_VAL_BYTES && entry->value.kind != OMC_VAL_ARRAY) {
        return 1;
    }
    for (i = 0U; i < 5U; ++i) {
        if (little_endian) {
            if (!omc_exif_read_u16le_raw(raw.data, raw.size, (omc_u64)i * 2U,
                                         &values[i])) {
                return 1;
            }
        } else if (!omc_exif_read_u16be_raw(raw.data, raw.size,
                                            (omc_u64)i * 2U, &values[i])) {
            return 1;
        }
    }

    for (i = 0U; i < 5U; ++i) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, (omc_u16)i, i,
                                                values[i]);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    return 1;
}

static int
omc_exif_samsung_type2_ifd_candidate(const omc_u8* raw, omc_u64 raw_size,
                                     omc_exif_cfg cfg,
                                     omc_u32 max_entries_per_ifd)
{
    omc_u16 entry_count16;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u32 i;

    if (raw == (const omc_u8*)0) {
        return 0;
    }
    if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, 0U, &entry_count16)
        || entry_count16 == 0U
        || entry_count16 > (omc_u16)max_entries_per_ifd) {
        return 0;
    }

    entry_table_off = 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)
        || entry_table_off > raw_size || table_bytes > (raw_size - entry_table_off)
        || 4U > ((raw_size - entry_table_off) - table_bytes)) {
        return 0;
    }

    for (i = 0U; i < (omc_u32)entry_count16; ++i) {
        omc_u64 entry_off;
        omc_u16 type16;
        omc_u32 count32;
        omc_u32 elem_size;
        omc_u64 value_size;

        entry_off = entry_table_off + ((omc_u64)i * 12U);
        if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, entry_off + 2U,
                               &type16)
            || !omc_exif_read_u32(cfg, raw, (omc_size)raw_size, entry_off + 4U,
                                  &count32)
            || !omc_exif_elem_size(type16, &elem_size)
            || !omc_exif_mul_u64((omc_u64)elem_size, (omc_u64)count32,
                                 &value_size)) {
            return 0;
        }
    }
    return 1;
}

static int
omc_exif_decode_samsung_malformed_type2_tag21(omc_exif_ctx* ctx,
                                              const omc_u8* raw,
                                              omc_u64 raw_size,
                                              const char* ifd_name,
                                              omc_exif_cfg cfg)
{
    omc_u16 entry_count16;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || ifd_name == (const char*)0) {
        return 1;
    }
    if (omc_exif_find_first_entry(ctx->store, ifd_name, 0x0021U)
        != (const omc_entry*)0) {
        return 1;
    }
    if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, 0U, &entry_count16)
        || entry_count16 == 0U) {
        return 1;
    }

    entry_table_off = 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)
        || entry_table_off > raw_size
        || table_bytes > (raw_size - entry_table_off)
        || 4U > ((raw_size - entry_table_off) - table_bytes)) {
        return 1;
    }

    for (i = 0U; i < (omc_u32)entry_count16; ++i) {
        omc_u64 entry_off;
        omc_u16 tag16;
        omc_u16 type16;
        omc_u32 count32;
        omc_u32 off32;
        omc_u32 elem_size;
        omc_u64 value_size;
        omc_exif_status status;

        entry_off = entry_table_off + ((omc_u64)i * 12U);
        if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, entry_off + 0U,
                               &tag16)
            || !omc_exif_read_u16(cfg, raw, (omc_size)raw_size, entry_off + 2U,
                                  &type16)
            || !omc_exif_read_u32(cfg, raw, (omc_size)raw_size, entry_off + 4U,
                                  &count32)
            || !omc_exif_read_u32(cfg, raw, (omc_size)raw_size, entry_off + 8U,
                                  &off32)
            || !omc_exif_elem_size(type16, &elem_size)
            || !omc_exif_mul_u64((omc_u64)elem_size, (omc_u64)count32,
                                 &value_size)) {
            return 1;
        }
        if (tag16 != 0x0021U) {
            continue;
        }
        if (value_size <= 4U) {
            return 1;
        }
        if ((omc_u64)off32 <= raw_size && value_size <= (raw_size - (omc_u64)off32)) {
            return 1;
        }
        status = omc_exif_emit_other_exif_empty(ctx, ifd_name, 0x0021U, i,
                                                type16, count32,
                                                OMC_ENTRY_FLAG_UNREADABLE);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        ctx->res.entries_decoded += 1U;
        return 1;
    }
    return 1;
}

static int
omc_exif_decode_samsung_stmn(omc_exif_ctx* ctx, const omc_u8* raw,
                             omc_u64 raw_size)
{
    static const char k_mk_ifd0[] = "mk_samsung0";
    omc_u32 value32;
    omc_u32 text_size;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 16U) {
        return 1;
    }
    if (memcmp(raw, "STMN", 4U) != 0) {
        return 1;
    }

    if (ctx->res.entries_decoded >= ctx->opts.limits.max_total_entries) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, 0U, 0x0000U);
        return 0;
    }
    text_size = (raw[7U] == 0U) ? 7U : 8U;
    status = omc_exif_emit_other_exif_text(ctx, k_mk_ifd0, 0x0000U, 0U, raw,
                                           text_size);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    ctx->res.entries_decoded += 1U;

    if (ctx->res.entries_decoded >= ctx->opts.limits.max_total_entries) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, 0U, 0x0002U);
        return 0;
    }
    if (!omc_exif_read_u32le_raw(raw, raw_size, 8U, &value32)) {
        omc_exif_update_status(&ctx->res, OMC_EXIF_MALFORMED);
        return 0;
    }
    status = omc_exif_emit_other_exif_u32(ctx, k_mk_ifd0, 0x0002U, 1U,
                                          value32);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    ctx->res.entries_decoded += 1U;

    if (ctx->res.entries_decoded >= ctx->opts.limits.max_total_entries) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, 0U, 0x0003U);
        return 0;
    }
    if (!omc_exif_read_u32le_raw(raw, raw_size, 12U, &value32)) {
        omc_exif_update_status(&ctx->res, OMC_EXIF_MALFORMED);
        return 0;
    }
    status = omc_exif_emit_other_exif_u32(ctx, k_mk_ifd0, 0x0003U, 2U,
                                          value32);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    ctx->res.entries_decoded += 1U;

    if (raw_size >= 48U && raw[44U] != 0U && raw[45U] == 0U && raw[46U] == 0U
        && raw[47U] == 0U
        && !omc_exif_decode_samsung_ifd(ctx, raw, raw_size, 44U,
                                        "mk_samsung_ifd_0")) {
        return 0;
    }

    return 1;
}

static int
omc_exif_decode_samsung_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                  omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;
    const char* type2_ifd_name;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size == 0U) {
        return 1;
    }

    if (!omc_exif_decode_samsung_stmn(ctx, raw, raw_size)) {
        return 0;
    }
    if (raw_size >= 4U && memcmp(raw, "STMN", 4U) == 0) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_samsung_tokens(&mn_opts);
    mn_opts.tokens.ifd_prefix = "mk_samsung_type2_";
    mn_opts.tokens.subifd_prefix = "mk_samsung_type2_subifd_";
    mn_opts.tokens.exif_ifd_token = "mk_samsung_type2_exififd";
    mn_opts.tokens.gps_ifd_token = "mk_samsung_type2_gpsifd";
    mn_opts.tokens.interop_ifd_token = "mk_samsung_type2_interopifd";
    type2_ifd_name = "mk_samsung_type2_0";

    cfg = omc_exif_make_classic_cfg(ctx->cfg.little_endian);
    if (!omc_exif_samsung_type2_ifd_candidate(
            raw, raw_size, cfg, ctx->opts.limits.max_entries_per_ifd)) {
        cfg = omc_exif_make_classic_cfg(!ctx->cfg.little_endian);
    }
    if (!omc_exif_samsung_type2_ifd_candidate(
            raw, raw_size, cfg, ctx->opts.limits.max_entries_per_ifd)) {
        return omc_exif_decode_samsung_compat_tag0(ctx, raw, raw_size,
                                                   "mk_samsung0");
    }

    if (!omc_exif_decode_ifd_blob_loose_cfg(ctx, raw, raw_size, 0U, &mn_opts,
                                            cfg)) {
        return 0;
    }
    if (!omc_exif_decode_samsung_malformed_type2_tag21(ctx, raw, raw_size,
                                                       type2_ifd_name, cfg)) {
        return 0;
    }
    if (!omc_exif_decode_samsung_picturewizard(ctx, type2_ifd_name,
                                               cfg.little_endian)) {
        return 0;
    }
    if (omc_exif_find_first_entry(ctx->store, type2_ifd_name, 0x0000U)
        == (const omc_entry*)0
        && !omc_exif_decode_samsung_compat_tag0(ctx, raw, raw_size,
                                                type2_ifd_name)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_minolta_binary_subdirs(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    omc_u8 stable16[16];
    omc_u8 stable8[8];
    omc_u32 value32;
    omc_u16 value16;
    omc_exif_status status;
    char ifd_name[64];

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_minolta0", 0x0001U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw) && raw.size >= 16U) {
        omc_u32 i;

        memcpy(stable16, raw.data, sizeof(stable16));
        if (omc_exif_make_subifd_name("mk_minolta", "camerasettings", 0U,
                                      ifd_name, sizeof(ifd_name))) {
            for (i = 0U; i < 4U; ++i) {
                if (!omc_exif_read_u32le_raw(stable16, sizeof(stable16),
                                             (omc_u64)i * 4U, &value32)) {
                    break;
                }
                status = omc_exif_emit_derived_exif_u32(ctx, ifd_name,
                                                        (omc_u16)i, i,
                                                        value32);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_minolta0", 0x0004U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw) && raw.size >= 8U) {
        omc_u32 i;

        memcpy(stable8, raw.data, sizeof(stable8));
        if (omc_exif_make_subifd_name("mk_minolta", "camerasettings7d", 0U,
                                      ifd_name, sizeof(ifd_name))) {
            for (i = 0U; i < 4U; ++i) {
                if (!omc_exif_read_u16le_raw(stable8, sizeof(stable8),
                                             (omc_u64)i * 2U, &value16)) {
                    break;
                }
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name,
                                                        (omc_u16)i, i,
                                                        value16);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_minolta0", 0x0114U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw) && raw.size >= 4U) {
        omc_u32 copy_size;
        omc_u32 i;

        memset(stable8, 0, sizeof(stable8));
        copy_size = (omc_u32)raw.size;
        if (copy_size > (omc_u32)sizeof(stable8)) {
            copy_size = (omc_u32)sizeof(stable8);
        }
        memcpy(stable8, raw.data, copy_size);
        if (omc_exif_make_subifd_name("mk_minolta", "camerasettings5d", 0U,
                                      ifd_name, sizeof(ifd_name))) {
            for (i = 0U; i < 4U; ++i) {
                if (!omc_exif_read_u16be_raw(stable8, sizeof(stable8),
                                             (omc_u64)i * 2U, &value16)) {
                    break;
                }
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name,
                                                        (omc_u16)i, i,
                                                        value16);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
        }
    }

    return 1;
}

static int
omc_exif_decode_minolta_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                  omc_u64 raw_size)
{
    omc_exif_opts mn_opts;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 6U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_minolta_tokens(&mn_opts);

    if (!omc_exif_decode_ifd_blob_loose(ctx, raw, raw_size, 0U, &mn_opts)) {
        return 0;
    }
    return omc_exif_decode_minolta_binary_subdirs(ctx);
}

static int
omc_exif_decode_sigma_binary_subdirs(omc_exif_ctx* ctx)
{
    static const struct {
        omc_u16 source_tag;
        const char* subtable;
    } k_specs[] = {
        { 0x0120U, "wbsettings" },
        { 0x0121U, "wbsettings2" }
    };
    static const omc_u16 k_sigma_wb_tags[] = {
        0x0000U, 0x0003U, 0x0006U, 0x0009U, 0x000CU,
        0x000FU, 0x0012U, 0x0015U, 0x0018U, 0x001BU
    };
    omc_size spec_i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }
    if (omc_exif_find_first_entry(ctx->store, "mk_sigma_wbsettings_0", 0x0000U)
        != (const omc_entry*)0
        || omc_exif_find_first_entry(ctx->store, "mk_sigma_wbsettings2_0",
                                     0x0000U) != (const omc_entry*)0) {
        return 1;
    }

    for (spec_i = 0U; spec_i < (sizeof(k_specs) / sizeof(k_specs[0]));
         ++spec_i) {
        const omc_entry* source;
        omc_const_bytes raw_view;
        char ifd_name[64];
        omc_u32 i;

        source = omc_exif_find_first_entry(ctx->store, "mk_sigma0",
                                           k_specs[spec_i].source_tag);
        if (source == (const omc_entry*)0
            || source->value.kind != OMC_VAL_ARRAY
            || source->value.elem_type != OMC_ELEM_F32_BITS
            || source->value.count < 30U
            || !omc_exif_entry_raw_view(ctx->store, source, &raw_view)
            || raw_view.size < 120U) {
            continue;
        }
        if (!omc_exif_make_subifd_name("mk_sigma", k_specs[spec_i].subtable,
                                       0U, ifd_name, sizeof(ifd_name))) {
            continue;
        }

        for (i = 0U; i < 10U; ++i) {
            omc_exif_status status;
            omc_size off;

            off = (omc_size)i * 12U;
            status = omc_exif_emit_derived_exif_array_copy(
                ctx, ifd_name, k_sigma_wb_tags[i], i, OMC_ELEM_F32_BITS,
                raw_view.data + off, 12U, 3U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
    }

    return 1;
}

static int omc_exif_find_classic_candidate(const omc_exif_ctx *ctx, const omc_u8 *raw,
                                           omc_u64 size, omc_u64 scan_limit,
                                           omc_u64 *out_offset, omc_exif_cfg *out_cfg);

static int
omc_exif_decode_sigma_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_u64 ifd_off;
    omc_exif_cfg cfg;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 6U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_sigma_tokens(&mn_opts);

    if (!omc_exif_find_classic_candidate(ctx, raw, raw_size, 256U, &ifd_off, &cfg)) {
        return 1;
    }
    if (!omc_exif_decode_ifd_blob_loose_cfg(ctx, raw, raw_size, ifd_off, &mn_opts,
                                            cfg)) {
        return 0;
    }
    return omc_exif_decode_sigma_binary_subdirs(ctx);
}

static int
omc_exif_decode_motorola_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                   omc_u64 raw_size)
{
    omc_exif_opts mn_opts;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 6U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_motorola_tokens(&mn_opts);

    return omc_exif_decode_ifd_blob_loose(ctx, raw, raw_size, 0U, &mn_opts);
}

static omc_exif_mn_vendor
omc_exif_detect_makernote_vendor(omc_exif_ctx* ctx, const omc_u8* raw,
                                 omc_u64 raw_size)
{
    const omc_u8* make_text;
    omc_u32 make_size;

    if (raw != NULL && raw_size >= 12U &&
        ((memcmp(raw, "IIII", 4U) == 0 && memcmp(raw + 5U, "waR", 3U) == 0) ||
         (memcmp(raw, "MMMMRaw", 7U) == 0)))
        return OMC_EXIF_MN_PHASEONE;
    if (raw != (const omc_u8*)0 && raw_size >= 6U
        && memcmp(raw, "Nikon\0", 6U) == 0) {
        return OMC_EXIF_MN_NIKON;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 4U
        && (memcmp(raw, "SONY", 4U) == 0 || memcmp(raw, "VHAB", 4U) == 0)) {
        return OMC_EXIF_MN_SONY;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 5U
        && memcmp(raw, "SIGMA", 5U) == 0) {
        return OMC_EXIF_MN_SIGMA;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 8U
        && memcmp(raw, "RICOH\0", 6U) == 0
        && ((raw[6U] == (omc_u8)'I' && raw[7U] == (omc_u8)'I')
            || (raw[6U] == (omc_u8)'M' && raw[7U] == (omc_u8)'M'))) {
        return OMC_EXIF_MN_PENTAX;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 5U
        && (memcmp(raw, "Ricoh", 5U) == 0
            || memcmp(raw, "RICOH", 5U) == 0)) {
        return OMC_EXIF_MN_RICOH;
    }
    if (raw != (const omc_u8*)0
        && ((raw_size >= 9U && memcmp(raw, "OM SYSTEM", 9U) == 0)
            || (raw_size >= 8U && memcmp(raw, "OLYMPUS\0", 8U) == 0)
            || (raw_size >= 6U && memcmp(raw, "OLYMP\0", 6U) == 0)
            || (raw_size >= 6U && memcmp(raw, "EPSON\0", 6U) == 0)
            || (raw_size >= 6U && memcmp(raw, "MINOL\0", 6U) == 0)
            || (raw_size >= 6U && memcmp(raw, "CAMER\0", 6U) == 0))) {
        return OMC_EXIF_MN_OLYMPUS;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 4U
        && memcmp(raw, "AOC\0", 4U) == 0) {
        return OMC_EXIF_MN_PENTAX;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 7U
        && memcmp(raw, "PENTAX ", 7U) == 0) {
        return OMC_EXIF_MN_PENTAX;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 8U
        && (memcmp(raw, "FUJIFILM", 8U) == 0
            || memcmp(raw, "GENERALE", 8U) == 0)) {
        return OMC_EXIF_MN_FUJI;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 9U
        && memcmp(raw, "Apple iOS", 9U) == 0) {
        return OMC_EXIF_MN_APPLE;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 6U
        && memcmp(raw, "IIII", 4U) == 0 && raw[5U] == 0U
        && (raw[4U] == 0x04U || raw[4U] == 0x05U || raw[4U] == 0x06U)) {
        return OMC_EXIF_MN_HP;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 3U
        && memcmp(raw, "KDK", 3U) == 0) {
        return OMC_EXIF_MN_KODAK;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 4U
        && memcmp(raw, "IIII", 4U) == 0) {
        return OMC_EXIF_MN_KODAK;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 9U
        && memcmp(raw, "RECONYXH2", 9U) == 0) {
        return OMC_EXIF_MN_RECONYX;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 4U
        && (memcmp(raw, "QVC\0", 4U) == 0 || memcmp(raw, "DCI\0", 4U) == 0)) {
        return OMC_EXIF_MN_CASIO;
    }
    if (raw != (const omc_u8*)0 && raw_size >= 4U
        && memcmp(raw, "STMN", 4U) == 0) {
        return OMC_EXIF_MN_SAMSUNG;
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
                                  &make_size)
        && !omc_exif_find_outer_ifd0_ascii(ctx, 0x010FU, &make_text,
                                           &make_size)) {
        return OMC_EXIF_MN_UNKNOWN;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Nikon")) {
        return OMC_EXIF_MN_NIKON;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "PENTAX")) {
        return OMC_EXIF_MN_PENTAX;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Canon")) {
        return OMC_EXIF_MN_CANON;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Sony")) {
        return OMC_EXIF_MN_SONY;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "SIGMA")) {
        return OMC_EXIF_MN_SIGMA;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "FUJIFILM")) {
        return OMC_EXIF_MN_FUJI;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Apple")) {
        return OMC_EXIF_MN_APPLE;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "KODAK")) {
        return OMC_EXIF_MN_KODAK;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "FLIR")) {
        return OMC_EXIF_MN_FLIR;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "RICOH")) {
        return OMC_EXIF_MN_RICOH;
    }
    if (omc_exif_ascii_contains_nocase(make_text, make_size, "MINOLTA")) {
        return OMC_EXIF_MN_MINOLTA;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "OLYMPUS")
        || omc_exif_ascii_starts_with_nocase(make_text, make_size, "OMDS")
        || omc_exif_ascii_starts_with_nocase(make_text, make_size, "OM SYSTEM")
        || omc_exif_ascii_starts_with_nocase(make_text, make_size, "EPSON")) {
        return OMC_EXIF_MN_OLYMPUS;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Panasonic")) {
        return OMC_EXIF_MN_PANASONIC;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "HP")) {
        return OMC_EXIF_MN_HP;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Nintendo")) {
        return OMC_EXIF_MN_NINTENDO;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "RECONYX")) {
        return OMC_EXIF_MN_RECONYX;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "CASIO")) {
        return OMC_EXIF_MN_CASIO;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "SAMSUNG")) {
        return OMC_EXIF_MN_SAMSUNG;
    }
    if (omc_exif_ascii_starts_with_nocase(make_text, make_size, "Motorola")) {
        return OMC_EXIF_MN_MOTOROLA;
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
    omc_byte_ref raw_ref;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x0001U);
    if (entry == (const omc_entry*)0 || entry->value.kind != OMC_VAL_ARRAY
        || entry->value.elem_type != OMC_ELEM_U16) {
        return 1;
    }

    raw_ref = entry->value.u.ref;
    for (i = 0U; i < entry->value.count; ++i) {
        omc_u16 value16;
        omc_exif_status status;
        omc_u64 off;

        raw = omc_arena_view(&ctx->store->arena, raw_ref);
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
omc_exif_decode_nikon_binary_subdirs(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    char ifd_name[64];
    omc_exif_status status;
    omc_u16 u16v;
    omc_u32 u32v;
    omc_s16 i16v;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x002BU);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 5U
            && omc_exif_make_subifd_name("mk_nikon", "distortinfo", 0U,
                                         ifd_name, sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0004U, 1U,
                                                   raw.data[4U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x00A8U);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 4U) {
        enum {
            OMC_EXIF_NIKON_FLASHINFO_0100 = 0,
            OMC_EXIF_NIKON_FLASHINFO_0102 = 1,
            OMC_EXIF_NIKON_FLASHINFO_0103 = 2,
            OMC_EXIF_NIKON_FLASHINFO_0106 = 3,
            OMC_EXIF_NIKON_FLASHINFO_0107 = 4,
            OMC_EXIF_NIKON_FLASHINFO_0300 = 5,
            OMC_EXIF_NIKON_FLASHINFO_UNKNOWN = 6
        } layout;
        const char* suffix;
        const omc_u16* u8_tags;
        const omc_u16* i8_tags;
        omc_u32 u8_tag_count;
        omc_u32 i8_tag_count;
        omc_u32 order;
        omc_u32 i;
        omc_entry_name_ctx_kind name_context_kind;
        omc_u8 flash_control_mode;
        omc_u8 flash_group_a_mode;
        omc_u8 flash_group_b_mode;
        omc_u8 flash_group_c_mode;
        omc_u8 flash_group_a_variant;
        omc_u8 flash_group_b_variant;
        omc_u8 flash_group_c_variant;

        static const omc_u16 k_flashinfo0100_u8_tags[] = { 0x0009U, 0x000CU,
                                                           0x000DU, 0x000EU,
                                                           0x000FU, 0x0010U };
        static const omc_u16 k_flashinfo0100_i8_tags[] = { 0x000AU, 0x0011U,
                                                           0x0012U, 0x0013U,
                                                           0x0014U, 0x0015U,
                                                           0x001BU, 0x001DU,
                                                           0x0027U, 0x0028U,
                                                           0x0029U, 0x002AU };
        static const omc_u16 k_flashinfo0102_u8_tags[] = { 0x000CU, 0x000DU,
                                                           0x000EU, 0x000FU };
        static const omc_u16 k_flashinfo0102_i8_tags[] = { 0x000AU, 0x0012U,
                                                           0x0013U, 0x0014U };
        static const omc_u16 k_flashinfo0103_u8_tags[] = { 0x000CU, 0x000DU,
                                                           0x000EU, 0x000FU,
                                                           0x0010U };
        static const omc_u16 k_flashinfo0103_i8_tags[] = { 0x000AU, 0x0013U,
                                                           0x0014U, 0x0015U,
                                                           0x001BU, 0x001DU,
                                                           0x0027U };
        static const omc_u16 k_flashinfo0106_u8_tags[] = { 0x0009U, 0x000CU,
                                                           0x000DU, 0x000EU,
                                                           0x000FU, 0x0010U };
        static const omc_u16 k_flashinfo0106_i8_tags[] = { 0x0027U, 0x0028U,
                                                           0x0029U, 0x002AU };
        static const omc_u16 k_flashinfo0107_u8_tags[] = { 0x000CU, 0x000DU,
                                                           0x000EU, 0x000FU };
        static const omc_u16 k_flashinfo0107_i8_tags[] = { 0x000AU, 0x0028U,
                                                           0x0029U, 0x002AU };
        static const omc_u16 k_flashinfo0300_u8_tags[] = { 0x000DU, 0x000EU,
                                                           0x000FU, 0x0010U,
                                                           0x0025U, 0x0026U };
        static const omc_u16 k_flashinfo0300_i8_tags[] = { 0x000AU, 0x0021U,
                                                           0x0028U, 0x0029U,
                                                           0x002AU };

        suffix = "flashinfo0100";
        layout = OMC_EXIF_NIKON_FLASHINFO_0100;
        if (memcmp(raw.data, "0102", 4U) == 0) {
            suffix = "flashinfo0102";
            layout = OMC_EXIF_NIKON_FLASHINFO_0102;
        } else if (memcmp(raw.data, "0103", 4U) == 0
                   || memcmp(raw.data, "0104", 4U) == 0
                   || memcmp(raw.data, "0105", 4U) == 0) {
            suffix = "flashinfo0103";
            layout = OMC_EXIF_NIKON_FLASHINFO_0103;
        } else if (memcmp(raw.data, "0106", 4U) == 0) {
            suffix = "flashinfo0106";
            layout = OMC_EXIF_NIKON_FLASHINFO_0106;
        } else if (memcmp(raw.data, "0107", 4U) == 0
                   || memcmp(raw.data, "0108", 4U) == 0) {
            suffix = "flashinfo0107";
            layout = OMC_EXIF_NIKON_FLASHINFO_0107;
        } else if (memcmp(raw.data, "0300", 4U) == 0
                   || memcmp(raw.data, "0301", 4U) == 0) {
            suffix = "flashinfo0300";
            layout = OMC_EXIF_NIKON_FLASHINFO_0300;
        } else if (memcmp(raw.data, "0100", 4U) != 0
                   && memcmp(raw.data, "0101", 4U) != 0) {
            suffix = "flashinfounknown";
            layout = OMC_EXIF_NIKON_FLASHINFO_UNKNOWN;
        }

        if (!omc_exif_make_subifd_name("mk_nikon", suffix, 0U, ifd_name,
                                       sizeof(ifd_name))) {
            return 1;
        }

        status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U, 0U,
                                                 raw.data, 4U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }

        order = 1U;
        if (raw.size > 4U) {
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0004U,
                                                   order++, raw.data[4U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
        if (raw.size >= 8U) {
            status = omc_exif_emit_derived_exif_array_copy(
                ctx, ifd_name, 0x0006U, order++, OMC_ELEM_U8, raw.data + 6U,
                2U, 2U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
        if ((layout == OMC_EXIF_NIKON_FLASHINFO_0100
             || layout == OMC_EXIF_NIKON_FLASHINFO_0102
             || layout == OMC_EXIF_NIKON_FLASHINFO_0103
             || layout == OMC_EXIF_NIKON_FLASHINFO_0106
             || layout == OMC_EXIF_NIKON_FLASHINFO_0300)
            && raw.size > 8U) {
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0008U,
                                                   order++, raw.data[8U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }

        flash_control_mode = (raw.size > 9U)
                                 ? (omc_u8)(raw.data[9U] & 0x7FU)
                                 : 0U;
        flash_group_a_mode = 0U;
        flash_group_b_mode = 0U;
        flash_group_c_mode = 0U;
        flash_group_a_variant = 0U;
        flash_group_b_variant = 0U;
        flash_group_c_variant = 0U;

        if (layout == OMC_EXIF_NIKON_FLASHINFO_0102 && raw.size > 0x11U) {
            flash_group_a_mode = (omc_u8)(raw.data[0x10U] & 0x0FU);
            flash_group_b_mode = (omc_u8)((raw.data[0x11U] >> 4U) & 0x0FU);
            flash_group_c_mode = (omc_u8)(raw.data[0x11U] & 0x0FU);
            flash_group_a_variant = (omc_u8)((flash_group_a_mode >= 0x06U)
                                                 ? 0U
                                                 : 2U);
            flash_group_b_variant = (omc_u8)((flash_group_b_mode >= 0x06U)
                                                 ? 0U
                                                 : 3U);
            flash_group_c_variant = (omc_u8)((flash_group_c_mode >= 0x06U)
                                                 ? 0U
                                                 : 4U);
        } else if ((layout == OMC_EXIF_NIKON_FLASHINFO_0103
                    || layout == OMC_EXIF_NIKON_FLASHINFO_0106
                    || layout == OMC_EXIF_NIKON_FLASHINFO_0107
                    || layout == OMC_EXIF_NIKON_FLASHINFO_0300)
                   && raw.size > 0x12U) {
            flash_group_a_mode = (omc_u8)(raw.data[0x11U] & 0x0FU);
            flash_group_b_mode = (omc_u8)((raw.data[0x12U] >> 4U) & 0x0FU);
            flash_group_c_mode = (omc_u8)(raw.data[0x12U] & 0x0FU);
            if (layout == OMC_EXIF_NIKON_FLASHINFO_0103
                || layout == OMC_EXIF_NIKON_FLASHINFO_0106) {
                flash_group_a_variant = (omc_u8)((flash_group_a_mode >= 0x06U)
                                                     ? 0U
                                                     : 2U);
                flash_group_b_variant = (omc_u8)((flash_group_b_mode >= 0x06U)
                                                     ? 0U
                                                     : 3U);
                flash_group_c_variant = (omc_u8)((flash_group_c_mode >= 0x06U)
                                                     ? 0U
                                                     : 4U);
            } else {
                flash_group_a_variant = (omc_u8)((flash_group_a_mode >= 0x06U)
                                                     ? 2U
                                                     : 1U);
                flash_group_b_variant = (omc_u8)((flash_group_b_mode >= 0x06U)
                                                     ? 2U
                                                     : 1U);
                flash_group_c_variant = (omc_u8)((flash_group_c_mode >= 0x06U)
                                                     ? 2U
                                                     : 1U);
            }
        }

        if (layout == OMC_EXIF_NIKON_FLASHINFO_0102 && raw.size > 0x11U) {
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0010U,
                                                   order++, flash_group_a_mode);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            omc_exif_mark_last_contextual_name(
                ctx, OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY, 5U);

            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0011U,
                                                   order++, flash_group_b_mode);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            omc_exif_mark_last_contextual_name(
                ctx, OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY, 6U);

            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0011U,
                                                   order++, flash_group_c_mode);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            omc_exif_mark_last_contextual_name(
                ctx, OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY, 7U);
        } else if (layout == OMC_EXIF_NIKON_FLASHINFO_0103
                   && raw.size > 0x12U) {
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0011U,
                                                   order++, flash_group_a_mode);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            omc_exif_mark_last_contextual_name(
                ctx, OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY, 5U);

            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0012U,
                                                   order++, flash_group_b_mode);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            omc_exif_mark_last_contextual_name(
                ctx, OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY, 6U);

            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0012U,
                                                   order++, flash_group_c_mode);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            omc_exif_mark_last_contextual_name(
                ctx, OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY, 7U);
        }

        u8_tags = (const omc_u16*)0;
        i8_tags = (const omc_u16*)0;
        u8_tag_count = 0U;
        i8_tag_count = 0U;
        name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
        switch (layout) {
        case OMC_EXIF_NIKON_FLASHINFO_0102:
            u8_tags = k_flashinfo0102_u8_tags;
            u8_tag_count = (omc_u32)(sizeof(k_flashinfo0102_u8_tags)
                                     / sizeof(k_flashinfo0102_u8_tags[0]));
            i8_tags = k_flashinfo0102_i8_tags;
            i8_tag_count = (omc_u32)(sizeof(k_flashinfo0102_i8_tags)
                                     / sizeof(k_flashinfo0102_i8_tags[0]));
            name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY;
            break;
        case OMC_EXIF_NIKON_FLASHINFO_0103:
            u8_tags = k_flashinfo0103_u8_tags;
            u8_tag_count = (omc_u32)(sizeof(k_flashinfo0103_u8_tags)
                                     / sizeof(k_flashinfo0103_u8_tags[0]));
            i8_tags = k_flashinfo0103_i8_tags;
            i8_tag_count = (omc_u32)(sizeof(k_flashinfo0103_i8_tags)
                                     / sizeof(k_flashinfo0103_i8_tags[0]));
            name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY;
            break;
        case OMC_EXIF_NIKON_FLASHINFO_0106:
            u8_tags = k_flashinfo0106_u8_tags;
            u8_tag_count = (omc_u32)(sizeof(k_flashinfo0106_u8_tags)
                                     / sizeof(k_flashinfo0106_u8_tags[0]));
            i8_tags = k_flashinfo0106_i8_tags;
            i8_tag_count = (omc_u32)(sizeof(k_flashinfo0106_i8_tags)
                                     / sizeof(k_flashinfo0106_i8_tags[0]));
            name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY;
            break;
        case OMC_EXIF_NIKON_FLASHINFO_0107:
            u8_tags = k_flashinfo0107_u8_tags;
            u8_tag_count = (omc_u32)(sizeof(k_flashinfo0107_u8_tags)
                                     / sizeof(k_flashinfo0107_u8_tags[0]));
            i8_tags = k_flashinfo0107_i8_tags;
            i8_tag_count = (omc_u32)(sizeof(k_flashinfo0107_i8_tags)
                                     / sizeof(k_flashinfo0107_i8_tags[0]));
            name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_GROUPS;
            break;
        case OMC_EXIF_NIKON_FLASHINFO_0300:
            u8_tags = k_flashinfo0300_u8_tags;
            u8_tag_count = (omc_u32)(sizeof(k_flashinfo0300_u8_tags)
                                     / sizeof(k_flashinfo0300_u8_tags[0]));
            i8_tags = k_flashinfo0300_i8_tags;
            i8_tag_count = (omc_u32)(sizeof(k_flashinfo0300_i8_tags)
                                     / sizeof(k_flashinfo0300_i8_tags[0]));
            name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_GROUPS;
            break;
        case OMC_EXIF_NIKON_FLASHINFO_UNKNOWN:
            break;
        default:
            u8_tags = k_flashinfo0100_u8_tags;
            u8_tag_count = (omc_u32)(sizeof(k_flashinfo0100_u8_tags)
                                     / sizeof(k_flashinfo0100_u8_tags[0]));
            i8_tags = k_flashinfo0100_i8_tags;
            i8_tag_count = (omc_u32)(sizeof(k_flashinfo0100_i8_tags)
                                     / sizeof(k_flashinfo0100_i8_tags[0]));
            name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY;
            break;
        }

        for (i = 0U; i < u8_tag_count; ++i) {
            omc_u16 tag16;

            tag16 = u8_tags[i];
            if (raw.size <= tag16) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, tag16,
                                                   order++, raw.data[tag16]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
        for (i = 0U; i < i8_tag_count; ++i) {
            omc_u16 tag16;
            omc_u8 variant;

            tag16 = i8_tags[i];
            if (raw.size <= tag16) {
                continue;
            }
            status = omc_exif_emit_derived_exif_i8(
                ctx, ifd_name, tag16, order++, omc_exif_u8_to_i8(raw.data[tag16]));
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }

            variant = 0U;
            if (name_context_kind == OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_LEGACY) {
                if (layout == OMC_EXIF_NIKON_FLASHINFO_0106 && tag16 == 0x0027U
                    && flash_control_mode < 0x06U) {
                    variant = 1U;
                } else if (layout == OMC_EXIF_NIKON_FLASHINFO_0100) {
                    if (tag16 == 0x000AU) {
                        variant = 8U;
                    } else if (tag16 == 0x0011U) {
                        variant = 2U;
                    } else if (tag16 == 0x0012U) {
                        variant = 3U;
                    }
                } else if (layout == OMC_EXIF_NIKON_FLASHINFO_0102) {
                    if (tag16 == 0x000AU) {
                        variant = 8U;
                    } else if (tag16 == 0x0012U) {
                        variant = flash_group_a_variant;
                    } else if (tag16 == 0x0013U) {
                        variant = flash_group_b_variant;
                    } else if (tag16 == 0x0014U) {
                        variant = flash_group_c_variant;
                    }
                } else if (layout == OMC_EXIF_NIKON_FLASHINFO_0103) {
                    if (tag16 == 0x0013U) {
                        variant = flash_group_a_variant;
                    } else if (tag16 == 0x0014U) {
                        variant = flash_group_b_variant;
                    } else if (tag16 == 0x0015U) {
                        variant = flash_group_c_variant;
                    }
                } else if (layout == OMC_EXIF_NIKON_FLASHINFO_0106) {
                    if (tag16 == 0x0028U) {
                        variant = flash_group_a_variant;
                    } else if (tag16 == 0x0029U) {
                        variant = flash_group_b_variant;
                    } else if (tag16 == 0x002AU) {
                        variant = flash_group_c_variant;
                    }
                }
            } else if (name_context_kind
                       == OMC_ENTRY_NAME_CTX_NIKON_FLASHINFO_GROUPS) {
                if (tag16 == 0x0028U) {
                    variant = flash_group_a_variant;
                } else if (tag16 == 0x0029U) {
                    variant = flash_group_b_variant;
                } else if (tag16 == 0x002AU) {
                    variant = flash_group_c_variant;
                }
            }
            if (variant != 0U) {
                omc_exif_mark_last_contextual_name(ctx, name_context_kind,
                                                   variant);
            }
        }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x00B0U);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 16U
            && omc_exif_make_subifd_name("mk_nikon", "multiexposure", 0U,
                                         ifd_name, sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            if (omc_exif_read_u32le_raw(raw.data, raw.size, 4U, &u32v)) {
                status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0001U,
                                                        1U, u32v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
            if (omc_exif_read_u32le_raw(raw.data, raw.size, 8U, &u32v)) {
                status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0002U,
                                                        2U, u32v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
            if (omc_exif_read_u32le_raw(raw.data, raw.size, 12U, &u32v)) {
                status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0003U,
                                                        3U, u32v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x00B7U);
    {
        omc_u8* raw_copy;
        omc_u32 order;
        omc_u32 i;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 4U && memcmp(raw.data, "0101", 4U) != 0 &&
            memcmp(raw.data, "0300", 4U) != 0 && memcmp(raw.data, "0301", 4U) != 0 &&
            memcmp(raw.data, "0400", 4U) != 0 && raw.size >= 29U &&
            omc_exif_make_subifd_name(
                "mk_nikon",
                memcmp(raw.data, "0200", 4U) == 0 ? "afinfo2v0200" : "afinfo2v0100", 0U,
                ifd_name, sizeof(ifd_name))) {
            static const omc_u16 u16_tags[] = { 0x0010U, 0x0012U, 0x0014U,
                                                0x0016U, 0x0018U, 0x001AU };

            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            order = 1U;
            for (i = 4U; i <= 7U; ++i) {
                if (raw.size <= i) {
                    continue;
                }
                status = omc_exif_emit_derived_exif_u8(ctx, ifd_name,
                                                       (omc_u16)i, order++,
                                                       raw.data[i]);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
            status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name, 0x0008U,
                                                      order++, raw.data + 8U,
                                                      5U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            for (i = 0U;
                 i < (omc_u32)(sizeof(u16_tags) / sizeof(u16_tags[0])); ++i) {
                omc_u16 tag16;

                tag16 = u16_tags[i];
                if ((raw.size - 1U) <= tag16) {
                    continue;
                }
                if (!omc_exif_read_u16(ctx->cfg, raw.data, (omc_size)raw.size,
                                       tag16, &u16v)) {
                    continue;
                }
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, tag16,
                                                        order++, u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
            for (i = 0x2AU; i <= 0x34U; i += 2U) {
                if (!omc_exif_read_u16(ctx->cfg, raw.data, raw.size, i, &u16v))
                    continue;
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, (omc_u16)i,
                                                        order++, u16v);
                if (status != OMC_EXIF_OK) {
                    free(raw_copy);
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
            for (i = 0x2EU; i <= 0x30U; i += 2U) {
                if (!omc_exif_read_u16(ctx->cfg, raw.data, raw.size, i, &u16v))
                    continue;
                status = omc_exif_emit_derived_exif_u16(
                    ctx, ifd_name, (omc_u16)(i + 1U), order++, u16v);
                if (status != OMC_EXIF_OK) {
                    free(raw_copy);
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x001CU, order++,
                                                   raw.data[28U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            if (raw.size > 0x52U) {
                status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x52U, order++,
                                                       raw.data[0x52U]);
                if (status != OMC_EXIF_OK) {
                    free(raw_copy);
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
        } else if (raw.size >= 4U && memcmp(raw.data, "0101", 4U) == 0 &&
                   raw.size >= 0x53U &&
                   omc_exif_make_subifd_name("mk_nikon", "afinfo2v0101", 0U, ifd_name,
                                             sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            order = 1U;
            for (i = 4U; i <= 6U; ++i) {
                status = omc_exif_emit_derived_exif_u8(ctx, ifd_name,
                                                       (omc_u16)i, order++,
                                                       raw.data[i]);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
            status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name, 0x0008U,
                                                      order++, raw.data + 8U,
                                                      7U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x001CU,
                                                   order++, raw.data[0x1CU]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name, 0x0030U,
                                                      order++, raw.data + 0x30U,
                                                      7U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0044U,
                                                   order++, raw.data[0x44U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            for (i = 0x46U; i <= 0x50U; i += 2U) {
                if (!omc_exif_read_u16(ctx->cfg, raw.data, (omc_size)raw.size,
                                       i, &u16v)) {
                    continue;
                }
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name,
                                                        (omc_u16)i, order++,
                                                        u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0052U,
                                                   order, raw.data[0x52U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        } else if (raw.size >= 4U &&
                   (memcmp(raw.data, "0300", 4U) == 0 ||
                    memcmp(raw.data, "0301", 4U) == 0) &&
                   raw.size >= 0x34U &&
                   omc_exif_make_subifd_name("mk_nikon", "afinfo2v0300", 0U, ifd_name,
                                             sizeof(ifd_name))) {
            static const omc_u16 u8_tags[] = { 0x0004U, 0x0005U, 0x0006U,
                                               0x0007U, 0x0038U };
            static const omc_u16 u16_tags[] = { 0x002AU, 0x002CU, 0x002EU,
                                                0x0031U, 0x0032U };
            static const omc_u16 raw_u16_offs[] = { 0x002AU, 0x002CU, 0x002EU,
                                                    0x0030U, 0x0032U };

            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            order = 1U;
            for (i = 0U;
                 i < (omc_u32)(sizeof(u8_tags) / sizeof(u8_tags[0])); ++i) {
                omc_u16 tag16;

                tag16 = u8_tags[i];
                if (raw.size <= tag16) {
                    continue;
                }
                status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, tag16,
                                                       order++, raw.data[tag16]);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
            for (i = 0U;
                 i < (omc_u32)(sizeof(u16_tags) / sizeof(u16_tags[0])); ++i) {
                if (!omc_exif_read_u16(ctx->cfg, raw.data, (omc_size)raw.size,
                                       raw_u16_offs[i], &u16v)) {
                    continue;
                }
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name,
                                                        u16_tags[i], order++,
                                                        u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x00B8U);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 8U
            && omc_exif_make_subifd_name("mk_nikon", "fileinfo", 0U, ifd_name,
                                         sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            if (omc_exif_read_u16le_raw(raw.data, raw.size, 4U, &u16v)) {
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0002U,
                                                        1U, u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
            if (omc_exif_read_u16le_raw(raw.data, raw.size, 6U, &u16v)) {
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0003U,
                                                        2U, u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
            if (omc_exif_read_u16le_raw(raw.data, raw.size, 8U, &u16v)) {
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0004U,
                                                        3U, u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x00BBU);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 6U
            && omc_exif_make_subifd_name("mk_nikon", "retouchinfo", 0U,
                                         ifd_name, sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_i8(ctx, ifd_name, 0x0005U, 1U,
                                                   (omc_s8)raw.data[5U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x0023U);
    if (entry == NULL)
        entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x00BDU);
    {
        static const omc_u16 tags1[] = {0x30U, 0x31U, 0x32U, 0x33U, 0x34U,
                                        0x35U, 0x36U, 0x37U, 0x38U, 0x39U};
        static const omc_u16 tags2[] = {0x30U, 0x31U, 0x33U, 0x35U, 0x37U, 0x39U,
                                        0x3BU, 0x3DU, 0x3FU, 0x40U, 0x41U};
        static const omc_u16 tags3[] = {0x36U, 0x37U, 0x39U, 0x3BU, 0x3DU, 0x3FU,
                                        0x41U, 0x43U, 0x45U, 0x47U, 0x48U, 0x49U};
        const omc_u16 *tags;
        const char *suffix;
        omc_u32 desc, base, tag_count, order, i;
        omc_u8 *copy = NULL;
        status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        if (raw.size >= 4U) {
            desc = 4U;
            base = 0x18U;
            tags = tags2;
            tag_count = sizeof(tags2) / sizeof(tags2[0]);
            suffix = "picturecontrolunknown";
            if (memcmp(raw.data, "01", 2U) == 0) {
                suffix = "picturecontrol";
                tags = tags1;
                tag_count = sizeof(tags1) / sizeof(tags1[0]);
            } else if (memcmp(raw.data, "02", 2U) == 0)
                suffix = "picturecontrol2";
            else if (memcmp(raw.data, "03", 2U) == 0) {
                suffix = "picturecontrol3";
                desc = 8U;
                base = 0x1CU;
                tags = tags3;
                tag_count = sizeof(tags3) / sizeof(tags3[0]);
            }
            if (raw.size >= base + 20U &&
                omc_exif_make_subifd_name("mk_nikon", suffix, 0U, ifd_name,
                                          sizeof(ifd_name))) {
                status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0U, 0U,
                                                         raw.data, 4U);
                if (status == OMC_EXIF_OK)
                    status = omc_exif_emit_derived_exif_text(
                        ctx, ifd_name, (omc_u16)desc, 1U, raw.data + desc,
                        omc_exif_trim_nul_size(raw.data + desc, 20U));
                if (status == OMC_EXIF_OK)
                    status = omc_exif_emit_derived_exif_text(
                        ctx, ifd_name, (omc_u16)base, 2U, raw.data + base,
                        omc_exif_trim_nul_size(raw.data + base, 20U));
                order = 3U;
                for (i = 0U; i < tag_count && status == OMC_EXIF_OK; ++i)
                    if (tags[i] < raw.size)
                        status = omc_exif_emit_derived_exif_u8(
                            ctx, ifd_name, tags[i], order++, raw.data[tags[i]]);
            }
        }
        free(copy);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x0024U);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 4U
            && omc_exif_make_subifd_name("mk_nikon", "worldtime", 0U,
                                         ifd_name, sizeof(ifd_name))) {
            if (omc_exif_read_u16(ctx->cfg, raw.data, (omc_size)raw.size, 0U,
                                  &u16v)) {
                i16v = (omc_s16)u16v;
                status = omc_exif_emit_derived_exif_i16(ctx, ifd_name, 0x0000U,
                                                        0U, i16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0002U, 1U,
                                                   raw.data[2U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0003U, 2U,
                                                   raw.data[3U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x0025U);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 12U
            && omc_exif_make_subifd_name("mk_nikon", "isoinfo", 0U, ifd_name,
                                         sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0000U, 0U,
                                                   raw.data[0U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            if (omc_exif_read_u16(ctx->cfg, raw.data, (omc_size)raw.size, 4U,
                                  &u16v)) {
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0004U,
                                                        1U, u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0006U, 2U,
                                                   raw.data[6U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            if (omc_exif_read_u16(ctx->cfg, raw.data, (omc_size)raw.size, 10U,
                                  &u16v)) {
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x000AU,
                                                        3U, u16v);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    free(raw_copy);
                    return 0;
                }
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x0035U);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size >= 8U
            && omc_exif_make_subifd_name("mk_nikon", "hdrinfo", 0U, ifd_name,
                                         sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0004U, 1U,
                                                   raw.data[4U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0005U, 2U,
                                                   raw.data[5U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0006U, 3U,
                                                   raw.data[6U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0007U, 4U,
                                                   raw.data[7U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
        }
        free(raw_copy);
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x0039U);
    {
        omc_u8* raw_copy;

        raw_copy = (omc_u8*)0;
        status =
            omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        if (raw.size > 9U
            && omc_exif_make_subifd_name("mk_nikon", "locationinfo", 0U,
                                         ifd_name, sizeof(ifd_name))) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     0U, raw.data, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0004U, 1U,
                                                   raw.data[4U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name, 0x0005U,
                                                      2U, raw.data + 5U, 3U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0008U, 3U,
                                                   raw.data[8U]);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
            status = omc_exif_emit_derived_exif_bytes(
                ctx, ifd_name, 0x0009U, 4U, raw.data + 9U,
                (omc_u32)(raw.size - 9U));
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                free(raw_copy);
                return 0;
            }
        }
        free(raw_copy);
    }

    return 1;
}

static int
omc_exif_decode_nikon_settings(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    char ifd_name[64];
    omc_u8* raw_copy;
    omc_u16 tag16;
    omc_u16 type_be;
    omc_u32 rec_count;
    omc_u32 i;
    omc_u32 value32;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw_copy = (omc_u8*)0;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x004EU);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    if (raw.size < 24U) {
        free(raw_copy);
        return 1;
    }
    if ((raw.size % 8U) != 0U
        || !omc_exif_read_u32le_raw(raw.data, raw.size, 20U, &rec_count)
        || rec_count == 0U || (24U + ((omc_u64)rec_count * 8U)) != raw.size
        || !omc_exif_make_subifd_name("mk_nikonsettings", "main", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        free(raw_copy);
        return 1;
    }

    for (i = 0U; i < rec_count; ++i) {
        omc_u64 off;

        off = 24U + ((omc_u64)i * 8U);
        if (!omc_exif_read_u16le_raw(raw.data, raw.size, off, &tag16)
            || !omc_exif_read_u16be_raw(raw.data, raw.size, off + 2U,
                                        &type_be)
            || !omc_exif_read_u32le_raw(raw.data, raw.size, off + 4U,
                                        &value32)) {
            break;
        }

        if (type_be == 1U) {
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, tag16, i,
                                                   (omc_u8)value32);
        } else if (type_be == 3U) {
            status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, tag16, i,
                                                    (omc_u16)value32);
        } else if (type_be == 8U) {
            status = omc_exif_emit_derived_exif_i16(ctx, ifd_name, tag16, i,
                                                    (omc_s16)value32);
        } else if (type_be == 9U) {
            status = omc_exif_emit_derived_exif_i32(ctx, ifd_name, tag16, i,
                                                    (omc_s32)value32);
        } else {
            status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, tag16, i,
                                                    value32);
        }
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }

    free(raw_copy);
    return 1;
}

static int
omc_exif_decode_nikon_aftune(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    char ifd_name[64];
    omc_exif_status status;
    omc_u8* raw_copy;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw_copy = (omc_u8*)0;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x00B9U);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    if (raw.size < 4U
        || !omc_exif_make_subifd_name("mk_nikon", "aftune", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        free(raw_copy);
        return 1;
    }

    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0000U, 0U,
                                           raw.data[0U]);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0001U, 1U,
                                           raw.data[1U]);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_i8(ctx, ifd_name, 0x0002U, 2U,
                                           (omc_s8)raw.data[2U]);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_i8(ctx, ifd_name, 0x0003U, 3U,
                                           (omc_s8)raw.data[3U]);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    free(raw_copy);
    return 1;
}

static int
omc_exif_decode_nikon_lensdata(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    const char* suffix;
    char ifd_name[64];
    omc_exif_status status;
    omc_u8* raw_copy;
    const omc_u16* want;
    omc_u32 want_count;
    omc_u32 wi;
    omc_u32 order;
    omc_u16 lens_model;
    static const omc_u16 k_lens0100_tags[] = { 0x0006U, 0x0007U, 0x0008U,
                                               0x0009U, 0x000AU, 0x000BU,
                                               0x000CU };
    static const omc_u16 k_lens0101_tags[] = { 0x0004U, 0x0005U, 0x0008U,
                                               0x0009U, 0x000AU, 0x000BU,
                                               0x000CU, 0x000DU, 0x000EU,
                                               0x000FU, 0x0010U, 0x0011U,
                                               0x0012U };

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw_copy = (omc_u8*)0;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x0098U);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    if (raw.size < 4U) {
        free(raw_copy);
        return 1;
    }

    lens_model = 0U;
    suffix = "lensdataunknown";
    want = (const omc_u16*)0;
    want_count = 0U;
    if (memcmp(raw.data, "0100", 4U) == 0) {
        suffix = "lensdata0100";
        want = k_lens0100_tags;
        want_count = (omc_u32)(sizeof(k_lens0100_tags)
                               / sizeof(k_lens0100_tags[0]));
    } else if (memcmp(raw.data, "0101", 4U) == 0) {
        suffix = "lensdata0101";
        want = k_lens0101_tags;
        want_count = (omc_u32)(sizeof(k_lens0101_tags)
                               / sizeof(k_lens0101_tags[0]));
    } else if (memcmp(raw.data, "0400", 4U) == 0 || memcmp(raw.data, "0401", 4U) == 0) {
        suffix = "lensdata0400";
        lens_model = 0x18AU;
    } else if (memcmp(raw.data, "0402", 4U) == 0) {
        suffix = "lensdata0402";
        lens_model = 0x18BU;
    } else if (memcmp(raw.data, "0403", 4U) == 0) {
        suffix = "lensdata0403";
        lens_model = 0x2ACU;
    } else if (memcmp(raw.data, "0201", 4U) == 0 || memcmp(raw.data, "0202", 4U) == 0 ||
               memcmp(raw.data, "0203", 4U) == 0)
        suffix = "lensdata0201";
    else if (memcmp(raw.data, "0204", 4U) == 0)
        suffix = "lensdata0204";
    else if (memcmp(raw.data, "0800", 4U) == 0)
        suffix = "lensdata0800";

    if (!omc_exif_make_subifd_name("mk_nikon", suffix, 0U, ifd_name,
                                   sizeof(ifd_name))) {
        free(raw_copy);
        return 1;
    }

    status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U, 0U,
                                             raw.data, 4U);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }

    if (lens_model != 0U && raw.size >= (omc_size)lens_model + 64U) {
        status = omc_exif_emit_derived_exif_text(
            ctx, ifd_name, lens_model, 1U, raw.data + lens_model,
            omc_exif_trim_nul_size(raw.data + lens_model, 64U));
        if (status != OMC_EXIF_OK) {
            free(raw_copy);
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    order = 1U;
    for (wi = 0U; wi < want_count; ++wi) {
        omc_u16 tag;

        tag = want[wi];
        if ((omc_size)tag >= raw.size) {
            continue;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, tag,
                                               order++, raw.data[(omc_size)tag]);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }
    free(raw_copy);
    return 1;
}

static int
omc_exif_decode_nikon_colorbalancec(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    char ifd_name[64];
    omc_exif_status status;
    omc_u8* raw_copy;
    omc_u16 u16v;
    omc_u32 i;
    static const struct {
        omc_u16 tag;
        omc_u32 off;
    } k_colorbalancec_tags[] = {
        { 0x0038U, 0x0038U }, { 0x004CU, 0x004CU }, { 0x0060U, 0x0060U },
        { 0x0074U, 0x0074U }, { 0x0088U, 0x0088U }, { 0x009CU, 0x009CU },
        { 0x00B0U, 0x00B0U }, { 0x00C4U, 0x00C4U }, { 0x00D8U, 0x00D8U },
        { 0x0100U, 0x0100U }, { 0x0114U, 0x0114U }
    };

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw_copy = (omc_u8*)0;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x0014U);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    if (raw.size < 8U
        || memcmp(raw.data, "NRW ", 4U) != 0
        || !omc_exif_make_subifd_name("mk_nikon", "colorbalancec", 0U,
                                      ifd_name, sizeof(ifd_name))) {
        free(raw_copy);
        return 1;
    }

    status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0004U, 0U,
                                             raw.data + 4U, 4U);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    if (raw.size >= 0x22U
        && omc_exif_read_u16(ctx->cfg, raw.data, (omc_size)raw.size, 0x20U,
                             &u16v)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0020U, 1U,
                                                u16v);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }
    for (i = 0U;
         i < (omc_u32)(sizeof(k_colorbalancec_tags)
                       / sizeof(k_colorbalancec_tags[0]));
         ++i) {
        omc_u8 raw_out[16];
        omc_u32 levels[4];
        omc_u32 off;
        omc_u32 k;

        off = k_colorbalancec_tags[i].off;
        if (raw.size < (omc_size)(off + 16U)) {
            continue;
        }
        for (k = 0U; k < 4U; ++k) {
            if (!omc_exif_read_u32(ctx->cfg, raw.data, (omc_size)raw.size,
                                   (omc_u64)off + (omc_u64)(k * 4U),
                                   &levels[k])) {
                break;
            }
        }
        if (k != 4U) {
            continue;
        }
        levels[0] *= 2U;
        levels[3] *= 2U;
        for (k = 0U; k < 4U; ++k) {
            if (!omc_exif_write_u32(ctx->cfg, raw_out, sizeof(raw_out),
                                    (omc_u64)(k * 4U), levels[k])) {
                break;
            }
        }
        if (k != 4U) {
            continue;
        }
        status = omc_exif_emit_derived_exif_array_copy(
            ctx, ifd_name, k_colorbalancec_tags[i].tag, 2U + i,
            OMC_ELEM_U32, raw_out, sizeof(raw_out), 4U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }
    free(raw_copy);
    return 1;
}

static int
omc_exif_decode_nikon_preview_ifd(omc_exif_ctx* ctx, const omc_u8* raw,
                                  omc_u64 raw_size)
{
    const omc_entry* entry;
    omc_const_bytes note;
    omc_u8 *copy;
    omc_u64 header, offset;
    omc_size first, i;
    omc_exif_cfg cfg;
    omc_exif_opts opts;
    omc_exif_status status;
    int ok;
    (void)raw;
    (void)raw_size;
    if (ctx->store == NULL || ctx->measure_only)
        return 1;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x11U);
    if (entry == NULL || entry->value.kind != OMC_VAL_SCALAR ||
        entry->value.elem_type != OMC_ELEM_U32)
        return 1;
    offset = entry->value.u.u64;
    entry = omc_exif_find_first_entry(ctx->store, "exififd", 0x927cU);
    if (entry == NULL || entry->value.kind != OMC_VAL_BYTES)
        return 1;
    copy = NULL;
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &note);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    if (!omc_exif_find_tiff_header(note.data, note.size, 0U, 128U, &header) ||
        offset >= note.size - header) {
        free(copy);
        return 1;
    }
    cfg = omc_exif_make_classic_cfg(note.data[header] == 'I');
    opts = ctx->opts;
    opts.decode_makernote = 0;
    opts.decode_geotiff = 0;
    opts.decode_printim = 0;
    opts.decode_embedded_containers = 0;
    first = ctx->store->entry_count;
    ok = omc_exif_decode_ifd_blob_loose_named_cfg(ctx, note.data + (omc_size)header,
                                                  note.size - (omc_size)header, offset,
                                                  &opts, cfg, "mk_nikon_preview_0");
    for (i = first; i < ctx->store->entry_count; ++i)
        ctx->store->entries[i].flags |= OMC_ENTRY_FLAG_DERIVED;
    free(copy);
    return ok ||
           (ctx->res.status != OMC_EXIF_LIMIT && ctx->res.status != OMC_EXIF_NOMEM);
}

/* Nikon byte substitution tables, pinned OpenMeta C++ f11de3e0. */
static const omc_u8 kNikonDecryptXlat0[256] = {
    0xC1u, 0xBFu, 0x6Du, 0x0Du, 0x59u, 0xC5u, 0x13u, 0x9Du, 0x83u, 0x61u, 0x6Bu, 0x4Fu,
    0xC7u, 0x7Fu, 0x3Du, 0x3Du, 0x53u, 0x59u, 0xE3u, 0xC7u, 0xE9u, 0x2Fu, 0x95u, 0xA7u,
    0x95u, 0x1Fu, 0xDFu, 0x7Fu, 0x2Bu, 0x29u, 0xC7u, 0x0Du, 0xDFu, 0x07u, 0xEFu, 0x71u,
    0x89u, 0x3Du, 0x13u, 0x3Du, 0x3Bu, 0x13u, 0xFBu, 0x0Du, 0x89u, 0xC1u, 0x65u, 0x1Fu,
    0xB3u, 0x0Du, 0x6Bu, 0x29u, 0xE3u, 0xFBu, 0xEFu, 0xA3u, 0x6Bu, 0x47u, 0x7Fu, 0x95u,
    0x35u, 0xA7u, 0x47u, 0x4Fu, 0xC7u, 0xF1u, 0x59u, 0x95u, 0x35u, 0x11u, 0x29u, 0x61u,
    0xF1u, 0x3Du, 0xB3u, 0x2Bu, 0x0Du, 0x43u, 0x89u, 0xC1u, 0x9Du, 0x9Du, 0x89u, 0x65u,
    0xF1u, 0xE9u, 0xDFu, 0xBFu, 0x3Du, 0x7Fu, 0x53u, 0x97u, 0xE5u, 0xE9u, 0x95u, 0x17u,
    0x1Du, 0x3Du, 0x8Bu, 0xFBu, 0xC7u, 0xE3u, 0x67u, 0xA7u, 0x07u, 0xF1u, 0x71u, 0xA7u,
    0x53u, 0xB5u, 0x29u, 0x89u, 0xE5u, 0x2Bu, 0xA7u, 0x17u, 0x29u, 0xE9u, 0x4Fu, 0xC5u,
    0x65u, 0x6Du, 0x6Bu, 0xEFu, 0x0Du, 0x89u, 0x49u, 0x2Fu, 0xB3u, 0x43u, 0x53u, 0x65u,
    0x1Du, 0x49u, 0xA3u, 0x13u, 0x89u, 0x59u, 0xEFu, 0x6Bu, 0xEFu, 0x65u, 0x1Du, 0x0Bu,
    0x59u, 0x13u, 0xE3u, 0x4Fu, 0x9Du, 0xB3u, 0x29u, 0x43u, 0x2Bu, 0x07u, 0x1Du, 0x95u,
    0x59u, 0x59u, 0x47u, 0xFBu, 0xE5u, 0xE9u, 0x61u, 0x47u, 0x2Fu, 0x35u, 0x7Fu, 0x17u,
    0x7Fu, 0xEFu, 0x7Fu, 0x95u, 0x95u, 0x71u, 0xD3u, 0xA3u, 0x0Bu, 0x71u, 0xA3u, 0xADu,
    0x0Bu, 0x3Bu, 0xB5u, 0xFBu, 0xA3u, 0xBFu, 0x4Fu, 0x83u, 0x1Du, 0xADu, 0xE9u, 0x2Fu,
    0x71u, 0x65u, 0xA3u, 0xE5u, 0x07u, 0x35u, 0x3Du, 0x0Du, 0xB5u, 0xE9u, 0xE5u, 0x47u,
    0x3Bu, 0x9Du, 0xEFu, 0x35u, 0xA3u, 0xBFu, 0xB3u, 0xDFu, 0x53u, 0xD3u, 0x97u, 0x53u,
    0x49u, 0x71u, 0x07u, 0x35u, 0x61u, 0x71u, 0x2Fu, 0x43u, 0x2Fu, 0x11u, 0xDFu, 0x17u,
    0x97u, 0xFBu, 0x95u, 0x3Bu, 0x7Fu, 0x6Bu, 0xD3u, 0x25u, 0xBFu, 0xADu, 0xC7u, 0xC5u,
    0xC5u, 0xB5u, 0x8Bu, 0xEFu, 0x2Fu, 0xD3u, 0x07u, 0x6Bu, 0x25u, 0x49u, 0x95u, 0x25u,
    0x49u, 0x6Du, 0x71u, 0xC7u,
};
static const omc_u8 kNikonDecryptXlat1[256] = {
    0xA7u, 0xBCu, 0xC9u, 0xADu, 0x91u, 0xDFu, 0x85u, 0xE5u, 0xD4u, 0x78u, 0xD5u, 0x17u,
    0x46u, 0x7Cu, 0x29u, 0x4Cu, 0x4Du, 0x03u, 0xE9u, 0x25u, 0x68u, 0x11u, 0x86u, 0xB3u,
    0xBDu, 0xF7u, 0x6Fu, 0x61u, 0x22u, 0xA2u, 0x26u, 0x34u, 0x2Au, 0xBEu, 0x1Eu, 0x46u,
    0x14u, 0x68u, 0x9Du, 0x44u, 0x18u, 0xC2u, 0x40u, 0xF4u, 0x7Eu, 0x5Fu, 0x1Bu, 0xADu,
    0x0Bu, 0x94u, 0xB6u, 0x67u, 0xB4u, 0x0Bu, 0xE1u, 0xEAu, 0x95u, 0x9Cu, 0x66u, 0xDCu,
    0xE7u, 0x5Du, 0x6Cu, 0x05u, 0xDAu, 0xD5u, 0xDFu, 0x7Au, 0xEFu, 0xF6u, 0xDBu, 0x1Fu,
    0x82u, 0x4Cu, 0xC0u, 0x68u, 0x47u, 0xA1u, 0xBDu, 0xEEu, 0x39u, 0x50u, 0x56u, 0x4Au,
    0xDDu, 0xDFu, 0xA5u, 0xF8u, 0xC6u, 0xDAu, 0xCAu, 0x90u, 0xCAu, 0x01u, 0x42u, 0x9Du,
    0x8Bu, 0x0Cu, 0x73u, 0x43u, 0x75u, 0x05u, 0x94u, 0xDEu, 0x24u, 0xB3u, 0x80u, 0x34u,
    0xE5u, 0x2Cu, 0xDCu, 0x9Bu, 0x3Fu, 0xCAu, 0x33u, 0x45u, 0xD0u, 0xDBu, 0x5Fu, 0xF5u,
    0x52u, 0xC3u, 0x21u, 0xDAu, 0xE2u, 0x22u, 0x72u, 0x6Bu, 0x3Eu, 0xD0u, 0x5Bu, 0xA8u,
    0x87u, 0x8Cu, 0x06u, 0x5Du, 0x0Fu, 0xDDu, 0x09u, 0x19u, 0x93u, 0xD0u, 0xB9u, 0xFCu,
    0x8Bu, 0x0Fu, 0x84u, 0x60u, 0x33u, 0x1Cu, 0x9Bu, 0x45u, 0xF1u, 0xF0u, 0xA3u, 0x94u,
    0x3Au, 0x12u, 0x77u, 0x33u, 0x4Du, 0x44u, 0x78u, 0x28u, 0x3Cu, 0x9Eu, 0xFDu, 0x65u,
    0x57u, 0x16u, 0x94u, 0x6Bu, 0xFBu, 0x59u, 0xD0u, 0xC8u, 0x22u, 0x36u, 0xDBu, 0xD2u,
    0x63u, 0x98u, 0x43u, 0xA1u, 0x04u, 0x87u, 0x86u, 0xF7u, 0xA6u, 0x26u, 0xBBu, 0xD6u,
    0x59u, 0x4Du, 0xBFu, 0x6Au, 0x2Eu, 0xAAu, 0x2Bu, 0xEFu, 0xE6u, 0x78u, 0xB6u, 0x4Eu,
    0xE0u, 0x2Fu, 0xDCu, 0x7Cu, 0xBEu, 0x57u, 0x19u, 0x32u, 0x7Eu, 0x2Au, 0xD0u, 0xB8u,
    0xBAu, 0x29u, 0x00u, 0x3Cu, 0x52u, 0x7Du, 0xA8u, 0x49u, 0x3Bu, 0x2Du, 0xEBu, 0x25u,
    0x49u, 0xFAu, 0xA3u, 0xAAu, 0x39u, 0xA7u, 0xC5u, 0xA7u, 0x50u, 0x11u, 0x36u, 0xFBu,
    0xC6u, 0x67u, 0x4Au, 0xF5u, 0xA5u, 0x12u, 0x65u, 0x7Eu, 0xB0u, 0xDFu, 0xAFu, 0x4Eu,
    0xB3u, 0x61u, 0x7Fu, 0x2Fu,
};

/* These helpers enforce the total entry budget for newly expanded tables. */
static int omc_exif_vendor_value(omc_exif_ctx *ctx, const char *ifd, omc_u16 tag,
                                 omc_u32 order, const omc_val *value)
{
    omc_byte_ref token;
    omc_key key;
    omc_exif_status status;
    if (ctx->measure_only || ctx->store == NULL)
        return 1;
    if (ctx->res.entries_decoded >= ctx->opts.limits.max_total_entries) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, 0U, tag);
        return 0;
    }
    status = omc_exif_store_cstr_len(ctx, ifd, strlen(ifd), &token);
    if (status == OMC_EXIF_OK) {
        omc_key_make_exif_tag(&key, token, tag);
        status = omc_exif_add_derived_entry(
            ctx, &key, value, order, value->kind == OMC_VAL_SCALAR ? 1U : value->count);
    }
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    ctx->res.entries_decoded++;
    return 1;
}

static int omc_exif_nikon_version(omc_exif_ctx *ctx, const char *ifd, const omc_u8 *raw)
{
    omc_u32 n;
    omc_byte_ref ref;
    omc_val value;
    omc_exif_status status;
    n = 0U;
    while (n < 4U && raw[n] != 0U)
        n++;
    status = omc_exif_store_ref(ctx, raw, n, &ref);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    omc_val_make_text(&value, ref, OMC_TEXT_ASCII);
    return omc_exif_vendor_value(ctx, ifd, 0U, 0U, &value);
}

static int omc_exif_decode_nikon_shotinfo(omc_exif_ctx *ctx)
{
    static const omc_u16 u8_tags[] = {
        0x000b, 0x000c, 0x000d, 0x0012, 0x0014, 0x0015, 0x0017, 0x0018, 0x0019, 0x001a,
        0x001b, 0x001c, 0x001d, 0x001e, 0x0020, 0x0021, 0x0024, 0x0026, 0x0028, 0x002c,
        0x002d, 0x002e, 0x002f, 0x0031, 0x0032, 0x0033, 0x0034, 0x0036, 0x0038, 0x003a,
        0x0046, 0x0048, 0x004a, 0x004b, 0x004c, 0x004e, 0x0050, 0x0051, 0x0052, 0x0053,
        0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
        0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069,
        0x006a, 0x006b, 0x006e, 0x0072, 0x0075, 0x0076, 0x0082, 0x008e, 0x0093, 0x0103,
        0x011a, 0x011b, 0x0128, 0x013c, 0x0159, 0x01a8, 0x01ac, 0x01ae, 0x01af, 0x01b0,
        0x01b4, 0x01d0, 0x0201, 0x0202, 0x020e, 0x0213, 0x0214, 0x0221, 0x0228, 0x022c,
        0x022e, 0x0234, 0x024e, 0x0256, 0x0257, 0x025c, 0x025d, 0x0265, 0x027a, 0x029f,
        0x02b5, 0x02c4, 0x02ca, 0x02d3, 0x02e2, 0x02e3, 0x0458, 0x04c0, 0x04c2, 0x04c3,
        0x04da, 0x04db, 0x051c, 0x0532, 0x06dd, 0x174c, 0x174d, 0x184d, 0x18ea, 0x18eb,
        0x3693,
    };
    static const omc_u16 u32_tags[] = {
        0x006a, 0x006e, 0x0157, 0x0242, 0x0246, 0x024a, 0x024d, 0x0276, 0x0279,
        0x0280, 0x0286, 0x02d5, 0x02d6, 0x0320, 0x0321, 0x05fb, 0x0bd8,
    };
    static const omc_u16 fallback_u8_tags[] = {
        0x0007, 0x0009, 0x000b, 0x000c, 0x000d, 0x000f, 0x0010, 0x0014, 0x0015, 0x0016,
        0x0017, 0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x0020, 0x0024,
        0x0028, 0x006a, 0x006e, 0x0072, 0x0075, 0x0076, 0x0082, 0x0100, 0x0101, 0x0102,
        0x0103, 0x011a, 0x011b, 0x0128, 0x0157, 0x0159, 0x01ae, 0x01af, 0x0201, 0x0202,
        0x0213, 0x027d, 0x0302, 0x032e, 0x0405, 0x0458, 0x0459, 0x0483, 0x0505, 0x0b08,
        0x0b0a, 0x2d2c, 0x3130, 0x3952, 0x5233, 0xff2c,
    };

    static const char *versions[] = {"0213", "0215", "0216", "0218", "0220",
                                     "0221", "0222", "0223", "0226", "0231",
                                     "0232", "0233", "0246", "0239", "0243"};
    static const char *tables[] = {"shotinfod90",  "shotinfod5000", "shotinfod300s",
                                   "shotinfod3s",  "shotinfod7000", "shotinfod5100",
                                   "shotinfod800", "shotinfod4",    "shotinfod5200",
                                   "shotinfod4s",  "shotinfod610",  "shotinfod810",
                                   "shotinfod6",   "shotinfod500",  "shotinfod850"};
    omc_const_bytes raw;
    const omc_entry *entry;
    const omc_u8 *text;
    omc_u8 *copy;
    omc_u8 ci, cj, ck;
    omc_u32 n, i, serial, shutter, order, v;
    omc_size size;
    const omc_u16 *tags;
    omc_u32 tag_count;
    const char *table;
    char model[96], ifd[64];
    omc_val value;
    omc_byte_ref prefix;
    omc_exif_status status;
    int decrypt, ok;
    if (ctx->store == NULL || ctx->measure_only)
        return 1;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x91U);
    copy = NULL;
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    if (raw.size < 4U) {
        free(copy);
        return 1;
    }
    model[0] = 0;
    if (omc_exif_find_first_text(ctx->store, "ifd0", 0x110U, &text, &n)) {
        if (n >= sizeof(model))
            n = sizeof(model) - 1U;
        memcpy(model, text, n);
        model[n] = 0;
    }
    serial = 0U;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x1dU);
    decrypt = entry != NULL && entry->value.kind == OMC_VAL_TEXT;
    if (decrypt) {
        omc_const_bytes sv = omc_arena_view(&ctx->store->arena, entry->value.u.ref);
        for (i = 0U; i < sv.size; ++i) {
            if (sv.data[i] < '0' || sv.data[i] > '9')
                continue;
            v = sv.data[i] - '0';
            if (serial > ((~(omc_u32)0) - v) / 10U) {
                serial = 0U;
                break;
            }
            serial = serial * 10U + v;
        }
    }
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0xa7U);
    decrypt = decrypt && entry != NULL && entry->value.kind == OMC_VAL_SCALAR &&
              entry->value.elem_type >= OMC_ELEM_U8 &&
              entry->value.elem_type <= OMC_ELEM_U32 &&
              entry->value.u.u64 <= (omc_u64)(~(omc_u32)0);
    shutter = decrypt ? (omc_u32)entry->value.u.u64 : 0U;
    table = "shotinfo";
    for (i = 0U; i < sizeof(versions) / sizeof(versions[0]); ++i)
        if (memcmp(copy, versions[i], 4U) == 0)
            table = tables[i];
    if (memcmp(copy, "0209", 4U) == 0 && strstr(model, "NIKON D40") != NULL)
        table = "shotinfod40";
    if (memcmp(copy, "0208", 4U) == 0 && strstr(model, "NIKON D80") != NULL)
        table = "shotinfod80";
    if (memcmp(copy, "0210", 4U) == 0) {
        if (strstr(model, "NIKON D300") != NULL)
            table = "shotinfod300a";
        else if (strstr(model, "NIKON D3") != NULL)
            table = "shotinfod3a";
    }
    if (memcmp(copy, "0214", 4U) == 0) {
        if (strstr(model, "NIKON D3X") != NULL)
            table = "shotinfod3x";
        else if (strstr(model, "NIKON D300") != NULL)
            table = "shotinfod300b";
        else if (strstr(model, "NIKON D3") != NULL)
            table = "shotinfod3b";
    }
    (void)omc_exif_make_subifd_name("mk_nikon", table, 0U, ifd, sizeof(ifd));
    ok = omc_exif_nikon_version(ctx, ifd, copy);
    order = 1U;
    if (ok && raw.size >= 9U) {
        status = omc_exif_store_ref(ctx, copy + 4U, 5U, &prefix);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            ok = 0;
        } else {
            omc_val_make_bytes(&value, prefix);
            ok = omc_exif_vendor_value(ctx, ifd, 4U, order++, &value);
        }
    }
    size = raw.size;
    if (decrypt && size > 4U) {
        if (size > 0x5200U)
            size = 0x5200U;
        ci = kNikonDecryptXlat0[serial & 255U];
        cj = kNikonDecryptXlat1[(shutter ^ (shutter >> 8) ^ (shutter >> 16) ^
                                 (shutter >> 24)) &
                                255U];
        ck = 0x60U;
        for (i = 4U; i < size; ++i) {
            cj = (omc_u8)((cj + (omc_u32)ci * ck) & 255U);
            ck = (omc_u8)((ck + 1U) & 255U);
            copy[i] ^= cj;
        }
        tags = u8_tags;
        tag_count = sizeof(u8_tags) / sizeof(u8_tags[0]);
    } else {
        decrypt = 0;
        tags = fallback_u8_tags;
        tag_count = sizeof(fallback_u8_tags) / sizeof(fallback_u8_tags[0]);
    }
    for (i = 0U; i < tag_count && ok; ++i) {
        if (tags[i] >= size)
            continue;
        omc_val_make_u8(&value, copy[tags[i]]);
        ok = omc_exif_vendor_value(ctx, ifd, tags[i], order++, &value);
    }
    if (ok && decrypt && size > 0x4d2U) {
        omc_val_make_i8(
            &value,
            (omc_s8)(copy[0x4d2U] <= 127U ? copy[0x4d2U] : (int)copy[0x4d2U] - 256));
        ok = omc_exif_vendor_value(ctx, ifd, 0x4d2U, order++, &value);
    }
    if (ok && decrypt && size >= 0x2d3U) {
        omc_val_make_u16(&value,
                         (omc_u16)(((omc_u16)copy[0x2d1U] << 8) | copy[0x2d2U]));
        ok = omc_exif_vendor_value(ctx, ifd, 0x2d1U, order++, &value);
    }
    for (i = 0U; decrypt && ok && i < sizeof(u32_tags) / sizeof(u32_tags[0]); ++i) {
        n = u32_tags[i];
        if (n + 4U > size)
            continue;
        v = ((omc_u32)copy[n] << 24) | ((omc_u32)copy[n + 1U] << 16) |
            ((omc_u32)copy[n + 2U] << 8) | copy[n + 3U];
        omc_val_make_u32(&value, v);
        ok = omc_exif_vendor_value(ctx, ifd, (omc_u16)n, order++, &value);
    }
    free(copy);
    return ok;
}

static int omc_exif_decode_nikon_preview_aliases(omc_exif_ctx *ctx)
{
    static const omc_u16 tags[] = {0x103U, 0x11aU, 0x11bU, 0x128U,
                                   0x201U, 0x202U, 0x213U};
    omc_u8 seen[7];
    omc_size i, count;
    omc_u32 j, order, n;
    const omc_u8 *text;
    int nikon;
    if (ctx->store == NULL || ctx->measure_only)
        return 1;
    count = ctx->store->entry_count;
    nikon = omc_exif_find_first_text(ctx->store, "ifd0", 0x10fU, &text, &n) &&
            n >= 5U && memcmp(text, "NIKON", 5U) == 0 &&
            omc_exif_find_first_entry(ctx->store, "exififd", 0x927cU) != NULL;
    for (i = 0U; !nikon && i < count; ++i) {
        const omc_entry *entry = &ctx->store->entries[i];
        omc_const_bytes name;
        if (entry->key.kind != OMC_KEY_EXIF_TAG)
            continue;
        name = omc_arena_view(&ctx->store->arena, entry->key.u.exif_tag.ifd);
        if (name.size >= 8U && memcmp(name.data, "mk_nikon", 8U) == 0)
            nikon = 1;
    }
    if (!nikon)
        return 1;
    memset(seen, 0, sizeof(seen));
    order = 0U;
    for (i = 0U; i < count; ++i) {
        omc_entry entry = ctx->store->entries[i];
        int ifd0 = omc_exif_entry_ifd_equals(ctx->store, &entry, "ifd0");
        if (!ifd0 && !omc_exif_entry_ifd_equals(ctx->store, &entry, "ifd1"))
            continue;
        for (j = 0U; j < 7U; ++j)
            if (!seen[j] && entry.key.u.exif_tag.tag == tags[j] && (!ifd0 || j == 6U)) {
                seen[j] = 1U;
                if (!omc_exif_vendor_value(ctx, "mk_nikon_preview_0", tags[j], order++,
                                           &entry.value))
                    return 0;
            }
    }
    return 1;
}
static int omc_exif_decode_nikon_extra_versions(omc_exif_ctx *ctx)
{
    static const char *encrypted[] = {"0100", "0102", "0205", "0213", "0219",
                                      "0209", "0211", "0215", "0217"};
    omc_const_bytes raw;
    omc_u8 *copy;
    omc_exif_status status;
    const omc_entry *entry;
    omc_u32 i;
    int known;
    for (i = 0U; i < 2U; ++i) {
        copy = NULL;
        entry =
            omc_exif_find_first_entry(ctx->store, "mk_nikon0", i == 0U ? 0x2cU : 0x32U);
        status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
        known = status == OMC_EXIF_OK;
        if (known && raw.size >= 4U)
            known = omc_exif_nikon_version(
                ctx, i == 0U ? "mk_nikon_unknowninfo_0" : "mk_nikon_unknowninfo2_0",
                raw.data);
        free(copy);
        if (!known) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    copy = NULL;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x88U);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
    if (status == OMC_EXIF_OK && raw.size >= 3U)
        for (i = 0U; i < 3U && status == OMC_EXIF_OK; ++i)
            status = omc_exif_emit_derived_exif_u8(ctx, "mk_nikon_afinfo_0", (omc_u16)i,
                                                   i, raw.data[i]);
    free(copy);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    copy = NULL;
    entry = omc_exif_find_first_entry(ctx->store, "mk_nikon0", 0x97U);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
    if (status == OMC_EXIF_OK && raw.size >= 4U) {
        known = 0;
        for (i = 0U; i < sizeof(encrypted) / sizeof(encrypted[0]); ++i)
            if (memcmp(raw.data, encrypted[i], 4U) == 0)
                known = 1;
        if (!known)
            status = omc_exif_emit_derived_exif_text(
                ctx, "mk_nikon_colorbalanceunknown2_0", 0U, 0U, raw.data, 4U);
    }
    free(copy);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    return 1;
}

static int omc_exif_decode_nikon_postpass(omc_exif_ctx *ctx, const omc_u8 *raw,
                                          omc_u64 raw_size)
{
    if (!omc_exif_decode_nikon_shotinfo(ctx))
        return 0;
    if (!omc_exif_decode_nikon_extra_versions(ctx))
        return 0;
    if (!omc_exif_decode_nikon_vrinfo(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_nikon_binary_subdirs(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_nikon_lensdata(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_nikon_colorbalancec(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_nikon_settings(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_nikon_aftune(ctx)) {
        return 0;
    }
    return omc_exif_decode_nikon_preview_ifd(ctx, raw, raw_size);
}

static int
omc_exif_decode_canon_afinfo2(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    char ifd_name[64];
    omc_exif_status status;
    omc_u8* raw_copy;
    omc_u16 size_bytes;
    omc_u16 num_points;
    omc_u16 value16;
    omc_u32 i;
    omc_u32 count;
    omc_u32 off_bytes;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw_copy = (omc_u8*)0;
    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x0026U);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &raw_copy, &raw);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    if (raw.size < 16U) {
        free(raw_copy);
        return 1;
    }
    if (!omc_exif_read_u16le_raw(raw.data, raw.size, 0U, &size_bytes)
        || size_bytes > raw.size
        || !omc_exif_read_u16le_raw(raw.data, raw.size, 4U, &num_points)
        || num_points == 0U
        || !omc_exif_make_subifd_name("mk_canon", "afinfo2", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        free(raw_copy);
        return 1;
    }

    count = num_points;
    if (raw.size < (omc_u64)(16U + (count * 8U) + 4U)) {
        free(raw_copy);
        return 1;
    }

    for (i = 0U; i < 8U; ++i) {
        if (!omc_exif_read_u16le_raw(raw.data, raw.size, (omc_u64)i * 2U,
                                     &value16)) {
            continue;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, (omc_u16)i, i,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name,
                                                (omc_u16)(0x2600U + i),
                                                8U + i, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }

    off_bytes = 16U;
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x0008U, 16U, OMC_ELEM_U16, raw.data + off_bytes,
        count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x0009U, 17U, OMC_ELEM_U16,
        raw.data + off_bytes + (count * 2U), count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x000AU, 18U, OMC_ELEM_I16,
        raw.data + off_bytes + (count * 4U), count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x000BU, 19U, OMC_ELEM_I16,
        raw.data + off_bytes + (count * 6U), count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x2608U, 20U, OMC_ELEM_U16, raw.data + off_bytes,
        count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x2609U, 21U, OMC_ELEM_U16,
        raw.data + off_bytes + (count * 2U), count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x260AU, 22U, OMC_ELEM_I16,
        raw.data + off_bytes + (count * 4U), count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        free(raw_copy);
        return 0;
    }
    status = omc_exif_emit_derived_exif_array_copy(
        ctx, ifd_name, 0x260BU, 23U, OMC_ELEM_I16,
        raw.data + off_bytes + (count * 6U), count * 2U, count);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    if (omc_exif_read_u16le_raw(raw.data, raw.size,
                                (omc_u64)(8U + (count * 4U)) * 2U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x000CU, 24U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x260CU, 25U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw.data, raw.size,
                                (omc_u64)(8U + (count * 4U) + 1U) * 2U,
                                &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x000DU, 26U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x260DU, 27U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw.data, raw.size,
                                (omc_u64)(8U + (count * 4U) + 2U) * 2U,
                                &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x000EU, 28U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x260EU, 29U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            free(raw_copy);
            return 0;
        }
    }

    free(raw_copy);
    return 1;
}

static int omc_exif_decode_canon_custom_functions2_bytes(omc_exif_ctx *ctx,
                                                         omc_const_bytes raw)
{
    char ifd_name[80];
    omc_exif_status status;
    omc_u16 total_size16;
    omc_u64 end;
    omc_u64 pos;
    omc_u64 rec_end;
    omc_u64 payload_off;
    omc_u64 payload_bytes;
    omc_u32 rec_len;
    omc_u32 rec_count;
    omc_u32 tag32;
    omc_u32 num;
    omc_u32 value32;
    omc_u32 order;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    if (raw.size < 20U)
        return 1;
    if (!omc_exif_read_u16le_raw(raw.data, raw.size, 0U, &total_size16)
        || !omc_exif_make_subifd_name("mk_canoncustom", "functions2", 0U,
                                      ifd_name, sizeof(ifd_name))) {
        return 1;
    }

    end = raw.size;
    if ((omc_u64)total_size16 >= 8U && (omc_u64)total_size16 < end) {
        end = total_size16;
    }
    pos = 8U;
    order = 0U;
    while (pos + 12U <= end) {
        if (!omc_exif_read_u32le_raw(raw.data, raw.size, pos + 4U, &rec_len)
            || !omc_exif_read_u32le_raw(raw.data, raw.size, pos + 8U,
                                        &rec_count)) {
            break;
        }
        if (rec_len < 8U) {
            break;
        }

        pos += 12U;
        rec_end = pos + ((omc_u64)rec_len - 8U);
        if (rec_end > end) {
            break;
        }

        for (i = 0U; i < rec_count && (pos + 8U) <= rec_end; ++i) {
            if (!omc_exif_read_u32le_raw(raw.data, raw.size, pos + 0U, &tag32)
                || !omc_exif_read_u32le_raw(raw.data, raw.size, pos + 4U,
                                            &num)) {
                break;
            }
            if (tag32 == 0x070cU && num == 0x66U &&
                pos + 8U + (omc_u64)num * 4U + 8U < rec_end) {
                omc_u32 next_tag;
                if (omc_exif_read_u32le_raw(raw.data, raw.size,
                                            pos + 12U + (omc_u64)num * 4U, &next_tag) &&
                    next_tag == 0x070fU)
                    num++;
            }
            payload_off = pos + 8U;
            payload_bytes = (omc_u64)num * 4U;
            if (num == 0U) {
                pos = payload_off;
                continue;
            }
            if (tag32 > 0xFFFFU || payload_off > rec_end
                || payload_bytes > (rec_end - payload_off)) {
                break;
            }

            if (num == 1U
                && omc_exif_read_u32le_raw(raw.data, raw.size, payload_off,
                                           &value32)) {
                status = omc_exif_emit_derived_exif_u32(
                    ctx, ifd_name, (omc_u16)tag32, order++, value32);
            } else {
                status = omc_exif_emit_derived_exif_array_copy(
                    ctx, ifd_name, (omc_u16)tag32, order++, OMC_ELEM_U32,
                    raw.data + (omc_size)payload_off, (omc_u32)payload_bytes,
                    num);
            }
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
            pos = payload_off + payload_bytes;
        }

        pos = rec_end;
    }

    return 1;
}

static int omc_exif_decode_canon_custom_functions2(omc_exif_ctx *ctx)
{
    const omc_entry *entry;
    omc_u8 *copy;
    omc_const_bytes raw;
    omc_exif_status status;
    int ok;
    copy = NULL;
    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x99U);
    status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    ok = omc_exif_decode_canon_custom_functions2_bytes(ctx, raw);
    free(copy);
    return ok;
}

static omc_u32
omc_exif_canon_colordata_family(omc_u32 count, omc_s16 version)
{
    switch (count) {
    case 582U: return 1U;
    case 653U: return 2U;
    case 796U: return 3U;
    case 674U:
    case 692U:
    case 702U:
    case 1227U:
    case 1250U:
    case 1251U:
    case 1337U:
    case 1338U:
    case 1346U: return 4U;
    case 5120U: return 5U;
    case 1273U:
    case 1275U: return 6U;
    case 1312U:
    case 1313U:
    case 1316U:
    case 1506U: return 7U;
    case 1353U:
    case 1560U:
    case 1592U:
    case 1602U: return 8U;
    case 1816U:
    case 1820U:
    case 1824U: return 9U;
    case 2024U:
    case 3656U: return 10U;
    case 3973U: return 11U;
    case 3778U: return (version == (omc_s16)65) ? 12U : 11U;
    case 4528U: return 12U;
    default: return 0U;
    }
}

static const char*
omc_exif_canon_colordata_family_name(omc_u32 family)
{
    switch (family) {
    case 1U: return "colordata1";
    case 2U: return "colordata2";
    case 3U: return "colordata3";
    case 4U: return "colordata4";
    case 5U: return "colordata5";
    case 6U: return "colordata6";
    case 7U: return "colordata7";
    case 8U: return "colordata8";
    case 9U: return "colordata9";
    case 10U: return "colordata10";
    case 11U: return "colordata11";
    case 12U: return "colordata12";
    default: return (const char*)0;
    }
}

static int
omc_exif_emit_derived_exif_u16_word_table_ref(omc_exif_ctx* ctx,
                                              omc_byte_ref bytes_ref,
                                              omc_u32 word_off,
                                              omc_u32 word_count,
                                              omc_u16 tag_base,
                                              omc_u32 order_base,
                                              const char* ifd_name)
{
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || ifd_name == (const char*)0) {
        return 1;
    }

    for (i = 0U; i < word_count; ++i) {
        omc_const_bytes bytes;
        omc_u16 value16;
        omc_u64 off;
        omc_u32 tag32;
        omc_exif_status status;

        tag32 = (omc_u32)tag_base + i;
        if (tag32 > 0xFFFFU) {
            break;
        }

        bytes = omc_arena_view(&ctx->store->arena, bytes_ref);
        off = (omc_u64)(word_off + i) * 2U;
        if (!omc_exif_read_u16le_raw(bytes.data, bytes.size, off, &value16)) {
            break;
        }

        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, (omc_u16)tag32,
                                                order_base + i, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_canon_ifd_has_prefix(const char* ifd_name, const char* prefix)
{
    omc_size prefix_len;

    if (ifd_name == (const char*)0 || prefix == (const char*)0) {
        return 0;
    }

    prefix_len = strlen(prefix);
    return strncmp(ifd_name, prefix, prefix_len) == 0;
}

static int
omc_exif_canon_colorcalib_tag_known(const char* ifd_name, omc_u16 tag)
{
    if (omc_exif_canon_ifd_has_prefix(ifd_name, "mk_canon_colorcalib2_")) {
        return tag <= 0x0046U && (tag % 5U) == 0U;
    }
    if (omc_exif_canon_ifd_has_prefix(ifd_name, "mk_canon_colorcalib_")) {
        return tag <= 0x0038U && (tag % 4U) == 0U;
    }
    return 0;
}

static int
omc_exif_emit_canon_named_u16_word_table_ref(omc_exif_ctx* ctx,
                                             omc_byte_ref bytes_ref,
                                             omc_u32 word_off,
                                             omc_u32 word_count,
                                             omc_u16 tag_base,
                                             omc_u32 order_base,
                                             const char* ifd_name)
{
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || ifd_name == (const char*)0) {
        return 1;
    }

    for (i = 0U; i < word_count; ++i) {
        omc_const_bytes bytes;
        omc_u16 value16;
        omc_u64 off;
        omc_u32 tag32;
        omc_exif_status status;

        tag32 = (omc_u32)tag_base + i;
        if (tag32 > 0xFFFFU) {
            break;
        }
        if (!omc_exif_canon_colorcalib_tag_known(ifd_name, (omc_u16)tag32)) {
            continue;
        }

        bytes = omc_arena_view(&ctx->store->arena, bytes_ref);
        off = (omc_u64)(word_off + i) * 2U;
        if (!omc_exif_read_u16le_raw(bytes.data, bytes.size, off, &value16)) {
            break;
        }

        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, (omc_u16)tag32,
                                                order_base + i, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_emit_derived_exif_u16_word_table_ref_skip_zero(omc_exif_ctx* ctx,
                                                        omc_byte_ref bytes_ref,
                                                        omc_u32 word_off,
                                                        omc_u32 word_count,
                                                        omc_u16 tag_base,
                                                        omc_u32 order_base,
                                                        const char* ifd_name)
{
    omc_u32 i;
    omc_u32 order;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || ifd_name == (const char*)0) {
        return 1;
    }

    order = order_base;
    for (i = 0U; i < word_count; ++i) {
        omc_const_bytes bytes;
        omc_u16 value16;
        omc_u64 off;
        omc_u32 tag32;
        omc_exif_status status;

        tag32 = (omc_u32)tag_base + i;
        if (tag32 > 0xFFFFU) {
            break;
        }

        bytes = omc_arena_view(&ctx->store->arena, bytes_ref);
        off = (omc_u64)(word_off + i) * 2U;
        if (!omc_exif_read_u16le_raw(bytes.data, bytes.size, off, &value16)) {
            break;
        }
        if (value16 == 0U) {
            continue;
        }

        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, (omc_u16)tag32,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static omc_s32
omc_exif_u32_to_i32(omc_u32 value32)
{
    if (value32 <= 0x7FFFFFFFU) {
        return (omc_s32)value32;
    }
    return (omc_s32)(-1 - (omc_s32)(0xFFFFFFFFU - value32));
}

static int
omc_exif_bytes_contains_text(const omc_u8* bytes, omc_u32 size,
                             const char* needle)
{
    omc_u32 i;
    omc_u32 needle_size;

    if (bytes == (const omc_u8*)0 || needle == (const char*)0) {
        return 0;
    }

    needle_size = (omc_u32)strlen(needle);
    if (needle_size == 0U || needle_size > size) {
        return 0;
    }

    for (i = 0U; i + needle_size <= size; ++i) {
        if (memcmp(bytes + i, needle, needle_size) == 0) {
            return 1;
        }
    }

    return 0;
}

static int
omc_exif_entry_ifd_equals(const omc_store* store, const omc_entry* entry,
                          const char* ifd_name)
{
    omc_const_bytes ifd_view;
    omc_size ifd_name_size;

    if (store == (const omc_store*)0 || entry == (const omc_entry*)0
        || ifd_name == (const char*)0 || entry->key.kind != OMC_KEY_EXIF_TAG) {
        return 0;
    }

    ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
    ifd_name_size = strlen(ifd_name);
    return ifd_view.size == ifd_name_size
           && memcmp(ifd_view.data, ifd_name, ifd_name_size) == 0;
}

static int
omc_exif_entry_ifd_starts_with(const omc_store* store, const omc_entry* entry,
                               const char* prefix)
{
    omc_const_bytes ifd_view;
    omc_size prefix_size;

    if (store == (const omc_store*)0 || entry == (const omc_entry*)0
        || prefix == (const char*)0 || entry->key.kind != OMC_KEY_EXIF_TAG) {
        return 0;
    }

    ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
    prefix_size = strlen(prefix);
    return ifd_view.size >= prefix_size
           && memcmp(ifd_view.data, prefix, prefix_size) == 0;
}

static int
omc_exif_canon_model_matches_any(const omc_u8* model, omc_u32 model_size,
                                 const char* const* needles,
                                 omc_u32 needle_count)
{
    omc_u32 i;

    if (model == (const omc_u8*)0 || needles == (const char* const*)0) {
        return 0;
    }
    for (i = 0U; i < needle_count; ++i) {
        if (omc_exif_bytes_contains_text(model, model_size, needles[i])) {
            return 1;
        }
    }
    return 0;
}

static int
omc_exif_canon_model_is_1d_family(const omc_u8* model, omc_u32 model_size)
{
    static const char* const k_needles[] = {
        "EOS-1D",
        "EOS-1DS"
    };
    return omc_exif_canon_model_matches_any(model, model_size, k_needles, 2U);
}

static int
omc_exif_canon_model_is_1ds(const omc_u8* model, omc_u32 model_size)
{
    return omc_exif_bytes_contains_text(model, model_size, "EOS-1DS");
}

static int
omc_exif_canon_model_is_early_kelvin_group(const omc_u8* model,
                                           omc_u32 model_size)
{
    static const char* const k_needles[] = {
        "EOS 10D",
        "EOS 300D",
        "EOS DIGITAL REBEL",
        "EOS Kiss Digital"
    };
    return omc_exif_canon_model_matches_any(model, model_size, k_needles, 4U);
}

static int
omc_exif_canon_model_is_1100d_blacklevel_group(const omc_u8* model,
                                               omc_u32 model_size)
{
    static const char* const k_needles[] = {
        "EOS 1100D",
        "EOS Kiss X50",
        "EOS REBEL T3",
        "EOS 60D"
    };
    return omc_exif_canon_model_matches_any(model, model_size, k_needles, 4U);
}

static int
omc_exif_canon_model_is_1100d_maxfocal_group(const omc_u8* model,
                                             omc_u32 model_size)
{
    static const char* const k_needles[] = {
        "EOS 1100D",
        "EOS Kiss X50",
        "EOS REBEL T3"
    };
    return omc_exif_canon_model_matches_any(model, model_size, k_needles, 3U);
}

static int
omc_exif_canon_model_is_1200d_wb_unknown7_group(const omc_u8* model,
                                                omc_u32 model_size)
{
    static const char* const k_needles[] = {
        "EOS 1200D",
        "EOS Kiss X70",
        "EOS REBEL T5"
    };
    return omc_exif_canon_model_matches_any(model, model_size, k_needles, 3U);
}

static int
omc_exif_canon_model_is_r1_r5m2_battery_group(const omc_u8* model,
                                              omc_u32 model_size)
{
    static const char* const k_needles[] = {
        "EOS R1",
        "EOS R5m2",
        "EOS R5 Mark II"
    };
    return omc_exif_canon_model_matches_any(model, model_size, k_needles, 3U);
}

static int
omc_exif_nikon_model_is_d7500(const omc_u8* model, omc_u32 model_size)
{
    return model != (const omc_u8*)0
           && model_size == 11U
           && memcmp(model, "NIKON D7500", 11U) == 0;
}

static int
omc_exif_nikon_model_is_d780(const omc_u8* model, omc_u32 model_size)
{
    return model != (const omc_u8*)0
           && model_size == 10U
           && memcmp(model, "NIKON D780", 10U) == 0;
}

static int
omc_exif_nikon_model_is_d850(const omc_u8* model, omc_u32 model_size)
{
    return model != (const omc_u8*)0
           && model_size == 10U
           && memcmp(model, "NIKON D850", 10U) == 0;
}

static int
omc_exif_nikon_model_is_z30(const omc_u8* model, omc_u32 model_size)
{
    return model != (const omc_u8*)0
           && model_size == 10U
           && memcmp(model, "NIKON Z 30", 10U) == 0;
}

static int
omc_exif_nikon_model_is_z5(const omc_u8* model, omc_u32 model_size)
{
    return model != (const omc_u8*)0
           && model_size == 9U
           && memcmp(model, "NIKON Z 5", 9U) == 0;
}

static int
omc_exif_nikon_model_is_d6(const omc_u8* model, omc_u32 model_size)
{
    return model != (const omc_u8*)0
           && model_size == 8U
           && memcmp(model, "NIKON D6", 8U) == 0;
}

static int
omc_exif_nikon_main_model_is_legacy_compact_type2(const omc_u8* model,
                                                  omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model, model_size, "E700")
           || omc_exif_ascii_equals_nocase(model, model_size, "E800")
           || omc_exif_ascii_equals_nocase(model, model_size, "E900")
           || omc_exif_ascii_equals_nocase(model, model_size, "E950");
}

static int
omc_exif_nikon_main_model_is_z_family(const omc_u8* model, omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model, model_size, "NIKON Z fc")
           || omc_exif_ascii_equals_nocase(model, model_size, "NIKON Z f")
           || omc_exif_ascii_starts_with_nocase(model, model_size, "NIKON Z ")
           || omc_exif_ascii_starts_with_nocase(model, model_size, "NIKON Z5")
           || omc_exif_ascii_starts_with_nocase(model, model_size, "NIKON Z6")
           || omc_exif_ascii_starts_with_nocase(model, model_size, "NIKON Z7")
           || omc_exif_ascii_starts_with_nocase(model, model_size, "NIKON Z50");
}

static int
omc_exif_nikon_main_z_tag_prefers_compat_name(omc_u16 tag)
{
    switch (tag) {
    case 0x002BU:
    case 0x002CU:
    case 0x002EU:
    case 0x002FU:
    case 0x0031U:
    case 0x0032U:
    case 0x0035U:
        return 1;
    default:
        return 0;
    }
}

static void
omc_exif_mark_last_contextual_name(omc_exif_ctx* ctx,
                                   omc_entry_name_ctx_kind kind,
                                   omc_u8 variant)
{
    omc_entry* entry;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || ctx->store->entry_count == 0U || kind == OMC_ENTRY_NAME_CTX_NONE
        || variant == 0U) {
        return;
    }

    entry = &ctx->store->entries[ctx->store->entry_count - 1U];
    entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
    entry->origin.name_context_kind = kind;
    entry->origin.name_context_variant = variant;
}

static omc_u8
omc_exif_nikonsettings_context_variant(omc_u16 tag, const omc_u8* model,
                                       omc_u32 model_size)
{
    const int d7500 = omc_exif_nikon_model_is_d7500(model, model_size);
    const int d780 = omc_exif_nikon_model_is_d780(model, model_size);
    const int d850 = omc_exif_nikon_model_is_d850(model, model_size);
    const int z30 = omc_exif_nikon_model_is_z30(model, model_size);
    const int z5 = omc_exif_nikon_model_is_z5(model, model_size);
    const int d6 = omc_exif_nikon_model_is_d6(model, model_size);

    switch (tag) {
    case 0x0104U:
    case 0x010CU:
    case 0x013AU:
    case 0x013CU: return 1U;
    case 0x0001U: return (omc_u8)((d7500 || d780 || z5 || z30) ? 1U : 0U);
    case 0x0002U:
    case 0x000DU: return (omc_u8)((d7500 || d780) ? 1U : 0U);
    case 0x001DU:
    case 0x0020U:
    case 0x002DU:
    case 0x0034U:
    case 0x0047U:
    case 0x0052U:
    case 0x0053U:
    case 0x0054U:
    case 0x006CU:
        if (tag == 0x001DU) {
            return (omc_u8)((d7500 || d780 || d850 || z30) ? 1U : 0U);
        }
        return (omc_u8)((d7500 || d780) ? 1U : 0U);
    case 0x0080U: return (omc_u8)((d7500 || d780 || d850 || z30) ? 1U : 0U);
    case 0x0097U:
    case 0x00A0U:
    case 0x00A2U:
    case 0x00A3U:
    case 0x00A5U:
    case 0x00A7U:
    case 0x00B6U: return (omc_u8)((d780 || d850 || z30) ? 1U : 0U);
    case 0x00B1U:
        if (z5) {
            return 2U;
        }
        return (omc_u8)((d7500 || d780 || d850 || z30) ? 1U : 0U);
    case 0x00B3U:
        if (z5) {
            return 3U;
        }
        return (omc_u8)((d850 || z30) ? 1U : 0U);
    case 0x0103U: return (omc_u8)(d6 ? 1U : 0U);
    default: return 0U;
    }
}

static int
omc_exif_fujifilm_prefers_placeholder(omc_u16 tag, const omc_u8* make,
                                      omc_u32 make_size)
{
    if (make != (const omc_u8*)0
        && omc_exif_ascii_starts_with_nocase(make, make_size,
                                             "GENERAL IMAGING")) {
        return 0;
    }

    switch (tag) {
    case 0x1051U:
    case 0x1150U:
    case 0x1151U:
    case 0x1152U:
    case 0x1304U:
    case 0x144AU:
    case 0x144BU:
    case 0x144CU: return 1;
    default: return 0;
    }
}

static int sony_model_is_stellar(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model, model_size, "Stellar");
}

static int sony_model_is_hasselblad_hv(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model, model_size, "HV");
}

static int sony_model_is_dsc(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_starts_with_nocase(model, model_size, "DSC-");
}

static int sony_model_is_zv(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_starts_with_nocase(model, model_size, "ZV-");
}

static int sony_model_is_slt(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_starts_with_nocase(model, model_size, "SLT-");
}

static int sony_model_is_ilca(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_starts_with_nocase(model, model_size, "ILCA-");
}

static int sony_model_is_ilca_68_or_77m2(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model, model_size, "ILCA-68") ||
           omc_exif_ascii_equals_nocase(model, model_size, "ILCA-77M2");
}

static int sony_main_201d_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    if (omc_exif_ascii_starts_with_nocase(model, model_size, "NEX-") ||
        omc_exif_ascii_starts_with_nocase(model, model_size, "ILCE-") ||
        omc_exif_ascii_starts_with_nocase(model, model_size, "ILME-") ||
        sony_model_is_zv(model, model_size)) {
        return 1;
    }
    return omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX10M4") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX100M6") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX100M7") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX100M5A") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-HX95") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-HX99") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX0M2");
}

static int sony_main_focus_area_uses_semantic_name(const omc_u8 *model,
                                                   omc_u32 model_size)
{
    if (omc_exif_ascii_starts_with_nocase(model, model_size, "NEX-") ||
        omc_exif_ascii_starts_with_nocase(model, model_size, "ILCE-") ||
        omc_exif_ascii_starts_with_nocase(model, model_size, "ILME-") ||
        sony_model_is_zv(model, model_size) || sony_model_is_slt(model, model_size) ||
        sony_model_is_ilca(model, model_size) ||
        sony_model_is_hasselblad_hv(model, model_size)) {
        return 1;
    }
    return omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX10M4") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX100M6") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX100M7") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX100M5A") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-HX95") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-HX99") ||
           omc_exif_ascii_equals_nocase(model, model_size, "DSC-RX0M2");
}

static int sony_model_is_model_name_placeholder(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model, model_size, "MODEL-NAME");
}

static int sony_main_201b_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    if (sony_model_is_model_name_placeholder(model, model_size)) {
        return 1;
    }
    return sony_main_focus_area_uses_semantic_name(model, model_size);
}

static int sony_main_201c_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    return sony_main_focus_area_uses_semantic_name(model, model_size);
}

static int sony_main_201e_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    return !sony_model_is_model_name_placeholder(model, model_size);
}

static int sony_main_2021_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    if (sony_model_is_model_name_placeholder(model, model_size)) {
        return 1;
    }
    return sony_main_focus_area_uses_semantic_name(model, model_size);
}

static int sony_main_2020_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    if (sony_model_is_ilca_68_or_77m2(model, model_size)) {
        return 1;
    }
    return !sony_model_is_ilca(model, model_size) &&
           !sony_model_is_dsc(model, model_size) &&
           !sony_model_is_zv(model, model_size);
}

static int sony_main_2022_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    return omc_exif_ascii_equals_nocase(model, model_size, "ILCE-5100") ||
           omc_exif_ascii_equals_nocase(model, model_size, "ILCE-6000") ||
           omc_exif_ascii_equals_nocase(model, model_size, "ILCE-7M2") ||
           omc_exif_ascii_equals_nocase(model, model_size, "ILCE-7RM2");
}

static int sony_main_b050_uses_semantic_name(const omc_u8 *model, omc_u32 model_size)
{
    return sony_model_is_dsc(model, model_size) ||
           sony_model_is_stellar(model, model_size);
}

static int canon_custom_functions2_0701_prefers_shutter_button(const omc_u8 *model,
                                                               omc_u32 size)
{
    static const char *const models[] = {
        "EOS 40D",        "EOS 50D",
        "EOS 5D Mark II", "EOS-1D Mark III",
        "EOS-1D Mark IV", "EOS-1Ds Mark III",
        "EOS 450D",       "EOS 650D",
        "EOS 700D",       "EOS 750D",
        "EOS 760D",       "EOS 8000D",
        "EOS 100D",       "EOS 1200D",
        "EOS 1300D",      "EOS 2000D",
        "EOS 4000D",      "EOS M",
        "EOS M2",         "EOS Rebel SL1",
        "EOS Rebel T4i",  "EOS Rebel T5",
        "EOS Rebel T5i",  "EOS Rebel T6",
        "EOS Rebel T6i",  "EOS Rebel T6s",
        "EOS Rebel T7",   "EOS DIGITAL REBEL XSi",
        "EOS Kiss X2",    "EOS Kiss X6i",
        "EOS Kiss X7",    "EOS Kiss X7i",
        "EOS Kiss X8i",   "EOS Kiss X70",
        "EOS Kiss X90",
    };
    omc_u32 i;
    for (i = 0U; i < sizeof(models) / sizeof(models[0]); ++i)
        if (omc_exif_ascii_contains_nocase(model, size, models[i]))
            return 1;
    return 0;
}

static int canon_custom_functions2_010c_prefers_placeholder(const omc_u8 *model,
                                                            omc_u32 size)
{
    static const char *const models[] = {
        "EOS R10",  "EOS R7",          "EOS R8",   "EOS R1",
        "EOS R3",   "EOS R5 Mark II",  "EOS R5m2", "EOS R6 Mark II",
        "EOS R6m2", "EOS R6 Mark III", "EOS C50",  "PowerShot V10",
    };
    omc_u32 i;
    for (i = 0U; i < sizeof(models) / sizeof(models[0]); ++i)
        if (omc_exif_ascii_contains_nocase(model, size, models[i]))
            return 1;
    return 0;
}

static int canon_custom_functions2_0701_prefers_af_and_metering(const omc_u8 *model,
                                                                omc_u32 size)
{
    static const char *const models[] = {
        "EOS 60D",
    };
    omc_u32 i;
    for (i = 0U; i < sizeof(models) / sizeof(models[0]); ++i)
        if (omc_exif_ascii_contains_nocase(model, size, models[i]))
            return 1;
    return 0;
}

static int
canon_custom_functions2_0510_prefers_superimposed_display(const omc_u8 *model,
                                                          omc_u32 size)
{
    static const char *const models[] = {
        "EOS 40D", "EOS 50D", "EOS 60D", "EOS 70D", "EOS 6D", "EOS 5D Mark II",
    };
    omc_u32 i;
    for (i = 0U; i < sizeof(models) / sizeof(models[0]); ++i)
        if (omc_exif_ascii_contains_nocase(model, size, models[i]))
            return 1;
    return 0;
}

static int canon_model_is_colordata7_psinfo2_group(const omc_u8 *model, omc_u32 size)
{
    static const char *const models[] = {
        "EOS Kiss X7i",
        "EOS-1D X",
    };
    omc_u32 i;
    for (i = 0U; i < sizeof(models) / sizeof(models[0]); ++i)
        if (omc_exif_ascii_contains_nocase(model, size, models[i]))
            return 1;
    return 0;
}

static void
omc_exif_maybe_mark_contextual_name(const omc_exif_ctx* ctx, omc_entry* entry)
{
    const omc_u8* model_text;
    const omc_u8* make_text;
    omc_u32 model_size;
    omc_u32 make_size;

    if (ctx == (const omc_exif_ctx*)0 || entry == (omc_entry*)0
        || ctx->store == (omc_store*)0 || entry->key.kind != OMC_KEY_EXIF_TAG) {
        return;
    }

    model_text = (const omc_u8*)0;
    make_text = (const omc_u8*)0;
    model_size = 0U;
    make_size = 0U;
    (void)omc_exif_find_first_text(ctx->store, "ifd0", 0x0110U, &model_text,
                                   &model_size);
    (void)omc_exif_find_first_text(ctx->store, "ifd0", 0x010FU, &make_text,
                                   &make_size);
    if (model_text == (const omc_u8*)0) {
        (void)omc_exif_find_outer_ifd0_ascii(ctx, 0x0110U, &model_text,
                                             &model_size);
    }
    if (make_text == (const omc_u8*)0) {
        (void)omc_exif_find_outer_ifd0_ascii(ctx, 0x010FU, &make_text,
                                             &make_size);
    }

    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_sony0")) {
        int placeholder = 0;
        while (model_size > 0U && (model_text[model_size - 1U] == 0U ||
                                   model_text[model_size - 1U] == ' '))
            model_size--;
        switch (entry->key.u.exif_tag.tag) {
        case 0x201bU:
            placeholder = !sony_main_201b_uses_semantic_name(model_text, model_size);
            break;
        case 0x201cU:
            placeholder = !sony_main_201c_uses_semantic_name(model_text, model_size);
            break;
        case 0x201dU:
        case 0xb042U:
        case 0xb043U:
            placeholder = !sony_main_201d_uses_semantic_name(model_text, model_size);
            break;
        case 0x201eU:
            placeholder = !sony_main_201e_uses_semantic_name(model_text, model_size);
            break;
        case 0x2020U:
            placeholder = !sony_main_2020_uses_semantic_name(model_text, model_size);
            break;
        case 0x2021U:
            placeholder = !sony_main_2021_uses_semantic_name(model_text, model_size);
            break;
        case 0x2022U:
            placeholder = !sony_main_2022_uses_semantic_name(model_text, model_size);
            break;
        case 0x205cU:
            placeholder = 1;
            break;
        case 0xb050U:
            placeholder = !sony_main_b050_uses_semantic_name(model_text, model_size);
            break;
        default:
            break;
        }
        if (placeholder) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_SONY_MAIN_COMPAT;
            entry->origin.name_context_variant = 1U;
        }
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_fuji0")
        && omc_exif_fujifilm_prefers_placeholder(entry->key.u.exif_tag.tag,
                                                 make_text, make_size)) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_FUJIFILM_MAIN_1304;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_nikon0")) {
        if (omc_exif_nikon_main_model_is_legacy_compact_type2(model_text,
                                                              model_size)) {
            switch (entry->key.u.exif_tag.tag) {
            case 0x0002U:
            case 0x0003U:
            case 0x0004U:
            case 0x0005U:
            case 0x0006U:
            case 0x0007U:
            case 0x0008U:
            case 0x0009U:
            case 0x000AU:
            case 0x000BU:
            case 0x0F00U:
                entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
                entry->origin.name_context_kind
                    = OMC_ENTRY_NAME_CTX_NIKON_MAIN_COMPACT_TYPE2;
                entry->origin.name_context_variant = 1U;
                return;
            default:
                break;
            }
        }
        if (omc_exif_nikon_main_model_is_z_family(model_text, model_size)
            && omc_exif_nikon_main_z_tag_prefers_compat_name(
                entry->key.u.exif_tag.tag)) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_NIKON_MAIN_Z;
            entry->origin.name_context_variant = 1U;
            return;
        }
    }
    if (omc_exif_entry_ifd_starts_with(ctx->store, entry,
                                       "mk_nikonsettings_main_")) {
        omc_u8 variant;

        variant = omc_exif_nikonsettings_context_variant(
            entry->key.u.exif_tag.tag, model_text, model_size);
        if (variant != 0U) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_NIKONSETTINGS_MAIN;
            entry->origin.name_context_variant = variant;
            return;
        }
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_kodak0")
        && entry->key.u.exif_tag.tag == 0x0028U
        && entry->value.kind == OMC_VAL_TEXT) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_KODAK_MAIN_0028;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_ricoh0")) {
        if ((entry->key.u.exif_tag.tag == 0x1002U
             || entry->key.u.exif_tag.tag == 0x1004U)
            && !(entry->origin.wire_type.family == OMC_WIRE_TIFF
                 && entry->origin.wire_type.code == 3U)) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_RICOH_MAIN_COMPAT;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (entry->key.u.exif_tag.tag == 0x1003U
            && entry->origin.wire_type.family == OMC_WIRE_TIFF
            && entry->origin.wire_type.code == 3U) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_RICOH_MAIN_COMPAT;
            entry->origin.name_context_variant = 2U;
            return;
        }
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_olympus_focusinfo_0")
        && entry->key.u.exif_tag.tag == 0x1600U) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind
            = OMC_ENTRY_NAME_CTX_OLYMPUS_FOCUSINFO_1600;
        if (omc_exif_find_first_entry(ctx->store, "mk_olympus_camerasettings_0",
                                      0x0604U)
            != (const omc_entry*)0) {
            entry->origin.name_context_variant = 2U;
        } else {
            entry->origin.name_context_variant = 1U;
        }
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_motorola0")
        && entry->key.u.exif_tag.tag == 0x6420U
        && omc_exif_motorola_model_prefers_placeholder(model_text,
                                                       model_size)) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind
            = OMC_ENTRY_NAME_CTX_MOTOROLA_MAIN_6420;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_sigma0")) {
        int prefer_placeholder;

        prefer_placeholder = 0;
        if (entry->key.u.exif_tag.tag == 0x0033U
            && omc_exif_sigma_main_0033_prefers_placeholder(model_text,
                                                            model_size)) {
            prefer_placeholder = 1;
        }
        if (entry->key.u.exif_tag.tag == 0x0034U
            && (omc_exif_sigma_main_0033_prefers_placeholder(model_text,
                                                             model_size)
                || omc_exif_sigma_main_sd_quattro_h_prefers_placeholder(
                    model_text, model_size, entry->key.u.exif_tag.tag))) {
            prefer_placeholder = 1;
        }
        if (omc_exif_sigma_main_fp_l_prefers_placeholder(
                model_text, model_size, entry->key.u.exif_tag.tag)
            || omc_exif_sigma_main_sd_quattro_h_prefers_placeholder(
                model_text, model_size, entry->key.u.exif_tag.tag)) {
            prefer_placeholder = 1;
        }
        if (prefer_placeholder) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_SIGMA_MAIN_COMPAT;
            entry->origin.name_context_variant = 1U;
            return;
        }
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_pentax0")
        && entry->key.u.exif_tag.tag == 0x0062U
        && omc_exif_ascii_contains_nocase(make_text, make_size, "kodak")) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_PENTAX_MAIN_0062;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_pentax0")
        && entry->key.u.exif_tag.tag == 0x0062U
        && omc_exif_ascii_contains_nocase(make_text, make_size, "samsung")) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_PENTAX_MAIN_0062;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_canon_colordata7_0") &&
        canon_model_is_colordata7_psinfo2_group(model_text, model_size)) {
        omc_u8 variant = 0U;
        switch (entry->key.u.exif_tag.tag) {
        case 0xe4U:
            variant = 1U;
            break;
        case 0xe8U:
            variant = 2U;
            break;
        case 0xecU:
            variant = 3U;
            break;
        case 0xf0U:
            variant = 4U;
            break;
        case 0xf2U:
            variant = 5U;
            break;
        default:
            break;
        }
        if (variant != 0U) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind =
                OMC_ENTRY_NAME_CTX_CANON_COLORDATA7_PSINFO2;
            entry->origin.name_context_variant = variant;
            return;
        }
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_canon0") &&
        entry->key.u.exif_tag.tag == 0x0038U && entry->value.kind == OMC_VAL_BYTES &&
        entry->origin.wire_type.code == 7U && entry->origin.wire_count <= 8U) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_CANON_MAIN_0038;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_canon_shotinfo_0")
        && entry->key.u.exif_tag.tag == 0x000EU
        && omc_exif_canon_model_is_1d_family(model_text, model_size)) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind
            = OMC_ENTRY_NAME_CTX_CANON_SHOTINFO_000E;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry,
                                  "mk_canon_camerasettings_0")
        && entry->key.u.exif_tag.tag == 0x0021U
        && omc_exif_canon_model_is_early_kelvin_group(model_text,
                                                      model_size)) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_CANON_CAMSET_0021;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_canon_colordata4_0")) {
        if (entry->key.u.exif_tag.tag == 0x00EAU
            && omc_exif_canon_model_is_1200d_wb_unknown7_group(model_text,
                                                               model_size)) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_CANON_COLORDATA4_00EA;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (entry->key.u.exif_tag.tag == 0x00EEU
            && omc_exif_canon_model_is_1100d_maxfocal_group(model_text,
                                                            model_size)) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_CANON_COLORDATA4_00EE;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (entry->key.u.exif_tag.tag == 0x02CFU
            && omc_exif_canon_model_is_1100d_blacklevel_group(model_text,
                                                              model_size)) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_CANON_COLORDATA4_02CF;
            entry->origin.name_context_variant = 1U;
            return;
        }
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_canon_colorcalib_0")
        && entry->key.u.exif_tag.tag == 0x0038U
        && omc_exif_canon_model_is_r1_r5m2_battery_group(model_text,
                                                         model_size)) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind
            = OMC_ENTRY_NAME_CTX_CANON_COLORCALIB_0038;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry, "mk_canon_camerainfo1d_0")
        && entry->key.u.exif_tag.tag == 0x0048U
        && omc_exif_canon_model_is_1ds(model_text, model_size)) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind
            = OMC_ENTRY_NAME_CTX_CANON_CAMERAINFO1D_0048;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry,
                                  "mk_canon_camerainfo600d_0")
        && entry->key.u.exif_tag.tag == 0x00EAU) {
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind
            = OMC_ENTRY_NAME_CTX_CANON_CAMERAINFO600D_00EA;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (omc_exif_entry_ifd_equals(ctx->store, entry,
                                  "mk_canoncustom_functions2_0")) {
        if (entry->key.u.exif_tag.tag == 0x0103U) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0103;
            entry->origin.name_context_variant
                = (entry->origin.wire_count > 1U) ? 2U : 1U;
            return;
        }
        if (entry->key.u.exif_tag.tag == 0x010CU && entry->value.count > 1U &&
            entry->value.count != 3U &&
            canon_custom_functions2_010c_prefers_placeholder(model_text, model_size)) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_010C;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (entry->key.u.exif_tag.tag == 0x0510U &&
            canon_custom_functions2_0510_prefers_superimposed_display(model_text,
                                                                      model_size)) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0510;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (entry->key.u.exif_tag.tag == 0x0701U &&
            (canon_custom_functions2_0701_prefers_af_and_metering(model_text,
                                                                  model_size) ||
             canon_custom_functions2_0701_prefers_shutter_button(model_text,
                                                                 model_size))) {
            entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
            entry->origin.name_context_kind
                = OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0701;
            entry->origin.name_context_variant =
                canon_custom_functions2_0701_prefers_af_and_metering(model_text,
                                                                     model_size)
                    ? 2U
                    : 1U;
            return;
        }
    }
}

static const char*
omc_exif_canon_camerainfo_subtable(const omc_u8* model, omc_u32 model_size,
                                   const omc_u8* raw, omc_u64 raw_size,
                                   int is_u32_table)
{
    omc_u16 entry_count;
    omc_u16 tag16;

    if (omc_exif_ascii_contains_nocase(model, model_size, "EOS-1D X"))
        return "camerainfo1dx";
    if (model != (const omc_u8*)0 && model_size != 0U) {
        if (omc_exif_bytes_contains_text(model, model_size, "5DS")
            || omc_exif_bytes_contains_text(model, model_size, "EOS R1")) {
            return "camerainfounknown";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "PowerShot S1")) {
            return is_u32_table ? "camerainfounknown32" : "camerainfo";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "1100D")) {
            return "camerainfo1100d";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "Kiss X70")
            || omc_exif_bytes_contains_text(model, model_size, "600D")) {
            return "camerainfo600d";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "450D")) {
            return "camerainfo450d";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "EOS 7D")) {
            return "camerainfo7d";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "EOS 6D")) {
            return "camerainfo6d";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "EOS-1D Mark II N")) {
            return "camerainfo1dmkiin";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "EOS-1D Mark II")) {
            return "camerainfo1dmkii";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "EOS-1DS")
            || omc_exif_bytes_contains_text(model, model_size, "EOS-1D")) {
            return "camerainfo1d";
        }
        if (omc_exif_bytes_contains_text(model, model_size, "EOS 5D")) {
            return "camerainfo5d";
        }
    }

    if (is_u32_table) {
        return "camerainfo";
    }

    if (raw != (const omc_u8*)0 && raw_size >= 18U
        && omc_exif_read_u16le_raw(raw, raw_size, 0U, &entry_count)
        && entry_count != 0U
        && omc_exif_read_u16le_raw(raw, raw_size, 2U, &tag16)) {
        if (tag16 == 0x01ACU) {
            return "camerainfo7d";
        }
        if (tag16 == 0x0256U) {
            return "camerainfo6d";
        }
        if (tag16 == 0x0107U) {
            return "camerainfo450d";
        }
    }

    return "camerainfo";
}

static int
omc_exif_emit_canon_camerainfo_text_field(omc_exif_ctx* ctx, const char* ifd_name,
                                          omc_u16 tag, const omc_u8* raw,
                                          omc_u64 raw_size, omc_u32 width,
                                          omc_u32 order_in_block)
{
    omc_u32 text_size;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0
        || raw == (const omc_u8*)0 || ((omc_u64)tag + width) > raw_size) {
        return 1;
    }

    text_size = omc_exif_trim_nul_size(raw + tag, width);
    if (text_size == 0U) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_text(ctx, ifd_name, tag, order_in_block,
                                             raw + tag, text_size);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    return 1;
}

enum {
    OMC_CANON_CAM_FIELD_U8 = 1,
    OMC_CANON_CAM_FIELD_U16,
    OMC_CANON_CAM_FIELD_U16_REV,
    OMC_CANON_CAM_FIELD_U16_ARRAY4,
    OMC_CANON_CAM_FIELD_U32,
    OMC_CANON_CAM_FIELD_U32_ARRAY4,
    OMC_CANON_CAM_FIELD_ASCII_FIXED
};

typedef struct omc_exif_canon_camerainfo_field {
    omc_u16 tag;
    omc_u8 kind;
    omc_u16 bytes;
} omc_exif_canon_camerainfo_field;

static int
omc_exif_emit_canon_camerainfo_common_fields(omc_exif_ctx* ctx,
                                             const char* ifd_name,
                                             omc_byte_ref raw_ref,
                                             omc_u32* io_order)
{
    static const omc_exif_canon_camerainfo_field fields[] = {
        { 0x0018U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0022U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0026U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0027U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x002BU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x002CU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x002DU, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0031U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0035U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0036U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0037U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0039U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x003AU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x003BU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0045U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x004AU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x004FU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0059U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x005EU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0063U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x006DU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x006EU, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0072U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0077U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0081U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0086U, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x008BU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x009AU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x009FU, OMC_CANON_CAM_FIELD_U16_ARRAY4, 8U },
        { 0x0041U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0042U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0044U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0048U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x004BU, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0047U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x004AU, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x004EU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0051U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x006FU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0073U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x00DEU, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x00A5U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0095U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 64U },
        { 0x0107U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x010AU, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x010BU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x010CU, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x010FU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 32U },
        { 0x0110U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0133U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0136U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0137U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x013AU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x013FU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0143U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0111U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0113U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0115U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0112U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0114U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0116U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0127U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0129U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x012BU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0131U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0135U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0169U, OMC_CANON_CAM_FIELD_U8, 1U },
        { 0x0184U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0186U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0188U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0190U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0199U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x019BU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x01A4U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x01D3U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01D9U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01DBU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01E4U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01E7U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01EDU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x01F0U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01F7U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0201U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x021BU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0220U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0238U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x023CU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0256U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x025EU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x016BU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 16U },
        { 0x014FU, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0151U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0153U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0155U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0157U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x015EU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0164U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 16U },
        { 0x0161U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0163U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0165U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0166U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x0168U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x016AU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0172U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0176U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x017EU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x045EU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 20U },
        { 0x045AU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x04AEU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x04BAU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x05C1U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x043DU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0449U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x0270U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0274U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x027CU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x028CU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0290U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0293U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0298U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x029CU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01A7U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x01A9U, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x01ABU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x0189U, OMC_CANON_CAM_FIELD_U16_REV, 2U },
        { 0x018BU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x018DU, OMC_CANON_CAM_FIELD_U16, 2U },
        { 0x01ACU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U },
        { 0x01BBU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01C7U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x01EBU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x02AAU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x02B6U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x02B3U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x02BFU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0933U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 64U },
        { 0x0937U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 64U },
        { 0x092BU, OMC_CANON_CAM_FIELD_ASCII_FIXED, 64U },
        { 0x0AF1U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0B21U, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x0B2DU, OMC_CANON_CAM_FIELD_U32, 4U },
        { 0x026AU, OMC_CANON_CAM_FIELD_U32_ARRAY4, 16U }
    };
    omc_u32 order;
    static const omc_exif_canon_camerainfo_field specific[] = {
        {0x0023U, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x007DU, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x008CU, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x008EU, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x00BCU, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x00C0U, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x00F4U, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x01A7U, OMC_CANON_CAM_FIELD_U16_REV, 2U},
        {0x01A9U, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x01ABU, OMC_CANON_CAM_FIELD_U16, 2U},
        {0x0280U, OMC_CANON_CAM_FIELD_ASCII_FIXED, 6U},
        {0x02D0U, OMC_CANON_CAM_FIELD_U32, 4U},
        {0x02DCU, OMC_CANON_CAM_FIELD_U32, 4U},
    };
    omc_u32 i;
    omc_const_bytes raw_view;
    const omc_u8* raw;
    omc_u64 raw_size;
    omc_size raw_size_narrow;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0
        || io_order == (omc_u32*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }
    order = *io_order;
    for (i = 0U; i < (omc_u32)(sizeof(fields) / sizeof(fields[0]) +
                               sizeof(specific) / sizeof(specific[0]));
         ++i) {
        const omc_exif_canon_camerainfo_field* field;
        omc_exif_status status;
        omc_u16 value16;
        omc_u32 value32;
        omc_u32 text_size;

        raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
        raw = raw_view.data;
        raw_size = raw_view.size;
        if (raw == (const omc_u8*)0 || raw_size > (omc_u64)(~(omc_size)0)) {
            return 1;
        }
        raw_size_narrow = (omc_size)raw_size;
        if (i < sizeof(fields) / sizeof(fields[0]))
            field = &fields[i];
        else {
            if (strcmp(ifd_name, "mk_canon_camerainfo1dx_0") != 0)
                break;
            field = &specific[i - sizeof(fields) / sizeof(fields[0])];
        }
        if (((omc_u64)field->tag + field->bytes) > raw_size) {
            continue;
        }

        switch (field->kind) {
        case OMC_CANON_CAM_FIELD_U8:
            status = omc_exif_emit_derived_exif_u8(
                ctx, ifd_name, field->tag, order++, raw[field->tag]);
            break;
        case OMC_CANON_CAM_FIELD_U16:
            if (!omc_exif_read_u16(ctx->cfg, raw, raw_size_narrow, field->tag,
                                   &value16)) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, field->tag,
                                                    order++, value16);
            break;
        case OMC_CANON_CAM_FIELD_U16_REV:
            if (ctx->cfg.little_endian) {
                if (!omc_exif_read_u16be_raw(raw, raw_size, field->tag,
                                             &value16)) {
                    continue;
                }
            } else {
                if (!omc_exif_read_u16le_raw(raw, raw_size, field->tag,
                                             &value16)) {
                    continue;
                }
            }
            status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, field->tag,
                                                    order++, value16);
            break;
        case OMC_CANON_CAM_FIELD_U16_ARRAY4:
            status = omc_exif_emit_derived_exif_array_copy(
                ctx, ifd_name, field->tag, order++, OMC_ELEM_U16,
                raw + field->tag, field->bytes, 4U);
            break;
        case OMC_CANON_CAM_FIELD_U32:
            if (!omc_exif_read_u32(ctx->cfg, raw, raw_size_narrow, field->tag,
                                   &value32)) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, field->tag,
                                                    order++, value32);
            break;
        case OMC_CANON_CAM_FIELD_U32_ARRAY4:
            status = omc_exif_emit_derived_exif_array_copy(
                ctx, ifd_name, field->tag, order++, OMC_ELEM_U32,
                raw + field->tag, field->bytes, 4U);
            break;
        case OMC_CANON_CAM_FIELD_ASCII_FIXED:
            text_size = omc_exif_trim_nul_size(raw + field->tag, field->bytes);
            status = omc_exif_emit_derived_exif_text(
                ctx, ifd_name, field->tag, order++, raw + field->tag,
                text_size);
            break;
        default:
            continue;
        }

        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    *io_order = order;
    return 1;
}

static int
omc_exif_emit_canon_camerainfo_fixed_fields(omc_exif_ctx* ctx,
                                            const char* ifd_name,
                                            omc_byte_ref raw_ref,
                                            omc_u32 order_base)
{
    omc_const_bytes raw_view;
    const omc_u8* raw;
    omc_u64 raw_size;
    omc_u16 value16;
    omc_u32 value32;
    omc_exif_status status;
    omc_u32 order;

    if (ifd_name == (const char*)0 || ctx == (omc_exif_ctx*)0
        || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
    raw = raw_view.data;
    raw_size = raw_view.size;
    if (raw == (const omc_u8*)0) {
        return 1;
    }
    order = order_base;
    if (strstr(ifd_name, "camerainfo") != (char*)0) {
        if (!omc_exif_emit_canon_camerainfo_common_fields(ctx, ifd_name,
                                                          raw_ref, &order)) {
            return 0;
        }
        raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
        raw = raw_view.data;
        raw_size = raw_view.size;
        if (raw == (const omc_u8*)0) {
            return 1;
        }
    }

    if (strstr(ifd_name, "camerainfo450d") != (char*)0) {
        return omc_exif_emit_canon_camerainfo_text_field(
            ctx, ifd_name, 0x0107U, raw, raw_size, 6U, order);
    }
    if (strstr(ifd_name, "camerainfo1100d") != (char*)0
        || strstr(ifd_name, "camerainfo600d") != (char*)0) {
        return omc_exif_emit_canon_camerainfo_text_field(
            ctx, ifd_name, 0x019BU, raw, raw_size, 6U, order);
    }
    if (strstr(ifd_name, "camerainfo7d") != (char*)0) {
        return omc_exif_emit_canon_camerainfo_text_field(
            ctx, ifd_name, 0x01ACU, raw, raw_size, 6U, order);
    }
    if (strstr(ifd_name, "camerainfo6d") != (char*)0) {
        return omc_exif_emit_canon_camerainfo_text_field(
            ctx, ifd_name, 0x0256U, raw, raw_size, 6U, order);
    }
    if (strstr(ifd_name, "camerainfo1dmkiin") != (char*)0) {
        if (raw_size <= 0x0074U || raw_size < (0x0079U + 4U)) {
            return 1;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0074U,
                                               order, raw[0x0074U]);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        return omc_exif_emit_canon_camerainfo_text_field(
            ctx, ifd_name, 0x0079U, raw, raw_size, 4U, order + 1U);
    }
    if (strstr(ifd_name, "camerainfo1dmkii") != (char*)0) {
        if (raw_size <= 0x0066U || raw_size < (0x0075U + 4U)) {
            return 1;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0066U,
                                               order, raw[0x0066U]);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        return omc_exif_emit_canon_camerainfo_text_field(
            ctx, ifd_name, 0x0075U, raw, raw_size, 4U, order + 1U);
    }
    if (strcmp(ifd_name, "mk_canon_camerainfo1d_0") == 0) {
        if (!omc_exif_read_u16le_raw(raw, raw_size, 0x0048U, &value16)) {
            return 1;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0048U,
                                                order, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        return 1;
    }
    if (strstr(ifd_name, "camerainfo5d") != (char*)0) {
        if (!omc_exif_read_u32le_raw(raw, raw_size, 0x011CU, &value32)) {
            return 1;
        }
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x011CU,
                                                order, value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        return 1;
    }
    if (strstr(ifd_name, "camerainfounknown32") != (char*)0) {
        if (!omc_exif_read_u32le_raw(raw, raw_size, 0x0047U * 4U, &value32)) {
            return 1;
        }
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0047U,
                                                order, value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        return 1;
    }
    if (strstr(ifd_name, "camerainfounknown") != (char*)0) {
        if (!omc_exif_emit_canon_camerainfo_text_field(
                ctx, ifd_name, 0x016BU, raw, raw_size, 16U, order)) {
            return 0;
        }
        return omc_exif_emit_canon_camerainfo_text_field(
            ctx, ifd_name, 0x05C1U, raw, raw_size, 6U, order + 1U);
    }

    return 1;
}

static int
omc_exif_emit_canon_psinfo_table(omc_exif_ctx* ctx, const char* ifd_name,
                                 omc_byte_ref raw_ref,
                                 int is_psinfo2)
{
    omc_const_bytes raw_view;
    const omc_u8* raw;
    omc_u64 raw_size;
    omc_u16 regular_max_tag;
    omc_u16 userdef1_tag;
    omc_u16 userdef2_tag;
    omc_u16 userdef3_tag;
    omc_u16 tag16;
    omc_u16 value16;
    omc_u32 value32;
    omc_u32 order;
    omc_exif_status status;

    if (ifd_name == (const char*)0 || ctx == (omc_exif_ctx*)0
        || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
    raw = raw_view.data;
    raw_size = raw_view.size;
    if (raw == (const omc_u8*)0) {
        return 1;
    }

    if (is_psinfo2) {
        regular_max_tag = 0x00ECU;
        userdef1_tag = 0x00F0U;
        userdef2_tag = 0x00F2U;
        userdef3_tag = 0x00F4U;
    } else {
        regular_max_tag = 0x00D4U;
        userdef1_tag = 0x00D8U;
        userdef2_tag = 0x00DAU;
        userdef3_tag = 0x00DCU;
    }

    order = 0U;
    for (tag16 = 0U; tag16 <= regular_max_tag; tag16 = (omc_u16)(tag16 + 4U)) {
        raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
        raw = raw_view.data;
        raw_size = raw_view.size;
        if (raw == (const omc_u8*)0) {
            return 1;
        }
        if (!omc_exif_read_u32le_raw(raw, raw_size, 0x025BU + tag16,
                                     &value32)) {
            break;
        }

        status = omc_exif_emit_derived_exif_i32(
            ctx, ifd_name, tag16, order++, omc_exif_u32_to_i32(value32));
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
    raw = raw_view.data;
    raw_size = raw_view.size;
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x025BU + userdef1_tag,
                                &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, userdef1_tag,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
    raw = raw_view.data;
    raw_size = raw_view.size;
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x025BU + userdef2_tag,
                                &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, userdef2_tag,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    raw_view = omc_arena_view(&ctx->store->arena, raw_ref);
    raw = raw_view.data;
    raw_size = raw_view.size;
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x025BU + userdef3_tag,
                                &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, userdef3_tag,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_emit_canon_psinfo_old(omc_exif_ctx* ctx, const char* ifd_name,
                               omc_byte_ref raw_ref)
{
    return omc_exif_emit_canon_psinfo_table(ctx, ifd_name, raw_ref, 0);
}

static int
omc_exif_emit_canon_psinfo2(omc_exif_ctx* ctx, const char* ifd_name,
                            omc_byte_ref raw_ref)
{
    return omc_exif_emit_canon_psinfo_table(ctx, ifd_name, raw_ref, 1);
}

static int
omc_exif_canon_psinfo2_tail_valid(const omc_u8* raw, omc_u64 raw_size)
{
    omc_u16 userdef1;

    if (raw == (const omc_u8*)0
        || !omc_exif_read_u16le_raw(raw, raw_size, 0x025BU + 0x00F0U,
                                    &userdef1)) {
        return 0;
    }

    return userdef1 != 0U;
}

static int
omc_exif_decode_canon_camerainfo(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    const omc_u8* model_text;
    omc_u32 model_size;
    omc_const_bytes raw;
    omc_byte_ref raw_ref;
    const char* subtable;
    char ifd_name[64];
    char ps_ifd_name[64];
    omc_u64 ifd_offset;
    omc_exif_cfg candidate_cfg;
    omc_exif_opts candidate_opts;
    omc_size first_entry, entry_index;
    char ifd_prefix[64];
    int decoded;
    omc_u32 order;
    int is_model_1000d;
    int use_psinfo2;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    model_text = (const omc_u8*)0;
    model_size = 0U;
    (void)omc_exif_find_first_text(ctx->store, "ifd0", 0x0110U, &model_text,
                                   &model_size);
    if (model_text == (const omc_u8*)0) {
        (void)omc_exif_find_outer_ifd0_ascii(ctx, 0x0110U, &model_text,
                                             &model_size);
    }
    is_model_1000d = model_text != (const omc_u8*)0
                     && omc_exif_bytes_contains_text(model_text, model_size,
                                                     "1000D");

    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x000DU);
    if (entry == (const omc_entry*)0 || !omc_exif_entry_raw_view(ctx->store, entry, &raw)
        || raw.size == 0U) {
        return 1;
    }
    raw_ref = entry->value.u.ref;

    subtable = omc_exif_canon_camerainfo_subtable(
        model_text, model_size, raw.data, raw.size,
        (entry->value.kind == OMC_VAL_ARRAY
         && entry->value.elem_type == OMC_ELEM_U32));
    if (!omc_exif_make_subifd_name("mk_canon", subtable, 0U, ifd_name,
                                   sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    if (entry->value.kind == OMC_VAL_BYTES &&
        omc_exif_find_classic_candidate(ctx, raw.data, raw.size, 512U, &ifd_offset,
                                        &candidate_cfg)) {
        candidate_opts = ctx->opts;
        candidate_opts.decode_makernote = 0;
        candidate_opts.decode_geotiff = 0;
        candidate_opts.decode_printim = 0;
        candidate_opts.decode_embedded_containers = 0;
        memcpy(ifd_prefix, ifd_name, strlen(ifd_name) + 1U);
        ifd_prefix[strlen(ifd_prefix) - 1U] = '\0';
        candidate_opts.tokens.ifd_prefix = ifd_prefix;
        first_entry = ctx->store->entry_count;
        decoded = omc_exif_decode_ifd_blob_offsets(
            ctx, raw.data, raw.size, ifd_offset, &candidate_opts, candidate_cfg, 4U, 0);
        for (entry_index = first_entry; entry_index < ctx->store->entry_count;
             ++entry_index) {
            ctx->store->entries[entry_index].flags |= OMC_ENTRY_FLAG_DERIVED;
            ++order;
        }
        if (!decoded &&
            (ctx->res.status == OMC_EXIF_LIMIT || ctx->res.status == OMC_EXIF_NOMEM))
            return 0;
    }

    if (!omc_exif_emit_canon_camerainfo_fixed_fields(ctx, ifd_name, raw_ref,
                                                     order)) {
        return 0;
    }

    if (strstr(ifd_name, "camerainfo") == (char*)0) {
        return 1;
    }

    raw = omc_arena_view(&ctx->store->arena, raw_ref);
    use_psinfo2 = omc_exif_canon_psinfo2_tail_valid(raw.data, raw.size);
    if (is_model_1000d) {
        use_psinfo2 = 0;
        if (raw.size > (0x025BU + 0x00DCU)) {
            if (!omc_exif_make_subifd_name("mk_canon", "psinfo", 0U,
                                           ps_ifd_name, sizeof(ps_ifd_name))) {
                return 1;
            }
            return omc_exif_emit_canon_psinfo_old(ctx, ps_ifd_name, raw_ref);
        }
    }

    if (use_psinfo2) {
        if (!omc_exif_make_subifd_name("mk_canon", "psinfo2", 0U, ps_ifd_name,
                                       sizeof(ps_ifd_name))) {
            return 1;
        }
        return omc_exif_emit_canon_psinfo2(ctx, ps_ifd_name, raw_ref);
    }

    if (raw.size > (0x025BU + 8U)
        && omc_exif_make_subifd_name("mk_canon", "psinfo", 0U, ps_ifd_name,
                                     sizeof(ps_ifd_name))) {
        return omc_exif_emit_canon_psinfo_old(ctx, ps_ifd_name, raw_ref);
    }

    return 1;
}

static int
omc_exif_decode_canon_colordata(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    omc_byte_ref raw_ref;
    omc_u32 family;
    omc_u16 version16;
    omc_s16 version_i16;
    omc_u32 count;
    const char* family_name;
    char ifd_name[64];
    char embed_ifd_name[64];

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x4001U);
    if (entry == (const omc_entry*)0 || entry->value.kind != OMC_VAL_ARRAY
        || entry->value.elem_type != OMC_ELEM_U16
        || !omc_exif_entry_raw_view(ctx->store, entry, &raw)
        || raw.size < 2U) {
        return 1;
    }

    count = entry->value.count;
    raw_ref = entry->value.u.ref;
    if ((omc_u64)count * 2U > raw.size
        || !omc_exif_read_u16le_raw(raw.data, raw.size, 0U, &version16)) {
        return 1;
    }

    version_i16 = (omc_s16)version16;
    family = omc_exif_canon_colordata_family(count, version_i16);
    family_name = omc_exif_canon_colordata_family_name(family);
    if (family_name == (const char*)0) {
        family_name = "colordata";
    }
    if (!omc_exif_make_subifd_name("mk_canon", family_name, 0U, ifd_name,
                                   sizeof(ifd_name))) {
        return 1;
    }
    if (!omc_exif_emit_derived_exif_u16_word_table_ref(ctx, raw_ref, 0U, count,
                                                       0U, 0U, ifd_name)) {
        return 0;
    }

    switch (family) {
    case 1U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0x4BU, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 2U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0xA4U, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 3U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0x85U, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 4U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcoefs", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_derived_exif_u16_word_table_ref_skip_zero(
                ctx, raw_ref, 0x3FU, 105U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0xA8U, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 5U:
        if (version_i16 == (omc_s16)-3) {
            if (!omc_exif_make_subifd_name("mk_canon", "colorcoefs", 0U,
                                           embed_ifd_name,
                                           sizeof(embed_ifd_name))
                || !omc_exif_emit_derived_exif_u16_word_table_ref_skip_zero(
                    ctx, raw_ref, 0x47U, 115U, 0U, 0U,
                    embed_ifd_name)) {
                return 0;
            }
            if (!omc_exif_make_subifd_name("mk_canon", "colorcalib2", 0U,
                                           embed_ifd_name,
                                           sizeof(embed_ifd_name))
                || !omc_exif_emit_canon_named_u16_word_table_ref(
                    ctx, raw_ref, 0xBAU, 75U, 0U, 0U,
                    embed_ifd_name)) {
                return 0;
            }
        } else if (version_i16 == (omc_s16)-4) {
            if (!omc_exif_make_subifd_name("mk_canon", "colorcoefs2", 0U,
                                           embed_ifd_name,
                                           sizeof(embed_ifd_name))
                || !omc_exif_emit_derived_exif_u16_word_table_ref_skip_zero(
                    ctx, raw_ref, 0x47U, 184U, 0U, 0U,
                    embed_ifd_name)) {
                return 0;
            }
            if (!omc_exif_make_subifd_name("mk_canon", "colorcalib2", 0U,
                                           embed_ifd_name,
                                           sizeof(embed_ifd_name))
                || !omc_exif_emit_canon_named_u16_word_table_ref(
                    ctx, raw_ref, 0xFFU, 75U, 0U, 0U,
                    embed_ifd_name)) {
                return 0;
            }
        }
        break;
    case 6U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0xBCU, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 7U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0xD5U, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 8U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0x107U, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 9U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0x10AU, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 10U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0x118U, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 11U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0x12CU, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    case 12U:
        if (!omc_exif_make_subifd_name("mk_canon", "colorcalib", 0U,
                                       embed_ifd_name, sizeof(embed_ifd_name))
            || !omc_exif_emit_canon_named_u16_word_table_ref(
                ctx, raw_ref, 0x140U, 60U, 0U, 0U, embed_ifd_name)) {
            return 0;
        }
        break;
    default: break;
    }

    return 1;
}

static int
omc_exif_decode_canon_timeinfo(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    char ifd_name[64];
    omc_u32 count;
    omc_u32 i;
    omc_u32 value32;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x0035U);
    if (!omc_exif_entry_raw_view(ctx->store, entry, &raw)
        || raw.size < 8U || (raw.size % 4U) != 0U
        || !omc_exif_make_subifd_name("mk_canon", "timeinfo", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    count = (omc_u32)(raw.size / 4U);
    for (i = 1U; i < count; ++i) {
        if (!omc_exif_read_u32le_raw(raw.data, raw.size, (omc_u64)i * 4U,
                                     &value32)) {
            break;
        }
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, (omc_u16)i, i,
                                                value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

/* FilterInfo contains groups of tagged little-endian U32 records. The flat
 * word projection is emitted independently by the shared Canon table pass. */
static int
omc_exif_decode_canon_filterinfo(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    omc_u32 length, group_length, record_count, tag, count, scalar, order, i;
    omc_u64 pos, group_end, record, payload, bytes;
    omc_exif_status status;
    omc_val value;
    omc_byte_ref ref;
    const char *ifd;

    if (ctx == NULL || ctx->store == NULL || ctx->measure_only)
        return 1;
    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x4024U);
    if (!omc_exif_entry_raw_view(ctx->store, entry, &raw) || raw.size < 8U ||
        !omc_exif_read_u32le_raw(raw.data, raw.size, 0U, &length) || length != raw.size)
        return 1;
    ifd = "mk_canon_filterinfo_0";
    pos = 8U;
    order = 0U;
    while (pos + 12U <= raw.size) {
        (void)omc_exif_read_u32le_raw(raw.data, raw.size, pos + 4U, &group_length);
        (void)omc_exif_read_u32le_raw(raw.data, raw.size, pos + 8U, &record_count);
        if (group_length < 8U)
            break;
        pos += 12U;
        if ((omc_u64)group_length - 8U > raw.size - pos) {
            omc_exif_update_status(&ctx->res, OMC_EXIF_MALFORMED);
            return 1;
        }
        group_end = pos + group_length - 8U;
        record = pos;
        for (i = 0U; i < record_count && record + 8U <= group_end; ++i) {
            (void)omc_exif_read_u32le_raw(raw.data, raw.size, record, &tag);
            (void)omc_exif_read_u32le_raw(raw.data, raw.size, record + 4U, &count);
            if (tag > 0xffffU || count == 0U)
                break;
            bytes = (omc_u64)count * 4U;
            if (count > ctx->opts.limits.max_entries_per_ifd ||
                bytes > ctx->opts.limits.max_value_bytes) {
                omc_exif_mark_limit(ctx, OMC_EXIF_LIM_VALUE_COUNT, record,
                                    (omc_u16)tag);
                return 0;
            }
            payload = record + 8U;
            if (bytes > group_end - payload)
                break;
            if (count == 1U) {
                (void)omc_exif_read_u32le_raw(raw.data, raw.size, payload, &scalar);
                omc_val_make_u32(&value, scalar);
            } else {
                status = omc_exif_store_ref(ctx, raw.data + (omc_size)payload,
                                            (omc_u32)bytes, &ref);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
                omc_val_init(&value);
                value.kind = OMC_VAL_ARRAY;
                value.elem_type = OMC_ELEM_U32;
                value.byte_order = OMC_BYTE_ORDER_LITTLE;
                value.count = count;
                value.u.ref = ref;
            }
            if (!omc_exif_vendor_value(ctx, ifd, (omc_u16)tag, order++, &value))
                return 0;
            record = payload + bytes;
        }
        pos = group_end;
    }
    return 1;
}

static int omc_exif_decode_canon_flat_tables(omc_exif_ctx *ctx)
{
    static const struct {
        omc_u16 tag, type;
        const char *name;
        int all;
    } rows[] = {{0x0002U, 3U, "focallength", 0},    {0x00aaU, 3U, "measuredcolor", 0},
                {0x4011U, 3U, "vignettingcorr", 0}, {0x0004U, 3U, "shotinfo", 1},
                {0x0005U, 3U, "panorama", 0},       {0x0093U, 3U, "fileinfo", 1},
                {0x0098U, 3U, "cropinfo", 0},       {0x001dU, 3U, "mycolors", 0},
                {0x00e0U, 3U, "sensorinfo", 0},     {0x00a0U, 3U, "processing", 0},
                {0x009aU, 4U, "aspectinfo", 0},     {0x4016U, 4U, "vignettingcorr2", 0},
                {0x4024U, 4U, "filterinfo", 1},     {0x4025U, 4U, "hdrinfo", 0},
                {0x4028U, 9U, "afconfig", 0},       {0x4018U, 4U, "lightingopt", 0},
                {0x4021U, 9U, "multiexp", 0},       {0x4020U, 4U, "ambience", 0},
                {0x403fU, 4U, "rawburstinfo", 0},   {0x4013U, 4U, "afmicroadj", 0}};
    omc_u32 r, i, width, count, v, numer, denom;
    omc_u16 word, dims[2], measured[4];
    const char *table;
    const omc_entry *entry;
    omc_const_bytes raw;
    omc_u8 *copy;
    omc_exif_cfg cfg;
    omc_exif_status status;
    omc_val value;
    char ifd[64];
    int ok;
    if (ctx->store == NULL || ctx->measure_only)
        return 1;
    entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", 0x4019U);
    if (entry != NULL && entry->origin.wire_type.code == 7U &&
        omc_exif_entry_raw_view(ctx->store, entry, &raw) && raw.size != 0U) {
        omc_u8 serial[5];
        omc_byte_ref serial_ref;
        count = raw.size < 5U ? (omc_u32)raw.size : 5U;
        memcpy(serial, raw.data, count);
        status = omc_exif_store_ref(ctx, serial, count, &serial_ref);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        omc_val_make_bytes(&value, serial_ref);
        if (!omc_exif_vendor_value(ctx, "mk_canon_lensinfo_0", 0U, 0U, &value))
            return 0;
    }
    for (r = 0U; r < sizeof(rows) / sizeof(rows[0]); ++r) {
        entry = omc_exif_find_first_entry(ctx->store, "mk_canon0", rows[r].tag);
        if (entry == NULL ||
            entry->origin.wire_type.code !=
                (rows[r].tag == 0x4011U ? 7U
                                        : (rows[r].type == 9U ? 4U : rows[r].type)))
            continue;
        if (rows[r].tag == 0x4024U &&
            omc_exif_find_first_entry(ctx->store, "mk_canon_filterinfo_0", 0U) != NULL)
            continue;
        copy = NULL;
        status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        width = rows[r].type == 3U ? 2U : 4U;
        cfg = omc_exif_make_classic_cfg(entry->value.byte_order != OMC_BYTE_ORDER_BIG);
        count = (omc_u32)(raw.size / width);
        if (count > ctx->opts.limits.max_entries_per_ifd) {
            free(copy);
            omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_IFD, 0U, rows[r].tag);
            return 0;
        }
        table = rows[r].name;
        if (rows[r].tag == 2U && count > 3U) {
            dims[0] = dims[1] = 0U;
            (void)omc_exif_read_u16(cfg, copy, raw.size, 4U, &dims[0]);
            (void)omc_exif_read_u16(cfg, copy, raw.size, 6U, &dims[1]);
            if (dims[0] == 0U || dims[1] == 0U || dims[0] > 5000U || dims[1] > 5000U)
                table = "focallength_unknown";
        }
        (void)omc_exif_make_subifd_name("mk_canon", table, 0U, ifd, sizeof(ifd));
        ok = 1;
        if (rows[r].tag == 0xaaU && count >= 5U) {
            for (i = 0U; i < 4U; ++i)
                (void)omc_exif_read_u16(cfg, copy, raw.size, (i + 1U) * 2U,
                                        &measured[i]);
            status =
                omc_exif_emit_derived_exif_u16_array_le(ctx, ifd, 1U, 0U, measured, 4U);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                ok = 0;
            }
        } else if (rows[r].tag == 0x4013U) {
            if (raw.size >= 16U) {
                (void)omc_exif_read_u32(cfg, copy, raw.size, 4U, &v);
                (void)omc_exif_read_u32(cfg, copy, raw.size, 8U, &numer);
                (void)omc_exif_read_u32(cfg, copy, raw.size, 12U, &denom);
                omc_val_make_u32(&value, v);
                ok = omc_exif_vendor_value(ctx, ifd, 1U, 0U, &value);
                omc_val_init(&value);
                value.kind = OMC_VAL_SCALAR;
                value.elem_type = OMC_ELEM_URATIONAL;
                value.count = 1U;
                value.u.ur.numer = numer;
                value.u.ur.denom = denom;
                if (ok)
                    ok = omc_exif_vendor_value(ctx, ifd, 2U, 1U, &value);
            }
        } else
            for (i = 0U; i < count && i <= 0xffffU && ok; ++i) {
                if (!rows[r].all && !omc_exif_canon_flat_tag(table, (omc_u16)i))
                    continue;
                if (width == 2U) {
                    (void)omc_exif_read_u16(cfg, copy, raw.size, (omc_u64)i * 2U,
                                            &word);
                    omc_val_make_u16(&value, word);
                } else {
                    (void)omc_exif_read_u32(cfg, copy, raw.size, (omc_u64)i * 4U, &v);
                    if (rows[r].type == 9U)
                        omc_val_make_i32(
                            &value, (omc_s32)(v <= 0x7fffffffU
                                                  ? (omc_s64)v
                                                  : (omc_s64)v - ((omc_s64)1 << 32)));
                    else
                        omc_val_make_u32(&value, v);
                }
                ok = omc_exif_vendor_value(ctx, ifd, (omc_u16)i, i, &value);
            }
        free(copy);
        if (!ok)
            return 0;
    }
    return 1;
}

static int
omc_exif_decode_canon_postpass(omc_exif_ctx* ctx)
{
    if (!omc_exif_decode_canon_camera_settings(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_canon_afinfo2(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_canon_custom_functions2(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_canon_camerainfo(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_canon_colordata(ctx)) {
        return 0;
    }
    if (!omc_exif_decode_canon_timeinfo(ctx)) {
        return 0;
    }
    return omc_exif_decode_canon_filterinfo(ctx) &&
           omc_exif_decode_canon_flat_tables(ctx);
}

typedef enum omc_exif_sony_field_kind {
    OMC_EXIF_SONY_F_U8 = 0,
    OMC_EXIF_SONY_F_U16LE = 1,
    OMC_EXIF_SONY_F_U32LE = 2,
    OMC_EXIF_SONY_F_I16LE = 3,
    OMC_EXIF_SONY_F_U8_ARRAY = 4,
    OMC_EXIF_SONY_F_U16LE_ARRAY = 5,
    OMC_EXIF_SONY_F_I16LE_ARRAY = 6,
    OMC_EXIF_SONY_F_BYTES = 7
} omc_exif_sony_field_kind;

typedef struct omc_exif_sony_field {
    omc_u16 tag;
    omc_u8 kind;
    omc_u16 count;
} omc_exif_sony_field;

static const omc_exif_sony_field k_omc_exif_sony_tag2010a_fields[] = {
    {0x1128U, OMC_EXIF_SONY_F_U8, 0U},    {0x112CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x1134U, OMC_EXIF_SONY_F_U8, 0U},    {0x1138U, OMC_EXIF_SONY_F_U8, 0U},
    {0x113EU, OMC_EXIF_SONY_F_U16LE, 0U}, {0x1140U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1144U, OMC_EXIF_SONY_F_U8, 0U},    {0x1148U, OMC_EXIF_SONY_F_U8, 0U},
    {0x114CU, OMC_EXIF_SONY_F_I16LE, 0U}, {0x115EU, OMC_EXIF_SONY_F_U8, 0U},
    {0x115FU, OMC_EXIF_SONY_F_U8, 0U},    {0x1163U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1170U, OMC_EXIF_SONY_F_U8, 0U},    {0x1174U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1175U, OMC_EXIF_SONY_F_U8, 0U},    {0x117CU, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag2010c_fields[] = {
    {0x0000U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x0004U, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0008U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x0200U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0210U, OMC_EXIF_SONY_F_BYTES, 7U}, {0x0300U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1104U, OMC_EXIF_SONY_F_U8, 0U},    {0x1108U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1110U, OMC_EXIF_SONY_F_U8, 0U},    {0x1114U, OMC_EXIF_SONY_F_U8, 0U},
    {0x111AU, OMC_EXIF_SONY_F_U16LE, 0U}, {0x111CU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1120U, OMC_EXIF_SONY_F_U8, 0U},    {0x1124U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1128U, OMC_EXIF_SONY_F_I16LE, 0U}, {0x113EU, OMC_EXIF_SONY_F_U8, 0U},
    {0x113FU, OMC_EXIF_SONY_F_U8, 0U},    {0x1143U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1150U, OMC_EXIF_SONY_F_U8, 0U},    {0x1154U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1155U, OMC_EXIF_SONY_F_U8, 0U},    {0x115CU, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U},
    {0x11F4U, OMC_EXIF_SONY_F_U16LE, 0U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag2010f_fields[] = {
    {0x0004U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x0050U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1014U, OMC_EXIF_SONY_F_U8, 0U},    {0x1018U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1020U, OMC_EXIF_SONY_F_U8, 0U},    {0x1024U, OMC_EXIF_SONY_F_U8, 0U},
    {0x102AU, OMC_EXIF_SONY_F_U16LE, 0U}, {0x102CU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1030U, OMC_EXIF_SONY_F_U8, 0U},    {0x1034U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1038U, OMC_EXIF_SONY_F_I16LE, 0U}, {0x104EU, OMC_EXIF_SONY_F_U8, 0U},
    {0x104FU, OMC_EXIF_SONY_F_U8, 0U},    {0x1053U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1060U, OMC_EXIF_SONY_F_U8, 0U},    {0x1064U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1065U, OMC_EXIF_SONY_F_U8, 0U},    {0x106CU, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U},
    {0x1134U, OMC_EXIF_SONY_F_U16LE, 0U}, {0x1136U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1138U, OMC_EXIF_SONY_F_U16LE, 0U}, {0x113CU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x192CU, OMC_EXIF_SONY_F_U8, 0U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag2010g_fields[] = {
    {0x0004U, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0050U, OMC_EXIF_SONY_F_U8, 0U},
    {0x020CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0210U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0218U, OMC_EXIF_SONY_F_U8, 0U},
    {0x021CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0222U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0224U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0228U, OMC_EXIF_SONY_F_U8, 0U},
    {0x022CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0230U, OMC_EXIF_SONY_F_I16LE, 0U},
    {0x0246U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0247U, OMC_EXIF_SONY_F_U8, 0U},
    {0x024BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0258U, OMC_EXIF_SONY_F_U8, 0U},
    {0x025CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x025DU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0264U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U},
    {0x032CU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x032EU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0330U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0344U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x189CU, OMC_EXIF_SONY_F_I16LE_ARRAY, 16U},
    {0x18BDU, OMC_EXIF_SONY_F_U8, 0U},
    {0x18BEU, OMC_EXIF_SONY_F_U8, 0U},
    {0x18BFU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x18C2U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x18C4U, OMC_EXIF_SONY_F_U8, 0U},
    {0x18C5U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1958U, OMC_EXIF_SONY_F_U8, 0U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag2010h_fields[] = {
    {0x0004U, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0050U, OMC_EXIF_SONY_F_U8, 0U},
    {0x020CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0210U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0218U, OMC_EXIF_SONY_F_U8, 0U},
    {0x021CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0222U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0224U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0228U, OMC_EXIF_SONY_F_U8, 0U},
    {0x022CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0230U, OMC_EXIF_SONY_F_I16LE, 0U},
    {0x0246U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0247U, OMC_EXIF_SONY_F_U8, 0U},
    {0x024BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0258U, OMC_EXIF_SONY_F_U8, 0U},
    {0x025CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x025DU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0264U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U},
    {0x032CU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x032EU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0330U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0346U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x18CCU, OMC_EXIF_SONY_F_I16LE_ARRAY, 16U},
    {0x18EDU, OMC_EXIF_SONY_F_U8, 0U},
    {0x18EEU, OMC_EXIF_SONY_F_U8, 0U},
    {0x18EFU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x18F2U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x18F4U, OMC_EXIF_SONY_F_U8, 0U},
    {0x18F5U, OMC_EXIF_SONY_F_U8, 0U},
    {0x192CU, OMC_EXIF_SONY_F_U8, 0U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag9050d_fields[] = {
    {0x000AU, OMC_EXIF_SONY_F_U32LE, 0U},    {0x001AU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x001CU, OMC_EXIF_SONY_F_U16LE, 0U},    {0x001FU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0038U, OMC_EXIF_SONY_F_U8_ARRAY, 6U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag9400b_fields[] = {
    {0x0008U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x000CU, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0010U, OMC_EXIF_SONY_F_U8, 0U},    {0x0012U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0016U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x001EU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0024U, OMC_EXIF_SONY_F_U8, 0U},    {0x0025U, OMC_EXIF_SONY_F_U8, 0U},
    {0x003FU, OMC_EXIF_SONY_F_U16LE, 0U}, {0x0046U, OMC_EXIF_SONY_F_U8, 0U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag9404a_fields[] = {
    {0x000BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x000DU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0019U, OMC_EXIF_SONY_F_U16LE, 0U},
};
static const omc_exif_sony_field k_omc_exif_sony_tag9050b_fields[] = {
    {0x0000U, OMC_EXIF_SONY_F_U8, 0U},          {0x0001U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0039U, OMC_EXIF_SONY_F_U8, 0U},          {0x004BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x006BU, OMC_EXIF_SONY_F_U8, 0U},          {0x006DU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0073U, OMC_EXIF_SONY_F_U8, 0U},          {0x0105U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0106U, OMC_EXIF_SONY_F_U8, 0U},          {0x010BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0114U, OMC_EXIF_SONY_F_U8, 0U},          {0x01EBU, OMC_EXIF_SONY_F_U8, 0U},
    {0x01EEU, OMC_EXIF_SONY_F_U8, 0U},          {0x021AU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0046U, OMC_EXIF_SONY_F_U16LE, 0U},       {0x0048U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0026U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U}, {0x003AU, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0050U, OMC_EXIF_SONY_F_U32LE, 0U},       {0x0052U, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0058U, OMC_EXIF_SONY_F_U32LE, 0U},       {0x019FU, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x01CBU, OMC_EXIF_SONY_F_U32LE, 0U},       {0x01CDU, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0107U, OMC_EXIF_SONY_F_U16LE, 0U},       {0x0109U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0061U, OMC_EXIF_SONY_F_U8_ARRAY, 2U},    {0x0088U, OMC_EXIF_SONY_F_U8_ARRAY, 6U},
    {0x0116U, OMC_EXIF_SONY_F_U8_ARRAY, 2U},    {0x01EDU, OMC_EXIF_SONY_F_U8_ARRAY, 2U},
    {0x01F0U, OMC_EXIF_SONY_F_U8_ARRAY, 2U},    {0x021CU, OMC_EXIF_SONY_F_U8_ARRAY, 2U},
    {0x021EU, OMC_EXIF_SONY_F_U8_ARRAY, 2U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9050c_fields[] = {
    {0x0026U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U}, {0x0039U, OMC_EXIF_SONY_F_U8, 0U},
    {0x004BU, OMC_EXIF_SONY_F_U8, 0U},          {0x006BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0046U, OMC_EXIF_SONY_F_U16LE, 0U},       {0x0048U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0066U, OMC_EXIF_SONY_F_U16LE, 0U},       {0x0068U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x003AU, OMC_EXIF_SONY_F_U32LE, 0U},       {0x0050U, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0088U, OMC_EXIF_SONY_F_U8_ARRAY, 6U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9050a_fields[] = {
    {0x0000U, OMC_EXIF_SONY_F_U8, 0U},          {0x0001U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0020U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U}, {0x0031U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0032U, OMC_EXIF_SONY_F_U32LE, 0U},       {0x003AU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x003CU, OMC_EXIF_SONY_F_U16LE, 0U},       {0x003FU, OMC_EXIF_SONY_F_U8, 0U},
    {0x004CU, OMC_EXIF_SONY_F_U32LE, 0U},       {0x0051U, OMC_EXIF_SONY_F_BYTES, 6U},
    {0x0067U, OMC_EXIF_SONY_F_U8, 0U},          {0x007CU, OMC_EXIF_SONY_F_U8_ARRAY, 4U},
    {0x00F0U, OMC_EXIF_SONY_F_U8_ARRAY, 5U},    {0x0105U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0106U, OMC_EXIF_SONY_F_U8, 0U},          {0x0107U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0109U, OMC_EXIF_SONY_F_U16LE, 0U},       {0x010BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0114U, OMC_EXIF_SONY_F_U8, 0U},          {0x0116U, OMC_EXIF_SONY_F_U8_ARRAY, 2U},
    {0x01A0U, OMC_EXIF_SONY_F_U32LE, 0U},       {0x01AAU, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x01BDU, OMC_EXIF_SONY_F_U32LE, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag2010b_fields[] = {
    {0x0000U, OMC_EXIF_SONY_F_U32LE, 0U},        {0x0004U, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0008U, OMC_EXIF_SONY_F_U32LE, 0U},        {0x01B6U, OMC_EXIF_SONY_F_BYTES, 7U},
    {0x0324U, OMC_EXIF_SONY_F_U8, 0U},           {0x1128U, OMC_EXIF_SONY_F_U8, 0U},
    {0x112CU, OMC_EXIF_SONY_F_U8, 0U},           {0x1134U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1138U, OMC_EXIF_SONY_F_U8, 0U},           {0x113EU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1140U, OMC_EXIF_SONY_F_U16LE, 0U},        {0x1144U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1148U, OMC_EXIF_SONY_F_U8, 0U},           {0x114CU, OMC_EXIF_SONY_F_I16LE, 0U},
    {0x1162U, OMC_EXIF_SONY_F_U8, 0U},           {0x1163U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1167U, OMC_EXIF_SONY_F_U8, 0U},           {0x1174U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1178U, OMC_EXIF_SONY_F_U8, 0U},           {0x1179U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1180U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U},  {0x1218U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1A23U, OMC_EXIF_SONY_F_I16LE_ARRAY, 16U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag2010e_fields[] = {
    {0x0000U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x0004U, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0008U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x021CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x022CU, OMC_EXIF_SONY_F_BYTES, 7U}, {0x0328U, OMC_EXIF_SONY_F_U8, 0U},
    {0x115CU, OMC_EXIF_SONY_F_U8, 0U},    {0x1160U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1168U, OMC_EXIF_SONY_F_U8, 0U},    {0x116CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x1172U, OMC_EXIF_SONY_F_U16LE, 0U}, {0x1174U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1178U, OMC_EXIF_SONY_F_U8, 0U},    {0x117CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x1180U, OMC_EXIF_SONY_F_I16LE, 0U}, {0x1196U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1197U, OMC_EXIF_SONY_F_U8, 0U},    {0x119BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x11A8U, OMC_EXIF_SONY_F_U8, 0U},    {0x11ACU, OMC_EXIF_SONY_F_U8, 0U},
    {0x11ADU, OMC_EXIF_SONY_F_U8, 0U},    {0x11B4U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U},
    {0x1254U, OMC_EXIF_SONY_F_U16LE, 0U}, {0x1870U, OMC_EXIF_SONY_F_I16LE_ARRAY, 16U},
    {0x1891U, OMC_EXIF_SONY_F_U8, 0U},    {0x1892U, OMC_EXIF_SONY_F_U8, 0U},
    {0x1893U, OMC_EXIF_SONY_F_U16LE, 0U}, {0x1896U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x1898U, OMC_EXIF_SONY_F_U8, 0U},    {0x1899U, OMC_EXIF_SONY_F_U8, 0U},
    {0x192CU, OMC_EXIF_SONY_F_U8, 0U},    {0x1A88U, OMC_EXIF_SONY_F_U8, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag2010i_fields[] = {
    {0x0004U, OMC_EXIF_SONY_F_U8, 0U},          {0x004EU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0204U, OMC_EXIF_SONY_F_U8, 0U},          {0x0208U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0210U, OMC_EXIF_SONY_F_U8, 0U},          {0x0211U, OMC_EXIF_SONY_F_U8, 0U},
    {0x021BU, OMC_EXIF_SONY_F_U8, 0U},          {0x021FU, OMC_EXIF_SONY_F_U8, 0U},
    {0x0237U, OMC_EXIF_SONY_F_U8, 0U},          {0x0238U, OMC_EXIF_SONY_F_U8, 0U},
    {0x023CU, OMC_EXIF_SONY_F_U8, 0U},          {0x0247U, OMC_EXIF_SONY_F_U8, 0U},
    {0x024BU, OMC_EXIF_SONY_F_U8, 0U},          {0x024CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x17F1U, OMC_EXIF_SONY_F_U8, 0U},          {0x17F2U, OMC_EXIF_SONY_F_U8, 0U},
    {0x17F8U, OMC_EXIF_SONY_F_U8, 0U},          {0x17F9U, OMC_EXIF_SONY_F_U8, 0U},
    {0x188CU, OMC_EXIF_SONY_F_U8, 0U},          {0x0217U, OMC_EXIF_SONY_F_I16LE, 0U},
    {0x0219U, OMC_EXIF_SONY_F_I16LE, 0U},       {0x0223U, OMC_EXIF_SONY_F_I16LE, 0U},
    {0x0252U, OMC_EXIF_SONY_F_U16LE_ARRAY, 3U}, {0x030AU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x030CU, OMC_EXIF_SONY_F_U16LE, 0U},       {0x030EU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0320U, OMC_EXIF_SONY_F_U16LE, 0U},       {0x17F3U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x17F6U, OMC_EXIF_SONY_F_U16LE, 0U},       {0x17D0U, OMC_EXIF_SONY_F_BYTES, 32U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9402_fields[] = {
    {0x0002U, OMC_EXIF_SONY_F_U8, 0U}, {0x0004U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0016U, OMC_EXIF_SONY_F_U8, 0U}, {0x0017U, OMC_EXIF_SONY_F_U8, 0U},
    {0x002DU, OMC_EXIF_SONY_F_U8, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9403_fields[] = {
    {0x0004U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0005U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0019U, OMC_EXIF_SONY_F_U16LE, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9400a_fields[] = {
    {0x0008U, OMC_EXIF_SONY_F_U32LE, 0U}, {0x000CU, OMC_EXIF_SONY_F_U32LE, 0U},
    {0x0010U, OMC_EXIF_SONY_F_U8, 0U},    {0x0012U, OMC_EXIF_SONY_F_U8, 0U},
    {0x001AU, OMC_EXIF_SONY_F_U32LE, 0U}, {0x0022U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0028U, OMC_EXIF_SONY_F_U8, 0U},    {0x0029U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0044U, OMC_EXIF_SONY_F_U16LE, 0U}, {0x0052U, OMC_EXIF_SONY_F_U8, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9404c_fields[] = {
    {0x000BU, OMC_EXIF_SONY_F_U8, 0U},
    {0x000DU, OMC_EXIF_SONY_F_U8, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9404b_fields[] = {
    {0x000CU, OMC_EXIF_SONY_F_U8, 0U},
    {0x000EU, OMC_EXIF_SONY_F_U8, 0U},
    {0x001EU, OMC_EXIF_SONY_F_U16LE, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag202a_fields[] = {
    {0x0001U, OMC_EXIF_SONY_F_U8, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9405a_fields[] = {
    {0x0600U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0601U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0603U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0604U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0605U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0608U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x064AU, OMC_EXIF_SONY_F_I16LE_ARRAY, 16U},
    {0x066AU, OMC_EXIF_SONY_F_I16LE_ARRAY, 32U},
    {0x06CAU, OMC_EXIF_SONY_F_I16LE_ARRAY, 16U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag9406_fields[] = {
    {0x0005U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0006U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0007U, OMC_EXIF_SONY_F_U8, 0U},
    {0x0008U, OMC_EXIF_SONY_F_U8, 0U},
};

static const omc_exif_sony_field k_omc_exif_sony_tag940c_fields[] = {
    {0x0008U, OMC_EXIF_SONY_F_U8, 0U},    {0x0009U, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x000BU, OMC_EXIF_SONY_F_U16LE, 0U}, {0x000DU, OMC_EXIF_SONY_F_U16LE, 0U},
    {0x0014U, OMC_EXIF_SONY_F_U16LE, 0U},
};

static omc_u32
omc_exif_sony_mod_mul_249(omc_u32 a, omc_u32 b)
{
    return (a * b) % 249U;
}

static omc_u8
omc_exif_sony_mod_pow_249(omc_u8 base, omc_u8 exp)
{
    omc_u32 result;
    omc_u32 cur;
    omc_u8 e;

    result = 1U;
    cur = base;
    e = exp;
    while (e != 0U) {
        if ((e & 1U) != 0U) {
            result = omc_exif_sony_mod_mul_249(result, cur);
        }
        cur = omc_exif_sony_mod_mul_249(cur, cur);
        e = (omc_u8)(e >> 1U);
    }
    return (omc_u8)result;
}

static omc_u8
omc_exif_sony_decipher_once(omc_u8 value8)
{
    if (value8 >= 249U) {
        return value8;
    }
    return omc_exif_sony_mod_pow_249(value8, 55U);
}

static omc_u8
omc_exif_sony_decipher(omc_u8 value8, omc_u32 rounds)
{
    omc_u32 i;
    omc_u8 out;

    out = value8;
    for (i = 0U; i < rounds; ++i) {
        out = omc_exif_sony_decipher_once(out);
    }
    return out;
}

static int
omc_exif_sony_read_u8(const omc_u8* raw, omc_u64 raw_size, omc_u64 off,
                      omc_u32 rounds, omc_u8* out_value)
{
    if (raw == (const omc_u8*)0 || out_value == (omc_u8*)0 || off >= raw_size) {
        return 0;
    }
    *out_value = omc_exif_sony_decipher(raw[(omc_size)off], rounds);
    return 1;
}

static int
omc_exif_sony_read_u16le(const omc_u8* raw, omc_u64 raw_size, omc_u64 off,
                         omc_u32 rounds, omc_u16* out_value)
{
    omc_u8 b0;
    omc_u8 b1;

    if (raw == (const omc_u8*)0 || out_value == (omc_u16*)0
        || off > raw_size || (raw_size - off) < 2U) {
        return 0;
    }
    b0 = omc_exif_sony_decipher(raw[(omc_size)off + 0U], rounds);
    b1 = omc_exif_sony_decipher(raw[(omc_size)off + 1U], rounds);
    *out_value = (omc_u16)(((omc_u16)b0) | (((omc_u16)b1) << 8));
    return 1;
}

static int
omc_exif_sony_read_i16le(const omc_u8* raw, omc_u64 raw_size, omc_u64 off,
                         omc_u32 rounds, omc_s16* out_value)
{
    omc_u16 raw16;

    if (!omc_exif_sony_read_u16le(raw, raw_size, off, rounds, &raw16)
        || out_value == (omc_s16*)0) {
        return 0;
    }
    *out_value = (omc_s16)raw16;
    return 1;
}

static int
omc_exif_sony_read_u32le(const omc_u8* raw, omc_u64 raw_size, omc_u64 off,
                         omc_u32 rounds, omc_u32* out_value)
{
    omc_u8 b0;
    omc_u8 b1;
    omc_u8 b2;
    omc_u8 b3;

    if (raw == (const omc_u8*)0 || out_value == (omc_u32*)0
        || off > raw_size || (raw_size - off) < 4U) {
        return 0;
    }
    b0 = omc_exif_sony_decipher(raw[(omc_size)off + 0U], rounds);
    b1 = omc_exif_sony_decipher(raw[(omc_size)off + 1U], rounds);
    b2 = omc_exif_sony_decipher(raw[(omc_size)off + 2U], rounds);
    b3 = omc_exif_sony_decipher(raw[(omc_size)off + 3U], rounds);
    *out_value = ((omc_u32)b0) | (((omc_u32)b1) << 8)
                 | (((omc_u32)b2) << 16) | (((omc_u32)b3) << 24);
    return 1;
}

static omc_u32
omc_exif_sony_guess_rounds(const omc_u8* raw, omc_u64 raw_size, omc_u64 off,
                           const omc_u8* allowed, omc_u32 allowed_count)
{
    omc_u8 value8;
    omc_u32 i;

    if (allowed == (const omc_u8*)0 || allowed_count == 0U) {
        return 1U;
    }
    if (omc_exif_sony_read_u8(raw, raw_size, off, 1U, &value8)) {
        for (i = 0U; i < allowed_count; ++i) {
            if (value8 == allowed[i]) {
                return 1U;
            }
        }
    }
    if (omc_exif_sony_read_u8(raw, raw_size, off, 2U, &value8)) {
        for (i = 0U; i < allowed_count; ++i) {
            if (value8 == allowed[i]) {
                return 2U;
            }
        }
    }
    return 1U;
}

static omc_exif_status
omc_exif_emit_derived_exif_sony_deciphered_bytes(omc_exif_ctx* ctx,
                                                 const char* ifd_name,
                                                 omc_u16 tag,
                                                 omc_u32 order_in_block,
                                                 const omc_u8* raw,
                                                 omc_u64 raw_size,
                                                 omc_u64 off,
                                                 omc_u32 byte_count,
                                                 omc_u32 rounds)
{
    omc_byte_ref ref;
    omc_mut_bytes view;
    omc_exif_status status;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ifd_name == (const char*)0
        || raw == (const omc_u8*)0) {
        return OMC_EXIF_MALFORMED;
    }
    if (off > raw_size || (omc_u64)byte_count > (raw_size - off)) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only) {
        return omc_exif_emit_derived_exif_bytes(ctx, ifd_name, tag,
                                                order_in_block,
                                                raw + (omc_size)off,
                                                byte_count);
    }

    status = omc_exif_store_ref(ctx, raw + (omc_size)off, byte_count, &ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }

    view = omc_arena_view_mut(&ctx->store->arena, ref);
    if (view.data == (omc_u8*)0 || view.size < byte_count) {
        return OMC_EXIF_MALFORMED;
    }
    for (i = 0U; i < byte_count; ++i) {
        view.data[i] = omc_exif_sony_decipher(view.data[i], rounds);
    }

    return omc_exif_emit_derived_exif_bytes_ref(ctx, ifd_name, tag,
                                                order_in_block, ref);
}

static omc_exif_status
omc_exif_emit_derived_exif_u8_array(omc_exif_ctx* ctx, const char* ifd_name,
                                    omc_u16 tag, omc_u32 order_in_block,
                                    const omc_u8* values, omc_u32 count)
{
    return omc_exif_emit_derived_exif_array_copy(ctx, ifd_name, tag,
                                                 order_in_block, OMC_ELEM_U8,
                                                 values, count, count);
}

static omc_exif_status
omc_exif_emit_derived_exif_u16_array_le(omc_exif_ctx* ctx, const char* ifd_name,
                                        omc_u16 tag, omc_u32 order_in_block,
                                        const omc_u16* values, omc_u32 count)
{
    omc_u8 raw_buf[128];
    omc_u32 i;

    if (values == (const omc_u16*)0 || count > 64U) {
        return OMC_EXIF_MALFORMED;
    }
    for (i = 0U; i < count; ++i) {
        raw_buf[(i * 2U) + 0U] = (omc_u8)(values[i] & 0xFFU);
        raw_buf[(i * 2U) + 1U] = (omc_u8)((values[i] >> 8) & 0xFFU);
    }
    return omc_exif_emit_derived_exif_array_copy(ctx, ifd_name, tag,
                                                 order_in_block, OMC_ELEM_U16,
                                                 raw_buf, count * 2U, count);
}

static omc_exif_status
omc_exif_emit_derived_exif_i16_array_le(omc_exif_ctx* ctx, const char* ifd_name,
                                        omc_u16 tag, omc_u32 order_in_block,
                                        const omc_s16* values, omc_u32 count)
{
    omc_u8 raw_buf[128];
    omc_u16 raw16;
    omc_u32 i;

    if (values == (const omc_s16*)0 || count > 64U) {
        return OMC_EXIF_MALFORMED;
    }
    for (i = 0U; i < count; ++i) {
        raw16 = (omc_u16)values[i];
        raw_buf[(i * 2U) + 0U] = (omc_u8)(raw16 & 0xFFU);
        raw_buf[(i * 2U) + 1U] = (omc_u8)((raw16 >> 8) & 0xFFU);
    }
    return omc_exif_emit_derived_exif_array_copy(ctx, ifd_name, tag,
                                                 order_in_block, OMC_ELEM_I16,
                                                 raw_buf, count * 2U, count);
}

static int
omc_exif_decode_sony_cipher_fields(omc_exif_ctx* ctx, const omc_u8* raw,
                                   omc_u64 raw_size, const char* subtable,
                                   omc_u32 rounds,
                                   const omc_exif_sony_field* fields,
                                   omc_u32 field_count)
{
    char ifd_name[64];
    omc_u32 order;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || subtable == (const char*)0 || fields == (const omc_exif_sony_field*)0
        || !omc_exif_make_subifd_name("mk_sony", subtable, 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    for (i = 0U; i < field_count; ++i) {
        const omc_exif_sony_field* field;
        omc_exif_status status;

        field = &fields[i];
        status = OMC_EXIF_OK;
        if (field->tag == 0U) {
            continue;
        }

        switch ((omc_exif_sony_field_kind)field->kind) {
        case OMC_EXIF_SONY_F_U8: {
            omc_u8 value8;

            if (!omc_exif_sony_read_u8(raw, raw_size, field->tag, rounds,
                                       &value8)) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, field->tag,
                                                   order++, value8);
            break;
        }
        case OMC_EXIF_SONY_F_U16LE: {
            omc_u16 value16;

            if (!omc_exif_sony_read_u16le(raw, raw_size, field->tag, rounds,
                                          &value16)) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, field->tag,
                                                    order++, value16);
            break;
        }
        case OMC_EXIF_SONY_F_U32LE: {
            omc_u32 value32;

            if (!omc_exif_sony_read_u32le(raw, raw_size, field->tag, rounds,
                                          &value32)) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, field->tag,
                                                    order++, value32);
            break;
        }
        case OMC_EXIF_SONY_F_I16LE: {
            omc_s16 value16;

            if (!omc_exif_sony_read_i16le(raw, raw_size, field->tag, rounds,
                                          &value16)) {
                continue;
            }
            status = omc_exif_emit_derived_exif_i16(ctx, ifd_name, field->tag,
                                                    order++, value16);
            break;
        }
        case OMC_EXIF_SONY_F_U8_ARRAY: {
            omc_u8 tmp[64];
            omc_u32 j;
            int ok;

            if (field->count == 0U || field->count > 64U) {
                continue;
            }
            ok = 1;
            for (j = 0U; j < field->count; ++j) {
                ok = ok && omc_exif_sony_read_u8(raw, raw_size,
                                                 (omc_u64)field->tag + j,
                                                 rounds, &tmp[j]);
            }
            if (!ok) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u8_array(ctx, ifd_name,
                                                         field->tag, order++,
                                                         tmp, field->count);
            break;
        }
        case OMC_EXIF_SONY_F_U16LE_ARRAY: {
            omc_u16 tmp[64];
            omc_u32 j;
            int ok;

            if (field->count == 0U || field->count > 64U) {
                continue;
            }
            ok = 1;
            for (j = 0U; j < field->count; ++j) {
                ok = ok && omc_exif_sony_read_u16le(
                               raw, raw_size,
                               (omc_u64)field->tag + ((omc_u64)j * 2U),
                               rounds, &tmp[j]);
            }
            if (!ok) {
                continue;
            }
            status = omc_exif_emit_derived_exif_u16_array_le(
                ctx, ifd_name, field->tag, order++, tmp, field->count);
            break;
        }
        case OMC_EXIF_SONY_F_I16LE_ARRAY: {
            omc_s16 tmp[64];
            omc_u32 j;
            int ok;

            if (field->count == 0U || field->count > 64U) {
                continue;
            }
            ok = 1;
            for (j = 0U; j < field->count; ++j) {
                ok = ok && omc_exif_sony_read_i16le(
                               raw, raw_size,
                               (omc_u64)field->tag + ((omc_u64)j * 2U),
                               rounds, &tmp[j]);
            }
            if (!ok) {
                continue;
            }
            status = omc_exif_emit_derived_exif_i16_array_le(
                ctx, ifd_name, field->tag, order++, tmp, field->count);
            break;
        }
        case OMC_EXIF_SONY_F_BYTES: {
            omc_u8 tmp[64];
            omc_u32 j;
            int ok;

            if (field->count == 0U || field->count > 64U) {
                continue;
            }
            ok = 1;
            for (j = 0U; j < field->count; ++j) {
                ok = ok && omc_exif_sony_read_u8(raw, raw_size,
                                                 (omc_u64)field->tag + j,
                                                 rounds, &tmp[j]);
            }
            if (!ok) {
                continue;
            }
            status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name,
                                                      field->tag, order++, tmp,
                                                      field->count);
            break;
        }
        default:
            continue;
        }

        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    return 1;
}

static int
omc_exif_read_u16_endian_raw(int little_endian, const omc_u8* raw,
                             omc_u64 raw_size, omc_u64 off,
                             omc_u16* out_value)
{
    if (little_endian) {
        return omc_exif_read_u16le_raw(raw, raw_size, off, out_value);
    }
    return omc_exif_read_u16be_raw(raw, raw_size, off, out_value);
}

static int omc_exif_decode_sony_faces(omc_exif_ctx *ctx, const omc_u8 *raw,
                                      omc_u64 size)
{
    omc_u16 off, len, count, rects[8][4];
    omc_u32 i, j;
    int le;
    const char *name;
    omc_exif_status status;
    if (size < 0x34U ||
        !((raw[0] == 'I' && raw[1] == 'I') || (raw[0] == 'M' && raw[1] == 'M')))
        return 1;
    le = raw[0] == 'I';
    (void)omc_exif_read_u16_endian_raw(le, raw, size, 2U, &off);
    (void)omc_exif_read_u16_endian_raw(le, raw, size, 0x30U, &count);
    (void)omc_exif_read_u16_endian_raw(le, raw, size, 0x32U, &len);
    if (count == 0U)
        return 1;
    if (off == 0x48U && len == 0x20U)
        name = "mk_sony_faceinfo1_0";
    else if (off == 0x5eU && len == 0x25U)
        name = "mk_sony_faceinfo2_0";
    else
        return 1;
    if (count > 8U)
        count = 8U;
    if ((omc_u64)off + (omc_u64)len * count > size)
        return 1;
    for (i = 0U; i < count; ++i)
        for (j = 0U; j < 4U; ++j)
            (void)omc_exif_read_u16_endian_raw(le, raw, size, off + len * i + j * 2U,
                                               &rects[i][j]);
    for (i = 0U; i < count; ++i) {
        if (ctx->res.entries_decoded >= ctx->opts.limits.max_total_entries) {
            omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, 0U,
                                (omc_u16)(len * i));
            return 0;
        }
        status = omc_exif_emit_derived_exif_u16_array_le(ctx, name, (omc_u16)(len * i),
                                                         i, rects[i], 4U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        ctx->res.entries_decoded++;
    }
    return 1;
}

static int
omc_exif_decode_sony_shotinfo(omc_exif_ctx* ctx, const omc_u8* raw,
                              omc_u64 raw_size)
{
    char ifd_name[64];
    omc_u16 value16;
    omc_u32 text_size;
    int little_endian;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 0x44U
        || !omc_exif_make_subifd_name("mk_sony", "shotinfo", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    if (raw[0U] == (omc_u8)'I' && raw[1U] == (omc_u8)'I') {
        little_endian = 1;
    } else if (raw[0U] == (omc_u8)'M' && raw[1U] == (omc_u8)'M') {
        little_endian = 0;
    } else {
        return 1;
    }

    if (omc_exif_read_u16_endian_raw(little_endian, raw, raw_size, 0x0002U,
                                     &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0002U, 0U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    text_size = omc_exif_trim_nul_size(raw + 0x0006U, 20U);
    if (ctx->opts.limits.max_value_bytes >= 20U) {
        status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0006U, 1U,
                                                 raw + 0x0006U, text_size);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    if (omc_exif_read_u16_endian_raw(little_endian, raw, raw_size, 0x001AU,
                                     &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x001AU, 2U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (omc_exif_read_u16_endian_raw(little_endian, raw, raw_size, 0x001CU,
                                     &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x001CU, 3U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (omc_exif_read_u16_endian_raw(little_endian, raw, raw_size, 0x0030U,
                                     &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0030U, 4U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (omc_exif_read_u16_endian_raw(little_endian, raw, raw_size, 0x0032U,
                                     &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0032U, 5U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    text_size = omc_exif_trim_nul_size(raw + 0x0034U, 16U);
    if (ctx->opts.limits.max_value_bytes >= 16U) {
        status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0034U, 6U,
                                                 raw + 0x0034U, text_size);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_sony_meterinfo(omc_exif_ctx* ctx, const omc_u8* raw,
                               omc_u64 raw_size, omc_u32 rounds,
                               omc_u16 meter_off)
{
    static const struct {
        omc_u16 tag;
        omc_u16 off;
        omc_u16 size;
    } rows[] = {
        { 0x0000U, 0x0000U, 0x006CU }, { 0x006CU, 0x006CU, 0x006CU },
        { 0x00D8U, 0x00D8U, 0x006CU }, { 0x0144U, 0x0144U, 0x006CU },
        { 0x01B0U, 0x01B0U, 0x006CU }, { 0x021CU, 0x021CU, 0x006CU },
        { 0x0288U, 0x0288U, 0x006CU }, { 0x02F4U, 0x02F4U, 0x0084U },
        { 0x0378U, 0x0378U, 0x0084U }, { 0x03FCU, 0x03FCU, 0x0084U },
        { 0x0480U, 0x0480U, 0x0084U }, { 0x0504U, 0x0504U, 0x0084U },
        { 0x0588U, 0x0588U, 0x0084U }, { 0x060CU, 0x060CU, 0x0084U },
        { 0x0690U, 0x0690U, 0x0084U }, { 0x0714U, 0x0714U, 0x0084U }
    };
    char ifd_name[64];
    omc_u32 i;
    omc_u32 order;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "meterinfo", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    for (i = 0U; i < (omc_u32)(sizeof(rows) / sizeof(rows[0])); ++i) {
        omc_u8 tmp[160];
        omc_u16 abs_off;
        omc_u32 j;
        int ok;
        omc_exif_status status;

        if (rows[i].size == 0U
            || rows[i].size > ctx->opts.limits.max_value_bytes
            || rows[i].size > (omc_u16)sizeof(tmp)) {
            continue;
        }
        abs_off = (omc_u16)(meter_off + rows[i].off);
        if ((omc_u64)abs_off + (omc_u64)rows[i].size > raw_size) {
            continue;
        }

        ok = 1;
        for (j = 0U; j < (omc_u32)rows[i].size; ++j) {
            ok = ok && omc_exif_sony_read_u8(raw, raw_size,
                                             (omc_u64)abs_off + j, rounds,
                                             &tmp[j]);
        }
        if (!ok) {
            continue;
        }

        status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name, rows[i].tag,
                                                  order++, tmp, rows[i].size);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_sony_meterinfo9(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size)
{
    static const struct {
        omc_u16 tag;
        omc_u16 size;
    } rows[] = {
        { 0x0000U, 0x005AU }, { 0x005AU, 0x005AU }, { 0x00B4U, 0x005AU },
        { 0x010EU, 0x005AU }, { 0x0168U, 0x005AU }, { 0x01C2U, 0x005AU },
        { 0x021CU, 0x005AU }, { 0x0276U, 0x006EU }, { 0x02E4U, 0x006EU },
        { 0x0352U, 0x006EU }, { 0x03C0U, 0x006EU }, { 0x042EU, 0x006EU },
        { 0x049CU, 0x006EU }, { 0x050AU, 0x006EU }, { 0x0578U, 0x006EU },
        { 0x05E6U, 0x006EU }
    };
    char ifd_name[64];
    omc_u32 i;
    omc_u32 order;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "meterinfo9", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    for (i = 0U; i < (omc_u32)(sizeof(rows) / sizeof(rows[0])); ++i) {
        omc_u8 tmp[128];
        omc_u32 j;
        int ok;
        omc_exif_status status;

        if ((omc_u64)rows[i].tag + (omc_u64)rows[i].size > raw_size
            || rows[i].size == 0U
            || rows[i].size > ctx->opts.limits.max_value_bytes
            || rows[i].size > (omc_u16)sizeof(tmp)) {
            continue;
        }

        ok = 1;
        for (j = 0U; j < (omc_u32)rows[i].size; ++j) {
            ok = ok && omc_exif_sony_read_u8(raw, raw_size,
                                             (omc_u64)rows[i].tag + j, 1U,
                                             &tmp[j]);
        }
        if (!ok) {
            continue;
        }

        status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name, rows[i].tag,
                                                  order++, tmp, rows[i].size);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_sony_afstatus(omc_exif_ctx* ctx, const omc_u8* raw,
                              omc_u64 raw_size, omc_u32 rounds,
                              omc_u16 base_off, omc_u32 count,
                              const char* subtable)
{
    char ifd_name[64];
    omc_u32 i;
    omc_u32 order;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || subtable == (const char*)0
        || !omc_exif_make_subifd_name("mk_sony", subtable, 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    for (i = 0U; i < count; ++i) {
        omc_s16 value16;
        omc_exif_status status;
        omc_u16 tag16;
        omc_u64 off;

        tag16 = (omc_u16)(i * 2U);
        off = (omc_u64)base_off + (omc_u64)tag16;
        if (!omc_exif_sony_read_i16le(raw, raw_size, off, rounds, &value16)) {
            continue;
        }
        status = omc_exif_emit_derived_exif_i16(ctx, ifd_name, tag16, order++,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_sony_afinfo(omc_exif_ctx* ctx, const omc_u8* raw,
                            omc_u64 raw_size)
{
    static const omc_u8 allowed_af_type[] = { 0U, 1U, 2U, 3U, 6U, 9U, 11U };
    static const omc_u16 u8_tags[] = { 0x0002U, 0x0004U, 0x0007U, 0x0008U,
                                       0x0009U, 0x000AU, 0x000BU };
    char ifd_name[64];
    omc_u32 rounds;
    omc_u8 af_type;
    omc_u32 order;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "afinfo", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    rounds = omc_exif_sony_guess_rounds(
        raw, raw_size, 0x0002U, allowed_af_type,
        (omc_u32)(sizeof(allowed_af_type) / sizeof(allowed_af_type[0])));
    order = 0U;
    af_type = 0U;

    for (i = 0U; i < (omc_u32)(sizeof(u8_tags) / sizeof(u8_tags[0])); ++i) {
        omc_u8 value8;
        omc_exif_status status;

        if (!omc_exif_sony_read_u8(raw, raw_size, u8_tags[i], rounds, &value8)) {
            continue;
        }
        if (u8_tags[i] == 0x0002U) {
            af_type = value8;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, u8_tags[i],
                                               order++, value8);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    {
        omc_u32 value32;
        omc_exif_status status;

        if (omc_exif_sony_read_u32le(raw, raw_size, 0x016EU, rounds, &value32)) {
            status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x016EU,
                                                    order++, value32);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
    }
    {
        omc_u8 value8;
        omc_exif_status status;

        if (omc_exif_sony_read_u8(raw, raw_size, 0x017DU, rounds, &value8)) {
            status = omc_exif_emit_derived_exif_i8(ctx, ifd_name, 0x017DU,
                                                   order++, (omc_s8)value8);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
        if (omc_exif_sony_read_u8(raw, raw_size, 0x017EU, rounds, &value8)) {
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x017EU,
                                                   order++, value8);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
    }

    if (af_type == 2U) {
        return omc_exif_decode_sony_afstatus(ctx, raw, raw_size, rounds,
                                             0x0011U, 30U, "afstatus19");
    }
    if (af_type == 1U) {
        return omc_exif_decode_sony_afstatus(ctx, raw, raw_size, rounds,
                                             0x0011U, 18U, "afstatus15");
    }

    return 1;
}

static int
omc_exif_decode_sony_tag940e(omc_exif_ctx* ctx, const omc_u8* raw,
                             omc_u64 raw_size)
{
    char ifd_name[64];
    omc_u8 width8;
    omc_u8 height8;
    omc_u32 rounds;
    omc_u32 image_bytes;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "tag940e", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    rounds = 1U;
    width8 = 0U;
    height8 = 0U;
    if (!omc_exif_sony_read_u8(raw, raw_size, 0x1A06U, 1U, &width8)
        || !omc_exif_sony_read_u8(raw, raw_size, 0x1A07U, 1U, &height8)
        || width8 == 0U || height8 == 0U) {
        rounds = 2U;
        if (!omc_exif_sony_read_u8(raw, raw_size, 0x1A06U, 2U, &width8)
            || !omc_exif_sony_read_u8(raw, raw_size, 0x1A07U, 2U, &height8)
            || width8 == 0U || height8 == 0U) {
            return 1;
        }
    }

    image_bytes = ((omc_u32)width8 * (omc_u32)height8) * 2U;
    if (image_bytes == 0U || image_bytes > ctx->opts.limits.max_value_bytes
        || (omc_u64)0x1A08U + (omc_u64)image_bytes > raw_size) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x1A06U, 0U, width8);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x1A07U, 1U, height8);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_sony_deciphered_bytes(
        ctx, ifd_name, 0x1A08U, 2U, raw, raw_size, 0x1A08U, image_bytes, rounds);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    return 1;
}

static int omc_exif_decode_sony_tag9405b(omc_exif_ctx *ctx, const omc_u8 *raw,
                                         omc_u64 raw_size, const char *model)
{
    char ifd_name[64];
    omc_u16 u16_tags[10];
    omc_u16 value16;
    omc_u16 i16_tags[5];
    omc_u16 tag16;
    omc_u32 value32;
    omc_u8 value8;
    omc_u32 i;
    omc_s16 i16v[32];
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "tag9405b", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    u16_tags[0] = 0x0004U;
    u16_tags[1] = 0x0006U;
    u16_tags[2] = 0x000AU;
    u16_tags[3] = 0x000EU;
    u16_tags[4] = 0x0014U;
    u16_tags[5] = 0x0016U;
    u16_tags[6] = 0x003EU;
    u16_tags[7] = 0x0040U;
    u16_tags[8] = sony_tag9405b_lens_zoom_tag(model);
    for (i = 0U; i < 9U; ++i) {
        if (!omc_exif_sony_read_u16le(raw, raw_size, u16_tags[i], 1U,
                                      &value16)) {
            continue;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, u16_tags[i], i,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    for (i = 0U; i < 11U; ++i) {
        static const omc_u16 u8_tags[] = {
            0x0034U, 0x0042U, 0x0044U, 0x0046U, 0x0048U, 0x004AU,
            0x0052U, 0x005AU, 0x005BU, 0x005DU, 0x005EU
        };
        if (!omc_exif_sony_read_u8(raw, raw_size, u8_tags[i], 1U, &value8)) {
            continue;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, u8_tags[i],
                                               10U + i, value8);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    if (omc_exif_sony_read_u32le(raw, raw_size, 0x0010U, 1U, &value32)) {
        omc_u32 denom;

        if (omc_exif_sony_read_u32le(raw, raw_size, 0x0014U, 1U, &denom)) {
            status = omc_exif_emit_derived_exif_urational(
                ctx, ifd_name, 0x0010U, 21U, value32, denom);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
    }

    if (omc_exif_sony_read_u32le(raw, raw_size, 0x0024U, 1U, &value32)) {
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0024U, 22U,
                                                value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    tag16 = 0x0060U;
    for (i = 0U; i < 2U; ++i) {
        if (!omc_exif_sony_read_u16le(raw, raw_size, tag16, 1U, &value16)) {
            tag16 = (omc_u16)(tag16 + 2U);
            continue;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, tag16,
                                                23U + i, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        tag16 = (omc_u16)(tag16 + 2U);
    }

    for (i = 0U; i < 16U; ++i) {
        if (!omc_exif_sony_read_i16le(raw, raw_size, 0x0064U + (i * 2U), 1U,
                                      &i16v[i])) {
            break;
        }
    }
    if (i == 16U) {
        status = omc_exif_emit_derived_exif_i16_array_le(ctx, ifd_name,
                                                         0x0064U, 25U, i16v,
                                                         16U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    i16_tags[0] = sony_tag9405b_vignetting_tag(model);
    for (i = 0U; i < 1U; ++i) {
        omc_u32 j;
        if (i16_tags[i] == 0U)
            continue;
        for (j = 0U; j < 16U; ++j) {
            if (!omc_exif_sony_read_i16le(raw, raw_size,
                                          (omc_u64)i16_tags[i]
                                              + ((omc_u64)j * 2U),
                                          1U, &i16v[j])) {
                break;
            }
        }
        if (j != 16U) {
            continue;
        }
        status = omc_exif_emit_derived_exif_i16_array_le(
            ctx, ifd_name, i16_tags[i], 26U + i, i16v, 16U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    i16_tags[0] = sony_tag9405b_chromatic_tag(model);
    for (i = 0U; i < 1U; ++i) {
        omc_u32 j;
        if (i16_tags[i] == 0U)
            continue;
        for (j = 0U; j < 32U; ++j) {
            if (!omc_exif_sony_read_i16le(raw, raw_size,
                                          (omc_u64)i16_tags[i]
                                              + ((omc_u64)j * 2U),
                                          1U, &i16v[j])) {
                break;
            }
        }
        if (j != 32U) {
            continue;
        }
        status = omc_exif_emit_derived_exif_i16_array_le(
            ctx, ifd_name, i16_tags[i], 30U + i, i16v, 32U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_sony_tag9400(omc_exif_ctx* ctx, const omc_u8* raw,
                             omc_u64 raw_size)
{
    static const omc_u8 allowed[] = { 0x07U, 0x09U, 0x0AU, 0x0CU, 0x23U, 0x24U,
                                      0x26U, 0x28U, 0x31U, 0x32U, 0x33U };
    char ifd_name[64];
    omc_u32 rounds;
    omc_u32 value32;
    omc_u16 value16;
    omc_u8 value8;
    omc_u16 u8_tags[6];
    omc_u32 i;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "tag9400c", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    rounds = omc_exif_sony_guess_rounds(raw, raw_size, 0U, allowed,
                                        (omc_u32)(sizeof(allowed)
                                                  / sizeof(allowed[0])));
    if (omc_exif_sony_read_u32le(raw, raw_size, 0x0012U, rounds, &value32)) {
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0012U, 0U,
                                                value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (omc_exif_sony_read_u32le(raw, raw_size, 0x001AU, rounds, &value32)) {
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x001AU, 1U,
                                                value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (omc_exif_sony_read_u16le(raw, raw_size, 0x0053U, rounds, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0053U, 2U,
                                                value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    u8_tags[0] = 0x0009U;
    u8_tags[1] = 0x000AU;
    u8_tags[2] = 0x0016U;
    u8_tags[3] = 0x001EU;
    u8_tags[4] = 0x0029U;
    u8_tags[5] = 0x002AU;
    for (i = 0U; i < 6U; ++i) {
        if (!omc_exif_sony_read_u8(raw, raw_size, u8_tags[i], rounds,
                                   &value8)) {
            continue;
        }
        if (u8_tags[i] == 0x000AU) {
            if (!omc_exif_sony_read_u32le(raw, raw_size, 0x000AU, rounds, &value32))
                continue;
            status =
                omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x000AU, 3U + i, value32);
        } else
            status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, u8_tags[i], 3U + i,
                                                   value8);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    return 1;
}

static int omc_exif_decode_sony_tag9416(omc_exif_ctx *ctx, const omc_u8 *raw,
                                        omc_u64 raw_size, const char *model)
{
    static const omc_u8 allowed_versions[] = {
        0x06U, 0x07U, 0x08U, 0x09U, 0x0CU, 0x0DU,
        0x0FU, 0x10U, 0x11U, 0x17U, 0x1BU
    };
    char ifd_name[64];
    omc_u32 rounds;
    omc_u16 vignette, apsc, chromatic;
    omc_u8 value8;
    omc_u16 value16;
    omc_u32 value32;
    omc_s16 i16v[32];
    omc_u32 i;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "tag9416", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    rounds = omc_exif_sony_guess_rounds(
        raw, raw_size, 0U, allowed_versions,
        (omc_u32)(sizeof(allowed_versions) / sizeof(allowed_versions[0])));

    for (i = 0U; i < 8U; ++i) {
        static const omc_u16 u8_tags[] = {
            0x0000U, 0x002BU, 0x0035U, 0x0037U,
            0x0048U, 0x0049U, 0x004AU, 0x0070U
        };
        if (!omc_exif_sony_read_u8(raw, raw_size, u8_tags[i], rounds,
                                   &value8)) {
            continue;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, u8_tags[i], i,
                                               value8);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    for (i = 0U; i < 9U; ++i) {
        static const omc_u16 u16_tags[] = {
            0x0004U, 0x0006U, 0x000AU, 0x0010U, 0x0012U,
            0x004BU, 0x0071U, 0x0073U, 0x0075U
        };
        if (!omc_exif_sony_read_u16le(raw, raw_size, u16_tags[i], rounds,
                                      &value16)) {
            continue;
        }
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, u16_tags[i],
                                                8U + i, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    if (omc_exif_sony_read_u32le(raw, raw_size, 0x001DU, rounds, &value32)) {
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x001DU, 17U,
                                                value32);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    if (omc_exif_sony_read_u32le(raw, raw_size, 0x000CU, rounds, &value32)) {
        omc_u32 denom;

        if (omc_exif_sony_read_u32le(raw, raw_size, 0x0010U, rounds,
                                     &denom)) {
            status = omc_exif_emit_derived_exif_urational(
                ctx, ifd_name, 0x000CU, 18U, value32, denom);
            if (status != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, status);
                return 0;
            }
        }
    }

    for (i = 0U; i < 16U; ++i) {
        if (!omc_exif_sony_read_i16le(raw, raw_size, 0x004FU + (i * 2U),
                                      rounds, &i16v[i])) {
            break;
        }
    }
    if (i == 16U) {
        status = omc_exif_emit_derived_exif_i16_array_le(ctx, ifd_name,
                                                         0x004FU, 19U, i16v,
                                                         16U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    vignette = sony_tag9416_vignetting_tag(model);
    apsc = sony_tag9416_apsc_tag(model);
    chromatic = sony_tag9416_chromatic_tag(model);
    for (i = 0U; i < 32U; ++i) {
        if (!omc_exif_sony_read_i16le(raw, raw_size, (omc_u64)vignette + (i * 2U),
                                      rounds, &i16v[i])) {
            break;
        }
    }
    if (i == 32U && vignette != 0U) {
        status = omc_exif_emit_derived_exif_i16_array_le(ctx, ifd_name, vignette, 20U,
                                                         i16v, 32U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    if (apsc != 0U && omc_exif_sony_read_u8(raw, raw_size, apsc, rounds, &value8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, apsc, 21U, value8);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    for (i = 0U; i < 32U; ++i) {
        if (!omc_exif_sony_read_i16le(raw, raw_size, (omc_u64)chromatic + (i * 2U),
                                      rounds, &i16v[i])) {
            break;
        }
    }
    if (i == 32U && chromatic != 0U) {
        status = omc_exif_emit_derived_exif_i16_array_le(ctx, ifd_name, chromatic, 22U,
                                                         i16v, 32U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_sony_isoinfo(omc_exif_ctx* ctx, const omc_u8* raw,
                             omc_u64 raw_size)
{
    static const omc_u16 iso_offsets[] = {
        0x03E2U, 0x03F4U, 0x044EU, 0x0498U, 0x049DU, 0x049EU, 0x04A1U, 0x04A2U,
        0x04BAU, 0x059DU, 0x0634U, 0x0636U, 0x064CU, 0x0653U, 0x0678U, 0x06B8U,
        0x06DEU, 0x06E7U
    };
    char ifd_name[64];
    omc_u16 best_off;
    omc_u32 best_score;
    omc_u8 best_setting;
    omc_u8 best_min;
    omc_u8 best_max;
    omc_u32 i;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "isoinfo", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    best_off = 0U;
    best_score = 0U;
    best_setting = 0U;
    best_min = 0U;
    best_max = 0U;
    for (i = 0U; i < (omc_u32)(sizeof(iso_offsets) / sizeof(iso_offsets[0]));
         ++i) {
        omc_u64 base;
        omc_u8 iso_setting;
        omc_u8 iso_min;
        omc_u8 iso_max;
        omc_u32 score;

        base = iso_offsets[i];
        if (!omc_exif_sony_read_u8(raw, raw_size, base + 0U, 1U, &iso_setting)
            || !omc_exif_sony_read_u8(raw, raw_size, base + 2U, 1U, &iso_min)
            || !omc_exif_sony_read_u8(raw, raw_size, base + 4U, 1U,
                                      &iso_max)) {
            continue;
        }
        score = 0U;
        if (iso_setting <= 80U) {
            score += 1U;
        }
        if (iso_min <= 80U) {
            score += 1U;
        }
        if (iso_max <= 80U) {
            score += 1U;
        }
        if (iso_setting == 0U) {
            score += 1U;
        }
        if (score > best_score) {
            best_off = iso_offsets[i];
            best_score = score;
            best_setting = iso_setting;
            best_min = iso_min;
            best_max = iso_max;
        }
    }

    if (best_score == 0U) {
        return 1;
    }
    (void)best_off;

    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0000U, 0U,
                                           best_setting);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0002U, 1U,
                                           best_min);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0004U, 2U,
                                           best_max);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    return 1;
}

static int
omc_exif_sony_model_has_tag2010e_1254(const omc_u8* model_text,
                                      omc_u32 model_size)
{
    if (model_text == (const omc_u8*)0) {
        return 0;
    }
    return omc_exif_ascii_equals_nocase(model_text, model_size, "SLT-A99")
           || omc_exif_ascii_equals_nocase(model_text, model_size, "SLT-A99V")
           || omc_exif_ascii_equals_nocase(model_text, model_size, "HV")
           || omc_exif_ascii_equals_nocase(model_text, model_size, "NEX-5R")
           || omc_exif_ascii_equals_nocase(model_text, model_size, "NEX-5T")
           || omc_exif_ascii_equals_nocase(model_text, model_size, "NEX-6")
           || omc_exif_ascii_equals_nocase(model_text, model_size,
                                           "NEX-VG900")
           || omc_exif_ascii_equals_nocase(model_text, model_size,
                                           "NEX-VG30E")
           || omc_exif_ascii_equals_nocase(model_text, model_size,
                                           "DSC-RX100")
           || omc_exif_ascii_equals_nocase(model_text, model_size, "Stellar");
}

static int
omc_exif_sony_model_has_tag2010e_1258(const omc_u8* model_text,
                                      omc_u32 model_size)
{
    if (model_text == (const omc_u8*)0) {
        return 0;
    }
    return omc_exif_ascii_equals_nocase(model_text, model_size, "DSC-RX1")
           || omc_exif_ascii_equals_nocase(model_text, model_size, "DSC-RX1R");
}

static int
omc_exif_sony_model_has_tag2010e_focal_fields(const omc_u8* model_text,
                                              omc_u32 model_size)
{
    static const char *models[] = {"SLT-A58",   "ILCE-3000", "ILCE-3500", "NEX-3N",
                                   "DSC-HX300", "DSC-HX50V", "DSC-WX60",  "DSC-WX80",
                                   "DSC-WX200", "DSC-WX300", "DSC-TX30"};
    omc_u32 i;
    if (model_text == NULL)
        return 0;
    for (i = 0U; i < sizeof(models) / sizeof(models[0]); ++i)
        if (omc_exif_ascii_equals_nocase(model_text, model_size, models[i]))
            return 1;
    return 0;
}

static int
omc_exif_decode_sony_tag2010e_model_fields(omc_exif_ctx* ctx,
                                           const omc_u8* raw,
                                           omc_u64 raw_size,
                                           const omc_u8* model_text,
                                           omc_u32 model_size)
{
    char ifd_name[64];
    omc_u16 value16;
    omc_u32 order_in_block;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_sony", "tag2010e", 0U, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    order_in_block = 100U;
    if (omc_exif_sony_model_has_tag2010e_1254(model_text, model_size) &&
        omc_exif_sony_read_u16le(raw, raw_size, 0x1254U, 1U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x1254U,
                                                order_in_block++, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (omc_exif_find_first_entry(ctx->store, ifd_name, 0x1258U)
            == (const omc_entry*)0
        && omc_exif_sony_model_has_tag2010e_1258(model_text, model_size)
        && omc_exif_sony_read_u16le(raw, raw_size, 0x1258U, 1U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x1258U,
                                                order_in_block++, value16);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (omc_exif_sony_model_has_tag2010e_focal_fields(model_text, model_size)) {
        static const omc_u16 tags[] = {0x1278U, 0x127aU, 0x127cU, 0x1280U};
        omc_u32 i;
        omc_u16 values[4];
        int valid[4];
        for (i = 0U; i < 4U; ++i)
            valid[i] = omc_exif_sony_read_u16le(raw, raw_size, tags[i], 1U, &values[i]);
        for (i = 0U; i < 4U; ++i)
            if (valid[i]) {
                status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, tags[i],
                                                        order_in_block++, values[i]);
                if (status != OMC_EXIF_OK) {
                    omc_exif_update_status(&ctx->res, status);
                    return 0;
                }
            }
    }
    return 1;
}

static int
omc_exif_decode_sony_postpass(omc_exif_ctx* ctx)
{
    static const omc_u8 allowed_9400[] = { 0x07U, 0x09U, 0x0AU, 0x0CU, 0x23U,
                                           0x24U, 0x26U, 0x28U, 0x31U, 0x32U,
                                           0x33U };
    const omc_entry* entry;
    const omc_u8* model_text;
    omc_u32 model_size;
    omc_const_bytes raw;
    int is_slt_family;
    int is_lunar;
    int is_stellar;
    char model[128], variant;
    omc_u8 first, fourth;
    int legacy;
    omc_u32 rounds;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    model_text = (const omc_u8*)0;
    model_size = 0U;
    (void)omc_exif_find_first_text(ctx->store, "ifd0", 0x0110U, &model_text,
                                   &model_size);
    if (model_text == (const omc_u8*)0) {
        (void)omc_exif_find_outer_ifd0_ascii(ctx, 0x0110U, &model_text,
                                             &model_size);
    }
    is_slt_family = model_text != (const omc_u8*)0
                    && (omc_exif_ascii_starts_with_nocase(
                            model_text, model_size, "SLT-")
                        || omc_exif_ascii_starts_with_nocase(
                            model_text, model_size, "ILCA-")
                        || (model_size == 2U && memcmp(model_text, "HV", 2U) == 0));
    is_lunar = model_text != (const omc_u8*)0
               && omc_exif_bytes_contains_text(model_text, model_size,
                                               "Lunar");
    is_stellar = model_text != (const omc_u8*)0
                 && omc_exif_bytes_contains_text(model_text, model_size,
                                                 "Stellar");
    if (model_size >= sizeof(model))
        model_size = sizeof(model) - 1U;
    if (model_text != NULL)
        memcpy(model, model_text, model_size);
    while (model_size > 0U &&
           (model[model_size - 1U] == 0 || model[model_size - 1U] == ' '))
        model_size--;
    model[model_size] = 0;
    model_text = (const omc_u8 *)model;
    legacy = strncmp(model, "SLT-", 4U) == 0 || strcmp(model, "HV") == 0;
    variant = select_sony_tag9050_variant(model, is_slt_family, is_lunar);

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9050U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)) {
        if (variant == 'A') {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9050a", 1U,
                    k_omc_exif_sony_tag9050a_fields,
                    (omc_u32)(sizeof(k_omc_exif_sony_tag9050a_fields)
                              / sizeof(k_omc_exif_sony_tag9050a_fields[0])))) {
                return 0;
            }
        } else if (variant == 'C') {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9050c", 1U,
                    k_omc_exif_sony_tag9050c_fields,
                    (omc_u32)(sizeof(k_omc_exif_sony_tag9050c_fields)
                              / sizeof(k_omc_exif_sony_tag9050c_fields[0])))) {
                return 0;
            }
        } else if (variant == 'D') {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9050d", 1U,
                    k_omc_exif_sony_tag9050d_fields,
                    sizeof(k_omc_exif_sony_tag9050d_fields) /
                        sizeof(k_omc_exif_sony_tag9050d_fields[0])))
                return 0;
        } else if (!omc_exif_decode_sony_cipher_fields(
                       ctx, raw.data, raw.size, "tag9050b", 1U,
                       k_omc_exif_sony_tag9050b_fields,
                       (omc_u32)(sizeof(k_omc_exif_sony_tag9050b_fields) /
                                 sizeof(k_omc_exif_sony_tag9050b_fields[0])))) {
            return 0;
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)) {
        variant = select_sony_tag2010_variant(model);
        switch (variant) {
        case 'A':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010a", 1U,
                    k_omc_exif_sony_tag2010a_fields,
                    sizeof(k_omc_exif_sony_tag2010a_fields) /
                        sizeof(k_omc_exif_sony_tag2010a_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo(ctx, raw.data, raw.size, 1U, 0x4b0U))
                return 0;
            break;
        case 'B':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010b", 1U,
                    k_omc_exif_sony_tag2010b_fields,
                    sizeof(k_omc_exif_sony_tag2010b_fields) /
                        sizeof(k_omc_exif_sony_tag2010b_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo(ctx, raw.data, raw.size, 1U, 0x4b4U))
                return 0;
            break;
        case 'C':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010c", 1U,
                    k_omc_exif_sony_tag2010c_fields,
                    sizeof(k_omc_exif_sony_tag2010c_fields) /
                        sizeof(k_omc_exif_sony_tag2010c_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo(ctx, raw.data, raw.size, 1U, 0x490U))
                return 0;
            break;
        case 'E':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010e", 1U,
                    k_omc_exif_sony_tag2010e_fields,
                    sizeof(k_omc_exif_sony_tag2010e_fields) /
                        sizeof(k_omc_exif_sony_tag2010e_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_tag2010e_model_fields(ctx, raw.data, raw.size,
                                                            model_text, model_size))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo(ctx, raw.data, raw.size, 1U, 0x4b8U))
                return 0;
            break;
        case 'F':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010f", 1U,
                    k_omc_exif_sony_tag2010f_fields,
                    sizeof(k_omc_exif_sony_tag2010f_fields) /
                        sizeof(k_omc_exif_sony_tag2010f_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo(ctx, raw.data, raw.size, 1U, 0x1e0U))
                return 0;
            break;
        case 'G':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010g", 1U,
                    k_omc_exif_sony_tag2010g_fields,
                    sizeof(k_omc_exif_sony_tag2010g_fields) /
                        sizeof(k_omc_exif_sony_tag2010g_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo(ctx, raw.data, raw.size, 1U, 0x388U))
                return 0;
            break;
        case 'H':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010h", 1U,
                    k_omc_exif_sony_tag2010h_fields,
                    sizeof(k_omc_exif_sony_tag2010h_fields) /
                        sizeof(k_omc_exif_sony_tag2010h_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo(ctx, raw.data, raw.size, 1U,
                                                sony_tag2010h_meter_offset(model)))
                return 0;
            break;
        case 'I':
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag2010i", 1U,
                    k_omc_exif_sony_tag2010i_fields,
                    sizeof(k_omc_exif_sony_tag2010i_fields) /
                        sizeof(k_omc_exif_sony_tag2010i_fields[0])))
                return 0;
            entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x2010U);
            if (!omc_exif_entry_raw_view(ctx->store, entry, &raw))
                return 0;
            if (!omc_exif_decode_sony_meterinfo9(ctx, raw.data, raw.size))
                return 0;
            break;
        default:
            break;
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x3000U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)
        && !omc_exif_decode_sony_shotinfo(ctx, raw.data, raw.size)) {
        return 0;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x3000U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw) &&
        !omc_exif_decode_sony_faces(ctx, raw.data, raw.size))
        return 0;

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9400U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)) {
        rounds = omc_exif_sony_guess_rounds(
            raw.data, raw.size, 0U, allowed_9400,
            (omc_u32)(sizeof(allowed_9400) / sizeof(allowed_9400[0])));
        first = 0U;
        (void)omc_exif_sony_read_u8(raw.data, raw.size, 0U, rounds, &first);
        variant =
            select_sony_tag9400_variant(model, first, legacy, is_lunar, is_stellar);
        if (variant == 'B') {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9400b", rounds,
                    k_omc_exif_sony_tag9400b_fields,
                    sizeof(k_omc_exif_sony_tag9400b_fields) /
                        sizeof(k_omc_exif_sony_tag9400b_fields[0])))
                return 0;
        } else if (variant == 'A') {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9400a", rounds,
                    k_omc_exif_sony_tag9400a_fields,
                    (omc_u32)(sizeof(k_omc_exif_sony_tag9400a_fields)
                              / sizeof(k_omc_exif_sony_tag9400a_fields[0])))) {
                return 0;
            }
        } else if (!omc_exif_decode_sony_tag9400(ctx, raw.data, raw.size)) {
            return 0;
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9401U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)
        && !omc_exif_decode_sony_isoinfo(ctx, raw.data, raw.size)) {
        return 0;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9402U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)
        && !omc_exif_decode_sony_cipher_fields(
               ctx, raw.data, raw.size, "tag9402", 1U,
               k_omc_exif_sony_tag9402_fields,
               (omc_u32)(sizeof(k_omc_exif_sony_tag9402_fields)
                         / sizeof(k_omc_exif_sony_tag9402_fields[0])))) {
        return 0;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9403U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)
        && !omc_exif_decode_sony_cipher_fields(
               ctx, raw.data, raw.size, "tag9403", 1U,
               k_omc_exif_sony_tag9403_fields,
               (omc_u32)(sizeof(k_omc_exif_sony_tag9403_fields)
                         / sizeof(k_omc_exif_sony_tag9403_fields[0])))) {
        return 0;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9404U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)) {
        first = fourth = 0U;
        (void)omc_exif_sony_read_u8(raw.data, raw.size, 0U, 1U, &first);
        (void)omc_exif_sony_read_u8(raw.data, raw.size, 3U, 1U, &fourth);
        variant = is_lunar || is_stellar ? 'B' : 'C';
        if ((first == 4U || first == 5U) && fourth == 1U)
            variant = 'A';
        if ((first == 9U || first == 12U || first == 13U || first == 15U ||
             first == 16U) &&
            fourth == 2U)
            variant = 'B';
        if (first == 17U && fourth == 1U)
            variant = 'C';
        if (variant == 'A') {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9404a", 1U,
                    k_omc_exif_sony_tag9404a_fields,
                    sizeof(k_omc_exif_sony_tag9404a_fields) /
                        sizeof(k_omc_exif_sony_tag9404a_fields[0])))
                return 0;
        } else if (variant == 'B') {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9404b", 1U,
                    k_omc_exif_sony_tag9404b_fields,
                    (omc_u32)(sizeof(k_omc_exif_sony_tag9404b_fields)
                              / sizeof(k_omc_exif_sony_tag9404b_fields[0])))) {
                return 0;
            }
        } else if (!omc_exif_decode_sony_cipher_fields(
                       ctx, raw.data, raw.size, "tag9404c", 1U,
                       k_omc_exif_sony_tag9404c_fields,
                       (omc_u32)(sizeof(k_omc_exif_sony_tag9404c_fields) /
                                 sizeof(k_omc_exif_sony_tag9404c_fields[0])))) {
            return 0;
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x940EU);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)) {
        if (is_slt_family) {
            if (!omc_exif_decode_sony_afinfo(ctx, raw.data, raw.size)) {
                return 0;
            }
        } else if (!omc_exif_decode_sony_tag940e(ctx, raw.data, raw.size)) {
            return 0;
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9405U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)) {
        if (sony_model_uses_tag9405a(model, legacy, is_lunar, is_stellar)) {
            if (!omc_exif_decode_sony_cipher_fields(
                    ctx, raw.data, raw.size, "tag9405a", 1U,
                    k_omc_exif_sony_tag9405a_fields,
                    (omc_u32)(sizeof(k_omc_exif_sony_tag9405a_fields)
                              / sizeof(k_omc_exif_sony_tag9405a_fields[0])))) {
                return 0;
            }
        } else if (!omc_exif_decode_sony_tag9405b(ctx, raw.data, raw.size, model)) {
            return 0;
        }
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9416U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw) &&
        !omc_exif_decode_sony_tag9416(ctx, raw.data, raw.size, model)) {
        return 0;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x202AU);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)
        && !omc_exif_decode_sony_cipher_fields(
               ctx, raw.data, raw.size, "tag202a", 1U,
               k_omc_exif_sony_tag202a_fields,
               (omc_u32)(sizeof(k_omc_exif_sony_tag202a_fields)
                         / sizeof(k_omc_exif_sony_tag202a_fields[0])))) {
        return 0;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x9406U);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)
        && !omc_exif_decode_sony_cipher_fields(
               ctx, raw.data, raw.size, "tag9406", 1U,
               k_omc_exif_sony_tag9406_fields,
               (omc_u32)(sizeof(k_omc_exif_sony_tag9406_fields)
                         / sizeof(k_omc_exif_sony_tag9406_fields[0])))) {
        return 0;
    }

    entry = omc_exif_find_first_entry(ctx->store, "mk_sony0", 0x940CU);
    if (omc_exif_entry_raw_view(ctx->store, entry, &raw)
        && !omc_exif_decode_sony_cipher_fields(
               ctx, raw.data, raw.size, "tag940c", 1U,
               k_omc_exif_sony_tag940c_fields,
               (omc_u32)(sizeof(k_omc_exif_sony_tag940c_fields)
                         / sizeof(k_omc_exif_sony_tag940c_fields[0])))) {
        return 0;
    }

    return 1;
}

static int omc_exif_select_sony_ifd(const omc_exif_ctx *ctx, const omc_u8 *raw,
                                    omc_u64 size, omc_u64 *offset, omc_exif_cfg *cfg);

static int
omc_exif_decode_sony_makernote(omc_exif_ctx* ctx, omc_u64 maker_note_off,
                               const omc_u8* raw, omc_u64 raw_size)
{
    omc_exif_opts opts;
    omc_exif_cfg cfg;
    omc_u64 offset;
    if (!omc_exif_select_sony_ifd(ctx, raw, raw_size, &offset, &cfg))
        return 1;
    opts = ctx->opts;
    opts.decode_makernote = 0;
    opts.decode_printim = 0;
    opts.decode_geotiff = 0;
    opts.decode_embedded_containers = 0;
    omc_exif_set_sony_tokens(&opts);
    if (!omc_exif_decode_ifd_blob_loose_cfg(ctx, ctx->bytes, ctx->size,
                                            maker_note_off + offset, &opts, cfg))
        return 0;
    return omc_exif_decode_sony_postpass(ctx);
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
            return omc_exif_decode_nikon_postpass(ctx, raw, raw_size);
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
        return omc_exif_decode_nikon_postpass(ctx, raw, raw_size);
    }

    if (!omc_exif_decode_ifd_blob_cfg(ctx, ctx->bytes, ctx->size,
                                      maker_note_off, &mn_opts, classic_cfg)) {
        return 0;
    }
    return omc_exif_decode_nikon_postpass(ctx, raw, raw_size);
}

static int omc_exif_source_canon(omc_exif_ctx *ctx, omc_u64 offset, const omc_u8 *raw,
                                 omc_u64 size);

static int
omc_exif_decode_canon_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size)
{
    return omc_exif_source_canon(ctx, (omc_u64)(raw - ctx->bytes), raw, raw_size);
}

static int
omc_exif_decode_apple_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;

    if (raw == (const omc_u8*)0 || raw_size < 16U) {
        return 1;
    }
    if (memcmp(raw, "Apple iOS", 9U) != 0) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_apple_tokens(&mn_opts);

    cfg = omc_exif_make_classic_cfg(0);
    if (!omc_exif_decode_ifd_blob_cfg(ctx, raw, raw_size, 14U, &mn_opts, cfg)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_flir_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                               omc_u64 raw_size)
{
    omc_exif_opts mn_opts;

    if (raw == (const omc_u8*)0 || raw_size < 18U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_flir_tokens(&mn_opts);

    if (!omc_exif_decode_ifd_blob_loose(ctx, raw, raw_size, 0U, &mn_opts)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_hp_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                             omc_u64 raw_size)
{
    static const omc_u8 k_prefix[] = "SERIAL NUMBER:";
    char ifd_name[64];
    omc_u64 serial_off;
    omc_u32 avail32;
    omc_u32 start;
    omc_u32 trimmed;
    omc_u16 value16;
    omc_u32 value32;
    omc_exif_status status;
    omc_u32 order;
    int is_type6;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 6U) {
        return 1;
    }
    if (memcmp(raw, "IIII", 4U) != 0 || raw[5U] != 0U) {
        return 1;
    }

    is_type6 = (raw[4U] == 0x06U);
    if (!is_type6 && raw[4U] != 0x04U && raw[4U] != 0x05U) {
        return 1;
    }
    if (!omc_exif_make_subifd_name("mk_hp", is_type6 ? "type6" : "type4", 0U,
                                   ifd_name, sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x000CU, &value16)) {
        status = omc_exif_emit_derived_exif_urational(ctx, ifd_name, 0x000CU,
                                                      order++, value16, 10U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u32le_raw(raw, raw_size, 0x0010U, &value32)) {
        status = omc_exif_emit_derived_exif_urational(ctx, ifd_name, 0x0010U,
                                                      order++, value32,
                                                      1000000U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (raw_size > 0x0014U) {
        avail32 = (omc_u32)(raw_size - 0x0014U);
        if (avail32 > 20U) {
            avail32 = 20U;
        }
        trimmed = omc_exif_trim_ascii_span(raw + 0x0014U, avail32, &start);
        if (trimmed != 0U) {
            status = omc_exif_emit_derived_exif_text(
                ctx, ifd_name, 0x0014U, order++, raw + 0x0014U + start,
                trimmed);
            if (status != OMC_EXIF_OK) {
                return 0;
            }
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x0034U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0034U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }

    serial_off = is_type6 ? 0x0058U : 0x005CU;
    if (raw_size > serial_off) {
        const omc_u8* serial_text;
        omc_u32 serial_start;
        omc_u32 serial_size;

        avail32 = (omc_u32)(raw_size - serial_off);
        if (avail32 > 26U) {
            avail32 = 26U;
        }
        serial_text = raw + (omc_size)serial_off;
        serial_size = omc_exif_trim_ascii_span(serial_text, avail32,
                                               &serial_start);
        serial_text += serial_start;
        if (serial_size >= (omc_u32)(sizeof(k_prefix) - 1U)
            && memcmp(serial_text, k_prefix, sizeof(k_prefix) - 1U) == 0) {
            serial_text += sizeof(k_prefix) - 1U;
            serial_size -= (omc_u32)(sizeof(k_prefix) - 1U);
            serial_size = omc_exif_trim_ascii_span(serial_text, serial_size,
                                                   &serial_start);
            serial_text += serial_start;
        }
        if (serial_size != 0U) {
            status = omc_exif_emit_derived_exif_text(
                ctx, ifd_name, (omc_u16)serial_off, order++, serial_text,
                serial_size);
            if (status != OMC_EXIF_OK) {
                return 0;
            }
        }
    }

    return 1;
}

static int
omc_exif_decode_kodak_kdk_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                    omc_u64 raw_size)
{
    const omc_u8* model_text;
    omc_u8 buf[11];
    omc_u16 value16;
    omc_u32 value32;
    omc_u8 quality8;
    omc_u8 burst8;
    omc_u8 month8;
    omc_u8 day8;
    omc_u8 hour8;
    omc_u8 minute8;
    omc_u8 second8;
    omc_u8 frac8;
    omc_u32 trimmed;
    omc_exif_status status;
    omc_u32 order;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 0x20U) {
        return 1;
    }
    if (memcmp(raw, "KDK", 3U) != 0 && memcmp(raw, "IIII", 4U) != 0) {
        return 1;
    }

    order = 0U;
    if (raw_size > 0x08U) {
        model_text = raw + 0x08U;
        trimmed = 0U;
        while (0x08U + trimmed < raw_size && trimmed < 16U) {
            omc_u8 ch;

            ch = model_text[trimmed];
            if (ch == 0U || ch == (omc_u8)' ' || ch < 0x20U || ch > 0x7EU) {
                break;
            }
            ++trimmed;
        }
        if (trimmed != 0U) {
            status = omc_exif_emit_derived_exif_text(ctx, "mk_kodak0", 0x0000U,
                                                     order++, model_text,
                                                     trimmed);
            if (status != OMC_EXIF_OK) {
                return 0;
            }
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x11U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x0009U,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x12U, &burst8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x000AU,
                                               order++, burst8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x14U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x000CU,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x16U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x000EU,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x18U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0010U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x1AU, &month8)
        && omc_exif_read_u8_raw(raw, raw_size, 0x1BU, &day8)) {
        omc_exif_write_two_digits(month8, buf + 0U);
        buf[2] = (omc_u8)':';
        omc_exif_write_two_digits(day8, buf + 3U);
        status = omc_exif_emit_derived_exif_text(ctx, "mk_kodak0", 0x0012U,
                                                 order++, buf, 5U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x1CU, &hour8)
        && omc_exif_read_u8_raw(raw, raw_size, 0x1DU, &minute8)
        && omc_exif_read_u8_raw(raw, raw_size, 0x1EU, &second8)
        && omc_exif_read_u8_raw(raw, raw_size, 0x1FU, &frac8)) {
        omc_exif_write_two_digits(hour8, buf + 0U);
        buf[2] = (omc_u8)':';
        omc_exif_write_two_digits(minute8, buf + 3U);
        buf[5] = (omc_u8)':';
        omc_exif_write_two_digits(second8, buf + 6U);
        buf[8] = (omc_u8)'.';
        omc_exif_write_two_digits(frac8, buf + 9U);
        status = omc_exif_emit_derived_exif_text(ctx, "mk_kodak0", 0x0014U,
                                                 order++, buf, 11U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }

    if (omc_exif_read_u16le_raw(raw, raw_size, 0x20U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0018U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x23U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x001BU,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x21U, &burst8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x001CU,
                                               order++, burst8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x24U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x001DU,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x26U, &value16)) {
        status = omc_exif_emit_derived_exif_urational(
            ctx, "mk_kodak0", 0x001EU, order++, value16, 100U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u32le_raw(raw, raw_size, 0x28U, &value32)) {
        status = omc_exif_emit_derived_exif_urational(
            ctx, "mk_kodak0", 0x0020U, order++, value32, 100000U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x2CU, &value16)) {
        status = omc_exif_emit_derived_exif_i16(ctx, "mk_kodak0", 0x0024U,
                                                order++, (omc_s16)value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x2EU, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0026U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x30U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0028U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x34U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x002CU,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x38U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0030U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x3CU, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0034U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x40U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0038U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x42U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x003AU,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x44U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x003CU,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x46U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x003EU,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x48U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x0040U,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x60U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x005CU,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x5CU, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x005DU,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x66U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x005EU,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x68U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x0060U,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x6AU, &value16)) {
        status = omc_exif_emit_derived_exif_urational(
            ctx, "mk_kodak0", 0x0062U, order++, value16, 100U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x65U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x0064U,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x12U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak0", 0x0066U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x5EU, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x0068U,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u8_raw(raw, raw_size, 0x67U, &quality8)) {
        status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak0", 0x006BU,
                                               order++, quality8);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_kodak_serial_only_matched(const omc_u8* raw, omc_u64 raw_size)
{
    omc_u32 n;
    int have_digit;
    int have_alpha;

    if (raw == (const omc_u8*)0 || raw_size < 8U) {
        return 0;
    }

    n = 0U;
    while ((omc_u64)n < raw_size && n < 32U) {
        omc_u8 c;

        c = raw[n];
        if (c == 0U) {
            break;
        }
        if (c < 0x20U || c > 0x7EU) {
            break;
        }
        ++n;
    }
    if (n < 8U) {
        return 0;
    }

    have_digit = 0;
    have_alpha = 0;
    while (n != 0U) {
        omc_u8 c;

        --n;
        c = raw[n];
        if (c >= (omc_u8)'0' && c <= (omc_u8)'9') {
            have_digit = 1;
        } else if ((c >= (omc_u8)'A' && c <= (omc_u8)'Z')
                   || (c >= (omc_u8)'a' && c <= (omc_u8)'z')) {
            have_alpha = 1;
        }
    }

    return have_digit && have_alpha;
}

static int
omc_exif_decode_kodak_serial_only(omc_exif_ctx* ctx, const omc_u8* raw,
                                  omc_u64 raw_size, int* out_matched)
{
    omc_u32 n;
    omc_exif_status status;

    if (out_matched != (int*)0) {
        *out_matched = 0;
    }
    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0) {
        return 1;
    }
    if (!omc_exif_kodak_serial_only_matched(raw, raw_size)) {
        return 1;
    }

    n = 0U;
    while ((omc_u64)n < raw_size && n < 32U) {
        omc_u8 c;

        c = raw[n];
        if (c == 0U || c < 0x20U || c > 0x7EU) {
            break;
        }
        ++n;
    }

    status = omc_exif_emit_derived_exif_text(ctx, "mk_kodak_type7_0", 0x0000U,
                                             0U, raw, n);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    if (out_matched != (int*)0) {
        *out_matched = 1;
    }
    return 1;
}

static int
omc_exif_kodak_ascii_blob(const omc_u8* raw, omc_u64 raw_size,
                          omc_u64 off, omc_u64 len)
{
    omc_u64 i;
    int have_printable;

    if (raw == (const omc_u8*)0 || off > raw_size || len > (raw_size - off)) {
        return 0;
    }

    have_printable = 0;
    for (i = 0U; i < len; ++i) {
        omc_u8 c;

        c = raw[(omc_size)(off + i)];
        if (c == 0U) {
            break;
        }
        if (c < 0x20U || c > 0x7EU) {
            return 0;
        }
        have_printable = 1;
    }
    return have_printable;
}

static int
omc_exif_emit_kodak_trimmed_text(omc_exif_ctx* ctx, const char* ifd_name,
                                 omc_u16 tag, omc_u32 order,
                                 const omc_u8* raw, omc_u32 raw_size)
{
    omc_u32 start;
    omc_u32 trimmed;
    omc_exif_status status;

    trimmed = omc_exif_trim_ascii_span(raw, raw_size, &start);
    if (trimmed == 0U) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_text(ctx, ifd_name, tag, order,
                                             raw + start, trimmed);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_kodak_type2(omc_exif_ctx* ctx, const omc_u8* raw,
                            omc_u64 raw_size, int* out_matched)
{
    omc_u32 width;
    omc_u32 height;
    omc_exif_status status;
    omc_u32 order;

    if (out_matched != (int*)0) {
        *out_matched = 0;
    }
    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 0x74U) {
        return 1;
    }
    if (!omc_exif_kodak_ascii_blob(raw, raw_size, 0x08U, 32U)
        || !omc_exif_kodak_ascii_blob(raw, raw_size, 0x28U, 32U)
        || !omc_exif_read_u32be_raw(raw, raw_size, 0x6CU, &width)
        || !omc_exif_read_u32be_raw(raw, raw_size, 0x70U, &height)
        || width == 0U || height == 0U || width > 200000U
        || height > 200000U) {
        return 1;
    }

    order = 0U;
    if (!omc_exif_emit_kodak_trimmed_text(ctx, "mk_kodak_type2_0", 0x0008U,
                                          order++, raw + 0x08U, 32U)
        || !omc_exif_emit_kodak_trimmed_text(ctx, "mk_kodak_type2_0", 0x0028U,
                                             order++, raw + 0x28U, 32U)) {
        return 0;
    }

    status = omc_exif_emit_derived_exif_u32(ctx, "mk_kodak_type2_0", 0x006CU,
                                            order++, width);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u32(ctx, "mk_kodak_type2_0", 0x0070U,
                                            order++, height);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    if (out_matched != (int*)0) {
        *out_matched = 1;
    }
    return 1;
}

static int
omc_exif_kodak_model_has(const omc_u8* model_text, omc_u32 model_size,
                         const char* needle)
{
    if (model_text == (const omc_u8*)0 || model_size == 0U
        || needle == (const char*)0) {
        return 0;
    }
    return omc_exif_ascii_contains_nocase(model_text, model_size, needle);
}

static int
omc_exif_kodak_model_is_type5(const omc_u8* model_text, omc_u32 model_size)
{
    return omc_exif_kodak_model_has(model_text, model_size, "CX4200")
           || omc_exif_kodak_model_has(model_text, model_size, "CX4210")
           || omc_exif_kodak_model_has(model_text, model_size, "CX4230")
           || omc_exif_kodak_model_has(model_text, model_size, "CX4300")
           || omc_exif_kodak_model_has(model_text, model_size, "CX4310")
           || omc_exif_kodak_model_has(model_text, model_size, "CX6200")
           || omc_exif_kodak_model_has(model_text, model_size, "CX6230");
}

static int
omc_exif_kodak_model_is_type11(const omc_u8* model_text, omc_u32 model_size)
{
    return omc_exif_kodak_model_has(model_text, model_size, "PIXPRO S-1")
           || omc_exif_kodak_model_has(model_text, model_size,
                                       "PIXPRO 4KVR360")
           || omc_exif_kodak_model_has(model_text, model_size,
                                       "KODAK PIXPRO SL10")
           || omc_exif_kodak_model_has(model_text, model_size,
                                       "KODAK PIXPRO SP360");
}

static int
omc_exif_decode_kodak_type5(omc_exif_ctx* ctx, const omc_u8* raw,
                            omc_u64 raw_size, const omc_u8* model_text,
                            omc_u32 model_size, int* out_matched)
{
    omc_u32 exposure_time;
    omc_u16 fnumber;
    omc_u16 iso;
    omc_u16 optical_zoom;
    omc_u16 digital_zoom;
    omc_u8 white_balance;
    omc_u8 flash_mode;
    omc_u8 image_rotated;
    omc_u8 macro;
    omc_exif_status status;
    omc_u32 order;

    if (out_matched != (int*)0) {
        *out_matched = 0;
    }
    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 0x2CU
        || !omc_exif_kodak_model_is_type5(model_text, model_size)
        || !omc_exif_read_u32be_raw(raw, raw_size, 0x14U, &exposure_time)
        || !omc_exif_read_u8_raw(raw, raw_size, 0x1AU, &white_balance)
        || !omc_exif_read_u16be_raw(raw, raw_size, 0x1CU, &fnumber)
        || !omc_exif_read_u16be_raw(raw, raw_size, 0x1EU, &iso)
        || !omc_exif_read_u16be_raw(raw, raw_size, 0x20U, &optical_zoom)
        || !omc_exif_read_u16be_raw(raw, raw_size, 0x22U, &digital_zoom)
        || !omc_exif_read_u8_raw(raw, raw_size, 0x27U, &flash_mode)
        || !omc_exif_read_u8_raw(raw, raw_size, 0x2AU, &image_rotated)
        || !omc_exif_read_u8_raw(raw, raw_size, 0x2BU, &macro)) {
        return 1;
    }

    order = 0U;
    status = omc_exif_emit_derived_exif_u32(ctx, "mk_kodak_type5_0", 0x0014U,
                                            order++, exposure_time);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak_type5_0", 0x001AU,
                                           order++, white_balance);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak_type5_0", 0x001CU,
                                            order++, fnumber);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak_type5_0", 0x001EU,
                                            order++, iso);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak_type5_0", 0x0020U,
                                            order++, optical_zoom);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u16(ctx, "mk_kodak_type5_0", 0x0022U,
                                            order++, digital_zoom);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak_type5_0", 0x0027U,
                                           order++, flash_mode);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak_type5_0", 0x002AU,
                                           order++, image_rotated);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }
    status = omc_exif_emit_derived_exif_u8(ctx, "mk_kodak_type5_0", 0x002BU,
                                           order++, macro);
    if (status != OMC_EXIF_OK) {
        omc_exif_update_status(&ctx->res, status);
        return 0;
    }

    if (out_matched != (int*)0) {
        *out_matched = 1;
    }
    return 1;
}

static int
omc_exif_kodak_absolute_candidate(const omc_u8* bytes, omc_u64 size,
                                  omc_u64 ifd_off, omc_exif_cfg cfg,
                                  omc_u32 max_entries_per_ifd)
{
    omc_u16 entry_count16;
    omc_u64 table_off;
    omc_u64 table_bytes;

    if (bytes == (const omc_u8*)0 || ifd_off >= size
        || !omc_exif_read_u16(cfg, bytes, (omc_size)size, ifd_off,
                              &entry_count16)
        || entry_count16 == 0U || entry_count16 > max_entries_per_ifd) {
        return 0;
    }

    table_off = ifd_off + 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)) {
        return 0;
    }
    if (table_off > size || table_bytes > (size - table_off)
        || 4U > ((size - table_off) - table_bytes)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_kodak_absolute_ifd(omc_exif_ctx* ctx, const omc_u8* raw,
                                   omc_u64 raw_size, const omc_u8* model_text,
                                   omc_u32 model_size, int* out_matched)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;
    omc_u64 ifd_off;
    const char* subtable;
    char ifd_name[64];
    omc_u64 maker_note_off;

    if (out_matched != (int*)0) {
        *out_matched = 0;
    }
    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || raw == (const omc_u8*)0 || raw_size < 8U || raw < ctx->bytes
        || raw > (ctx->bytes + ctx->size)) {
        return 1;
    }

    maker_note_off = (omc_u64)(raw - ctx->bytes);
    if ((raw[0U] == (omc_u8)'I' && raw[1U] == (omc_u8)'I')
        || (raw[0U] == (omc_u8)'M' && raw[1U] == (omc_u8)'M')) {
        cfg = omc_exif_make_classic_cfg(raw[0U] == (omc_u8)'I');
        ifd_off = maker_note_off + 2U;
        if (!omc_exif_kodak_absolute_candidate(ctx->bytes,
                                               (omc_u64)ctx->size, ifd_off,
                                               cfg,
                                               ctx->opts.limits
                                                   .max_entries_per_ifd)) {
            return 1;
        }
        subtable = "type10";
    } else {
        ifd_off = maker_note_off;
        cfg = omc_exif_make_classic_cfg(1);
        if (omc_exif_kodak_absolute_candidate(ctx->bytes, (omc_u64)ctx->size,
                                              ifd_off, cfg,
                                              ctx->opts.limits
                                                  .max_entries_per_ifd)) {
            /* use little-endian */
        } else {
            cfg = omc_exif_make_classic_cfg(0);
            if (!omc_exif_kodak_absolute_candidate(
                    ctx->bytes, (omc_u64)ctx->size, ifd_off, cfg,
                    ctx->opts.limits.max_entries_per_ifd)) {
                return 1;
            }
        }
        subtable = omc_exif_kodak_model_is_type11(model_text, model_size)
                       ? "type11"
                       : "type8";
    }

    if (!omc_exif_make_subifd_name("mk_kodak", subtable, 0U,
                                   ifd_name, sizeof(ifd_name))) {
        return 0;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;

    if (!omc_exif_decode_ifd_blob_loose_named_cfg(ctx, ctx->bytes,
                                                  (omc_u64)ctx->size, ifd_off,
                                                  &mn_opts, cfg, ifd_name)) {
        return 0;
    }
    if (out_matched != (int*)0) {
        *out_matched = 1;
    }
    return 1;
}

static int
omc_exif_decode_kodak_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size)
{
    const omc_u8* model_text;
    omc_u32 model_size;
    int matched;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 8U) {
        return 1;
    }

    if (raw_size >= 3U && memcmp(raw, "KDK", 3U) == 0) {
        return omc_exif_decode_kodak_kdk_makernote(ctx, raw, raw_size);
    }

    if (!omc_exif_decode_kodak_serial_only(ctx, raw, raw_size, &matched)) {
        return 0;
    }
    if (matched) {
        return 1;
    }

    model_text = (const omc_u8*)0;
    model_size = 0U;
    if (ctx->store != (omc_store*)0) {
        (void)omc_exif_find_first_text(ctx->store, "ifd0", 0x0110U,
                                       &model_text, &model_size);
    }
    if (model_text == (const omc_u8*)0) {
        (void)omc_exif_find_outer_ifd0_ascii(ctx, 0x0110U,
                                             &model_text, &model_size);
    }

    if (!omc_exif_decode_kodak_type5(ctx, raw, raw_size, model_text,
                                     model_size, &matched)) {
        return 0;
    }
    if (matched) {
        return 1;
    }

    if (!omc_exif_decode_kodak_type2(ctx, raw, raw_size, &matched)) {
        return 0;
    }
    if (matched) {
        return 1;
    }

    if (!omc_exif_decode_kodak_absolute_ifd(ctx, raw, raw_size, model_text,
                                            model_size, &matched)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_nintendo_postpass(omc_exif_ctx* ctx)
{
    const omc_entry* entry;
    omc_const_bytes raw;
    omc_u8 stable[256];
    const omc_u8* cam;
    omc_u64 cam_size;
    char ifd_name[64];
    omc_u32 value32;
    omc_u16 value16;
    omc_exif_status status;
    omc_u32 order;

    entry = omc_exif_find_first_entry(ctx->store, "mk_nintendo0", 0x1101U);
    if (!omc_exif_entry_raw_view(ctx->store, entry, &raw) || raw.size == 0U) {
        return 1;
    }
    if (raw.size > sizeof(stable)) {
        return 1;
    }
    memcpy(stable, raw.data, raw.size);
    cam = stable;
    cam_size = raw.size;

    if (!omc_exif_make_subifd_name("mk_nintendo", "camerainfo", 0U, ifd_name,
                                   sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    if (cam_size >= 4U) {
        omc_u32 start;
        omc_u32 trimmed;

        trimmed = omc_exif_trim_ascii_span(cam, 4U, &start);
        if (trimmed != 0U) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0000U,
                                                     order++, cam + start,
                                                     trimmed);
            if (status != OMC_EXIF_OK) {
                return 0;
            }
        }
    }
    if (omc_exif_read_u32le_raw(cam, cam_size, 0x0008U, &value32)) {
        status = omc_exif_emit_derived_exif_u32(ctx, ifd_name, 0x0008U,
                                                order++, value32);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (cam_size >= 0x001CU) {
        status = omc_exif_emit_derived_exif_bytes(ctx, ifd_name, 0x0018U,
                                                  order++, cam + 0x0018U, 4U);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u32le_raw(cam, cam_size, 0x0028U, &value32)) {
        status = omc_exif_emit_derived_exif_f32_bits(ctx, ifd_name, 0x0028U,
                                                     order++, value32);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (omc_exif_read_u16le_raw(cam, cam_size, 0x0030U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0030U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    return 1;
}

static int
omc_exif_decode_nintendo_makernote(omc_exif_ctx* ctx, omc_u64 maker_note_off,
                                   const omc_u8* raw, omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;
    omc_u16 entry_count;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;
    omc_u32 i;
    int use_outer;
    int have_candidate;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 6U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_nintendo_tokens(&mn_opts);

    have_candidate = 0;
    use_outer = 0;
    for (i = 0U; i < 2U; ++i) {
        omc_u64 j;
        int ok_abs;
        int ok_rel;

        cfg = omc_exif_make_classic_cfg(i == 0U ? ctx->cfg.little_endian
                                                : !ctx->cfg.little_endian);
        if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, 0U, &entry_count)) {
            continue;
        }
        if (entry_count == 0U
            || entry_count > ctx->opts.limits.max_entries_per_ifd) {
            continue;
        }
        entry_table_off = 2U;
        if (!omc_exif_mul_u64((omc_u64)entry_count, 12U, &table_bytes)) {
            continue;
        }
        if (entry_table_off > raw_size || table_bytes > (raw_size - entry_table_off)
            || 4U > ((raw_size - entry_table_off) - table_bytes)) {
            continue;
        }

        ok_abs = 0;
        ok_rel = 0;
        for (j = 0U; j < (omc_u64)entry_count; ++j) {
            omc_u64 entry_off;
            omc_u16 type;
            omc_u32 count32;
            omc_u32 elem_size;
            omc_u64 value_size;
            omc_u32 off32;

            entry_off = entry_table_off + (j * 12U);
            if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, entry_off + 2U,
                                   &type)
                || !omc_exif_read_u32(cfg, raw, (omc_size)raw_size,
                                      entry_off + 4U, &count32)
                || !omc_exif_read_u32(cfg, raw, (omc_size)raw_size,
                                      entry_off + 8U, &off32)) {
                break;
            }
            if (!omc_exif_elem_size(type, &elem_size)
                || !omc_exif_mul_u64((omc_u64)elem_size, (omc_u64)count32,
                                     &value_size)) {
                continue;
            }
            if (value_size <= 4U) {
                continue;
            }
            if ((omc_u64)off32 + value_size <= raw_size) {
                ok_rel = 1;
            }
            if ((omc_u64)off32 + value_size <= (omc_u64)ctx->size) {
                ok_abs = 1;
            }
            if ((omc_u64)off32 >= raw_size && ok_abs) {
                ok_rel = 0;
                break;
            }
        }

        have_candidate = 1;
        use_outer = (ok_abs && !ok_rel);
        if (use_outer) {
            if (!omc_exif_decode_ifd_blob_loose_cfg(ctx, ctx->bytes,
                                                    (omc_u64)ctx->size,
                                                    maker_note_off, &mn_opts,
                                                    cfg)) {
                return 0;
            }
        } else if (!omc_exif_decode_ifd_blob_loose_cfg(ctx, raw, raw_size, 0U,
                                                       &mn_opts, cfg)) {
            return 0;
        }
        return omc_exif_decode_nintendo_postpass(ctx);
    }

    if (!have_candidate) {
        return 1;
    }
    return 1;
}

static int
omc_exif_decode_reconyx_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                  omc_u64 raw_size)
{
    char ifd_name[64];
    omc_u16 value16;
    omc_exif_status status;
    omc_u32 start;
    omc_u32 trimmed;
    omc_u32 order;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 0x006AU) {
        return 1;
    }
    if (memcmp(raw, "RECONYXH2", 9U) != 0) {
        return 1;
    }
    if (!omc_exif_make_subifd_name("mk_reconyx", "hyperfire2", 0U, ifd_name,
                                   sizeof(ifd_name))) {
        return 1;
    }

    order = 0U;
    if (omc_exif_read_u16le_raw(raw, raw_size, 0x0010U, &value16)) {
        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0010U,
                                                order++, value16);
        if (status != OMC_EXIF_OK) {
            return 0;
        }
    }
    if (raw_size > 0x0034U) {
        trimmed = omc_exif_trim_ascii_span(raw + 0x0034U,
                                           (omc_u32)(raw_size - 0x0034U >= 2U
                                                         ? 2U
                                                         : raw_size - 0x0034U),
                                           &start);
        if (trimmed != 0U) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0034U,
                                                     order++, raw + 0x0034U + start,
                                                     trimmed);
            if (status != OMC_EXIF_OK) {
                return 0;
            }
        }
    }
    if (raw_size > 0x0068U) {
        omc_u32 avail32;

        avail32 = (omc_u32)(raw_size - 0x0068U);
        if (avail32 > 16U) {
            avail32 = 16U;
        }
        trimmed = omc_exif_trim_ascii_span(raw + 0x0068U, avail32, &start);
        if (trimmed != 0U) {
            status = omc_exif_emit_derived_exif_text(ctx, ifd_name, 0x0068U,
                                                     order++, raw + 0x0068U + start,
                                                     trimmed);
            if (status != OMC_EXIF_OK) {
                return 0;
            }
        }
    }
    return 1;
}

static int
omc_exif_casio_legacy_main_tag(omc_u16 tag)
{
    return (tag <= 0x0019U || tag == 0x0E00U);
}

static void
omc_exif_casio_mark_last_legacy_entry(omc_exif_ctx* ctx, omc_u16 tag)
{
    omc_entry* entry;

    if (ctx == (omc_exif_ctx*)0 || ctx->measure_only || ctx->store == (omc_store*)0
        || ctx->store->entry_count == 0U || !omc_exif_casio_legacy_main_tag(tag)) {
        return;
    }
    entry = &ctx->store->entries[ctx->store->entry_count - 1U];
    if (entry->key.kind != OMC_KEY_EXIF_TAG) {
        return;
    }
    entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
    entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_CASIO_TYPE2_LEGACY;
    entry->origin.name_context_variant = 1U;
}

static void
omc_exif_casio_mark_legacy_entries(omc_exif_ctx* ctx, const char* ifd_name)
{
    omc_size i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || ifd_name == (const char*)0) {
        return;
    }
    for (i = 0U; i < ctx->store->entry_count; ++i) {
        omc_entry* entry;

        entry = &ctx->store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || !omc_exif_casio_legacy_main_tag(entry->key.u.exif_tag.tag)
            || !omc_exif_entry_ifd_equals(ctx->store, entry, ifd_name)) {
            continue;
        }
        entry->flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
        entry->origin.name_context_kind = OMC_ENTRY_NAME_CTX_CASIO_TYPE2_LEGACY;
        entry->origin.name_context_variant = 1U;
    }
}

static int
omc_exif_casio_faceinfo1_bytes(const omc_u8* raw, omc_u64 raw_size)
{
    if (raw == (const omc_u8*)0) {
        return 0;
    }
    if (raw_size >= 2U && raw[0] == 0U && raw[1] == 0U) {
        return 1;
    }
    if (raw_size >= 5U && raw[1] == 0x02U && raw[2] == 0x80U
        && raw[3] == 0x01U && raw[4] == 0xE0U) {
        return 1;
    }
    return 0;
}

static int
omc_exif_casio_faceinfo2_bytes(const omc_u8* raw, omc_u64 raw_size)
{
    return raw != (const omc_u8*)0 && raw_size >= 2U && raw[0] == 0x02U
           && raw[1] == 0x01U;
}

static int
omc_exif_casio_frame_size_plausible(omc_u16 width, omc_u16 height)
{
    return width != 0U && height != 0U && width <= 20000U && height <= 20000U;
}

static int
omc_exif_casio_choose_pair_endian(const omc_u8* raw, omc_u64 raw_size,
                                  omc_u64 off, int default_le, int* out_le)
{
    omc_u16 a_be;
    omc_u16 b_be;
    omc_u16 a_le;
    omc_u16 b_le;
    int be_ok;
    int le_ok;

    if (out_le == (int*)0 || raw == (const omc_u8*)0 || off > raw_size
        || (raw_size - off) < 4U) {
        return 0;
    }

    a_be = 0U;
    b_be = 0U;
    a_le = 0U;
    b_le = 0U;
    be_ok = omc_exif_read_u16be_raw(raw, raw_size, off + 0U, &a_be)
            && omc_exif_read_u16be_raw(raw, raw_size, off + 2U, &b_be)
            && omc_exif_casio_frame_size_plausible(a_be, b_be);
    le_ok = omc_exif_read_u16le_raw(raw, raw_size, off + 0U, &a_le)
            && omc_exif_read_u16le_raw(raw, raw_size, off + 2U, &b_le)
            && omc_exif_casio_frame_size_plausible(a_le, b_le);

    if (be_ok && !le_ok) {
        *out_le = 0;
        return 1;
    }
    if (le_ok && !be_ok) {
        *out_le = 1;
        return 1;
    }

    *out_le = default_le;
    return 1;
}

static int
omc_exif_casio_read_u16_array(const omc_u8* raw, omc_u64 raw_size, omc_u64 off,
                              int little_endian, omc_u16* out_values,
                              omc_u32 count)
{
    omc_u32 i;

    if (raw == (const omc_u8*)0 || out_values == (omc_u16*)0 || count == 0U
        || off > raw_size || (raw_size - off) < ((omc_u64)count * 2U)) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        omc_u16 value16;

        value16 = 0U;
        if (little_endian) {
            if (!omc_exif_read_u16le_raw(raw, raw_size, off + ((omc_u64)i * 2U),
                                         &value16)) {
                return 0;
            }
        } else if (!omc_exif_read_u16be_raw(raw, raw_size,
                                            off + ((omc_u64)i * 2U),
                                            &value16)) {
            return 0;
        }
        out_values[i] = value16;
    }
    return 1;
}

static int
omc_exif_casio_decode_faceinfo1(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size, omc_u32 index)
{
    char ifd_name[64];
    omc_u16 dims[2];
    int little_endian;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0
        || !omc_exif_make_subifd_name("mk_casio", "faceinfo1", index, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0000U, 0U, raw[0]);
    if (status != OMC_EXIF_OK) {
        ctx->res.status = status;
        return 0;
    }

    little_endian = 0;
    (void)omc_exif_casio_choose_pair_endian(raw, raw_size, 0x0001U, 0,
                                            &little_endian);
    if (raw_size >= 5U && omc_exif_casio_read_u16_array(raw, raw_size, 0x0001U,
                                                        little_endian, dims,
                                                        2U)) {
        status = omc_exif_emit_derived_exif_array_copy(
            ctx, ifd_name, 0x0001U, 1U, OMC_ELEM_U16, (const omc_u8*)dims,
            (omc_u32)sizeof(dims), 2U);
        if (status != OMC_EXIF_OK) {
            ctx->res.status = status;
            return 0;
        }
    }
    return 1;
}

static int
omc_exif_casio_decode_faceinfo2(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size, omc_u32 index)
{
    char ifd_name[64];
    omc_u16 dims[2];
    omc_u16 rect[4];
    static const omc_u16 k_face_pos_tags[10] = {
        0x0018U, 0x004CU, 0x0080U, 0x00B4U, 0x00E8U,
        0x011CU, 0x0150U, 0x0184U, 0x01B8U, 0x01ECU,
    };
    int little_endian;
    omc_u32 face_count;
    omc_u32 face_n;
    omc_u32 i;
    omc_exif_status status;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 3U
        || !omc_exif_make_subifd_name("mk_casio", "faceinfo2", index, ifd_name,
                                      sizeof(ifd_name))) {
        return 1;
    }

    status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0002U, 0U, raw[2]);
    if (status != OMC_EXIF_OK) {
        ctx->res.status = status;
        return 0;
    }
    face_count = (omc_u32)raw[2];

    little_endian = 1;
    (void)omc_exif_casio_choose_pair_endian(raw, raw_size, 0x0004U, 1,
                                            &little_endian);
    if (raw_size >= 8U && omc_exif_casio_read_u16_array(raw, raw_size, 0x0004U,
                                                        little_endian, dims,
                                                        2U)) {
        status = omc_exif_emit_derived_exif_array_copy(
            ctx, ifd_name, 0x0004U, 1U, OMC_ELEM_U16, (const omc_u8*)dims,
            (omc_u32)sizeof(dims), 2U);
        if (status != OMC_EXIF_OK) {
            ctx->res.status = status;
            return 0;
        }
    }
    if (raw_size >= 9U) {
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, 0x0008U, 2U,
                                               raw[0x0008U]);
        if (status != OMC_EXIF_OK) {
            ctx->res.status = status;
            return 0;
        }
    }
    face_n = (face_count < 10U) ? face_count : 10U;
    for (i = 0U; i < face_n; ++i) {
        omc_u16 tag;

        tag = k_face_pos_tags[i];
        if ((omc_u64)tag > raw_size || (raw_size - (omc_u64)tag) < 8U
            || !omc_exif_casio_read_u16_array(raw, raw_size, (omc_u64)tag,
                                              little_endian, rect, 4U)) {
            continue;
        }
        status = omc_exif_emit_derived_exif_array_copy(
            ctx, ifd_name, tag, 3U + i, OMC_ELEM_U16, (const omc_u8*)rect,
            (omc_u32)sizeof(rect), 4U);
        if (status != OMC_EXIF_OK) {
            ctx->res.status = status;
            return 0;
        }
    }
    return 1;
}

static omc_exif_status
omc_exif_casio_add_u16_array_entry(omc_exif_ctx* ctx,
                                   const omc_byte_ref* token_ref,
                                   omc_u16 tag, omc_u32 count,
                                   const omc_u8* raw, omc_u64 raw_size,
                                   omc_u32 order_in_block,
                                   omc_entry_flags flags)
{
    omc_entry entry;
    omc_byte_ref ref;
    omc_mut_bytes view;
    omc_exif_status status;
    omc_status st;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || token_ref == (const omc_byte_ref*)0
        || raw == (const omc_u8*)0 || count == 0U
        || raw_size < ((omc_u64)count * 2U)) {
        return OMC_EXIF_MALFORMED;
    }
    if (ctx->measure_only
        || ((omc_u64)count * 2U) > ctx->opts.limits.max_value_bytes) {
        return omc_exif_add_entry(ctx, token_ref, tag, 3U, (omc_u64)count,
                                  raw, raw_size, order_in_block, flags);
    }

    status = omc_exif_store_ref(ctx, raw, (omc_u64)count * 2U, &ref);
    if (status != OMC_EXIF_OK) {
        return status;
    }
    view = omc_arena_view_mut(&ctx->store->arena, ref);
    if (view.data == (omc_u8*)0
        || view.size < (omc_size)((omc_u64)count * 2U)) {
        return OMC_EXIF_MALFORMED;
    }

    for (i = 0U; i < count; ++i) {
        omc_u16 value16;

        value16 = 0U;
        if (!omc_exif_read_u16(ctx->cfg, raw, (omc_size)raw_size,
                               (omc_u64)i * 2U, &value16)) {
            return OMC_EXIF_MALFORMED;
        }
        view.data[(omc_size)i * 2U + 0U] = (omc_u8)(value16 & 0xFFU);
        view.data[(omc_size)i * 2U + 1U] = (omc_u8)((value16 >> 8) & 0xFFU);
    }

    memset(&entry, 0, sizeof(entry));
    entry.key.kind = OMC_KEY_EXIF_TAG;
    entry.key.u.exif_tag.ifd = *token_ref;
    entry.key.u.exif_tag.tag = tag;
    entry.origin.block = ctx->source_block;
    entry.origin.order_in_block = order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_TIFF;
    entry.origin.wire_type.code = 3U;
    entry.origin.wire_count = count;
    entry.origin.name_context_kind = OMC_ENTRY_NAME_CTX_NONE;
    entry.origin.name_context_variant = 0U;
    entry.flags = flags;
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_U16;
    entry.value.count = count;
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

static int
omc_exif_casio_decode_binary_subdirs(omc_exif_ctx* ctx)
{
    omc_u32 faceinfo1_index;
    omc_u32 faceinfo2_index;
    omc_size i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    faceinfo1_index = 0U;
    faceinfo2_index = 0U;
    for (i = 0U; i < ctx->store->entry_count; ++i) {
        omc_entry* entry;
        omc_const_bytes raw_view;

        entry = &ctx->store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != 0x2089U
            || !omc_exif_entry_ifd_equals(ctx->store, entry, "mk_casio_type2_0")) {
            continue;
        }
        if (entry->value.kind != OMC_VAL_BYTES
            && entry->value.kind != OMC_VAL_ARRAY) {
            continue;
        }
        raw_view = omc_arena_view(&ctx->store->arena, entry->value.u.ref);
        if (raw_view.data == (const omc_u8*)0 || raw_view.size == 0U) {
            continue;
        }

        if (omc_exif_casio_faceinfo1_bytes(raw_view.data, raw_view.size)) {
            if (!omc_exif_casio_decode_faceinfo1(ctx, raw_view.data,
                                                 raw_view.size,
                                                 faceinfo1_index++)) {
                return 0;
            }
            continue;
        }
        if (omc_exif_casio_faceinfo2_bytes(raw_view.data, raw_view.size)) {
            if (!omc_exif_casio_decode_faceinfo2(ctx, raw_view.data,
                                                 raw_view.size,
                                                 faceinfo2_index++)) {
                return 0;
            }
        }
    }
    return 1;
}

static int
omc_exif_casio_try_read_entry(const omc_u8* raw, omc_u64 raw_size,
                              omc_exif_cfg cfg, omc_u64 entry_off,
                              omc_u16* out_tag, omc_u16* out_type,
                              omc_u32* out_count, omc_u32* out_off32)
{
    return omc_exif_read_u16(cfg, raw, (omc_size)raw_size, entry_off + 0U,
                             out_tag)
           && omc_exif_read_u16(cfg, raw, (omc_size)raw_size, entry_off + 2U,
                                out_type)
           && omc_exif_read_u32(cfg, raw, (omc_size)raw_size, entry_off + 4U,
                                out_count)
           && omc_exif_read_u32(cfg, raw, (omc_size)raw_size, entry_off + 8U,
                                out_off32);
}

static int
omc_exif_casio_has_legacy_main_compat(const omc_u8* raw, omc_u64 raw_size,
                                      omc_exif_cfg cfg, omc_u64 entry_count,
                                      omc_u64 entries_off)
{
    int has_printim;
    int has_modern;
    omc_u64 i;

    has_printim = 0;
    has_modern = 0;
    for (i = 0U; i < entry_count; ++i) {
        omc_u16 tag16;
        omc_u16 type16;
        omc_u32 count32;
        omc_u32 off32;

        tag16 = 0U;
        type16 = 0U;
        count32 = 0U;
        off32 = 0U;
        if (!omc_exif_casio_try_read_entry(raw, raw_size, cfg,
                                           entries_off + (i * 12U),
                                           &tag16, &type16, &count32,
                                           &off32)) {
            return 0;
        }
        if (tag16 == 0x0E00U) {
            has_printim = 1;
        }
        if (tag16 >= 0x2000U) {
            has_modern = 1;
        }
    }
    return has_printim && !has_modern;
}

static int
omc_exif_decode_casio_makernote(omc_exif_ctx* ctx, omc_u64 maker_note_off,
                                const omc_u8* raw, omc_u64 raw_size)
{
    omc_exif_cfg qvc_cfg;
    omc_exif_opts mn_opts;
    omc_exif_ctx child;
    omc_byte_ref token_ref;
    omc_u64 entry_count;
    omc_u64 entries_off;
    int legacy_main_compat;
    int little_endian;
    int be_ok;
    int le_ok;

    (void)maker_note_off;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 2U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_casio_tokens(&mn_opts);

    if (raw_size >= 4U
        && (memcmp(raw, "QVC\0", 4U) == 0 || memcmp(raw, "DCI\0", 4U) == 0)) {
        omc_u32 be_count;
        omc_u16 le_version;
        omc_u16 le_count;

        entries_off = 8U;
        be_count = 0U;
        le_version = 0U;
        le_count = 0U;
        be_ok = omc_exif_read_u32be_raw(raw, raw_size, 4U, &be_count)
                && be_count != 0U
                && be_count <= ctx->opts.limits.max_entries_per_ifd
                && entries_off <= raw_size
                && ((omc_u64)be_count * 12U) <= (raw_size - entries_off);
        le_ok = 0;
        if (!be_ok) {
            le_ok = omc_exif_read_u16le_raw(raw, raw_size, 4U, &le_version)
                    && omc_exif_read_u16le_raw(raw, raw_size, 6U, &le_count)
                    && le_count != 0U
                    && le_count <= ctx->opts.limits.max_entries_per_ifd
                    && entries_off <= raw_size
                    && ((omc_u64)le_count * 12U) <= (raw_size - entries_off);
        }
        if (!be_ok && !le_ok) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 1;
        }

        little_endian = le_ok ? 1 : 0;
        entry_count = le_ok ? (omc_u64)le_count : (omc_u64)be_count;
        qvc_cfg = omc_exif_make_classic_cfg(little_endian);
        legacy_main_compat = omc_exif_casio_has_legacy_main_compat(
            raw, raw_size, qvc_cfg, entry_count, entries_off);

        omc_exif_init_child_cfg(&child, ctx, ctx->bytes, ctx->size, &mn_opts,
                                qvc_cfg);
        memset(&token_ref, 0, sizeof(token_ref));
        if (omc_exif_make_token(&child, OMC_EXIF_IFD, 0U, &token_ref)
            != OMC_EXIF_OK) {
            ctx->res.status = OMC_EXIF_LIMIT;
            return 0;
        }

        {
            omc_u64 i;

            for (i = 0U; i < entry_count; ++i) {
                omc_u16 tag16;
                omc_u16 type16;
                omc_u32 count32;
                omc_u32 off32;
                omc_u32 elem_size;
                omc_u64 value_bytes;
                const omc_u8* value_ptr;
                omc_exif_status status;

                tag16 = 0U;
                type16 = 0U;
                count32 = 0U;
                off32 = 0U;
                value_ptr = (const omc_u8*)0;
                if (!omc_exif_casio_try_read_entry(raw, raw_size, qvc_cfg,
                                                   entries_off + (i * 12U),
                                                   &tag16, &type16, &count32,
                                                   &off32)) {
                    ctx->res.status = OMC_EXIF_MALFORMED;
                    return 1;
                }
                if (!omc_exif_elem_size(type16, &elem_size)
                    || !omc_exif_mul_u64((omc_u64)elem_size,
                                         (omc_u64)count32, &value_bytes)) {
                    continue;
                }
                if (value_bytes <= 4U) {
                    value_ptr = raw + entries_off + (i * 12U) + 8U;
                } else if ((omc_u64)off32 <= (omc_u64)ctx->size
                           && value_bytes <= ((omc_u64)ctx->size
                                              - (omc_u64)off32)) {
                    value_ptr = ctx->bytes + off32;
                } else if ((omc_u64)off32 <= raw_size
                           && value_bytes <= (raw_size - (omc_u64)off32)) {
                    value_ptr = raw + off32;
                } else {
                    ctx->res.status = OMC_EXIF_MALFORMED;
                    continue;
                }

                if (type16 == 3U && count32 > 1U) {
                    status = omc_exif_casio_add_u16_array_entry(
                        &child, &token_ref, tag16, count32, value_ptr,
                        value_bytes, (omc_u32)i, OMC_ENTRY_FLAG_NONE);
                } else {
                    status = omc_exif_add_entry(&child, &token_ref, tag16,
                                                type16, (omc_u64)count32,
                                                value_ptr, value_bytes,
                                                (omc_u32)i,
                                                OMC_ENTRY_FLAG_NONE);
                }
                if (status != OMC_EXIF_OK) {
                    ctx->res.status = status;
                    return 0;
                }
                if (legacy_main_compat) {
                    omc_exif_casio_mark_last_legacy_entry(&child, tag16);
                }
            }
        }
        return omc_exif_casio_decode_binary_subdirs(ctx);
    }

    qvc_cfg = omc_exif_make_classic_cfg(1);
    {
        omc_u16 count16;

        count16 = 0U;
        if (!omc_exif_read_u16le_raw(raw, raw_size, 0U, &count16)
            || count16 == 0U || count16 > ctx->opts.limits.max_entries_per_ifd
            || (2U + ((omc_u64)count16 * 12U) + 4U) > raw_size) {
            return 1;
        }
        legacy_main_compat = omc_exif_casio_has_legacy_main_compat(
            raw, raw_size, qvc_cfg, (omc_u64)count16, 2U);
    }

    if (!omc_exif_decode_ifd_blob_loose(ctx, raw, raw_size, 0U, &mn_opts)) {
        return 0;
    }
    if (legacy_main_compat) {
        omc_exif_casio_mark_legacy_entries(ctx, "mk_casio_type2_0");
    }
    return omc_exif_casio_decode_binary_subdirs(ctx);
}

static int
omc_exif_decode_pentax_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                 omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;
    int use_type2;
    omc_u64 ifd_off;
    const char* ifd_name;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 8U) {
        return 1;
    }

    use_type2 = omc_exif_pentax_makernote_has_type2_signature(raw, raw_size)
                || omc_exif_pentax_model_uses_type2_store(ctx);
    mn_opts = ctx->opts;
    if (use_type2) {
        omc_exif_set_pentax_type2_tokens(&mn_opts);
        ifd_name = "mk_pentax_type2_0";
    } else {
        omc_exif_set_pentax_tokens(&mn_opts);
        ifd_name = "mk_pentax0";
    }

    cfg = omc_exif_make_classic_cfg(1);
    ifd_off = 0U;
    if (memcmp(raw, "AOC\0", 4U) == 0) {
        ifd_off = 6U;
        cfg.little_endian = (raw[4U] == (omc_u8)'I'
                             && raw[5U] == (omc_u8)'I');
    }

    if (!omc_exif_decode_ifd_blob_loose_cfg(ctx, raw, raw_size, ifd_off,
                                            &mn_opts, cfg)) {
        return 0;
    }
    if (!use_type2
        && omc_exif_find_first_entry(ctx->store, ifd_name, 0x0000U)
               == (const omc_entry*)0) {
        omc_exif_status status;

        status = omc_exif_emit_derived_exif_u16(ctx, ifd_name, 0x0000U, 0U,
                                                0U);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    if (!use_type2 && !omc_exif_decode_pentax_binary_subdirs(ctx, ifd_name)) {
        return 0;
    }
    return 1;
}

static int
omc_exif_decode_ricoh_type2_table(omc_exif_ctx* parent, const omc_u8* bytes,
                                  omc_u64 size, omc_u64 count_off,
                                  omc_u64 entry_table_off,
                                  omc_u16 entry_count16,
                                  const omc_exif_opts* opts,
                                  omc_exif_cfg cfg)
{
    omc_exif_ctx child;
    omc_u64 table_bytes;
    omc_u32 entry_count32;
    omc_byte_ref token_ref;
    omc_exif_status tok_status;
    omc_u32 i;

    if (bytes == (const omc_u8*)0 || opts == (const omc_exif_opts*)0
        || size > (omc_u64)(~(omc_size)0) || entry_count16 == 0U
        || entry_count16 > opts->limits.max_entries_per_ifd) {
        return 0;
    }

    omc_exif_init_child_cfg(&child, parent, bytes, (omc_size)size, opts, cfg);
    entry_count32 = entry_count16;
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
        omc_exif_mark_limit(&child, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, count_off,
                            0U);
        omc_exif_merge_makernote_child(parent, &child);
        return 0;
    }

    memset(&token_ref, 0, sizeof(token_ref));
    omc_exif_emit_ifd(&child, OMC_EXIF_IFD, 0U, count_off);
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
        omc_u64 count64;
        omc_u32 elem_size;
        omc_u64 raw_size;
        omc_u64 value_off;
        omc_exif_status status;

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

        count64 = count32;
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

        if (!child.measure_only) {
            status = omc_exif_add_entry(&child, &token_ref, tag, type,
                                        count64,
                                        child.bytes + (omc_size)value_off,
                                        raw_size, i, OMC_ENTRY_FLAG_NONE);
            if (status != OMC_EXIF_OK) {
                if (status == OMC_EXIF_LIMIT) {
                    omc_exif_mark_limit(&child, OMC_EXIF_LIM_VALUE_COUNT,
                                        count_off, tag);
                } else {
                    omc_exif_update_status(&child.res, status);
                }
                omc_exif_merge_makernote_child(parent, &child);
                return 0;
            }
        }
        child.res.entries_decoded += 1U;
    }

    omc_exif_merge_makernote_child(parent, &child);
    return 1;
}

static int
omc_exif_decode_ricoh_type2_ricoh_header_ifd(omc_exif_ctx* ctx,
                                             const omc_u8* raw,
                                             omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;
    omc_u16 entry_count16;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 16U) {
        return 1;
    }
    if (memcmp(raw, "RICOH", 5U) != 0) {
        return 1;
    }
    if (!omc_exif_read_u16le_raw(raw, raw_size, 8U, &entry_count16)
        || entry_count16 == 0U
        || entry_count16 > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_ricoh_type2_tokens(&mn_opts);

    cfg = omc_exif_make_classic_cfg(1);
    return omc_exif_decode_ricoh_type2_table(ctx, raw, raw_size, 8U, 12U,
                                             entry_count16, &mn_opts, cfg);
}

static int
omc_exif_decode_ricoh_type2_padded_ifd(omc_exif_ctx* ctx, const omc_u8* raw,
                                       omc_u64 raw_size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;
    omc_u16 version;
    omc_u32 ifd0_off32;
    omc_u64 ifd0_off;
    omc_u16 entry_count16;
    omc_u64 entry_table_off;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 16U) {
        return 1;
    }
    if (!((raw[0U] == (omc_u8)'I' && raw[1U] == (omc_u8)'I')
          || (raw[0U] == (omc_u8)'M' && raw[1U] == (omc_u8)'M'))) {
        return 1;
    }

    cfg = omc_exif_make_classic_cfg(raw[0U] == (omc_u8)'I');
    if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, 2U, &version)
        || version != 42U
        || !omc_exif_read_u32(cfg, raw, (omc_size)raw_size, 4U, &ifd0_off32)) {
        return 1;
    }

    ifd0_off = ifd0_off32;
    if (ifd0_off == 0U || ifd0_off + 8U > raw_size
        || !omc_exif_read_u16(cfg, raw, (omc_size)raw_size, ifd0_off,
                              &entry_count16)
        || entry_count16 == 0U
        || entry_count16 > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }

    entry_table_off = ifd0_off + 2U;
    if (raw[(omc_size)(ifd0_off + 2U)] == 0U
        && raw[(omc_size)(ifd0_off + 3U)] == 0U) {
        entry_table_off = ifd0_off + 4U;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_ricoh_type2_tokens(&mn_opts);

    return omc_exif_decode_ricoh_type2_table(ctx, raw, raw_size, ifd0_off,
                                             entry_table_off, entry_count16,
                                             &mn_opts, cfg);
}

static int
omc_exif_ricoh_native_ifd_candidate(const omc_u8* raw, omc_u64 raw_size,
                                    omc_u64 ifd_off, omc_exif_cfg cfg,
                                    omc_u32 max_entries_per_ifd)
{
    omc_u16 entry_count16;
    omc_u64 entry_table_off;
    omc_u64 table_bytes;

    if (raw == (const omc_u8*)0 || ifd_off >= raw_size
        || !omc_exif_read_u16(cfg, raw, (omc_size)raw_size, ifd_off,
                              &entry_count16)
        || entry_count16 == 0U
        || entry_count16 > max_entries_per_ifd) {
        return 0;
    }

    entry_table_off = ifd_off + 2U;
    if (!omc_exif_mul_u64((omc_u64)entry_count16, 12U, &table_bytes)) {
        return 0;
    }
    return entry_table_off <= raw_size
           && table_bytes <= (raw_size - entry_table_off)
           && 4U <= ((raw_size - entry_table_off) - table_bytes);
}

static int
omc_exif_ricoh_find_subdir_header(const omc_u8* bytes, omc_u64 size,
                                  omc_u64* out_off)
{
    static const char header[] = "[Ricoh Camera Info]";
    omc_u64 i;

    if (bytes == (const omc_u8*)0 || out_off == (omc_u64*)0) {
        return 0;
    }

    *out_off = 0U;
    if (size < (sizeof(header) - 1U)) {
        return 0;
    }

    for (i = 0U; i + (sizeof(header) - 1U) <= size; ++i) {
        if (memcmp(bytes + (omc_size)i, header, sizeof(header) - 1U) == 0) {
            *out_off = i;
            return 1;
        }
    }
    return 0;
}

static int
omc_exif_decode_ricoh_imageinfo_ref(omc_exif_ctx* ctx, omc_byte_ref raw_ref)
{
    char ifd_name[64];
    omc_const_bytes raw;
    omc_u32 i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    raw = omc_arena_view(&ctx->store->arena, raw_ref);
    if (raw.size == 0U) {
        return 1;
    }
    if (ctx->opts.limits.max_entries_per_ifd != 0U
        && raw.size > ctx->opts.limits.max_entries_per_ifd) {
        return 1;
    }
    if (!omc_exif_make_subifd_name("mk_ricoh", "imageinfo", 0U,
                                   ifd_name, sizeof(ifd_name))) {
        return 0;
    }

    for (i = 0U; i < (omc_u32)raw.size && i <= 0xFFFFU; ++i) {
        omc_exif_status status;

        raw = omc_arena_view(&ctx->store->arena, raw_ref);
        if (i >= (omc_u32)raw.size) {
            break;
        }
        status = omc_exif_emit_derived_exif_u8(ctx, ifd_name, (omc_u16)i, i,
                                               raw.data[i]);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
    }
    return 1;
}

static int
omc_exif_decode_ricoh_subdir_bytes(omc_exif_ctx* ctx, const omc_u8* bytes,
                                   omc_u64 size)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;
    omc_u64 hdr_off;

    if (ctx == (omc_exif_ctx*)0 || bytes == (const omc_u8*)0 || size < 24U) {
        return 1;
    }
    if (!omc_exif_ricoh_find_subdir_header(bytes, size, &hdr_off)
        || hdr_off > size || (size - hdr_off) < 24U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    mn_opts.tokens.ifd_prefix = "mk_ricoh_subdir_";
    mn_opts.tokens.subifd_prefix = "mk_ricoh_subdir_subifd_";
    mn_opts.tokens.exif_ifd_token = "mk_ricoh_subdir_exififd";
    mn_opts.tokens.gps_ifd_token = "mk_ricoh_subdir_gpsifd";
    mn_opts.tokens.interop_ifd_token = "mk_ricoh_subdir_interopifd";

    cfg = omc_exif_make_classic_cfg(0);
    return omc_exif_decode_ifd_blob_loose_cfg(ctx, bytes, size, hdr_off + 20U,
                                              &mn_opts, cfg);
}

static int
omc_exif_decode_ricoh_theta_ifd(omc_exif_ctx* ctx, omc_u32 abs_off)
{
    omc_exif_opts mn_opts;
    omc_exif_cfg cfg;

    if (ctx == (omc_exif_ctx*)0 || abs_off >= (omc_u32)ctx->size) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    mn_opts.tokens.ifd_prefix = "mk_ricoh_thetasubdir_";
    mn_opts.tokens.subifd_prefix = "mk_ricoh_thetasubdir_subifd_";
    mn_opts.tokens.exif_ifd_token = "mk_ricoh_thetasubdir_exififd";
    mn_opts.tokens.gps_ifd_token = "mk_ricoh_thetasubdir_gpsifd";
    mn_opts.tokens.interop_ifd_token = "mk_ricoh_thetasubdir_interopifd";

    cfg = omc_exif_make_classic_cfg(ctx->cfg.little_endian);
    return omc_exif_decode_ifd_blob_loose_cfg(ctx, ctx->bytes,
                                              (omc_u64)ctx->size,
                                              (omc_u64)abs_off, &mn_opts, cfg);
}

static int
omc_exif_decode_ricoh_main_postpass(omc_exif_ctx* ctx, const omc_u8* raw,
                                    omc_u64 raw_size, omc_size entry_start)
{
    omc_byte_ref imageinfo_refs[4];
    omc_byte_ref subdir_refs[4];
    omc_u32 subdir_abs_offs[4];
    omc_u32 theta_abs_offs[4];
    omc_u32 imageinfo_count;
    omc_u32 subdir_ref_count;
    omc_u32 subdir_abs_count;
    omc_u32 theta_count;
    int have_subdir;
    omc_size i;

    if (ctx == (omc_exif_ctx*)0 || ctx->store == (omc_store*)0
        || entry_start >= ctx->store->entry_count) {
        return 1;
    }

    imageinfo_count = 0U;
    subdir_ref_count = 0U;
    subdir_abs_count = 0U;
    theta_count = 0U;
    have_subdir = 0;

    if (raw != (const omc_u8*)0 && raw_size > 8U
        && omc_exif_decode_ricoh_subdir_bytes(ctx, raw + 8U, raw_size - 8U)) {
        have_subdir = 1;
    }

    for (i = entry_start; i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &ctx->store->entries[i];
        if (!omc_exif_entry_ifd_equals(ctx->store, entry, "mk_ricoh0")) {
            continue;
        }

        if (entry->key.u.exif_tag.tag == 0x1001U
            && entry->origin.wire_type.family == OMC_WIRE_TIFF
            && entry->origin.wire_type.code != 3U
            && (entry->value.kind == OMC_VAL_BYTES
                || entry->value.kind == OMC_VAL_ARRAY)
            && imageinfo_count < 4U) {
            imageinfo_refs[imageinfo_count++] = entry->value.u.ref;
        } else if (entry->key.u.exif_tag.tag == 0x2001U) {
            if ((entry->value.kind == OMC_VAL_BYTES
                 || entry->value.kind == OMC_VAL_ARRAY)
                && subdir_ref_count < 4U) {
                subdir_refs[subdir_ref_count++] = entry->value.u.ref;
            } else if (entry->value.kind == OMC_VAL_SCALAR
                       && entry->value.elem_type == OMC_ELEM_U32
                       && subdir_abs_count < 4U) {
                subdir_abs_offs[subdir_abs_count++] = (omc_u32)entry->value.u.u64;
            }
        } else if (entry->key.u.exif_tag.tag == 0x4001U
                   && entry->value.kind == OMC_VAL_SCALAR
                   && entry->value.elem_type == OMC_ELEM_U32
                   && theta_count < 4U) {
            theta_abs_offs[theta_count++] = (omc_u32)entry->value.u.u64;
        }
    }

    for (i = 0U; i < imageinfo_count; ++i) {
        if (!omc_exif_decode_ricoh_imageinfo_ref(ctx, imageinfo_refs[i])) {
            return 0;
        }
    }
    if (!have_subdir) {
        for (i = 0U; i < subdir_ref_count; ++i) {
            omc_const_bytes raw_view;

            raw_view = omc_arena_view(&ctx->store->arena, subdir_refs[i]);
            if (!omc_exif_decode_ricoh_subdir_bytes(ctx, raw_view.data,
                                                    raw_view.size)) {
                return 0;
            }
        }
        for (i = 0U; i < subdir_abs_count; ++i) {
            omc_u32 abs_off;

            abs_off = subdir_abs_offs[i];
            if (abs_off >= (omc_u32)ctx->size) {
                continue;
            }
            if (!omc_exif_decode_ricoh_subdir_bytes(ctx,
                                                    ctx->bytes
                                                        + (omc_size)abs_off,
                                                    (omc_u64)ctx->size
                                                        - (omc_u64)abs_off)) {
                return 0;
            }
        }
    }
    for (i = 0U; i < theta_count; ++i) {
        if (!omc_exif_decode_ricoh_theta_ifd(ctx, theta_abs_offs[i])) {
            return 0;
        }
    }

    return 1;
}

static int
omc_exif_decode_ricoh_native_main_makernote(omc_exif_ctx* ctx,
                                            const omc_u8* raw,
                                            omc_u64 raw_size)
{
    static const omc_u64 k_offsets[2] = { 8U, 10U };
    omc_exif_opts mn_opts;
    omc_u32 i;
    omc_u32 endian_i;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 12U) {
        return 1;
    }

    mn_opts = ctx->opts;
    mn_opts.decode_printim = 0;
    mn_opts.decode_geotiff = 0;
    mn_opts.decode_makernote = 0;
    mn_opts.decode_embedded_containers = 0;
    omc_exif_set_ricoh_tokens(&mn_opts);

    for (i = 0U; i < 2U; ++i) {
        for (endian_i = 0U; endian_i < 2U; ++endian_i) {
            omc_exif_cfg cfg;
            omc_size entry_start;
            omc_exif_status status_before;

            cfg = omc_exif_make_classic_cfg(endian_i == 0U);
            if (!omc_exif_ricoh_native_ifd_candidate(
                    raw, raw_size, k_offsets[i], cfg,
                    ctx->opts.limits.max_entries_per_ifd)) {
                continue;
            }

            entry_start = (ctx->store != (omc_store*)0) ? ctx->store->entry_count
                                                        : 0U;
            status_before = ctx->res.status;
            if (!omc_exif_decode_ifd_blob_loose_cfg(ctx, raw, raw_size,
                                                    k_offsets[i], &mn_opts,
                                                    cfg)) {
                if (ctx->res.status != status_before
                    && ctx->res.status != OMC_EXIF_OK
                    && ctx->res.status != OMC_EXIF_TRUNCATED) {
                    return 0;
                }
                continue;
            }
            if (!omc_exif_decode_ricoh_main_postpass(ctx, raw, raw_size,
                                                     entry_start)) {
                return 0;
            }
            return 1;
        }
    }

    return 1;
}

static int
omc_exif_decode_ricoh_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                omc_u64 raw_size)
{
    omc_u16 entry_count16;
    omc_exif_cfg cfg;
    omc_u16 version;
    omc_u32 ifd0_off32;
    omc_u64 ifd0_off;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size < 8U) {
        return 1;
    }

    if (raw_size >= 16U && memcmp(raw, "RICOH", 5U) == 0
        && omc_exif_read_u16le_raw(raw, raw_size, 8U, &entry_count16)
        && entry_count16 != 0U
        && entry_count16 <= ctx->opts.limits.max_entries_per_ifd) {
        return omc_exif_decode_ricoh_type2_ricoh_header_ifd(ctx, raw,
                                                            raw_size);
    }

    if (raw_size >= 16U
        && ((raw[0U] == (omc_u8)'I' && raw[1U] == (omc_u8)'I')
            || (raw[0U] == (omc_u8)'M' && raw[1U] == (omc_u8)'M'))) {
        cfg = omc_exif_make_classic_cfg(raw[0U] == (omc_u8)'I');
        if (omc_exif_read_u16(cfg, raw, (omc_size)raw_size, 2U, &version)
            && version == 42U
            && omc_exif_read_u32(cfg, raw, (omc_size)raw_size, 4U,
                                 &ifd0_off32)) {
            ifd0_off = ifd0_off32;
            if (ifd0_off != 0U && ifd0_off + 8U <= raw_size
                && omc_exif_read_u16(cfg, raw, (omc_size)raw_size, ifd0_off,
                                     &entry_count16)
                && entry_count16 != 0U
                && entry_count16 <= ctx->opts.limits.max_entries_per_ifd) {
                return omc_exif_decode_ricoh_type2_padded_ifd(ctx, raw,
                                                              raw_size);
            }
        }
    }

    return omc_exif_decode_ricoh_native_main_makernote(ctx, raw, raw_size);
}

/* Phase One main entries are 16 bytes; the sensor-calibration child uses
 * 12-byte entries. Offsets are relative to each bounded directory. */
static int omc_exif_decode_nikon_nefinfo(omc_exif_ctx *ctx)
{
    static const char *outer[] = {"subifd1", "subifd0", "subifd2"};
    const omc_entry *entry;
    omc_const_bytes raw;
    omc_u8 *copy;
    omc_exif_status status;
    omc_exif_cfg cfg;
    omc_exif_opts opts;
    omc_u64 header;
    omc_u32 first, i;
    omc_size before, j;
    if (ctx->measure_only || ctx->store == NULL)
        return 1;
    for (i = 0U; i < sizeof(outer) / sizeof(outer[0]); ++i) {
        entry = omc_exif_find_first_entry(ctx->store, outer[i], 0xC7D5U);
        copy = NULL;
        status = omc_exif_entry_raw_copy_view(ctx->store, entry, &copy, &raw);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        if (omc_exif_find_tiff_header(raw.data, raw.size, 0U, 64U, &header)) {
            cfg = omc_exif_make_classic_cfg(raw.data[header] == 'I');
            if (omc_exif_read_u32(cfg, raw.data, raw.size, header + 4U, &first)) {
                opts = ctx->opts;
                opts.decode_makernote = 0;
                opts.decode_printim = 0;
                opts.decode_geotiff = 0;
                opts.decode_embedded_containers = 0;
                opts.tokens.ifd_prefix = "mk_nikon_nefinfo_";
                before = ctx->store->entry_count;
                (void)omc_exif_decode_ifd_blob_loose_cfg(
                    ctx, raw.data + (omc_size)header, raw.size - header, first, &opts,
                    cfg);
                for (j = before; j < ctx->store->entry_count; ++j)
                    ctx->store->entries[j].flags |= OMC_ENTRY_FLAG_DERIVED;
            }
        }
        free(copy);
        if (ctx->res.status == OMC_EXIF_NOMEM || ctx->res.status == OMC_EXIF_LIMIT)
            return 0;
    }
    return 1;
}

static int omc_exif_decode_phaseone_input(omc_exif_ctx *ctx, omc_input *input,
                                          int sensor)
{
    omc_exif_cfg cfg;
    omc_byte_ref token;
    omc_exif_status xs;
    omc_u32 start, count, i, tag, format, length, relative, number;
    omc_u32 stride, text_size;
    omc_u64 entry_off, value_off;
    omc_u64 size;
    omc_u8 header[12], wire[16];
    const omc_u8 *value;
    omc_input child;
    omc_u16 short_value;
    omc_entry entry;
    omc_status status;
    int readable;
    const char *name;
    size = input->range.size;
    if (size < 12U)
        return 1;
    if (!omc_input_read(input, 0U, header, sizeof(header)))
        return 0;
    cfg = omc_exif_make_classic_cfg(memcmp(header, "IIII", 4U) == 0);
    if (sensor) {
        if ((cfg.little_endian && memcmp(header + 4U, "\001\000\000\000", 4U) != 0) ||
            (!cfg.little_endian && (memcmp(header, "MMMM", 4U) != 0 ||
                                    memcmp(header + 4U, "\000\000\000\001", 4U) != 0)))
            return 1;
    } else if ((cfg.little_endian && memcmp(header + 5U, "waR", 3U) != 0) ||
               (!cfg.little_endian && memcmp(header, "MMMMRaw", 7U) != 0))
        return 1;
    (void)omc_exif_read_u32(cfg, header, sizeof(header), 8U, &start);
    stride = sensor ? 12U : 16U;
    if (start > size || size - start < 8U)
        return 1;
    if (!omc_input_read(input, start, wire, 4U))
        return 0;
    (void)omc_exif_read_u32(cfg, wire, 4U, 0U, &count);
    if (count == 0U || count > 300U || (omc_u64)count * stride > size - start - 8U)
        return 1;
    if (count > ctx->opts.limits.max_entries_per_ifd ||
        ctx->res.entries_decoded > ctx->opts.limits.max_total_entries ||
        count > ctx->opts.limits.max_total_entries - ctx->res.entries_decoded) {
        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, start, 0U);
        return 0;
    }
    name = sensor ? "mk_phaseone_sensorcalibration_0" : "mk_phaseone0";
    memset(&token, 0, sizeof(token));
    if (!ctx->measure_only) {
        xs = omc_exif_store_cstr_len(ctx, name, strlen(name), &token);
        if (xs != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, xs);
            return 0;
        }
    }
    for (i = 0U; i < count; ++i) {
        if (ctx->res.entries_decoded >= ctx->opts.limits.max_total_entries) {
            omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_ENTRIES_TOTAL, start, 0U);
            return 0;
        }
        entry_off = (omc_u64)start + 8U + (omc_u64)i * stride;
        if (!omc_input_read(input, entry_off, wire, stride))
            return 0;
        (void)omc_exif_read_u32(cfg, wire, stride, 0U, &tag);
        if (tag > 65535U)
            continue;
        format = 4U;
        if (!sensor)
            (void)omc_exif_read_u32(cfg, wire, stride, 4U, &format);
        if (format != 1U && format != 2U && format != 4U)
            format = 1U;
        (void)omc_exif_read_u32(cfg, wire, stride, stride - 8U, &length);
        value_off = entry_off + stride - 4U;
        if (length > 4U) {
            (void)omc_exif_read_u32(cfg, wire, stride, stride - 4U, &relative);
            value_off = relative;
        }
        readable = value_off <= size && length <= size - value_off;
        if (!ctx->measure_only) {
            memset(&entry, 0, sizeof(entry));
            entry.key.kind = OMC_KEY_EXIF_TAG;
            entry.key.u.exif_tag.ifd = token;
            entry.key.u.exif_tag.tag = (omc_u16)tag;
            entry.origin.block = ctx->source_block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type.family = OMC_WIRE_OTHER;
            entry.origin.wire_type.code = (omc_u16)format;
            entry.origin.wire_count = length / format;
            xs = OMC_EXIF_OK;
            if (length > ctx->opts.limits.max_value_bytes)
                entry.flags = OMC_ENTRY_FLAG_TRUNCATED;
            else if (!readable)
                entry.flags = OMC_ENTRY_FLAG_UNREADABLE;
            else if (length != 0U) {
                if (input->range.source.contiguous_data != NULL) {
                    value = input->range.source.contiguous_data +
                            (omc_size)(input->range.source_offset + value_off);
                } else if (length <= sizeof(wire)) {
                    if (!omc_input_read(input, value_off, wire, length))
                        return 0;
                    value = wire;
                } else {
                    if (length > ctx->workspace.value_capacity) {
                        if (length > ctx->source_result->value_scratch_needed)
                            ctx->source_result->value_scratch_needed = length;
                        omc_exif_mark_limit(ctx, OMC_EXIF_LIM_VALUE_COUNT, start,
                                            (omc_u16)tag);
                        return 0;
                    }
                    if (!omc_input_read(input, value_off, ctx->workspace.value, length))
                        return 0;
                    if (length > ctx->source_result->value_scratch_used)
                        ctx->source_result->value_scratch_used = length;
                    value = ctx->workspace.value;
                }
                if (format == 1U) {
                    for (text_size = 0U; text_size < length && value[text_size] != 0U;
                         ++text_size) {
                    }
                    xs = omc_exif_store_ref(ctx, value, text_size, &entry.value.u.ref);
                    entry.value.kind = OMC_VAL_TEXT;
                    entry.value.text_encoding = OMC_TEXT_ASCII;
                    entry.value.count = text_size;
                } else if (length == 1U)
                    omc_val_make_u8(&entry.value, value[0]);
                else if (length == 2U) {
                    (void)omc_exif_read_u16(cfg, value, length, 0U, &short_value);
                    if (format == 2U)
                        omc_val_make_i16(&entry.value, (omc_s16)short_value);
                    else
                        omc_val_make_u16(&entry.value, short_value);
                } else if (length == 4U) {
                    (void)omc_exif_read_u32(cfg, value, length, 0U, &number);
                    omc_val_make_u32(&entry.value, number);
                } else {
                    xs = omc_exif_store_ref(ctx, value, length, &entry.value.u.ref);
                    entry.value.kind = OMC_VAL_BYTES;
                    entry.value.count = length;
                }
            }
            if (xs != OMC_EXIF_OK) {
                omc_exif_update_status(&ctx->res, xs);
                return 0;
            }
            omc_exif_maybe_mark_contextual_name(ctx, &entry);
            status = omc_store_add_entry(ctx->store, &entry, NULL);
            if (status != OMC_STATUS_OK) {
                omc_exif_update_status(&ctx->res, status == OMC_STATUS_NO_MEMORY
                                                      ? OMC_EXIF_NOMEM
                                                      : OMC_EXIF_LIMIT);
                return 0;
            }
        }
        ctx->res.entries_decoded++;
        if (!sensor && tag == 0x0110U && readable && length != 0U) {
            child = omc_input_slice(input, value_off, length);
            if (!omc_exif_decode_phaseone_input(ctx, &child, 1))
                return 0;
        }
    }
    return 1;
}

static int omc_exif_decode_phaseone(omc_exif_ctx *ctx, const omc_u8 *raw, omc_u64 size,
                                    int sensor)
{
    omc_input input;
    omc_input_memory(&input, raw, (omc_size)size);
    return omc_exif_decode_phaseone_input(ctx, &input, sensor);
}

static int omc_exif_probe_large_makernote(omc_exif_ctx *ctx, omc_u64 offset,
                                          omc_u64 size)
{
    omc_input input, note;
    if (ctx->source != NULL) {
        memset(&input, 0, sizeof(input));
        input.range = *ctx->source;
        input.state = ctx->input;
        input.limits = ctx->io_limits;
    } else
        omc_input_memory(&input, ctx->bytes, (omc_size)ctx->size);
    note = omc_input_slice(&input, offset, size);
    return omc_exif_decode_phaseone_input(ctx, &note, 0);
}

/* Match the reference's bounded classic-IFD fallback. Score complete tables,
 * not just a plausible count: arbitrary vendor headers often contain one. */
static int omc_exif_find_classic_candidate(const omc_exif_ctx *ctx, const omc_u8 *raw,
                                           omc_u64 size, omc_u64 scan_limit,
                                           omc_u64 *out_offset, omc_exif_cfg *out_cfg)
{
    omc_u64 off, entry, bytes, value_off;
    omc_u32 best, valid, i, count, value, width;
    omc_u16 entries, type;
    omc_exif_cfg cfg;
    int endian;
    best = 0U;
    if (scan_limit > size)
        scan_limit = size;
    for (off = 0U; off + 2U <= scan_limit; off += 2U) {
        for (endian = 0; endian < 2; ++endian) {
            cfg = omc_exif_make_classic_cfg(endian == 0);
            if (!omc_exif_read_u16(cfg, raw, (omc_size)size, off, &entries) ||
                entries == 0U || entries > 512U ||
                entries > ctx->opts.limits.max_entries_per_ifd ||
                (omc_u64)entries * 12U + 6U > size - off)
                continue;
            valid = 0U;
            for (i = 0U; i < entries; ++i) {
                entry = off + 2U + (omc_u64)i * 12U;
                (void)omc_exif_read_u16(cfg, raw, (omc_size)size, entry + 2U, &type);
                (void)omc_exif_read_u32(cfg, raw, (omc_size)size, entry + 4U, &count);
                (void)omc_exif_read_u32(cfg, raw, (omc_size)size, entry + 8U, &value);
                if (!omc_exif_elem_size(type, &width))
                    continue;
                bytes = (omc_u64)width * count;
                if (bytes > ctx->opts.limits.max_value_bytes)
                    continue;
                value_off = bytes <= 4U ? entry + 8U : value;
                if (value_off <= size && bytes <= size - value_off)
                    valid++;
            }
            if (valid <= best || valid < (entries > 4U ? entries / 2U : entries))
                continue;
            best = valid;
            *out_offset = off;
            *out_cfg = cfg;
        }
    }
    return best != 0U;
}

static int omc_exif_select_sony_ifd(const omc_exif_ctx *ctx, const omc_u8 *raw,
                                    omc_u64 size, omc_u64 *offset, omc_exif_cfg *cfg)
{
    omc_u64 known;
    omc_u16 count;
    int endian;
    known = size >= 6U && memcmp(raw, "SONY", 4U) == 0
                ? 4U
                : (size >= 14U && memcmp(raw, "VHAB", 4U) == 0 ? 12U : ~(omc_u64)0);
    if (known != ~(omc_u64)0) {
        for (endian = 0; endian < 2; ++endian) {
            *cfg = omc_exif_make_classic_cfg(endian == 0 ? ctx->cfg.little_endian
                                                         : !ctx->cfg.little_endian);
            if (omc_exif_read_u16(*cfg, raw, (omc_size)size, known, &count) &&
                count != 0U && count <= ctx->opts.limits.max_entries_per_ifd &&
                (omc_u64)count * 12U + 6U <= size - known) {
                *offset = known;
                return 1;
            }
        }
    }
    if (omc_exif_find_classic_candidate(ctx, raw, size, 256U, offset, cfg))
        return 1;
    *cfg = omc_exif_make_classic_cfg(ctx->cfg.little_endian);
    if (omc_exif_read_u16(*cfg, raw, (omc_size)size, 0U, &count) && count != 0U &&
        count <= ctx->opts.limits.max_entries_per_ifd &&
        (omc_u64)count * 12U + 6U <= size) {
        *offset = 0U;
        return 1;
    }
    return 0;
}

static int omc_exif_decode_generic_makernote(omc_exif_ctx *ctx, const omc_u8 *raw,
                                             omc_u64 raw_size)
{
    omc_exif_cfg cfg;
    omc_exif_opts opts;
    omc_u64 offset;
    if (!omc_exif_find_classic_candidate(ctx, raw, raw_size, 256U, &offset, &cfg))
        return 1;
    opts = ctx->opts;
    opts.decode_printim = 0;
    opts.decode_makernote = 0;
    opts.decode_geotiff = 0;
    opts.decode_embedded_containers = 0;
    opts.tokens.ifd_prefix = "mkifd";
    return omc_exif_decode_ifd_blob_loose_cfg(ctx, raw, raw_size, offset, &opts, cfg);
}

static int
omc_exif_decode_source_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                 omc_u64 raw_size);

static int
omc_exif_decode_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                          omc_u64 raw_size)
{
    omc_exif_mn_vendor vendor;
    omc_u64 maker_note_off;

    if (ctx == (omc_exif_ctx*)0 || raw == (const omc_u8*)0 || raw_size == 0U) {
        return 1;
    }
    if (ctx->source != NULL)
        return omc_exif_decode_source_makernote(ctx, raw, raw_size);
    if (raw < ctx->bytes || raw > (ctx->bytes + ctx->size)) {
        return 1;
    }

    maker_note_off = (omc_u64)(raw - ctx->bytes);
    vendor = omc_exif_detect_makernote_vendor(ctx, raw, raw_size);
    if (vendor == OMC_EXIF_MN_PHASEONE)
        return omc_exif_decode_phaseone(ctx, raw, raw_size, 0);
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
    if (vendor == OMC_EXIF_MN_SONY) {
        return omc_exif_decode_sony_makernote(ctx, maker_note_off, raw,
                                              raw_size);
    }
    if (vendor == OMC_EXIF_MN_SIGMA) {
        return omc_exif_decode_sigma_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_CANON) {
        return omc_exif_decode_canon_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_APPLE) {
        return omc_exif_decode_apple_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_KODAK) {
        return omc_exif_decode_kodak_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_FLIR) {
        return omc_exif_decode_flir_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_HP) {
        return omc_exif_decode_hp_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_NINTENDO) {
        return omc_exif_decode_nintendo_makernote(ctx, maker_note_off, raw,
                                                  raw_size);
    }
    if (vendor == OMC_EXIF_MN_RECONYX) {
        return omc_exif_decode_reconyx_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_CASIO) {
        return omc_exif_decode_casio_makernote(ctx, maker_note_off, raw,
                                               raw_size);
    }
    if (vendor == OMC_EXIF_MN_SAMSUNG) {
        return omc_exif_decode_samsung_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_MINOLTA) {
        return omc_exif_decode_minolta_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_MOTOROLA) {
        return omc_exif_decode_motorola_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_PENTAX) {
        return omc_exif_decode_pentax_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_RICOH) {
        return omc_exif_decode_ricoh_makernote(ctx, raw, raw_size);
    }
    if (vendor == OMC_EXIF_MN_OLYMPUS) {
        return omc_exif_decode_olympus_makernote(ctx, maker_note_off, raw,
                                                 raw_size);
    }
    if (vendor == OMC_EXIF_MN_PANASONIC) {
        return omc_exif_decode_panasonic_makernote(ctx, maker_note_off, raw,
                                                   raw_size);
    }
    return omc_exif_decode_generic_makernote(ctx, raw, raw_size);
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

    if (ctx->source != NULL) {
        omc_u8 mask = (omc_u8)(1U << (unsigned)task.kind);
        for (i = 0U; i < ctx->visited_count; ++i) {
            if (ctx->visited_offsets[i] != task.offset) continue;
            if ((ctx->visited_masks[i] & mask) != 0U ||
                !((task.kind == OMC_EXIF_GPS_IFD && ctx->visited_masks[i] == 8U) ||
                  (task.kind == OMC_EXIF_INTEROP_IFD && ctx->visited_masks[i] == 4U)))
                return 1;
            break;
        }
        if (i == ctx->visited_count) {
            if (i == OMC_EXIF_TASK_CAP) {
                omc_exif_mark_limit(ctx, OMC_EXIF_LIM_MAX_IFDS, task.offset, 0U);
                return 0;
            }
            ctx->visited_offsets[i] = task.offset;
            ctx->visited_count++;
        }
        ctx->visited_masks[i] |= mask;
    }
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

        if (!omc_exif_input_u16(ctx, task.offset,
                               &count16)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }
        entry_count64 = count16;
    } else {
        if (!omc_exif_input_u64(ctx, task.offset,
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
        omc_entry_flags value_flags;

        entry_off = entry_table_off + ((omc_u64)i * ctx->cfg.entry_size);
        if (!omc_exif_input_u16(ctx, entry_off,
                               &tag)
            || !omc_exif_input_u16(ctx, entry_off + 2U,
                                  &type)) {
            ctx->res.status = OMC_EXIF_MALFORMED;
            return 0;
        }

        if (!ctx->cfg.big_tiff) {
            omc_u32 count32;

            if (!omc_exif_input_u32(ctx, entry_off + 4U, &count32)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            count = count32;
            value_off = entry_off + 8U;
        } else {
            if (!omc_exif_input_u64(ctx, entry_off + 4U, &count)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            value_off = entry_off + 12U;
        }

        value_flags = OMC_ENTRY_FLAG_NONE;
        if (!omc_exif_resolve_raw(ctx, value_off, type, count, &raw, &raw_size)) {
            if (!ctx->source_single_ifd || ctx->res.status == OMC_EXIF_LIMIT ||
                ctx->res.status == OMC_EXIF_NOMEM ||
                (ctx->input != NULL && ctx->input->code != OMC_SOURCE_OK)) {
                omc_exif_update_status(&ctx->res, OMC_EXIF_MALFORMED);
                return 0;
            }
            raw = NULL;
            raw_size = 0U;
            value_flags = OMC_ENTRY_FLAG_UNREADABLE;
        }

        if (ctx->opts.decode_geotiff && count <= (omc_u64)(~(omc_u32)0)) {
            if (tag == 0x87AFU) {
                geotiff_dir.present = 1;
                geotiff_dir.type = type;
                geotiff_dir.count32 = (omc_u32)count;
                geotiff_dir.raw = raw;
                geotiff_dir.raw_size = raw_size;
                geotiff_dir.source_offset = ctx->raw_source_offset;
            } else if (tag == 0x87B0U) {
                geotiff_double.present = 1;
                geotiff_double.type = type;
                geotiff_double.count32 = (omc_u32)count;
                geotiff_double.raw = raw;
                geotiff_double.raw_size = raw_size;
                geotiff_double.source_offset = ctx->raw_source_offset;
            } else if (tag == 0x87B1U) {
                geotiff_ascii.present = 1;
                geotiff_ascii.type = type;
                geotiff_ascii.count32 = (omc_u32)count;
                geotiff_ascii.raw = raw;
                geotiff_ascii.raw_size = raw_size;
                geotiff_ascii.source_offset = ctx->raw_source_offset;
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
                                         raw_size, i, value_flags);
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

        if (ctx->opts.decode_makernote && tag == 0x927CU &&
            (raw == NULL || raw_size > ctx->opts.limits.max_value_bytes)) {
            /* IIQ can declare nearly the whole file as a MakerNote. Read its
             * directory and bounded values without retaining the image span. */
            if (!omc_exif_probe_large_makernote(ctx, ctx->raw_source_offset, raw_size))
                return 0;
        }
        if (ctx->source != NULL && raw == NULL) continue;

        if (ctx->opts.decode_printim && tag == 0xC4A5U && raw_size != 0U
            && raw_size <= ctx->opts.limits.max_value_bytes) {
            if (!omc_exif_decode_printim(ctx, raw, raw_size)) {
                return 0;
            }
        }
        if (ctx->opts.decode_makernote && tag == 0x927CU && raw_size != 0U
            && raw_size <= ctx->opts.limits.max_value_bytes) {
            omc_exif_status prior_status;
            int decoded;
            prior_status = ctx->res.status;
            decoded = omc_exif_decode_makernote(ctx, raw, raw_size);
            if (ctx->res.status == OMC_EXIF_LIMIT ||
                ctx->res.status == OMC_EXIF_NOMEM ||
                (ctx->input != NULL && ctx->input->code != OMC_SOURCE_OK))
                return 0;
            if (!decoded || ctx->res.status != prior_status) {
                /* Optional nested syntax must not prevent later outer tags.
                 * Resource and source failures above remain fatal. */
                if (ctx->source_result != NULL)
                    ctx->source_result->nested_payloads_skipped++;
                ctx->res.status = prior_status;
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
        if (ctx->source != NULL) {
            omc_exif_geotiff_tag_ref* refs[3];
            omc_u64 needed;
            omc_size used;
            unsigned n;
            refs[0] = &geotiff_dir;
            refs[1] = &geotiff_double;
            refs[2] = &geotiff_ascii;
            needed = 0U;
            for (n = 0U; n < 3U; ++n) {
                if (refs[n]->raw_size > ~(omc_u64)0 - needed) {
                    ctx->res.status = OMC_EXIF_LIMIT;
                    return 0;
                }
                if (refs[n]->raw_size > ctx->opts.limits.max_value_bytes) {
                    refs[n]->present = 0;
                    refs[n]->raw_size = 0U;
                }
                needed += refs[n]->raw_size;
            }
            if (needed > ctx->workspace.value_capacity) {
                if (needed > ctx->source_result->value_scratch_needed)
                    ctx->source_result->value_scratch_needed = needed;
                ctx->res.status = OMC_EXIF_LIMIT;
                return 0;
            }
            used = 0U;
            if (needed > ctx->source_result->value_scratch_used)
                ctx->source_result->value_scratch_used = (omc_size)needed;
            for (n = 0U; n < 3U; ++n) {
                if (refs[n]->raw_size != 0U) {
                    refs[n]->raw = ctx->workspace.value + used;
                    if (!omc_exif_input_read(ctx, refs[n]->source_offset,
                                              ctx->workspace.value + used,
                                              (omc_size)refs[n]->raw_size))
                        return 0;
                    used += (omc_size)refs[n]->raw_size;
                }
            }
        }
        if (!omc_exif_decode_geotiff(ctx, &geotiff_dir, &geotiff_double,
                                     &geotiff_ascii)) {
            return 0;
        }
    }

    next_ifd_off = entry_table_off + table_bytes;
    if (task.kind == OMC_EXIF_IFD && !ctx->source_single_ifd) {
        omc_u64 next_offset;

        if (!ctx->cfg.big_tiff) {
            omc_u32 next32;

            if (!omc_exif_input_u32(ctx, next_ifd_off,
                                   &next32)) {
                ctx->res.status = OMC_EXIF_MALFORMED;
                return 0;
            }
            next_offset = next32;
        } else {
            if (!omc_exif_input_u64(ctx, next_ifd_off,
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

/* Source-relative classic MakerNote IFDs reuse the normal value conversion.
 * Their links are vendor-specific; do not follow a generic next-IFD chain. */
static int
omc_exif_source_classic(omc_exif_ctx* parent, const omc_source_range* range,
                         omc_u64 offset, omc_exif_cfg cfg, omc_s64 value_base,
                         const char* token)
{
    omc_exif_ctx child;
    omc_exif_task task;
    omc_exif_init_child_cfg(&child, parent, NULL, 0U, &parent->opts, cfg);
    child.source = range;
    child.size = range->size;
    child.value_base = value_base;
    child.source_single_ifd = 1;
    child.source_ifd_token = token;
    child.cfg.next_size = 0U;
    child.opts.decode_makernote = 0;
    child.opts.decode_geotiff = 0;
    child.opts.decode_printim = 0;
    child.opts.decode_embedded_containers = 0;
    task.kind = OMC_EXIF_IFD;
    task.index = 0U;
    task.offset = offset;
    if (!omc_exif_process_ifd(&child, task)) {
        omc_exif_merge_makernote_child(parent, &child);
        return 0;
    }
    omc_exif_merge_makernote_child(parent, &child);
    return 1;
}

static int
omc_exif_source_nested_tiff(omc_exif_ctx* parent, omc_u64 offset,
                             const omc_exif_opts* opts)
{
    omc_source_range range;
    omc_exif_ctx child;
    omc_exif_task task;
    if (offset >= parent->size)
        return 0;
    range = *parent->source;
    range.source_offset += offset;
    range.size -= offset;
    omc_exif_init_child_cfg(&child, parent, NULL, 0U, opts, parent->cfg);
    child.source = &range;
    child.size = range.size;
    if (!omc_exif_parse_header(&child) ||
        !omc_exif_push_task(&child, OMC_EXIF_IFD, 0U, child.cfg.first_ifd)) {
        omc_exif_merge_makernote_child(parent, &child);
        return 0;
    }
    while (omc_exif_pop_task(&child, &task))
        if (!omc_exif_process_ifd(&child, task)) break;
    if (child.res.status != OMC_EXIF_LIMIT && child.res.status != OMC_EXIF_NOMEM)
        (void)omc_exif_decode_nikon_preview_aliases(&child);
    omc_exif_merge_makernote_child(parent, &child);
    return child.res.status != OMC_EXIF_MALFORMED &&
           child.res.status != OMC_EXIF_LIMIT && child.res.status != OMC_EXIF_NOMEM;
}

static int
omc_exif_source_canon(omc_exif_ctx* ctx, omc_u64 offset,
                       const omc_u8* raw, omc_u64 size)
{
    omc_exif_cfg cfg;
    omc_s64 bases[4], schema;
    omc_u32 scores[4], best, i, j, min_offset, value, count;
    omc_u16 entries, type;
    omc_u32 width;
    omc_u64 needed, n, resolved;
    int enabled[4], all_in[4], in;
    const omc_entry* schema_entry;
    cfg = omc_exif_make_classic_cfg(ctx->cfg.little_endian);
    if (!omc_exif_read_u16(cfg, raw, (omc_size)size, 0U, &entries) ||
        entries > ctx->opts.limits.max_entries_per_ifd ||
        2U + (omc_u64)entries * 12U > size) {
        cfg.little_endian = !cfg.little_endian;
        if (!omc_exif_read_u16(cfg, raw, (omc_size)size, 0U, &entries) ||
            entries > ctx->opts.limits.max_entries_per_ifd ||
            2U + (omc_u64)entries * 12U > size) return 0;
    }
    needed = 2U + (omc_u64)entries * 12U + 4U;
    min_offset = ~(omc_u32)0;
    for (i = 0U; i < entries; ++i) {
        n = 2U + (omc_u64)i * 12U;
        (void)omc_exif_read_u16(cfg, raw, (omc_size)size, n + 2U, &type);
        (void)omc_exif_read_u32(cfg, raw, (omc_size)size, n + 4U, &count);
        (void)omc_exif_read_u32(cfg, raw, (omc_size)size, n + 8U, &value);
        if (omc_exif_elem_size(type, &width) && (omc_u64)width * count > 4U &&
            value >= needed && value < min_offset) min_offset = value;
    }
    memset(bases, 0, sizeof(bases));
    memset(scores, 0, sizeof(scores));
    memset(enabled, 0, sizeof(enabled));
    enabled[0] = 1;
    if (offset <= (~(omc_u64)0 >> 1U)) {
        enabled[1] = 1;
        bases[1] = (omc_s64)offset;
        if (min_offset != ~(omc_u32)0 && offset <= (~(omc_u64)0 >> 1U) - needed) {
            enabled[2] = 1;
            bases[2] = (omc_s64)(offset + needed) - min_offset;
        }
        schema_entry = omc_exif_find_first_entry(ctx->store, "exififd", 0xEA1DU);
        if (schema_entry != NULL && schema_entry->value.kind == OMC_VAL_SCALAR &&
            schema_entry->value.elem_type == OMC_ELEM_I32) {
            schema = schema_entry->value.u.i64;
            if (schema <= 0 || offset <= (~(omc_u64)0 >> 1U) - (omc_u64)schema) {
                enabled[3] = 1;
                bases[3] = (omc_s64)offset + schema;
            }
        }
    }
    for (j = 0U; j < 4U; ++j) all_in[j] = enabled[j];
    for (i = 0U; i < entries; ++i) {
        n = 2U + (omc_u64)i * 12U;
        (void)omc_exif_read_u16(cfg, raw, (omc_size)size, n + 2U, &type);
        (void)omc_exif_read_u32(cfg, raw, (omc_size)size, n + 4U, &count);
        (void)omc_exif_read_u32(cfg, raw, (omc_size)size, n + 8U, &value);
        if (!omc_exif_elem_size(type, &width)) continue;
        n = (omc_u64)width * count;
        if (n <= 4U || n > ctx->opts.limits.max_value_bytes) continue;
        for (j = 0U; j < 4U; ++j) {
            if (!enabled[j]) continue;
            if (bases[j] < 0) {
                omc_u64 delta = (omc_u64)(-(bases[j] + 1)) + 1U;
                if (value < delta) { all_in[j] = 0; continue; }
                resolved = value - delta;
            } else {
                resolved = (omc_u64)bases[j] + value;
            }
            if (resolved > ctx->size || n > ctx->size - resolved) {
                all_in[j] = 0; continue;
            }
            scores[j]++;
            in = resolved >= offset && resolved - offset <= size &&
                 n <= size - (resolved - offset);
            if (in) {
                scores[j] += 2U;
                if (resolved - offset >= needed) scores[j]++;
            } else all_in[j] = 0;
        }
    }
    best = 0U;
    for (j = 1U; j < 4U; ++j)
        if (enabled[j] && (scores[j] > scores[best] ||
            (scores[j] == scores[best] && all_in[j] && !all_in[best]))) best = j;
    if (ctx->source != NULL) {
        if (!omc_exif_source_classic(ctx, ctx->source, offset, cfg, bases[best],
                                     "mk_canon0"))
            return 0;
    } else {
        omc_exif_opts opts = ctx->opts;
        opts.decode_makernote = 0;
        opts.decode_printim = 0;
        opts.decode_geotiff = 0;
        opts.decode_embedded_containers = 0;
        omc_exif_set_canon_tokens(&opts);
        if (!omc_exif_decode_ifd_blob_offsets(ctx, ctx->bytes, ctx->size, offset, &opts,
                                              cfg, 4U, bases[best]))
            return 0;
    }
    return omc_exif_decode_canon_postpass(ctx);
}

static int
omc_exif_source_olympus_children(omc_exif_ctx* ctx, omc_u64 ifd,
                                  omc_exif_cfg cfg, int camera_settings)
{
    omc_u8 entry[12], count_bytes[2];
    omc_u16 count, tag, type, child_count;
    omc_u32 n, value, i, width, index;
    const char* name;
    char token[96];
    if (!omc_exif_input_read(ctx, ifd, count_bytes, 2U) ||
        !omc_exif_read_u16(cfg, count_bytes, 2U, 0U, &count)) return 0;
    if (count > ctx->opts.limits.max_entries_per_ifd) return 0;
    index = 0U;
    for (i = 0U; i < count; ++i) {
        if (!omc_exif_input_read(ctx, ifd + 2U + (omc_u64)i * 12U, entry, 12U)) return 0;
        (void)omc_exif_read_u16(cfg, entry, 12U, 0U, &tag);
        (void)omc_exif_read_u16(cfg, entry, 12U, 2U, &type);
        (void)omc_exif_read_u32(cfg, entry, 12U, 4U, &n);
        (void)omc_exif_read_u32(cfg, entry, 12U, 8U, &value);
        name = camera_settings ? omc_exif_olympus_camerasettings_subtable_name(tag)
                               : omc_exif_olympus_main_subtable_name(tag);
        if (name == NULL || value >= ctx->size) continue;
        if (!((type == 4U || type == 13U) && n == 1U) &&
            (!omc_exif_elem_size(type, &width) || (omc_u64)width * n <= 4U)) continue;
        /* Binary payload tags can share a sub-IFD tag number. Probe only the
         * small directory header before treating such a payload as an IFD. */
        if (ctx->size - value < 2U)
            continue;
        if (!omc_exif_input_read(ctx, value, count_bytes, 2U))
            return 0;
        if (!omc_exif_read_u16(cfg, count_bytes, 2U, 0U, &child_count) ||
            child_count == 0U || child_count > ctx->opts.limits.max_entries_per_ifd ||
            (omc_u64)child_count * 12U + 6U > ctx->size - value)
            continue;
        if (!omc_exif_make_subifd_name("mk_olympus", name,
                strcmp(name, "fetags") == 0 ? index++ : 0U, token, sizeof(token))) return 0;
        if (!omc_exif_source_classic(ctx, ctx->source, value, cfg, 0, token)) return 0;
        if (!camera_settings && strcmp(name, "camerasettings") == 0 &&
            !omc_exif_source_olympus_children(ctx, value, cfg, 1)) return 0;
    }
    return 1;
}

static int
omc_exif_source_casio(omc_exif_ctx* ctx, omc_u64 offset,
                       const omc_u8* raw, omc_u64 size)
{
    omc_exif_ctx child;
    omc_exif_opts opts;
    omc_exif_cfg cfg;
    omc_byte_ref token;
    omc_u32 count, i, count32;
    omc_u16 count16, tag, type;
    omc_u8 entry[12];
    const omc_u8* value;
    omc_u64 value_size;
    omc_exif_status status;
    int legacy;
    cfg = omc_exif_make_classic_cfg(0);
    if (!omc_exif_read_u32(cfg, raw, (omc_size)size, 4U, &count) ||
        count == 0U || count > ctx->opts.limits.max_entries_per_ifd ||
        8U + (omc_u64)count * 12U > size) {
        cfg.little_endian = 1;
        if (!omc_exif_read_u16(cfg, raw, (omc_size)size, 6U, &count16)) return 0;
        count = count16;
        if (count == 0U || count > ctx->opts.limits.max_entries_per_ifd ||
            8U + (omc_u64)count * 12U > size) return 0;
    }
    legacy = omc_exif_casio_has_legacy_main_compat(raw, size, cfg, count, 8U);
    opts = ctx->opts;
    omc_exif_set_casio_tokens(&opts);
    omc_exif_init_child_cfg(&child, ctx, NULL, (omc_size)ctx->size, &opts, cfg);
    if (omc_exif_make_token(&child, OMC_EXIF_IFD, 0U, &token) != OMC_EXIF_OK) return 0;
    for (i = 0U; i < count; ++i) {
        if (!omc_exif_input_read(ctx, offset + 8U + (omc_u64)i * 12U, entry, 12U)) return 0;
        (void)omc_exif_read_u16(cfg, entry, 12U, 0U, &tag);
        (void)omc_exif_read_u16(cfg, entry, 12U, 2U, &type);
        (void)omc_exif_read_u32(cfg, entry, 12U, 4U, &count32);
        if (!omc_exif_resolve_raw(&child, offset + 16U + (omc_u64)i * 12U,
                                    type, count32, &value, &value_size)) {
            omc_exif_merge_makernote_child(ctx, &child);
            return 0;
        }
        if (type == 3U && count32 > 1U)
            status = omc_exif_casio_add_u16_array_entry(&child, &token, tag,
                        count32, value, value_size, i, OMC_ENTRY_FLAG_NONE);
        else
            status = omc_exif_add_entry(&child, &token, tag, type, count32,
                                          value, value_size, i, OMC_ENTRY_FLAG_NONE);
        if (status != OMC_EXIF_OK) {
            omc_exif_update_status(&ctx->res, status);
            return 0;
        }
        child.res.entries_decoded++;
        if (legacy) omc_exif_casio_mark_last_legacy_entry(&child, tag);
    }
    omc_exif_merge_makernote_child(ctx, &child);
    return omc_exif_casio_decode_binary_subdirs(ctx);
}

static int
omc_exif_decode_source_makernote(omc_exif_ctx* ctx, const omc_u8* raw,
                                 omc_u64 raw_size)
{
    omc_exif_mn_vendor vendor;
    omc_exif_ctx local;
    omc_exif_opts opts;
    omc_exif_cfg cfg;
    omc_source_range nested;
    omc_u64 offset, header, local_ifd, limit;
    omc_u32 ifd32;
    omc_u16 count;
    omc_size before;
    int endian;
    vendor = omc_exif_detect_makernote_vendor(ctx, raw, raw_size);
    offset = ctx->raw_source_offset;
    cfg = omc_exif_make_classic_cfg(ctx->cfg.little_endian);
    if (vendor == OMC_EXIF_MN_CANON)
        return omc_exif_source_canon(ctx, offset, raw, raw_size);
    if (vendor == OMC_EXIF_MN_NIKON) {
        opts = ctx->opts;
        opts.decode_makernote = 0;
        opts.decode_geotiff = 0;
        opts.decode_printim = 0;
        opts.decode_embedded_containers = 0;
        omc_exif_set_nikon_tokens(&opts);
        if (omc_exif_find_tiff_header(raw, raw_size, 0U, 128U, &header)) {
            if (!omc_exif_source_nested_tiff(ctx, offset + header, &opts)) return 0;
        } else {
            local_ifd = raw_size >= 10U && memcmp(raw, "Nikon\0", 6U) == 0 &&
                        raw[6U] == 1U && raw[7U] == 0U ? 8U : 0U;
            if (!omc_exif_source_classic(ctx, ctx->source, offset + local_ifd,
                                           cfg, 0, "mk_nikon0")) return 0;
        }
        /* Nested value reads reused the value scratch. Restore the note for
         * the existing bounded preview and binary postpasses. */
        if (!omc_exif_input_read(ctx, offset, ctx->workspace.value,
                                  (omc_size)raw_size)) return 0;
        return omc_exif_decode_nikon_postpass(ctx, ctx->workspace.value, raw_size);
    }
    if (vendor == OMC_EXIF_MN_SONY) {
        if (!omc_exif_select_sony_ifd(ctx, raw, raw_size, &local_ifd, &cfg))
            return 1;
        if (!omc_exif_source_classic(ctx, ctx->source, offset + local_ifd, cfg, 0,
                                     "mk_sony0"))
            return 0;
        return omc_exif_decode_sony_postpass(ctx);
    }
    if (vendor == OMC_EXIF_MN_PANASONIC) {
        limit = 512U;
        if (limit > raw_size) limit = raw_size;
        for (local_ifd = 0U; local_ifd + 2U <= limit; local_ifd += 2U) {
            for (endian = 0; endian < 2; ++endian) {
                cfg.little_endian = endian ? !ctx->cfg.little_endian : ctx->cfg.little_endian;
                if (!omc_exif_read_u16(cfg, raw, (omc_size)raw_size, local_ifd, &count) ||
                    count == 0U || count > ctx->opts.limits.max_entries_per_ifd ||
                    (omc_u64)count * 12U > raw_size - local_ifd - 2U) continue;
                before = ctx->store->entry_count;
                if (!omc_exif_source_classic(ctx, ctx->source, offset + local_ifd, cfg,
                                             0, "mk_panasonic0"))
                    return 0;
                return omc_exif_decode_panasonic_binary_subdirs(ctx, before, cfg.little_endian);
            }
        }
    }
    if (vendor == OMC_EXIF_MN_FUJI && raw_size >= 12U &&
        (memcmp(raw, "FUJIFILM", 8U) == 0 || memcmp(raw, "GENERALE", 8U) == 0)) {
        cfg = omc_exif_make_classic_cfg(1);
        (void)omc_exif_read_u32(cfg, raw, (omc_size)raw_size, 8U, &ifd32);
        nested = *ctx->source;
        nested.source_offset += offset;
        nested.size -= offset;
        return omc_exif_source_classic(ctx, &nested, ifd32, cfg, 0, "mk_fuji0");
    }
    if (vendor == OMC_EXIF_MN_OLYMPUS && raw_size >= 8U &&
        (memcmp(raw, "OLYMP\0", 6U) == 0 || memcmp(raw, "EPSON\0", 6U) == 0 ||
         memcmp(raw, "MINOL\0", 6U) == 0 || memcmp(raw, "CAMER\0", 6U) == 0)) {
        if (!omc_exif_source_classic(ctx, ctx->source, offset + 8U, cfg, 0,
                                       "mk_olympus0")) return 0;
        return omc_exif_source_olympus_children(ctx, offset + 8U, cfg, 0);
    }
    if (vendor == OMC_EXIF_MN_CASIO && raw_size >= 8U &&
        (memcmp(raw, "QVC\0", 4U) == 0 || memcmp(raw, "DCI\0", 4U) == 0))
        return omc_exif_source_casio(ctx, offset, raw, raw_size);
    /* Payload-local layouts retain their existing bounded decoders. Source
     * layouts with external references are dispatched above or added below. */
    omc_exif_init_child_cfg(&local, ctx, raw, (omc_size)raw_size, &ctx->opts, ctx->cfg);
    before = ctx->store->entry_count;
    if (!omc_exif_decode_makernote(&local, raw, raw_size)) {
        omc_exif_merge_makernote_child(ctx, &local);
        /* Some local postpasses report an absent optional subtable through
         * their return value after successfully emitting the main table. */
        return local.res.status == OMC_EXIF_OK;
    }
    omc_exif_merge_makernote_child(ctx, &local);
    if (vendor == OMC_EXIF_MN_RICOH) {
        omc_size end = ctx->store->entry_count;
        omc_size i;
        for (i = before; i < end; ++i) {
            const omc_entry* e = &ctx->store->entries[i];
            if (omc_exif_entry_ifd_equals(ctx->store, e, "mk_ricoh0") &&
                e->key.u.exif_tag.tag == 0x4001U && e->value.kind == OMC_VAL_SCALAR &&
                e->value.elem_type == OMC_ELEM_U32 && e->value.u.u64 < ctx->size &&
                !omc_exif_source_classic(ctx, ctx->source, e->value.u.u64, cfg, 0,
                                            "mk_ricoh_thetasubdir_0")) return 0;
        }
    }
    return 1;
}

static omc_exif_res
omc_exif_run(const omc_u8 *tiff_bytes, omc_size tiff_size, omc_store *store,
             omc_block_id source_block, omc_exif_ifd_ref *out_ifds, omc_u32 ifd_cap,
             const omc_exif_opts *opts, int measure_only,
             const omc_source_range *source, const omc_exif_source_workspace *workspace,
             omc_source_state *input, const omc_source_limits *io_limits,
             omc_exif_source_res *source_result, int canon_block)
{
    omc_exif_ctx ctx;
    omc_exif_task task;

    memset(&ctx, 0, sizeof(ctx));
    ctx.bytes = tiff_bytes;
    ctx.size = source != NULL ? source->size : tiff_size;
    ctx.source = source;
    ctx.input = input;
    ctx.io_limits = io_limits;
    ctx.source_result = source_result;
    if (workspace != NULL)
        ctx.workspace = *workspace;
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

    if (source == NULL && tiff_bytes == (const omc_u8*)0) {
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
    if (canon_block) {
        if (ctx.cfg.big_tiff) {
            ctx.res.status = OMC_EXIF_UNSUPPORTED;
            return ctx.res;
        }
        omc_exif_set_canon_tokens(&ctx.opts);
        ctx.opts.decode_makernote = 0;
        ctx.opts.decode_geotiff = 0;
        ctx.opts.decode_printim = 0;
        ctx.opts.decode_embedded_containers = 0;
        ctx.source_single_ifd = 1;
        ctx.source_ifd_token = "mk_canon0";
    }
    if (!omc_exif_push_task(&ctx, OMC_EXIF_IFD, 0U, ctx.cfg.first_ifd)) {
        return ctx.res;
    }

    while (omc_exif_pop_task(&ctx, &task)) {
        if (!omc_exif_process_ifd(&ctx, task)) {
            break;
        }
    }

    if (canon_block && ctx.res.status != OMC_EXIF_NOMEM &&
        ctx.res.status != OMC_EXIF_LIMIT &&
        (ctx.input == NULL || ctx.input->code == OMC_SOURCE_OK))
        (void)omc_exif_decode_canon_postpass(&ctx);
    if (ctx.opts.decode_makernote && ctx.res.status != OMC_EXIF_NOMEM &&
        ctx.res.status != OMC_EXIF_LIMIT &&
        (ctx.input == NULL || ctx.input->code == OMC_SOURCE_OK))
        (void)omc_exif_decode_nikon_nefinfo(&ctx);
    if (ctx.res.status != OMC_EXIF_LIMIT && ctx.res.status != OMC_EXIF_NOMEM &&
        (ctx.input == NULL || ctx.input->code == OMC_SOURCE_OK))
        (void)omc_exif_decode_nikon_preview_aliases(&ctx);
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
    return omc_exif_run(tiff_bytes, tiff_size, store, source_block, out_ifds, ifd_cap,
                        opts, 0, NULL, NULL, NULL, NULL, NULL, 0);
}

omc_exif_res
omc_exif_meas(const omc_u8* tiff_bytes, omc_size tiff_size,
              const omc_exif_opts* opts)
{
    return omc_exif_run(tiff_bytes, tiff_size, (omc_store *)0, OMC_INVALID_BLOCK_ID,
                        (omc_exif_ifd_ref *)0, 0U, opts, 1, NULL, NULL, NULL, NULL,
                        NULL, 0);
}

static omc_exif_source_res
omc_exif_dec_source_impl(const omc_source_range *range, omc_store *store,
                         omc_block_id source_block, omc_exif_ifd_ref *out_ifds,
                         omc_u32 ifd_cap, const omc_exif_source_workspace *workspace,
                         omc_source_state *state, const omc_source_limits *io_limits,
                         const omc_exif_opts *opts, int canon_block)
{
    omc_exif_source_res res;
    memset(&res, 0, sizeof(res));
    if (!omc_source_range_valid(range) || store == NULL || workspace == NULL ||
        state == NULL || (ifd_cap && out_ifds == NULL) ||
        (workspace->value_capacity && workspace->value == NULL)) {
        res.decoded.status = OMC_EXIF_MALFORMED;
        return res;
    }
    if (state->code != OMC_SOURCE_OK) {
        res.decoded.status = OMC_EXIF_TRUNCATED;
        return res;
    }
    if (range->source.contiguous_data != NULL) {
        res.decoded = omc_exif_dec(range->source.contiguous_data +
                                    (omc_size)range->source_offset,
                                    (omc_size)range->size, store, source_block,
                                    out_ifds, ifd_cap, opts);
    } else {
        res.decoded =
            omc_exif_run(NULL, 0U, store, source_block, out_ifds, ifd_cap, opts, 0,
                         range, workspace, state, io_limits, &res, canon_block);
    }
    if (state->code != OMC_SOURCE_OK) {
        switch (state->code) {
        case OMC_SOURCE_REQUEST_TOO_LARGE:
        case OMC_SOURCE_REQUEST_LIMIT:
        case OMC_SOURCE_BYTE_LIMIT:
        case OMC_SOURCE_SCRATCH_TOO_SMALL:
            res.decoded.status = OMC_EXIF_LIMIT; break;
        case OMC_SOURCE_SHORT_READ:
        case OMC_SOURCE_IO_FAILURE:
        case OMC_SOURCE_CHANGED:
        case OMC_SOURCE_CANCELLED:
            res.decoded.status = OMC_EXIF_TRUNCATED; break;
        default: res.decoded.status = OMC_EXIF_MALFORMED; break;
        }
    }
    return res;
}

omc_exif_res omc_exif_dec_cmt3(const omc_u8 *bytes, omc_size size, omc_store *store,
                               omc_block_id block, const omc_exif_opts *opts)
{
    return omc_exif_run(bytes, size, store, block, NULL, 0U, opts, 0, NULL, NULL, NULL,
                        NULL, NULL, 1);
}

omc_exif_source_res omc_exif_dec_source(const omc_source_range *range, omc_store *store,
                                        omc_block_id source_block,
                                        omc_exif_ifd_ref *out_ifds, omc_u32 ifd_cap,
                                        const omc_exif_source_workspace *workspace,
                                        omc_source_state *state,
                                        const omc_source_limits *io_limits,
                                        const omc_exif_opts *opts)
{
    return omc_exif_dec_source_impl(range, store, source_block, out_ifds, ifd_cap,
                                    workspace, state, io_limits, opts, 0);
}

omc_exif_source_res omc_exif_dec_cmt3_source(const omc_source_range *range,
                                             omc_store *store, omc_block_id block,
                                             const omc_exif_source_workspace *workspace,
                                             omc_source_state *state,
                                             const omc_source_limits *io_limits,
                                             const omc_exif_opts *opts)
{
    return omc_exif_dec_source_impl(range, store, block, NULL, 0U, workspace, state,
                                    io_limits, opts, 1);
}
