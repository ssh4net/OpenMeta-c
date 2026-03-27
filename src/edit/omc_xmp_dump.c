#include "omc/omc_xmp_dump.h"

#include <string.h>

typedef struct omc_xmp_dump_writer {
    omc_u8* out;
    omc_size cap;
    omc_u64 needed;
    omc_u64 written;
} omc_xmp_dump_writer;

typedef struct omc_xmp_ns_view {
    const omc_u8* data;
    omc_size size;
} omc_xmp_ns_view;

typedef struct omc_xmp_dump_property {
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    const omc_val* value;
} omc_xmp_dump_property;

static const char k_xmp_ns_xap[] = "http://ns.adobe.com/xap/1.0/";
static const char k_xmp_ns_dc[] = "http://purl.org/dc/elements/1.1/";
static const char k_xmp_ns_ps[] = "http://ns.adobe.com/photoshop/1.0/";
static const char k_xmp_ns_exif[] = "http://ns.adobe.com/exif/1.0/";
static const char k_xmp_ns_tiff[] = "http://ns.adobe.com/tiff/1.0/";
static const char k_xmp_ns_mm[] = "http://ns.adobe.com/xap/1.0/mm/";
static const char k_xmp_ns_iptc4xmp[]
    = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";

static void
omc_xmp_dump_writer_init(omc_xmp_dump_writer* writer, omc_u8* out,
                         omc_size out_cap)
{
    writer->out = out;
    writer->cap = out_cap;
    writer->needed = 0U;
    writer->written = 0U;
}

static void
omc_xmp_dump_write_bytes(omc_xmp_dump_writer* writer, const char* bytes,
                         omc_size size)
{
    omc_size copy;

    if (writer == (omc_xmp_dump_writer*)0 || bytes == (const char*)0) {
        return;
    }

    if (writer->written < writer->cap) {
        copy = writer->cap - (omc_size)writer->written;
        if (copy > size) {
            copy = size;
        }
        if (copy != 0U) {
            memcpy(writer->out + writer->written, bytes, copy);
            writer->written += (omc_u64)copy;
        }
    }

    writer->needed += (omc_u64)size;
}

static void
omc_xmp_dump_write_byte(omc_xmp_dump_writer* writer, char byte)
{
    omc_xmp_dump_write_bytes(writer, &byte, 1U);
}

static int
omc_xmp_dump_is_name_start(omc_u8 c)
{
    return ((c >= (omc_u8)'A' && c <= (omc_u8)'Z')
            || (c >= (omc_u8)'a' && c <= (omc_u8)'z') || c == (omc_u8)'_');
}

static int
omc_xmp_dump_is_name_char(omc_u8 c)
{
    return omc_xmp_dump_is_name_start(c)
           || (c >= (omc_u8)'0' && c <= (omc_u8)'9') || c == (omc_u8)'-'
           || c == (omc_u8)'.';
}

static int
omc_xmp_dump_is_simple_name(omc_xmp_ns_view name)
{
    omc_size i;

    if (name.data == (const omc_u8*)0 || name.size == 0U) {
        return 0;
    }
    if (!omc_xmp_dump_is_name_start(name.data[0])) {
        return 0;
    }

    for (i = 1U; i < name.size; ++i) {
        if (!omc_xmp_dump_is_name_char(name.data[i])) {
            return 0;
        }
    }

    return 1;
}

static int
omc_xmp_dump_ns_equal(omc_xmp_ns_view a, omc_xmp_ns_view b)
{
    if (a.size != b.size) {
        return 0;
    }
    if (a.size == 0U) {
        return 1;
    }
    return memcmp(a.data, b.data, a.size) == 0;
}

static int
omc_xmp_dump_ns_equal_lit(omc_xmp_ns_view view, const char* lit)
{
    omc_size lit_size;

    lit_size = strlen(lit);
    if (view.size != lit_size) {
        return 0;
    }
    return memcmp(view.data, lit, lit_size) == 0;
}

static omc_xmp_ns_view
omc_xmp_dump_view_from_ref(const omc_arena* arena, omc_byte_ref ref)
{
    omc_const_bytes bytes;
    omc_xmp_ns_view view;

    view.data = (const omc_u8*)0;
    view.size = 0U;

    bytes = omc_arena_view(arena, ref);
    if (bytes.data == (const omc_u8*)0) {
        return view;
    }

    view.data = bytes.data;
    view.size = bytes.size;
    return view;
}

static int
omc_xmp_dump_value_supported(const omc_val* value, const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }

    switch (value->kind) {
    case OMC_VAL_SCALAR:
        switch (value->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_I8:
        case OMC_ELEM_U16:
        case OMC_ELEM_I16:
        case OMC_ELEM_U32:
        case OMC_ELEM_I32:
        case OMC_ELEM_U64:
        case OMC_ELEM_I64:
        case OMC_ELEM_URATIONAL:
        case OMC_ELEM_SRATIONAL:
            return 1;
        default: return 0;
        }
    case OMC_VAL_TEXT:
        if (value->text_encoding != OMC_TEXT_ASCII
            && value->text_encoding != OMC_TEXT_UTF8) {
            return 0;
        }
        bytes = omc_arena_view(arena, value->u.ref);
        return bytes.data != (const omc_u8*)0;
    default: return 0;
    }
}

static int
omc_xmp_dump_extract_property(const omc_store* store, omc_size index,
                              const omc_xmp_sidecar_opts* opts,
                              omc_xmp_dump_property* out_prop)
{
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0
        || out_prop == (omc_xmp_dump_property*)0) {
        return 0;
    }
    if (index >= store->entry_count) {
        return 0;
    }
    if (!opts->include_existing_xmp) {
        return 0;
    }
    if (store->entries[index].key.kind != OMC_KEY_XMP_PROPERTY) {
        return 0;
    }

    schema_ns = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.xmp_property.schema_ns);
    property_name = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.xmp_property.property_path);
    if (schema_ns.data == (const omc_u8*)0
        || property_name.data == (const omc_u8*)0) {
        return 0;
    }
    if (!omc_xmp_dump_is_simple_name(property_name)) {
        return 0;
    }
    if (!omc_xmp_dump_value_supported(&store->entries[index].value,
                                      &store->arena)) {
        return 0;
    }

    out_prop->schema_ns = schema_ns;
    out_prop->property_name = property_name;
    out_prop->value = &store->entries[index].value;
    return 1;
}

static int
omc_xmp_dump_known_prefix(omc_xmp_ns_view schema_ns, const char** out_prefix)
{
    if (omc_xmp_dump_ns_equal_lit(schema_ns, k_xmp_ns_xap)) {
        *out_prefix = "xmp";
        return 1;
    }
    if (omc_xmp_dump_ns_equal_lit(schema_ns, k_xmp_ns_dc)) {
        *out_prefix = "dc";
        return 1;
    }
    if (omc_xmp_dump_ns_equal_lit(schema_ns, k_xmp_ns_ps)) {
        *out_prefix = "photoshop";
        return 1;
    }
    if (omc_xmp_dump_ns_equal_lit(schema_ns, k_xmp_ns_exif)) {
        *out_prefix = "exif";
        return 1;
    }
    if (omc_xmp_dump_ns_equal_lit(schema_ns, k_xmp_ns_tiff)) {
        *out_prefix = "tiff";
        return 1;
    }
    if (omc_xmp_dump_ns_equal_lit(schema_ns, k_xmp_ns_mm)) {
        *out_prefix = "xmpMM";
        return 1;
    }
    if (omc_xmp_dump_ns_equal_lit(schema_ns, k_xmp_ns_iptc4xmp)) {
        *out_prefix = "Iptc4xmpCore";
        return 1;
    }

    return 0;
}

static omc_u32
omc_xmp_dump_unknown_prefix_ordinal(const omc_store* store, omc_size index,
                                    const omc_xmp_sidecar_opts* opts,
                                    omc_xmp_ns_view schema_ns)
{
    omc_size i;
    omc_u32 ordinal;
    omc_u32 emitted;
    omc_xmp_dump_property prop;
    omc_size j;
    int seen;
    const char* known_prefix;

    ordinal = 0U;
    emitted = 0U;
    for (i = 0U; i < index; ++i) {
        if (!omc_xmp_dump_extract_property(store, i, opts, &prop)) {
            continue;
        }
        emitted += 1U;
        if (opts->limits.max_entries != 0U
            && emitted > opts->limits.max_entries) {
            break;
        }
        if (omc_xmp_dump_known_prefix(prop.schema_ns, &known_prefix)) {
            continue;
        }

        seen = 0;
        for (j = 0U; j < i; ++j) {
            omc_xmp_dump_property prior;
            if (!omc_xmp_dump_extract_property(store, j, opts, &prior)) {
                continue;
            }
            if (omc_xmp_dump_ns_equal(prior.schema_ns, prop.schema_ns)) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            ordinal += 1U;
        }
    }

    if (ordinal == 0U) {
        ordinal = 1U;
    }
    if (!omc_xmp_dump_known_prefix(schema_ns, &known_prefix)) {
        return ordinal;
    }
    return 0U;
}

static int
omc_xmp_dump_schema_first_for_emitted(const omc_store* store, omc_size index,
                                      const omc_xmp_sidecar_opts* opts,
                                      omc_xmp_ns_view schema_ns)
{
    omc_size i;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    for (i = 0U; i < index; ++i) {
        if (!omc_xmp_dump_extract_property(store, i, opts, &prop)) {
            continue;
        }
        emitted += 1U;
        if (opts->limits.max_entries != 0U
            && emitted > opts->limits.max_entries) {
            break;
        }
        if (omc_xmp_dump_ns_equal(prop.schema_ns, schema_ns)) {
            return 0;
        }
    }

    return 1;
}

static void
omc_xmp_dump_write_u32_decimal(omc_xmp_dump_writer* writer, omc_u32 value)
{
    char buf[16];
    omc_u32 pos;

    pos = 0U;
    do {
        buf[pos++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (pos != 0U) {
        pos -= 1U;
        omc_xmp_dump_write_byte(writer, buf[pos]);
    }
}

static void
omc_xmp_dump_write_u64_decimal(omc_xmp_dump_writer* writer, omc_u64 value)
{
    char buf[32];
    omc_u32 pos;

    pos = 0U;
    do {
        buf[pos++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (pos != 0U) {
        pos -= 1U;
        omc_xmp_dump_write_byte(writer, buf[pos]);
    }
}

static void
omc_xmp_dump_write_i64_decimal(omc_xmp_dump_writer* writer, omc_s64 value)
{
    omc_u64 magnitude;

    if (value < 0) {
        omc_xmp_dump_write_byte(writer, '-');
        magnitude = (omc_u64)(-(value + 1)) + 1U;
    } else {
        magnitude = (omc_u64)value;
    }

    omc_xmp_dump_write_u64_decimal(writer, magnitude);
}

static void
omc_xmp_dump_write_hex_byte(omc_xmp_dump_writer* writer, omc_u8 value)
{
    static const char k_hex[] = "0123456789ABCDEF";

    omc_xmp_dump_write_byte(writer, k_hex[(value >> 4) & 0x0FU]);
    omc_xmp_dump_write_byte(writer, k_hex[value & 0x0FU]);
}

static void
omc_xmp_dump_write_xml_escaped(omc_xmp_dump_writer* writer,
                               const omc_u8* bytes, omc_size size,
                               int for_attribute)
{
    omc_size i;
    omc_u8 c;

    for (i = 0U; i < size; ++i) {
        c = bytes[i];
        switch (c) {
        case (omc_u8)'&':
            omc_xmp_dump_write_bytes(writer, "&amp;", 5U);
            break;
        case (omc_u8)'<':
            omc_xmp_dump_write_bytes(writer, "&lt;", 4U);
            break;
        case (omc_u8)'>':
            omc_xmp_dump_write_bytes(writer, "&gt;", 4U);
            break;
        case (omc_u8)'"':
            if (for_attribute != 0) {
                omc_xmp_dump_write_bytes(writer, "&quot;", 6U);
            } else {
                omc_xmp_dump_write_byte(writer, '"');
            }
            break;
        case (omc_u8)'\'':
            if (for_attribute != 0) {
                omc_xmp_dump_write_bytes(writer, "&apos;", 6U);
            } else {
                omc_xmp_dump_write_byte(writer, '\'');
            }
            break;
        default:
            if (c < 0x20U && c != (omc_u8)'\t' && c != (omc_u8)'\r'
                && c != (omc_u8)'\n') {
                omc_xmp_dump_write_bytes(writer, "\\x", 2U);
                omc_xmp_dump_write_hex_byte(writer, c);
            } else {
                omc_xmp_dump_write_byte(writer, (char)c);
            }
            break;
        }
    }
}

static void
omc_xmp_dump_write_prefix(omc_xmp_dump_writer* writer, const omc_store* store,
                          omc_size index, const omc_xmp_sidecar_opts* opts,
                          omc_xmp_ns_view schema_ns)
{
    const char* known_prefix;
    omc_u32 ordinal;

    if (omc_xmp_dump_known_prefix(schema_ns, &known_prefix)) {
        omc_xmp_dump_write_bytes(writer, known_prefix, strlen(known_prefix));
        return;
    }

    omc_xmp_dump_write_bytes(writer, "ns", 2U);
    ordinal = omc_xmp_dump_unknown_prefix_ordinal(store, index, opts, schema_ns);
    omc_xmp_dump_write_u32_decimal(writer, ordinal);
}

static void
omc_xmp_dump_write_value(omc_xmp_dump_writer* writer, const omc_val* value,
                         const omc_arena* arena)
{
    omc_const_bytes bytes;

    switch (value->kind) {
    case OMC_VAL_SCALAR:
        switch (value->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_U16:
        case OMC_ELEM_U32:
        case OMC_ELEM_U64:
            omc_xmp_dump_write_u64_decimal(writer, value->u.u64);
            return;
        case OMC_ELEM_I8:
        case OMC_ELEM_I16:
        case OMC_ELEM_I32:
        case OMC_ELEM_I64:
            omc_xmp_dump_write_i64_decimal(writer, value->u.i64);
            return;
        case OMC_ELEM_URATIONAL:
            omc_xmp_dump_write_u32_decimal(writer, value->u.ur.numer);
            omc_xmp_dump_write_byte(writer, '/');
            omc_xmp_dump_write_u32_decimal(writer, value->u.ur.denom);
            return;
        case OMC_ELEM_SRATIONAL:
            omc_xmp_dump_write_i64_decimal(writer, value->u.sr.numer);
            omc_xmp_dump_write_byte(writer, '/');
            omc_xmp_dump_write_i64_decimal(writer, value->u.sr.denom);
            return;
        default: return;
        }
    case OMC_VAL_TEXT:
        bytes = omc_arena_view(arena, value->u.ref);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        return;
    default: return;
    }
}

static void
omc_xmp_dump_emit_namespace_decls(omc_xmp_dump_writer* writer,
                                  const omc_store* store,
                                  const omc_xmp_sidecar_opts* opts,
                                  int* out_limit_hit)
{
    omc_size i;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        if (!omc_xmp_dump_extract_property(store, i, opts, &prop)) {
            continue;
        }
        if (opts->limits.max_entries != 0U
            && emitted >= opts->limits.max_entries) {
            *out_limit_hit = 1;
            continue;
        }
        if (!omc_xmp_dump_schema_first_for_emitted(store, i, opts,
                                                   prop.schema_ns)) {
            emitted += 1U;
            continue;
        }

        omc_xmp_dump_write_bytes(writer, " xmlns:", 7U);
        omc_xmp_dump_write_prefix(writer, store, i, opts, prop.schema_ns);
        omc_xmp_dump_write_bytes(writer, "=\"", 2U);
        omc_xmp_dump_write_xml_escaped(writer, prop.schema_ns.data,
                                       prop.schema_ns.size, 1);
        omc_xmp_dump_write_byte(writer, '"');
        emitted += 1U;
    }
}

static omc_u32
omc_xmp_dump_emit_properties(omc_xmp_dump_writer* writer, const omc_store* store,
                             const omc_xmp_sidecar_opts* opts,
                             int* out_limit_hit)
{
    omc_size i;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        if (!omc_xmp_dump_extract_property(store, i, opts, &prop)) {
            continue;
        }
        if (opts->limits.max_entries != 0U
            && emitted >= opts->limits.max_entries) {
            *out_limit_hit = 1;
            continue;
        }

        omc_xmp_dump_write_byte(writer, '<');
        omc_xmp_dump_write_prefix(writer, store, i, opts, prop.schema_ns);
        omc_xmp_dump_write_byte(writer, ':');
        omc_xmp_dump_write_xml_escaped(writer, prop.property_name.data,
                                       prop.property_name.size, 0);
        omc_xmp_dump_write_byte(writer, '>');
        omc_xmp_dump_write_value(writer, prop.value, &store->arena);
        omc_xmp_dump_write_bytes(writer, "</", 2U);
        omc_xmp_dump_write_prefix(writer, store, i, opts, prop.schema_ns);
        omc_xmp_dump_write_byte(writer, ':');
        omc_xmp_dump_write_xml_escaped(writer, prop.property_name.data,
                                       prop.property_name.size, 0);
        omc_xmp_dump_write_byte(writer, '>');
        emitted += 1U;
    }

    return emitted;
}

void
omc_xmp_sidecar_opts_init(omc_xmp_sidecar_opts* opts)
{
    if (opts == (omc_xmp_sidecar_opts*)0) {
        return;
    }

    opts->limits.max_output_bytes = 0U;
    opts->limits.max_entries = 0U;
    opts->include_existing_xmp = 1;
}

omc_status
omc_xmp_dump_sidecar(const omc_store* store, omc_u8* out, omc_size out_cap,
                     const omc_xmp_sidecar_opts* opts,
                     omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_opts local_opts;
    omc_xmp_dump_writer writer;
    int limit_hit;
    static const char k_packet_open[]
        = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
          "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
          "x:xmptk=\"OpenMeta-c\"><rdf:RDF><rdf:Description";
    static const char k_packet_close[]
        = "</rdf:Description></rdf:RDF></x:xmpmeta>";

    if (store == (const omc_store*)0 || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (out == (omc_u8*)0 && out_cap != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_sidecar_opts*)0) {
        omc_xmp_sidecar_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_dump_writer_init(&writer, out, out_cap);
    limit_hit = 0;

    omc_xmp_dump_write_bytes(&writer, k_packet_open,
                             sizeof(k_packet_open) - 1U);
    omc_xmp_dump_emit_namespace_decls(&writer, store, opts, &limit_hit);
    omc_xmp_dump_write_byte(&writer, '>');
    out_res->entries
        = omc_xmp_dump_emit_properties(&writer, store, opts, &limit_hit);
    omc_xmp_dump_write_bytes(&writer, k_packet_close,
                             sizeof(k_packet_close) - 1U);

    out_res->needed = writer.needed;
    out_res->written = writer.written;

    if (opts->limits.max_output_bytes != 0U
        && writer.needed > opts->limits.max_output_bytes) {
        limit_hit = 1;
    }

    if (limit_hit != 0) {
        out_res->status = OMC_XMP_DUMP_LIMIT;
    } else if (writer.needed > out_cap) {
        out_res->status = OMC_XMP_DUMP_TRUNCATED;
    } else {
        out_res->status = OMC_XMP_DUMP_OK;
    }

    return OMC_STATUS_OK;
}
