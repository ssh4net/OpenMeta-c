#include "omc/omc_xmp.h"

#include <stdlib.h>
#include <string.h>

#define OMC_XMP_ATTR_CAP 1024U

typedef struct omc_xmp_span {
    omc_u32 off;
    omc_u32 len;
} omc_xmp_span;

typedef struct omc_xmp_attr {
    omc_xmp_span prefix;
    omc_xmp_span local;
    omc_xmp_span value;
    int is_xmlns;
    int is_default_xmlns;
} omc_xmp_attr;

typedef struct omc_xmp_ns_decl {
    omc_xmp_span prefix;
    omc_xmp_span uri;
    omc_u32 depth;
} omc_xmp_ns_decl;

typedef enum omc_xmp_frame_kind {
    OMC_XMP_FRAME_GENERIC = 0,
    OMC_XMP_FRAME_XMPMETA = 1,
    OMC_XMP_FRAME_RDF = 2,
    OMC_XMP_FRAME_DESC = 3,
    OMC_XMP_FRAME_PROPERTY = 4,
    OMC_XMP_FRAME_ARRAY = 5,
    OMC_XMP_FRAME_LI = 6,
    OMC_XMP_FRAME_STRUCT_FIELD = 7
} omc_xmp_frame_kind;

typedef struct omc_xmp_frame {
    omc_xmp_frame_kind kind;
    omc_xmp_span prefix;
    omc_xmp_span local;
    omc_xmp_span ns_uri;
    omc_xmp_span prop_ns_uri;
    omc_xmp_span prop_name;
    omc_u32 array_index;
    omc_u32 current_index;
    omc_u32 ns_mark;
    omc_u32 path_off;
    omc_u32 path_len;
    omc_u32 text_start;
    omc_u32 text_end;
    int saw_text;
    int had_child;
    int has_path;
    int emitted_resource;
} omc_xmp_frame;

typedef struct omc_xmp_ctx {
    const omc_u8* bytes;
    omc_size size;
    omc_store* store;
    omc_block_id source_block;
    omc_entry_flags base_flags;
    int measure_only;
    omc_xmp_opts opts;
    omc_xmp_res res;
    omc_u32 order;
    omc_u64 total_value_bytes;
    omc_xmp_attr *attrs;
    omc_xmp_frame* frames;
    omc_u32 frame_count;
    omc_u32 frame_cap;
    omc_xmp_ns_decl* ns_decls;
    omc_u32 ns_count;
    omc_u32 ns_cap;
    char* frame_path_buf;
    omc_u32 path_cap;
    int saw_xmp_shape;
    int root_seen;
    omc_u8* input_copy;
} omc_xmp_ctx;

static const char k_ns_meta[] = "adobe:ns:meta/";
static const char k_ns_rdf[] = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
static const char k_ns_xml[] = "http://www.w3.org/XML/1998/namespace";

static void
omc_xmp_ctx_fini(omc_xmp_ctx* ctx);

static void
omc_xmp_opts_apply_defaults(omc_xmp_opts* opts)
{
    if (opts->limits.max_depth == 0U) {
        opts->limits.max_depth = 128U;
    }
    if (opts->limits.max_properties == 0U) {
        opts->limits.max_properties = 200000U;
    }
    if (opts->limits.max_input_bytes == 0U) {
        opts->limits.max_input_bytes = 64U * 1024U * 1024U;
    }
    if (opts->limits.max_path_bytes == 0U) {
        opts->limits.max_path_bytes = 1024U;
    }
    if (opts->limits.max_value_bytes == 0U) {
        opts->limits.max_value_bytes = 8U * 1024U * 1024U;
    }
    if (opts->limits.max_attributes_per_element == 0U)
        opts->limits.max_attributes_per_element = OMC_XMP_ATTR_CAP;
    if (opts->limits.max_namespace_bytes == 0U)
        opts->limits.max_namespace_bytes = 4096U;
    if (opts->limits.max_arena_bytes == 0U)
        opts->limits.max_arena_bytes = 64U * 1024U * 1024U;
    if (opts->limits.max_total_value_bytes == 0U) {
        opts->limits.max_total_value_bytes = 64U * 1024U * 1024U;
    }
}

void
omc_xmp_opts_init(omc_xmp_opts* opts)
{
    if (opts == (omc_xmp_opts*)0) {
        return;
    }

    opts->decode_description_attributes = 1;
    opts->malformed_mode = OMC_XMP_MALFORMED_STRICT;
    opts->limits.max_depth = 128U;
    opts->limits.max_properties = 200000U;
    opts->limits.max_input_bytes = 64U * 1024U * 1024U;
    opts->limits.max_path_bytes = 1024U;
    opts->limits.max_value_bytes = 8U * 1024U * 1024U;
    opts->limits.max_total_value_bytes = 64U * 1024U * 1024U;
    opts->limits.max_attributes_per_element = OMC_XMP_ATTR_CAP;
    opts->limits.max_namespace_bytes = 4096U;
    opts->limits.max_arena_bytes = 64U * 1024U * 1024U;
}

static void
omc_xmp_set_res(omc_xmp_res* res, omc_xmp_status status)
{
    if (res == (omc_xmp_res*)0) {
        return;
    }
    res->status = status;
    res->entries_decoded = 0U;
}

static int
omc_xmp_u32_mul_add_fits(omc_u32 value, omc_u32 mul, omc_u32 add,
                         omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (mul != 0U
        && value > (((omc_u32)(~(omc_u32)0) - add) / mul)) {
        return 0;
    }

    *out_value = value * mul + add;
    return 1;
}

static int
omc_xmp_u32_add3_fits(omc_u32 a, omc_u32 b, omc_u32 c, omc_u32* out_value)
{
    omc_u32 max_value;
    omc_u32 tmp;

    if (out_value == (omc_u32*)0) {
        return 0;
    }

    max_value = (omc_u32)~(omc_u32)0;
    if (a > max_value - b) {
        return 0;
    }
    tmp = a + b;
    if (tmp > max_value - c) {
        return 0;
    }

    *out_value = tmp + c;
    return 1;
}

static int
omc_xmp_alloc_size_fits(omc_size count, omc_size elem_size)
{
    return elem_size == 0U || count <= ((omc_size)(~(omc_size)0) / elem_size);
}

static void
omc_xmp_mark_malformed(omc_xmp_ctx* ctx)
{
    if (ctx == (omc_xmp_ctx*)0) {
        return;
    }
    if (ctx->res.status == OMC_XMP_LIMIT || ctx->res.status == OMC_XMP_NOMEM) {
        return;
    }
    ctx->res.status = (ctx->opts.malformed_mode == OMC_XMP_MALFORMED_TRUNCATED)
                          ? OMC_XMP_TRUNCATED
                          : OMC_XMP_MALFORMED;
}

static int
omc_xmp_is_space(omc_u8 c)
{
    return c == (omc_u8)' ' || c == (omc_u8)'\t' || c == (omc_u8)'\r'
           || c == (omc_u8)'\n';
}

static int
omc_xmp_is_name_start(omc_u8 c)
{
    return ((c >= (omc_u8)'A' && c <= (omc_u8)'Z')
            || (c >= (omc_u8)'a' && c <= (omc_u8)'z') || c == (omc_u8)'_');
}

static int
omc_xmp_is_name_char(omc_u8 c)
{
    return omc_xmp_is_name_start(c) || (c >= (omc_u8)'0' && c <= (omc_u8)'9')
           || c == (omc_u8)'-' || c == (omc_u8)'.' || c == (omc_u8)':';
}

static int
omc_xmp_is_bad_xml_char(omc_u8 c)
{
    return c < 0x20U && c != (omc_u8)'\t' && c != (omc_u8)'\r'
           && c != (omc_u8)'\n';
}

static omc_xmp_span
omc_xmp_span_make(omc_u32 off, omc_u32 len)
{
    omc_xmp_span span;
    span.off = off;
    span.len = len;
    return span;
}

static int
omc_xmp_span_eq_lit(const omc_xmp_ctx* ctx, omc_xmp_span span,
                    const char* lit)
{
    omc_size lit_len;

    if (ctx == (const omc_xmp_ctx*)0 || lit == (const char*)0) {
        return 0;
    }

    lit_len = strlen(lit);
    if ((omc_size)span.len != lit_len) {
        return 0;
    }
    return memcmp(ctx->bytes + span.off, lit, lit_len) == 0;
}

static int
omc_xmp_spans_equal(const omc_xmp_ctx* ctx, omc_xmp_span a, omc_xmp_span b)
{
    if (ctx == (const omc_xmp_ctx*)0) {
        return 0;
    }
    if (a.len != b.len) {
        return 0;
    }
    if (a.len == 0U) {
        return 1;
    }
    return memcmp(ctx->bytes + a.off, ctx->bytes + b.off, (omc_size)a.len) == 0;
}

static void
omc_xmp_trim_value(const omc_xmp_ctx* ctx, omc_xmp_span in, omc_xmp_span* out)
{
    omc_u32 start;
    omc_u32 end;

    if (out == (omc_xmp_span*)0) {
        return;
    }
    start = in.off;
    end = in.off + in.len;
    while (start < end && omc_xmp_is_space(ctx->bytes[start])) {
        start += 1U;
    }
    while (end > start && omc_xmp_is_space(ctx->bytes[end - 1U])) {
        end -= 1U;
    }
    *out = omc_xmp_span_make(start, end - start);
}

static void
omc_xmp_skip_space(omc_xmp_ctx* ctx, omc_u32* io_pos)
{
    omc_u32 pos;

    pos = *io_pos;
    while ((omc_size)pos < ctx->size && omc_xmp_is_space(ctx->bytes[pos])) {
        pos += 1U;
    }
    *io_pos = pos;
}

static int
omc_xmp_parse_name(const omc_xmp_ctx* ctx, omc_u32* io_pos,
                   omc_xmp_span* out_prefix, omc_xmp_span* out_local)
{
    omc_u32 pos;
    omc_u32 start;
    omc_u32 colon;

    pos = *io_pos;
    if ((omc_size)pos >= ctx->size || !omc_xmp_is_name_start(ctx->bytes[pos])) {
        return 0;
    }

    start = pos;
    colon = 0xFFFFFFFFU;
    while ((omc_size)pos < ctx->size && omc_xmp_is_name_char(ctx->bytes[pos])) {
        if (ctx->bytes[pos] == (omc_u8)':' && colon == 0xFFFFFFFFU) {
            colon = pos;
        } else if (ctx->bytes[pos] == (omc_u8)':') {
            return 0;
        }
        pos += 1U;
    }

    if (colon == 0xFFFFFFFFU) {
        *out_prefix = omc_xmp_span_make(start, 0U);
        *out_local = omc_xmp_span_make(start, pos - start);
    } else {
        if (colon + 1U >= pos || !omc_xmp_is_name_start(ctx->bytes[colon + 1U]))
            return 0;
        *out_prefix = omc_xmp_span_make(start, colon - start);
        *out_local = omc_xmp_span_make(colon + 1U, pos - colon - 1U);
    }
    *io_pos = pos;
    return 1;
}

static int
omc_xmp_find_until(const omc_xmp_ctx* ctx, omc_u32 pos, const char* tail,
                   omc_u32* out_end)
{
    omc_size tail_len;
    omc_u32 limit;

    tail_len = strlen(tail);
    if (tail_len == 0U) {
        return 0;
    }
    if ((omc_size)pos > ctx->size || tail_len > (ctx->size - (omc_size)pos)) {
        return 0;
    }

    limit = (omc_u32)(ctx->size - tail_len);
    while (pos <= limit) {
        if (memcmp(ctx->bytes + pos, tail, tail_len) == 0) {
            *out_end = pos;
            return 1;
        }
        pos += 1U;
    }
    return 0;
}

static int
omc_xmp_contains_literal(const omc_xmp_ctx* ctx, omc_u32 pos, const char* lit)
{
    omc_size lit_len;
    omc_u32 limit;

    lit_len = strlen(lit);
    if (lit_len == 0U) {
        return 0;
    }
    if ((omc_size)pos > ctx->size || lit_len > (ctx->size - (omc_size)pos)) {
        return 0;
    }

    limit = (omc_u32)(ctx->size - lit_len);
    while (pos <= limit) {
        if (memcmp(ctx->bytes + pos, lit, lit_len) == 0) {
            return 1;
        }
        pos += 1U;
    }
    return 0;
}

static int
omc_xmp_lookup_ns(const omc_xmp_ctx* ctx, omc_xmp_span prefix,
                  omc_xmp_span* out_uri)
{
    omc_u32 i;

    if (out_uri == (omc_xmp_span*)0) {
        return 0;
    }

    i = ctx->ns_count;
    while (i > 0U) {
        i -= 1U;
        if (ctx->ns_decls[i].prefix.len == prefix.len
            && omc_xmp_spans_equal(ctx, ctx->ns_decls[i].prefix, prefix)) {
            *out_uri = ctx->ns_decls[i].uri;
            return 1;
        }
    }

    if (prefix.len == 3U
        && memcmp(ctx->bytes + prefix.off, "xml", 3U) == 0) {
        *out_uri = omc_xmp_span_make(0U, 0U);
        return 0;
    }

    return 0;
}

static int
omc_xmp_attr_find_resource(const omc_xmp_ctx* ctx, const omc_xmp_attr* attrs,
                           omc_u32 attr_count, omc_xmp_span* out_value)
{
    omc_u32 i;

    for (i = 0U; i < attr_count; ++i) {
        omc_xmp_span attr_ns;

        if (attrs[i].is_xmlns || attrs[i].is_default_xmlns) {
            continue;
        }
        if (!attrs[i].prefix.len ||
            !omc_xmp_lookup_ns(ctx, attrs[i].prefix, &attr_ns)) {
            continue;
        }
        if (omc_xmp_span_eq_lit(ctx, attr_ns, k_ns_rdf)
            && omc_xmp_span_eq_lit(ctx, attrs[i].local, "resource")) {
            *out_value = attrs[i].value;
            return 1;
        }
    }
    return 0;
}

static int
omc_xmp_attr_find_xml_lang(const omc_xmp_ctx* ctx, const omc_xmp_attr* attrs,
                           omc_u32 attr_count, omc_xmp_span* out_value)
{
    omc_u32 i;

    if (out_value == (omc_xmp_span*)0) {
        return 0;
    }

    for (i = 0U; i < attr_count; ++i) {
        if (attrs[i].is_xmlns || attrs[i].is_default_xmlns) {
            continue;
        }
        if (attrs[i].prefix.len == 3U
            && memcmp(ctx->bytes + attrs[i].prefix.off, "xml", 3U) == 0
            && omc_xmp_span_eq_lit(ctx, attrs[i].local, "lang")) {
            omc_u32 j;
            omc_xmp_trim_value(ctx, attrs[i].value, out_value);
            if (out_value->len == 0U)
                return 0;
            for (j = 0U; j < out_value->len; ++j) {
                omc_u8 c;
                c = ctx->bytes[out_value->off + j];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-'))
                    return 0;
            }
            return 1;
        }
    }

    return 0;
}

static int
omc_xmp_frame_has_value_path(omc_xmp_frame_kind kind)
{
    return kind == OMC_XMP_FRAME_PROPERTY || kind == OMC_XMP_FRAME_LI
           || kind == OMC_XMP_FRAME_STRUCT_FIELD;
}

/* Decode only the predefined XML entities and character references. No DTD,
 * external entity, or network resolver is part of the core parser. Spans are
 * compacted in the owned input copy; unread bytes retain their file offsets. */
static int omc_xmp_decode_text(omc_xmp_ctx *ctx, omc_xmp_span *span, int attribute,
                               int cdata)
{
    omc_u32 read_pos;
    omc_u32 write_pos;
    omc_u32 end;
    omc_u32 cp;
    omc_u32 minimum;
    omc_u32 remaining;
    omc_u8 c;
    read_pos = span->off;
    write_pos = read_pos;
    end = read_pos + span->len;
    while (read_pos < end) {
        c = ctx->bytes[read_pos++];
        cp = c;
        if (c == '&' && !cdata) {
            omc_u32 entity_start;
            omc_u32 entity_len;
            entity_start = read_pos;
            while (read_pos < end && ctx->bytes[read_pos] != ';')
                ++read_pos;
            if (read_pos == end)
                return 0;
            entity_len = read_pos - entity_start;
            ++read_pos;
            if (entity_len && ctx->bytes[entity_start] == '#') {
                omc_u32 radix;
                omc_u32 i;
                radix = 10U;
                i = entity_start + 1U;
                if (i < read_pos - 1U && ctx->bytes[i] == 'x') {
                    radix = 16U;
                    ++i;
                }
                if (i == read_pos - 1U)
                    return 0;
                cp = 0U;
                for (; i < read_pos - 1U; ++i) {
                    omc_u32 digit;
                    c = ctx->bytes[i];
                    if (c >= '0' && c <= '9')
                        digit = c - '0';
                    else if (radix == 16U && c >= 'a' && c <= 'f')
                        digit = c - 'a' + 10U;
                    else if (radix == 16U && c >= 'A' && c <= 'F')
                        digit = c - 'A' + 10U;
                    else
                        return 0;
                    if (cp > (0x10FFFFU - digit) / radix)
                        return 0;
                    cp = cp * radix + digit;
                }
            } else if (entity_len == 3U &&
                       !memcmp(ctx->bytes + entity_start, "amp", 3U))
                cp = '&';
            else if (entity_len == 2U && !memcmp(ctx->bytes + entity_start, "lt", 2U))
                cp = '<';
            else if (entity_len == 2U && !memcmp(ctx->bytes + entity_start, "gt", 2U))
                cp = '>';
            else if (entity_len == 4U && !memcmp(ctx->bytes + entity_start, "apos", 4U))
                cp = '\'';
            else if (entity_len == 4U && !memcmp(ctx->bytes + entity_start, "quot", 4U))
                cp = '"';
            else
                return 0;
        } else {
            if (attribute && c == '<')
                return 0;
            if (!attribute && !cdata && c == ']' && end - read_pos >= 2U &&
                ctx->bytes[read_pos] == ']' && ctx->bytes[read_pos + 1U] == '>')
                return 0;
            if (c == '\r') {
                if (read_pos < end && ctx->bytes[read_pos] == '\n')
                    ++read_pos;
                cp = '\n';
            }
            if (attribute && (cp == '\n' || cp == '\t'))
                cp = ' ';
            if (c >= 0x80U) {
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
                    return 0;
                if (remaining > end - read_pos)
                    return 0;
                while (remaining--) {
                    c = ctx->bytes[read_pos++];
                    if ((c & 0xC0U) != 0x80U)
                        return 0;
                    cp = (cp << 6U) | (c & 63U);
                }
                if (cp < minimum)
                    return 0;
            }
        }
        if (!(cp == 9U || cp == 10U || cp == 13U || (cp >= 0x20U && cp <= 0xD7FFU) ||
              (cp >= 0xE000U && cp <= 0xFFFDU) || (cp >= 0x10000U && cp <= 0x10FFFFU)))
            return 0;
        if (cp < 0x80U)
            ctx->input_copy[write_pos++] = (omc_u8)cp;
        else if (cp < 0x800U) {
            ctx->input_copy[write_pos++] = (omc_u8)(0xC0U | (cp >> 6U));
            ctx->input_copy[write_pos++] = (omc_u8)(0x80U | (cp & 63U));
        } else if (cp < 0x10000U) {
            ctx->input_copy[write_pos++] = (omc_u8)(0xE0U | (cp >> 12U));
            ctx->input_copy[write_pos++] = (omc_u8)(0x80U | ((cp >> 6U) & 63U));
            ctx->input_copy[write_pos++] = (omc_u8)(0x80U | (cp & 63U));
        } else {
            ctx->input_copy[write_pos++] = (omc_u8)(0xF0U | (cp >> 18U));
            ctx->input_copy[write_pos++] = (omc_u8)(0x80U | ((cp >> 12U) & 63U));
            ctx->input_copy[write_pos++] = (omc_u8)(0x80U | ((cp >> 6U) & 63U));
            ctx->input_copy[write_pos++] = (omc_u8)(0x80U | (cp & 63U));
        }
    }
    span->len = write_pos - span->off;
    return 1;
}

static int omc_xmp_append_text(omc_xmp_ctx *ctx, omc_xmp_frame *frame, omc_u32 start,
                               omc_u32 end, int cdata)
{
    omc_xmp_span span;
    span = omc_xmp_span_make(start, end - start);
    if (!omc_xmp_decode_text(ctx, &span, 0, cdata))
        return 0;
    if (frame && omc_xmp_frame_has_value_path(frame->kind) && !frame->had_child) {
        if (!frame->saw_text) {
            frame->text_start = span.off;
            frame->text_end = span.off;
            frame->saw_text = 1;
        }
        if ((omc_u64)(frame->text_end - frame->text_start) + span.len >
            ctx->opts.limits.max_value_bytes) {
            ctx->res.status = OMC_XMP_LIMIT;
            return 0;
        }
        memmove(ctx->input_copy + frame->text_end, ctx->bytes + span.off, span.len);
        frame->text_end += span.len;
    }
    return 1;
}

static omc_xmp_status
omc_xmp_store_bytes(omc_xmp_ctx* ctx, const omc_u8* src, omc_u32 size,
                    omc_byte_ref* out_ref)
{
    omc_status st;

    st = omc_arena_append(&ctx->store->arena, src, (omc_size)size, out_ref);
    if (st == OMC_STATUS_OK) {
        return OMC_XMP_OK;
    }
    if (st == OMC_STATUS_NO_MEMORY) {
        return OMC_XMP_NOMEM;
    }
    return OMC_XMP_LIMIT;
}

static omc_xmp_status
omc_xmp_add_property(omc_xmp_ctx* ctx, omc_xmp_span schema_ns,
                     const omc_u8* path_bytes, omc_u32 path_size,
                     omc_xmp_span value_span)
{
    omc_entry entry;
    omc_byte_ref ns_ref;
    omc_byte_ref path_ref;
    omc_byte_ref val_ref;
    omc_status st;
    omc_xmp_status status;

    if (schema_ns.len == 0U || path_size == 0U)
        return OMC_XMP_OK;
    if (ctx->res.entries_decoded >= ctx->opts.limits.max_properties) {
        return OMC_XMP_LIMIT;
    }
    if (schema_ns.len > ctx->opts.limits.max_namespace_bytes ||
        (omc_u64)ctx->store->arena.size + schema_ns.len + path_size + value_span.len >
            ctx->opts.limits.max_arena_bytes ||
        path_size > ctx->opts.limits.max_path_bytes ||
        value_span.len > ctx->opts.limits.max_value_bytes) {
        return OMC_XMP_LIMIT;
    }
    if (ctx->opts.limits.max_total_value_bytes != 0U
        && ctx->total_value_bytes + value_span.len
               > ctx->opts.limits.max_total_value_bytes) {
        return OMC_XMP_LIMIT;
    }
    if (!ctx->measure_only) {
        status = omc_xmp_store_bytes(ctx, ctx->bytes + schema_ns.off,
                                     schema_ns.len, &ns_ref);
        if (status != OMC_XMP_OK) {
            return status;
        }
        status = omc_xmp_store_bytes(ctx, path_bytes, path_size, &path_ref);
        if (status != OMC_XMP_OK) {
            return status;
        }
        status = omc_xmp_store_bytes(ctx, ctx->bytes + value_span.off,
                                     value_span.len, &val_ref);
        if (status != OMC_XMP_OK) {
            return status;
        }

        memset(&entry, 0, sizeof(entry));
        omc_key_make_xmp_property(&entry.key, ns_ref, path_ref);
        omc_val_make_text(&entry.value, val_ref, OMC_TEXT_UTF8);
        entry.origin.block = ctx->source_block;
        entry.origin.order_in_block = ctx->order;
        entry.origin.wire_type.family = OMC_WIRE_OTHER;
        entry.origin.wire_type.code = 0U;
        entry.origin.wire_count = value_span.len;
        entry.flags = ctx->base_flags;
        st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
        if (st == OMC_STATUS_NO_MEMORY) {
            return OMC_XMP_NOMEM;
        }
        if (st != OMC_STATUS_OK) {
            return OMC_XMP_LIMIT;
        }
    }

    ctx->total_value_bytes += value_span.len;
    ctx->res.entries_decoded += 1U;
    ctx->order += 1U;
    return OMC_XMP_OK;
}

static omc_xmp_status
omc_xmp_emit_property_from_spans(omc_xmp_ctx* ctx, omc_xmp_span schema_ns,
                                 omc_xmp_span path, omc_xmp_span value_span)
{
    return omc_xmp_add_property(ctx, schema_ns, ctx->bytes + path.off, path.len,
                                value_span);
}

static char*
omc_xmp_frame_path_slot(omc_xmp_ctx* ctx, omc_u32 depth)
{
    return ctx->frame_path_buf + ((omc_size)depth * (omc_size)ctx->path_cap);
}

static int
omc_xmp_frame_set_path_from_span(omc_xmp_ctx* ctx, omc_xmp_frame* frame,
                                 omc_u32 depth, omc_xmp_span path)
{
    char* dst;

    if (path.len > ctx->opts.limits.max_path_bytes
        || path.len > ctx->path_cap) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }

    dst = omc_xmp_frame_path_slot(ctx, depth);
    if (path.len != 0U) {
        memcpy(dst, ctx->bytes + path.off, path.len);
    }
    frame->path_off = depth * ctx->path_cap;
    frame->path_len = path.len;
    frame->has_path = 1;
    return 1;
}

static int
omc_xmp_frame_set_path_from_parent(omc_xmp_ctx* ctx, omc_xmp_frame* frame,
                                   omc_u32 depth,
                                   const omc_xmp_frame* parent)
{
    char* dst;
    const char* src;

    if (parent == (const omc_xmp_frame*)0 || !parent->has_path) {
        return 0;
    }
    if (parent->path_len > ctx->path_cap) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }

    dst = omc_xmp_frame_path_slot(ctx, depth);
    src = ctx->frame_path_buf + parent->path_off;
    if (parent->path_len != 0U) {
        memcpy(dst, src, parent->path_len);
    }
    frame->path_off = depth * ctx->path_cap;
    frame->path_len = parent->path_len;
    frame->has_path = 1;
    return 1;
}

static omc_u32
omc_xmp_count_u32_digits(omc_u32 value)
{
    omc_u32 count;

    count = 0U;
    do {
        value /= 10U;
        count += 1U;
    } while (value != 0U);

    return count;
}

static int
omc_xmp_frame_set_path_indexed(omc_xmp_ctx* ctx, omc_xmp_frame* frame,
                               omc_u32 depth, const omc_xmp_frame* parent,
                               omc_u32 index)
{
    char digits[16];
    char* dst;
    const char* src;
    omc_u32 digit_count;
    omc_u32 path_len;
    omc_u32 value;
    omc_u32 pos;
    omc_u32 i;

    if (parent == (const omc_xmp_frame*)0 || !parent->has_path) {
        return 0;
    }

    digit_count = omc_xmp_count_u32_digits(index);
    if (!omc_xmp_u32_add3_fits(parent->path_len, digit_count, 2U,
                               &path_len)
        || path_len > ctx->opts.limits.max_path_bytes
        || path_len > ctx->path_cap) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }

    value = index;
    for (i = 0U; i < digit_count; ++i) {
        digits[digit_count - i - 1U] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    dst = omc_xmp_frame_path_slot(ctx, depth);
    src = ctx->frame_path_buf + parent->path_off;
    if (parent->path_len != 0U) {
        memcpy(dst, src, parent->path_len);
    }
    pos = parent->path_len;
    dst[pos] = '[';
    pos += 1U;
    memcpy(dst + pos, digits, digit_count);
    pos += digit_count;
    dst[pos] = ']';
    pos += 1U;

    frame->path_off = depth * ctx->path_cap;
    frame->path_len = pos;
    frame->has_path = 1;
    return 1;
}

static int
omc_xmp_frame_set_path_lang(omc_xmp_ctx* ctx, omc_xmp_frame* frame,
                            omc_u32 depth, const omc_xmp_frame* parent,
                            omc_xmp_span lang)
{
    static const char prefix[] = "[@xml:lang=";
    char* dst;
    const char* src;
    omc_u32 pos;
    omc_u32 prefix_len;
    omc_u32 path_len;

    if (parent == (const omc_xmp_frame*)0 || !parent->has_path) {
        return 0;
    }

    prefix_len = (omc_u32)(sizeof(prefix) - 1U);
    if (!omc_xmp_u32_add3_fits(parent->path_len, prefix_len, lang.len,
                               &path_len)
        || path_len == (omc_u32)~(omc_u32)0) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }
    path_len += 1U;
    if (path_len > ctx->opts.limits.max_path_bytes
        || path_len > ctx->path_cap) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }

    dst = omc_xmp_frame_path_slot(ctx, depth);
    src = ctx->frame_path_buf + parent->path_off;
    if (parent->path_len != 0U) {
        memcpy(dst, src, parent->path_len);
    }
    pos = parent->path_len;
    memcpy(dst + pos, prefix, prefix_len);
    pos += prefix_len;
    if (lang.len != 0U) {
        memcpy(dst + pos, ctx->bytes + lang.off, lang.len);
    }
    pos += lang.len;
    dst[pos] = ']';
    pos += 1U;

    frame->path_off = depth * ctx->path_cap;
    frame->path_len = pos;
    frame->has_path = 1;
    return 1;
}

static const char *omc_xmp_nested_prefix(const omc_xmp_ctx *ctx, omc_xmp_span ns)
{
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/"))
        return "xmp";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/tiff/1.0/"))
        return "tiff";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/exif/1.0/"))
        return "exif";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/exif/1.0/aux/"))
        return "aux";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://purl.org/dc/elements/1.1/"))
        return "dc";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/pdf/1.3/"))
        return "pdf";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/bj/"))
        return "xmpBJ";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.useplus.org/ldf/xmp/1.0/"))
        return "plus";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/camera-raw-settings/1.0/"))
        return "crs";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/lightroom/1.0/"))
        return "lr";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xmp/1.0/DynamicMedia/"))
        return "xmpDM";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/mm/"))
        return "xmpMM";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/t/pg/"))
        return "xmpTPg";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/rights/"))
        return "xmpRights";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/sType/Dimensions#"))
        return "stDim";
    if (omc_xmp_span_eq_lit(ctx, ns,
                            "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#"))
        return "stEvt";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/sType/Font#"))
        return "stFnt";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/sType/Job#"))
        return "stJob";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/sType/ManifestItem#"))
        return "stMfs";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/sType/ResourceRef#"))
        return "stRef";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/sType/Version#"))
        return "stVer";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/xap/1.0/g/"))
        return "xmpG";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://ns.adobe.com/photoshop/1.0/"))
        return "photoshop";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"))
        return "Iptc4xmpCore";
    if (omc_xmp_span_eq_lit(ctx, ns, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/"))
        return "Iptc4xmpExt";
    return (const char *)0;
}

static int omc_xmp_frame_set_path_child(omc_xmp_ctx *ctx, omc_xmp_frame *frame,
                                        omc_u32 depth, const omc_xmp_frame *parent,
                                        omc_xmp_span child_local, omc_xmp_span child_ns)
{
    static const char hex[] = "0123456789abcdef";
    const char *prefix;
    char* dst;
    omc_u32 prefix_len;
    omc_u32 component_len;
    omc_u32 path_len;
    omc_u32 pos;
    omc_u32 i;
    int unknown;
    if (parent == (const omc_xmp_frame *)0 || !parent->has_path)
        return 0;
    prefix = (const char *)0;
    unknown = 0;
    prefix_len = 0U;
    if (child_ns.len && !omc_xmp_spans_equal(ctx, child_ns, parent->prop_ns_uri)) {
        prefix = omc_xmp_nested_prefix(ctx, child_ns);
        if (prefix)
            prefix_len = (omc_u32)strlen(prefix);
        else {
            unknown = 1;
            if (!omc_xmp_u32_mul_add_fits(child_ns.len, 2U, 4U, &prefix_len)) {
                ctx->res.status = OMC_XMP_LIMIT;
                return 0;
            }
        }
    }
    if (!omc_xmp_u32_add3_fits(prefix_len, child_local.len, prefix_len ? 1U : 0U,
                               &component_len) ||
        !omc_xmp_u32_add3_fits(parent->path_len, component_len, 1U, &path_len) ||
        path_len > ctx->opts.limits.max_path_bytes || path_len > ctx->path_cap) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }
    dst = omc_xmp_frame_path_slot(ctx, depth);
    memcpy(dst, ctx->frame_path_buf + parent->path_off, parent->path_len);
    pos = parent->path_len;
    dst[pos++] = '/';
    if (unknown) {
        memcpy(dst + pos, "nsu_", 4U);
        pos += 4U;
        for (i = 0U; i < child_ns.len; ++i) {
            omc_u8 c;
            c = ctx->bytes[child_ns.off + i];
            dst[pos++] = hex[c >> 4U];
            dst[pos++] = hex[c & 15U];
        }
    } else if (prefix_len) {
        memcpy(dst + pos, prefix, prefix_len);
        pos += prefix_len;
    }
    if (prefix_len)
        dst[pos++] = ':';
    memcpy(dst + pos, ctx->bytes + child_local.off, child_local.len);
    frame->path_off = depth * ctx->path_cap;
    frame->path_len = path_len;
    frame->has_path = 1;
    return 1;
}

static omc_xmp_status
omc_xmp_emit_property_from_frame_path(omc_xmp_ctx* ctx,
                                      const omc_xmp_frame* frame,
                                      omc_xmp_span value_span)
{
    if (frame == (const omc_xmp_frame*)0 || !frame->has_path) {
        return OMC_XMP_LIMIT;
    }
    return omc_xmp_add_property(
        ctx, frame->prop_ns_uri,
        (const omc_u8*)(ctx->frame_path_buf + frame->path_off),
        frame->path_len, value_span);
}

static int
omc_xmp_validate_span_text(const omc_xmp_ctx* ctx, omc_xmp_span span)
{
    omc_u32 i;

    for (i = 0U; i < span.len; ++i) {
        if (omc_xmp_is_bad_xml_char(ctx->bytes[span.off + i])) {
            return 0;
        }
    }
    return 1;
}

static void
omc_xmp_frame_reset_text(omc_xmp_frame* frame)
{
    frame->text_start = 0U;
    frame->text_end = 0U;
    frame->saw_text = 0;
}

static omc_xmp_status
omc_xmp_emit_root_attrs(omc_xmp_ctx* ctx, omc_xmp_span elem_ns,
                        const omc_xmp_attr* attrs, omc_u32 attr_count)
{
    omc_u32 i;

    for (i = 0U; i < attr_count; ++i) {
        omc_xmp_span attr_ns;
        omc_xmp_span value_trim;

        if (attrs[i].is_xmlns || attrs[i].is_default_xmlns) {
            continue;
        }
        if (!attrs[i].prefix.len ||
            !omc_xmp_lookup_ns(ctx, attrs[i].prefix, &attr_ns)) {
            continue;
        }
        if (!omc_xmp_spans_equal(ctx, attr_ns, elem_ns)) {
            continue;
        }
        if (omc_xmp_span_eq_lit(ctx, attrs[i].local, "xmptk")) {
            omc_xmp_trim_value(ctx, attrs[i].value, &value_trim);
            return omc_xmp_add_property(ctx, elem_ns,
                                        (const omc_u8*)"XMPToolkit", 10U,
                                        value_trim);
        }
    }

    return OMC_XMP_OK;
}

static omc_xmp_status
omc_xmp_emit_description_attrs(omc_xmp_ctx* ctx, const omc_xmp_attr* attrs,
                               omc_u32 attr_count)
{
    omc_u32 i;

    for (i = 0U; i < attr_count; ++i) {
        omc_xmp_span attr_ns;
        omc_xmp_span value_trim;
        omc_xmp_status status;

        if (attrs[i].is_xmlns || attrs[i].is_default_xmlns) {
            continue;
        }
        if (!attrs[i].prefix.len ||
            !omc_xmp_lookup_ns(ctx, attrs[i].prefix, &attr_ns)) {
            continue;
        }
        omc_xmp_trim_value(ctx, attrs[i].value, &value_trim);
        if (omc_xmp_span_eq_lit(ctx, attr_ns, k_ns_rdf)) {
            if (value_trim.len && omc_xmp_span_eq_lit(ctx, attrs[i].local, "about")) {
                status = omc_xmp_add_property(ctx, attr_ns, (const omc_u8 *)"About", 5U,
                                              value_trim);
                if (status != OMC_XMP_OK)
                    return status;
            }
            continue;
        }
        if (omc_xmp_span_eq_lit(ctx, attr_ns, k_ns_xml))
            continue;
        status = omc_xmp_emit_property_from_spans(ctx, attr_ns, attrs[i].local,
                                                  value_trim);
        if (status != OMC_XMP_OK) {
            return status;
        }
    }

    return OMC_XMP_OK;
}

static int
omc_xmp_push_frame(omc_xmp_ctx* ctx, const omc_xmp_frame* frame)
{
    if (ctx->frame_count >= ctx->frame_cap
        || ctx->frame_count >= ctx->opts.limits.max_depth) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }
    ctx->frames[ctx->frame_count] = *frame;
    ctx->frame_count += 1U;
    return 1;
}

static omc_xmp_frame*
omc_xmp_top_frame(omc_xmp_ctx* ctx)
{
    if (ctx->frame_count == 0U) {
        return (omc_xmp_frame*)0;
    }
    return &ctx->frames[ctx->frame_count - 1U];
}

static int
omc_xmp_parse_start_tag(omc_xmp_ctx* ctx, omc_u32* io_pos)
{
    omc_u32 pos;
    omc_xmp_span prefix;
    omc_xmp_span local;
    omc_xmp_attr *attrs;
    omc_u32 attr_count;
    int empty_element;
    omc_xmp_frame frame;
    omc_xmp_frame* parent;
    omc_xmp_span elem_ns;
    omc_u32 ns_mark;
    omc_u32 i;
    int in_description;
    omc_xmp_frame *array;
    attrs = ctx->attrs;
    pos = *io_pos;
    pos += 1U;
    if (!omc_xmp_parse_name(ctx, &pos, &prefix, &local)) {
        return 0;
    }

    attr_count = 0U;
    ns_mark = ctx->ns_count;
    empty_element = 0;

    for (;;) {
        omc_xmp_span aprefix;
        omc_xmp_span alocal;
        omc_u8 quote;
        omc_u32 value_start;
        omc_u32 value_end;
        omc_u32 space_start;

        space_start = pos;
        omc_xmp_skip_space(ctx, &pos);
        if ((omc_size)pos >= ctx->size) {
            return 0;
        }
        if (ctx->bytes[pos] == (omc_u8)'/') {
            if ((omc_size)(pos + 1U) >= ctx->size
                || ctx->bytes[pos + 1U] != (omc_u8)'>') {
                return 0;
            }
            empty_element = 1;
            pos += 2U;
            break;
        }
        if (ctx->bytes[pos] == (omc_u8)'>') {
            pos += 1U;
            break;
        }
        if (space_start == pos)
            return 0;
        if (attr_count >= ctx->opts.limits.max_attributes_per_element) {
            ctx->res.status = OMC_XMP_LIMIT;
            return 0;
        }

        if (!omc_xmp_parse_name(ctx, &pos, &aprefix, &alocal)) {
            return 0;
        }
        omc_xmp_skip_space(ctx, &pos);
        if ((omc_size)pos >= ctx->size || ctx->bytes[pos] != (omc_u8)'=') {
            return 0;
        }
        pos += 1U;
        omc_xmp_skip_space(ctx, &pos);
        if ((omc_size)pos >= ctx->size) {
            return 0;
        }
        quote = ctx->bytes[pos];
        if (quote != (omc_u8)'\'' && quote != (omc_u8)'"') {
            return 0;
        }
        pos += 1U;
        value_start = pos;
        while ((omc_size)pos < ctx->size && ctx->bytes[pos] != quote) {
            if (omc_xmp_is_bad_xml_char(ctx->bytes[pos])) {
                omc_xmp_mark_malformed(ctx);
                return 0;
            }
            pos += 1U;
        }
        if ((omc_size)pos >= ctx->size) {
            return 0;
        }
        value_end = pos;
        pos += 1U;

        attrs[attr_count].prefix = aprefix;
        attrs[attr_count].local = alocal;
        attrs[attr_count].value =
            omc_xmp_span_make(value_start, value_end - value_start);
        if (!omc_xmp_decode_text(ctx, &attrs[attr_count].value, 1, 0))
            return 0;
        attrs[attr_count].is_default_xmlns = 0;
        attrs[attr_count].is_xmlns = 0;

        if (aprefix.len == 0U && omc_xmp_span_eq_lit(ctx, alocal, "xmlns")) {
            attrs[attr_count].is_default_xmlns = 1;
            if (ctx->ns_count >= ctx->ns_cap) {
                ctx->res.status = OMC_XMP_LIMIT;
                return 0;
            }
            ctx->ns_decls[ctx->ns_count].prefix = omc_xmp_span_make(0U, 0U);
            ctx->ns_decls[ctx->ns_count].uri = attrs[attr_count].value;
            ctx->ns_decls[ctx->ns_count].depth = ctx->frame_count + 1U;
            ctx->ns_count += 1U;
        } else if (aprefix.len == 5U
                   && memcmp(ctx->bytes + aprefix.off, "xmlns", 5U) == 0) {
            attrs[attr_count].is_xmlns = 1;
            if (ctx->ns_count >= ctx->ns_cap) {
                ctx->res.status = OMC_XMP_LIMIT;
                return 0;
            }
            ctx->ns_decls[ctx->ns_count].prefix = alocal;
            ctx->ns_decls[ctx->ns_count].uri = attrs[attr_count].value;
            ctx->ns_decls[ctx->ns_count].depth = ctx->frame_count + 1U;
            ctx->ns_count += 1U;
        }
        attr_count += 1U;
    }

    /* Namespaces apply to the whole start tag, including earlier attributes.
     * Unprefixed attributes never inherit the default element namespace. */
    for (i = 0U; i < attr_count; ++i) {
        omc_u32 j;
        omc_xmp_span uri;
        if (!attrs[i].is_xmlns && !attrs[i].is_default_xmlns && attrs[i].prefix.len &&
            !omc_xmp_span_eq_lit(ctx, attrs[i].prefix, "xml") &&
            !omc_xmp_lookup_ns(ctx, attrs[i].prefix, &uri))
            return 0;
        for (j = 0U; j < i; ++j) {
            omc_xmp_span other;
            if (!omc_xmp_spans_equal(ctx, attrs[i].local, attrs[j].local))
                continue;
            if (omc_xmp_spans_equal(ctx, attrs[i].prefix, attrs[j].prefix))
                return 0;
            if (attrs[i].prefix.len && attrs[j].prefix.len &&
                omc_xmp_lookup_ns(ctx, attrs[i].prefix, &uri) &&
                omc_xmp_lookup_ns(ctx, attrs[j].prefix, &other) &&
                omc_xmp_spans_equal(ctx, uri, other))
                return 0;
        }
    }
    if (!ctx->frame_count) {
        if (ctx->root_seen)
            return 0;
        ctx->root_seen = 1;
    }
    if (ctx->frame_count >= ctx->frame_cap) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }
    memset(&frame, 0, sizeof(frame));
    frame.kind = OMC_XMP_FRAME_GENERIC;
    frame.prefix = prefix;
    frame.local = local;
    frame.ns_mark = ns_mark;
    omc_xmp_frame_reset_text(&frame);

    elem_ns = omc_xmp_span_make(0U, 0U);
    if (prefix.len != 0U && !omc_xmp_lookup_ns(ctx, prefix, &elem_ns)) {
        return 0;
    } else if (prefix.len == 0U) {
        (void)omc_xmp_lookup_ns(ctx, prefix, &elem_ns);
    }
    frame.ns_uri = elem_ns;

    parent = omc_xmp_top_frame(ctx);
    if (parent) {
        parent->had_child = 1;
        if (parent->has_path) {
            frame.prop_ns_uri = parent->prop_ns_uri;
            if (!omc_xmp_frame_set_path_from_parent(ctx, &frame, ctx->frame_count,
                                                    parent))
                return 0;
        }
    }
    in_description = 0;
    array = (omc_xmp_frame *)0;
    for (i = 0U; i < ctx->frame_count; ++i) {
        if (ctx->frames[i].kind == OMC_XMP_FRAME_DESC)
            in_description = 1;
        if (ctx->frames[i].kind == OMC_XMP_FRAME_ARRAY)
            array = &ctx->frames[i];
    }
    if (omc_xmp_span_eq_lit(ctx, elem_ns, k_ns_meta)
        && omc_xmp_span_eq_lit(ctx, local, "xmpmeta")) {
        frame.kind = OMC_XMP_FRAME_XMPMETA;
        ctx->saw_xmp_shape = 1;
        ctx->res.status = omc_xmp_emit_root_attrs(ctx, elem_ns, attrs, attr_count);
        if (ctx->res.status != OMC_XMP_OK)
            return 0;
    }
    if (omc_xmp_span_eq_lit(ctx, elem_ns, k_ns_rdf)) {
        if (omc_xmp_span_eq_lit(ctx, local, "RDF")) {
            frame.kind = OMC_XMP_FRAME_RDF;
            ctx->saw_xmp_shape = 1;
        } else if (omc_xmp_span_eq_lit(ctx, local, "Description")) {
            frame.kind = OMC_XMP_FRAME_DESC;
            in_description = 1;
            ctx->saw_xmp_shape = 1;
            if (ctx->opts.decode_description_attributes) {
                ctx->res.status =
                    omc_xmp_emit_description_attrs(ctx, attrs, attr_count);
                if (ctx->res.status != OMC_XMP_OK)
                    return 0;
            }
        } else if (omc_xmp_span_eq_lit(ctx, local, "Seq") ||
                   omc_xmp_span_eq_lit(ctx, local, "Bag") ||
                   omc_xmp_span_eq_lit(ctx, local, "Alt")) {
            frame.kind = OMC_XMP_FRAME_ARRAY;
        } else if (omc_xmp_span_eq_lit(ctx, local, "li")) {
            frame.kind = OMC_XMP_FRAME_LI;
            if (in_description && frame.has_path && array) {
                omc_xmp_span lang;
                if (array->array_index == (omc_u32) ~(omc_u32)0) {
                    ctx->res.status = OMC_XMP_LIMIT;
                    return 0;
                }
                frame.current_index = ++array->array_index;
                if (omc_xmp_span_eq_lit(ctx, array->local, "Alt") &&
                    omc_xmp_attr_find_xml_lang(ctx, attrs, attr_count, &lang)) {
                    if (!omc_xmp_frame_set_path_lang(ctx, &frame, ctx->frame_count,
                                                     parent, lang))
                        return 0;
                } else if (!omc_xmp_frame_set_path_indexed(ctx, &frame,
                                                           ctx->frame_count, parent,
                                                           frame.current_index))
                    return 0;
            }
        }
    } else if (in_description && !omc_xmp_span_eq_lit(ctx, elem_ns, k_ns_xml)) {
        omc_xmp_span value;
        frame.prop_name = local;
        if (frame.has_path) {
            frame.kind = OMC_XMP_FRAME_STRUCT_FIELD;
            if (!omc_xmp_frame_set_path_child(ctx, &frame, ctx->frame_count, parent,
                                              local, elem_ns))
                return 0;
        } else {
            frame.kind = OMC_XMP_FRAME_PROPERTY;
            frame.prop_ns_uri = elem_ns;
            if (!omc_xmp_frame_set_path_from_span(ctx, &frame, ctx->frame_count, local))
                return 0;
        }
        if (omc_xmp_attr_find_resource(ctx, attrs, attr_count, &value)) {
            omc_xmp_trim_value(ctx, value, &value);
            ctx->res.status = omc_xmp_emit_property_from_frame_path(ctx, &frame, value);
            if (ctx->res.status != OMC_XMP_OK)
                return 0;
            frame.emitted_resource = 1;
        }
    }
    if (empty_element) {
        if (in_description && frame.has_path && !frame.emitted_resource &&
            (omc_xmp_frame_has_value_path(frame.kind) ||
             frame.kind == OMC_XMP_FRAME_ARRAY)) {
            ctx->res.status = omc_xmp_emit_property_from_frame_path(
                ctx, &frame, omc_xmp_span_make(pos, 0U));
            if (ctx->res.status != OMC_XMP_OK)
                return 0;
        }
        ctx->ns_count = ns_mark;
        *io_pos = pos;
        return 1;
    }

    if (!omc_xmp_push_frame(ctx, &frame)) {
        return 0;
    }
    *io_pos = pos;
    return 1;
}

static int
omc_xmp_parse_end_tag(omc_xmp_ctx* ctx, omc_u32* io_pos)
{
    omc_u32 pos;
    omc_xmp_span prefix;
    omc_xmp_span local;
    omc_xmp_frame frame;

    pos = *io_pos + 2U;
    if (!omc_xmp_parse_name(ctx, &pos, &prefix, &local)) {
        return 0;
    }
    omc_xmp_skip_space(ctx, &pos);
    if ((omc_size)pos >= ctx->size || ctx->bytes[pos] != (omc_u8)'>') {
        return 0;
    }
    pos += 1U;

    if (ctx->frame_count == 0U) {
        return 0;
    }

    frame = ctx->frames[ctx->frame_count - 1U];
    if (!omc_xmp_spans_equal(ctx, prefix, frame.prefix) ||
        !omc_xmp_spans_equal(ctx, local, frame.local))
        return 0;
    ctx->frame_count -= 1U;
    ctx->ns_count = frame.ns_mark;

    if (frame.has_path && !frame.had_child && !frame.emitted_resource &&
        (omc_xmp_frame_has_value_path(frame.kind) ||
         frame.kind == OMC_XMP_FRAME_ARRAY)) {
        omc_xmp_span raw_text;
        omc_xmp_span value_span;
        omc_xmp_status status;

        if (frame.saw_text) {
            raw_text = omc_xmp_span_make(frame.text_start,
                                         frame.text_end - frame.text_start);
        } else {
            raw_text = omc_xmp_span_make(pos, 0U);
        }
        if (!omc_xmp_validate_span_text(ctx, raw_text)) {
            omc_xmp_mark_malformed(ctx);
            *io_pos = pos;
            return 0;
        }
        omc_xmp_trim_value(ctx, raw_text, &value_span);
        status = omc_xmp_emit_property_from_frame_path(ctx, &frame,
                                                       value_span);
        if (status != OMC_XMP_OK) {
            ctx->res.status = status;
            return 0;
        }
    }

    if (ctx->frame_count == 0U &&
        (frame.kind == OMC_XMP_FRAME_XMPMETA || frame.kind == OMC_XMP_FRAME_RDF))
        ctx->size = pos;
    *io_pos = pos;
    return 1;
}

static int
omc_xmp_skip_processing_instruction(omc_xmp_ctx* ctx, omc_u32* io_pos)
{
    omc_u32 end;

    if (!omc_xmp_find_until(ctx, *io_pos + 2U, "?>", &end)) {
        return 0;
    }
    *io_pos = end + 2U;
    return 1;
}

static int
omc_xmp_skip_comment(omc_xmp_ctx* ctx, omc_u32* io_pos)
{
    omc_u32 end;

    if (!omc_xmp_find_until(ctx, *io_pos + 4U, "-->", &end)) {
        return 0;
    }
    {
        omc_u32 i;
        for (i = *io_pos + 4U; i + 1U < end; ++i) {
            if (ctx->bytes[i] == '-' && ctx->bytes[i + 1U] == '-')
                return 0;
        }
    }
    *io_pos = end + 3U;
    return 1;
}

static int
omc_xmp_skip_cdata(omc_xmp_ctx* ctx, omc_u32* io_pos)
{
    omc_u32 end;
    omc_xmp_frame* top;

    if (!omc_xmp_find_until(ctx, *io_pos + 9U, "]]>", &end)) {
        return 0;
    }
    top = omc_xmp_top_frame(ctx);
    if (!top || !omc_xmp_append_text(ctx, top, *io_pos + 9U, end, 1))
        return 0;
    *io_pos = end + 3U;
    return 1;
}

static int
omc_xmp_run(omc_xmp_ctx* ctx)
{
    omc_u32 pos;

    pos = 0U;
    while ((omc_size)pos < ctx->size && ctx->bytes[pos] != (omc_u8)'<') {
        pos += 1U;
    }
    while (ctx->size > 0U && ctx->bytes[ctx->size - 1U] == 0U) {
        ctx->size -= 1U;
    }

    if ((omc_size)pos >= ctx->size) {
        ctx->res.status = OMC_XMP_UNSUPPORTED;
        return 0;
    }

    if (ctx->opts.limits.max_input_bytes != 0U
        && (omc_u64)(ctx->size - (omc_size)pos) > ctx->opts.limits.max_input_bytes) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }

    if (omc_xmp_contains_literal(ctx, pos, "<!DOCTYPE")
        || omc_xmp_contains_literal(ctx, pos, "<!ENTITY")) {
        omc_xmp_mark_malformed(ctx);
        return 0;
    }

    while ((omc_size)pos < ctx->size) {
        if (ctx->bytes[pos] != (omc_u8)'<') {
            omc_xmp_frame* top;
            omc_u32 start;

            start = pos;
            while ((omc_size)pos < ctx->size && ctx->bytes[pos] != (omc_u8)'<') {
                if (omc_xmp_is_bad_xml_char(ctx->bytes[pos])) {
                    omc_xmp_mark_malformed(ctx);
                    return 0;
                }
                pos += 1U;
            }
            top = omc_xmp_top_frame(ctx);
            if (!top) {
                omc_u32 i;
                for (i = start; i < pos; ++i)
                    if (!omc_xmp_is_space(ctx->bytes[i])) {
                        omc_xmp_mark_malformed(ctx);
                        return 0;
                    }
            }
            if (!omc_xmp_append_text(ctx, top, start, pos, 0)) {
                omc_xmp_mark_malformed(ctx);
                return 0;
            }
            continue;
        }

        if ((omc_size)(pos + 1U) >= ctx->size) {
            omc_xmp_mark_malformed(ctx);
            return 0;
        }

        if (ctx->bytes[pos + 1U] == (omc_u8)'?') {
            if (!omc_xmp_skip_processing_instruction(ctx, &pos)) {
                omc_xmp_mark_malformed(ctx);
                return 0;
            }
        } else if ((omc_size)(pos + 3U) < ctx->size
                   && memcmp(ctx->bytes + pos, "<!--", 4U) == 0) {
            if (!omc_xmp_skip_comment(ctx, &pos)) {
                omc_xmp_mark_malformed(ctx);
                return 0;
            }
        } else if ((omc_size)(pos + 8U) < ctx->size
                   && memcmp(ctx->bytes + pos, "<![CDATA[", 9U) == 0) {
            if (!omc_xmp_skip_cdata(ctx, &pos)) {
                omc_xmp_mark_malformed(ctx);
                return 0;
            }
        } else if (ctx->bytes[pos + 1U] == (omc_u8)'/') {
            if (!omc_xmp_parse_end_tag(ctx, &pos)) {
                omc_xmp_mark_malformed(ctx);
                return 0;
            }
        } else if (ctx->bytes[pos + 1U] == (omc_u8)'!') {
            omc_xmp_mark_malformed(ctx);
            return 0;
        } else {
            if (!omc_xmp_parse_start_tag(ctx, &pos)) {
                if (ctx->res.status == OMC_XMP_OK) {
                    omc_xmp_mark_malformed(ctx);
                }
                return 0;
            }
        }
    }

    if (!ctx->saw_xmp_shape) {
        ctx->res.status = OMC_XMP_UNSUPPORTED;
        return 0;
    }
    if (ctx->frame_count != 0U) {
        omc_xmp_mark_malformed(ctx);
        return 0;
    }
    return 1;
}

static int
omc_xmp_ctx_init(omc_xmp_ctx* ctx, const omc_u8* xmp_bytes, omc_size xmp_size,
                 omc_store* store, omc_block_id source_block,
                 omc_entry_flags flags, const omc_xmp_opts* opts,
                 int measure_only)
{
    omc_xmp_opts local_opts;
    omc_u32 ns_cap;
    omc_u32 path_cap;
    omc_u32 frame_path_cap;

    memset(ctx, 0, sizeof(*ctx));
    ctx->store = store;
    ctx->source_block = source_block;
    ctx->base_flags = flags;
    ctx->measure_only = measure_only;
    ctx->res.status = OMC_XMP_OK;
    ctx->res.entries_decoded = 0U;

    if (opts == (const omc_xmp_opts*)0) {
        omc_xmp_opts_init(&local_opts);
        ctx->opts = local_opts;
    } else {
        ctx->opts = *opts;
        omc_xmp_opts_apply_defaults(&ctx->opts);
    }

    if (ctx->opts.limits.max_depth == 0U) {
        ctx->opts.limits.max_depth = 128U;
    }

    if (xmp_size > ctx->opts.limits.max_input_bytes ||
        xmp_size > (omc_size)((omc_u32) ~(omc_u32)0)) {
        ctx->res.status = OMC_XMP_LIMIT;
        return 0;
    }
    if (xmp_size != 0U) {
        /* Keep parser spans stable even when decoded properties append into
         * store->arena during the same parse. */
        ctx->input_copy = (omc_u8*)malloc(xmp_size);
        if (ctx->input_copy == (omc_u8*)0) {
            omc_xmp_set_res(&ctx->res, OMC_XMP_NOMEM);
            return 0;
        }
        memcpy(ctx->input_copy, xmp_bytes, xmp_size);
        ctx->bytes = ctx->input_copy;
    } else {
        ctx->bytes = xmp_bytes;
    }
    ctx->size = xmp_size;

    ctx->frame_cap = ctx->opts.limits.max_depth;
    if (!omc_xmp_u32_mul_add_fits(ctx->frame_cap, 8U, 32U, &ns_cap)
        || !omc_xmp_u32_mul_add_fits(ctx->opts.limits.max_path_bytes, 1U,
                                     32U, &path_cap)
        || !omc_xmp_u32_mul_add_fits(ctx->frame_cap, path_cap, 0U,
                                     &frame_path_cap)) {
        omc_xmp_set_res(&ctx->res, OMC_XMP_LIMIT);
        omc_xmp_ctx_fini(ctx);
        return 0;
    }
    if (!omc_xmp_alloc_size_fits((omc_size)ctx->frame_cap,
                                 sizeof(*ctx->frames))
        || !omc_xmp_alloc_size_fits((omc_size)ns_cap,
                                    sizeof(*ctx->ns_decls))
        || !omc_xmp_alloc_size_fits((omc_size)frame_path_cap,
                                    sizeof(*ctx->frame_path_buf))) {
        omc_xmp_set_res(&ctx->res, OMC_XMP_LIMIT);
        omc_xmp_ctx_fini(ctx);
        return 0;
    }

    if (!omc_xmp_alloc_size_fits(ctx->opts.limits.max_attributes_per_element,
                                 sizeof(*ctx->attrs))) {
        ctx->res.status = OMC_XMP_LIMIT;
        omc_xmp_ctx_fini(ctx);
        return 0;
    }
    ctx->attrs = (omc_xmp_attr *)malloc(
        (omc_size)ctx->opts.limits.max_attributes_per_element * sizeof(*ctx->attrs));
    ctx->frames = (omc_xmp_frame*)malloc((omc_size)ctx->frame_cap
                                         * sizeof(*ctx->frames));
    ctx->ns_decls = (omc_xmp_ns_decl*)malloc((omc_size)ns_cap
                                             * sizeof(*ctx->ns_decls));
    ctx->frame_path_buf = (char*)malloc((omc_size)frame_path_cap);
    ctx->ns_cap = ns_cap;
    ctx->path_cap = path_cap;

    if (ctx->attrs == (omc_xmp_attr *)0 || ctx->frames == (omc_xmp_frame *)0 ||
        ctx->ns_decls == (omc_xmp_ns_decl *)0 || ctx->frame_path_buf == (char *)0) {
        omc_xmp_set_res(&ctx->res, OMC_XMP_NOMEM);
        omc_xmp_ctx_fini(ctx);
        return 0;
    }
    return 1;
}

static void
omc_xmp_ctx_fini(omc_xmp_ctx* ctx)
{
    if (ctx == (omc_xmp_ctx*)0) {
        return;
    }
    free(ctx->attrs);
    free(ctx->frames);
    free(ctx->ns_decls);
    free(ctx->frame_path_buf);
    free(ctx->input_copy);
}

omc_xmp_res
omc_xmp_dec(const omc_u8* xmp_bytes, omc_size xmp_size, omc_store* store,
            omc_block_id source_block, omc_entry_flags flags,
            const omc_xmp_opts* opts)
{
    omc_xmp_ctx ctx;

    if (xmp_bytes == (const omc_u8*)0 || store == (omc_store*)0) {
        ctx.res.status = OMC_XMP_MALFORMED;
        ctx.res.entries_decoded = 0U;
        return ctx.res;
    }

    if (!omc_xmp_ctx_init(&ctx, xmp_bytes, xmp_size, store, source_block,
                          flags, opts, 0)) {
        return ctx.res;
    }
    (void)omc_xmp_run(&ctx);
    omc_xmp_ctx_fini(&ctx);
    return ctx.res;
}

omc_xmp_res
omc_xmp_meas(const omc_u8* xmp_bytes, omc_size xmp_size,
             const omc_xmp_opts* opts)
{
    omc_store scratch;
    omc_xmp_res res;

    omc_store_init(&scratch);
    res = omc_xmp_dec(xmp_bytes, xmp_size, &scratch, OMC_INVALID_BLOCK_ID,
                      OMC_ENTRY_FLAG_NONE, opts);
    omc_store_fini(&scratch);
    return res;
}
