#include "omc/omc_translation.h"
#include "../core/omc_value_internal.h"

#include <string.h>

#define OMC_EXIF_TEXT_FIELD_COUNT 4U
#define OMC_EXIF_TEXT_DEFAULT_SOURCE_LIMIT 1024U
#define OMC_EXIF_TEXT_DEFAULT_ADDITION_LIMIT 13U
#define OMC_EXIF_TEXT_DEFAULT_OPERATION_LIMIT 4096U
#define OMC_EXIF_TEXT_DEFAULT_PROPERTY_LIMIT 65536U
#define OMC_EXIF_TEXT_DEFAULT_TOTAL_LIMIT ((omc_u64)1024U * 1024U)

typedef struct omc_exif_text_field {
    omc_u32 mapping;
    omc_u16 tag;
    const char *schema;
    const char *path;
} omc_exif_text_field;

typedef struct omc_exif_text_plan {
    omc_entry_id source;
    omc_const_bytes text;
    omc_u32 native_count;
    omc_u32 version;
    int present;
    int deleted;
    int apply;
    int ascii;
} omc_exif_text_plan;

static omc_status
omc_exif_text_append(omc_edit *edit, const omc_u8 *data, omc_size size,
                     omc_byte_ref *ref);

static int
omc_exif_text_utf16_equal(omc_const_bytes bytes, omc_const_bytes utf8);

static const omc_exif_text_field omc_exif_text_fields[OMC_EXIF_TEXT_FIELD_COUNT] = {
    { OMC_EXIF_TEXT_TRANSLATE_EXIF_VERSION, 0x9000U,
      "http://ns.adobe.com/exif/1.0/", "ExifVersion" },
    { OMC_EXIF_TEXT_TRANSLATE_FLASHPIX_VERSION, 0xA000U,
      "http://ns.adobe.com/exif/1.0/", "FlashpixVersion" },
    { OMC_EXIF_TEXT_TRANSLATE_USER_COMMENT, 0x9286U,
      "http://ns.adobe.com/exif/1.0/", "UserComment" },
    { OMC_EXIF_TEXT_TRANSLATE_IMAGE_TITLE, 0xA436U,
      "http://cipa.jp/exif/1.0/", "ImageTitle" }
};

static omc_exif_text_translation_res
omc_exif_text_error(omc_exif_text_translation_status status, omc_u32 mapping,
                     omc_entry_id source)
{
    omc_exif_text_translation_res result;
    memset(&result, 0, sizeof(result));
    result.status = status;
    result.failed_mapping = mapping;
    result.failed_source = source;
    return result;
}

static int
omc_exif_text_key_matches(const omc_store *store, const omc_entry *entry,
                          const omc_exif_text_field *field, int *shape)
{
    omc_const_bytes schema;
    omc_const_bytes path;
    omc_size prefix_size;

    *shape = 0;
    if (entry->key.kind != OMC_KEY_XMP_PROPERTY) {
        return 0;
    }
    schema = omc_arena_view(&store->arena, entry->key.u.xmp_property.schema_ns);
    path = omc_arena_view(&store->arena, entry->key.u.xmp_property.property_path);
    if (schema.size != strlen(field->schema)
        || memcmp(schema.data, field->schema, schema.size) != 0) {
        return 0;
    }
    prefix_size = strlen(field->path);
    if (path.size == prefix_size
        && memcmp(path.data, field->path, prefix_size) == 0) {
        *shape = 1;
        return 1;
    }
    if (field->mapping == OMC_EXIF_TEXT_TRANSLATE_USER_COMMENT
        && path.size == prefix_size + strlen("[@xml:lang=x-default]")
        && memcmp(path.data, field->path, prefix_size) == 0
        && memcmp(path.data + prefix_size, "[@xml:lang=x-default]",
                  strlen("[@xml:lang=x-default]")) == 0) {
        *shape = 1;
        return 1;
    }
    if (path.size > prefix_size
        && memcmp(path.data, field->path, prefix_size) == 0) {
        /* Other language alternatives are valid XMP but are not reverse
         * sources. Any other suffix is an unsupported source shape. */
        if (field->mapping == OMC_EXIF_TEXT_TRANSLATE_USER_COMMENT
            && path.size > prefix_size + strlen("[@xml:lang=")
            && memcmp(path.data + prefix_size, "[@xml:lang=",
                      strlen("[@xml:lang=")) == 0) {
            return 0;
        }
        *shape = -1;
        return 1;
    }
    return 0;
}

static int
omc_exif_text_utf8(const omc_u8 *data, omc_size size)
{
    omc_size i;
    for (i = 0U; i < size;) {
        omc_u8 c = data[i];
        omc_u32 need;
        omc_u32 length;
        omc_u32 code;
        if (c == 0U || (c < 0x20U && c != '\t' && c != '\n' && c != '\r')) {
            return 0;
        }
        if (c < 0x80U) {
            ++i;
            continue;
        }
        if (c >= 0xC2U && c <= 0xDFU) {
            need = 1U;
            code = (omc_u32)(c & 0x1FU);
        } else if (c >= 0xE0U && c <= 0xEFU) {
            need = 2U;
            code = (omc_u32)(c & 0x0FU);
        } else if (c >= 0xF0U && c <= 0xF4U) {
            need = 3U;
            code = (omc_u32)(c & 0x07U);
        } else {
            return 0;
        }
        length = need + 1U;
        if (length > size - i) {
            return 0;
        }
        while (need != 0U) {
            omc_u8 tail = data[i + (omc_size)(length - need)];
            if ((tail & 0xC0U) != 0x80U) {
                return 0;
            }
            code = (code << 6U) | (omc_u32)(tail & 0x3FU);
            --need;
        }
        if (code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) {
            return 0;
        }
        if ((length == 2U && code < 0x80U)
            || (length == 3U && code < 0x800U)
            || (length == 4U && code < 0x10000U)) {
            return 0;
        }
        i += (omc_size)length;
    }
    return 1;
}

static int
omc_exif_text_digits(omc_const_bytes bytes, omc_u32 *value)
{
    omc_size i;
    omc_u32 parsed = 0U;
    if (bytes.size != 4U) {
        return 0;
    }
    for (i = 0U; i < bytes.size; ++i) {
        if (bytes.data[i] < '0' || bytes.data[i] > '9') {
            return 0;
        }
        parsed = parsed * 10U + (omc_u32)(bytes.data[i] - '0');
    }
    *value = parsed;
    return 1;
}

static int
omc_exif_text_next_utf8(omc_const_bytes text, omc_size *offset,
                        omc_u32 *code)
{
    omc_u8 first;
    omc_u32 need;
    omc_u32 value;
    omc_size i;
    if (*offset >= text.size) {
        return 0;
    }
    first = text.data[*offset];
    if (first < 0x80U) {
        *code = first;
        *offset += 1U;
        return 1;
    }
    if (first <= 0xDFU) {
        need = 1U;
        value = first & 0x1FU;
    } else if (first <= 0xEFU) {
        need = 2U;
        value = first & 0x0FU;
    } else {
        need = 3U;
        value = first & 0x07U;
    }
    if (need > text.size - *offset - 1U) {
        return 0;
    }
    for (i = 1U; i <= need; ++i) {
        omc_u8 tail = text.data[*offset + i];
        if ((tail & 0xC0U) != 0x80U) {
            return 0;
        }
        value = (value << 6U) | (omc_u32)(tail & 0x3FU);
    }
    *offset += need + 1U;
    *code = value;
    return 1;
}

static omc_status
omc_exif_text_user_value(omc_edit *edit, const omc_exif_text_plan *plan,
                         omc_u32 version, omc_byte_ref *out_ref)
{
    const char *prefix;
    omc_byte_ref prefix_ref;
    omc_byte_ref part_ref;
    omc_u8 bom[2];
    omc_u8 unit[2];
    omc_size offset;
    omc_u32 code;
    omc_u32 encoded;
    omc_status status;
    prefix = plan->ascii ? "ASCII\0\0\0" : "UNICODE\0";
    status = omc_exif_text_append(edit, (const omc_u8 *)prefix, 8U,
                                  &prefix_ref);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (plan->ascii || version >= 300U) {
        status = omc_exif_text_append(edit, plan->text.data, plan->text.size,
                                      &part_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        out_ref->offset = prefix_ref.offset;
        out_ref->size = prefix_ref.size + part_ref.size;
        return OMC_STATUS_OK;
    }
    bom[0] = 0xFFU;
    bom[1] = 0xFEU;
    status = omc_exif_text_append(edit, bom, 2U, &part_ref);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    offset = 0U;
    while (offset < plan->text.size) {
        if (!omc_exif_text_next_utf8(plan->text, &offset, &code)) {
            return OMC_STATUS_STATE;
        }
        if (code >= 0x10000U) {
            code -= 0x10000U;
            encoded = 0xD800U + (code >> 10U);
            unit[0] = (omc_u8)encoded;
            unit[1] = (omc_u8)(encoded >> 8U);
            status = omc_exif_text_append(edit, unit, 2U, &part_ref);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            encoded = 0xDC00U + (code & 0x3FFU);
        } else {
            encoded = code;
        }
        unit[0] = (omc_u8)encoded;
        unit[1] = (omc_u8)(encoded >> 8U);
        status = omc_exif_text_append(edit, unit, 2U, &part_ref);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }
    out_ref->offset = prefix_ref.offset;
    out_ref->size = (omc_u32)(edit->arena.size - (omc_size)prefix_ref.offset);
    return OMC_STATUS_OK;
}

static int
omc_exif_text_native_key(const omc_store *store, const omc_entry *entry,
                         omc_u16 tag)
{
    omc_const_bytes ifd;
    if (entry->key.kind != OMC_KEY_EXIF_TAG
        || entry->key.u.exif_tag.tag != tag
        || (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
        return 0;
    }
    ifd = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
    return ifd.size == strlen("exififd")
           && memcmp(ifd.data, "exififd", ifd.size) == 0;
}

static omc_const_bytes
omc_exif_text_native_bytes(const omc_store *store, const omc_entry *entry)
{
    if (entry->value.kind != OMC_VAL_BYTES
        && entry->value.kind != OMC_VAL_TEXT) {
        omc_const_bytes empty;
        empty.data = (const omc_u8 *)0;
        empty.size = 0U;
        return empty;
    }
    return omc_arena_view(&store->arena, entry->value.u.ref);
}

static int
omc_exif_text_native_value_equal(const omc_store *store,
                                 const omc_exif_text_plan *plan,
                                 omc_u16 tag)
{
    omc_size i;
    omc_u32 seen = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        omc_const_bytes bytes;
        omc_const_bytes utf16;
        if (!omc_exif_text_native_key(store, entry, tag)) {
            continue;
        }
        ++seen;
        if (seen != 1U) {
            return 0;
        }
        if (entry->origin.wire_type.family == OMC_WIRE_TIFF) {
            if (tag < 0xA436U && entry->origin.wire_type.code != 7U) {
                return 0;
            }
            if (tag == 0xA436U
                && entry->origin.wire_type.code
                       != (plan->ascii ? 2U : 129U)) {
                return 0;
            }
        }
        bytes = omc_exif_text_native_bytes(store, entry);
        if (tag == 0x9286U) {
            const char *prefix;
            if (bytes.size < 8U) {
                return 0;
            }
            prefix = plan->ascii ? "ASCII\0\0\0" : "UNICODE\0";
            if (memcmp(bytes.data, prefix, 8U) != 0) {
                return 0;
            }
            if (plan->ascii) {
                if (bytes.size - 8U != plan->text.size
                    || memcmp(bytes.data + 8U, plan->text.data,
                              plan->text.size) != 0) {
                    return 0;
                }
            } else if (bytes.size - 8U == plan->text.size
                       && memcmp(bytes.data + 8U, plan->text.data,
                                 plan->text.size) == 0) {
                /* EXIF 3 UNICODE stores UTF-8 after the marker. */
            } else if (bytes.size >= 10U
                       && bytes.data[8] == 0xFFU && bytes.data[9] == 0xFEU
                       ) {
                utf16.data = bytes.data + 10U;
                utf16.size = bytes.size - 10U;
                if (!omc_exif_text_utf16_equal(utf16, plan->text)) {
                    return 0;
                }
            } else if (bytes.size < 10U
                       || bytes.data[8] != 0xFFU
                       || bytes.data[9] != 0xFEU) {
                return 0;
            }
        } else if (bytes.size != plan->text.size
                   || memcmp(bytes.data, plan->text.data, bytes.size) != 0) {
            return 0;
        }
    }
    return seen == 1U;
}

static int
omc_exif_text_native_version(const omc_store *store, omc_u32 *version)
{
    omc_size i;
    omc_u32 seen = 0U;
    omc_const_bytes bytes;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        if (!omc_exif_text_native_key(store, entry, 0x9000U)) {
            continue;
        }
        ++seen;
        if (seen != 1U) {
            return 0;
        }
        bytes = omc_exif_text_native_bytes(store, entry);
        if (!omc_exif_text_digits(bytes, version)) {
            return 0;
        }
    }
    return seen == 1U;
}

static int
omc_exif_text_utf16_equal(omc_const_bytes bytes, omc_const_bytes utf8)
{
    omc_size a = 0U;
    omc_size b = 0U;
    while (a < bytes.size) {
        omc_u32 code;
        omc_u32 first;
        if (a + 1U >= bytes.size) {
            return 0;
        }
        first = (omc_u32)bytes.data[a] | ((omc_u32)bytes.data[a + 1U] << 8U);
        a += 2U;
        if (first >= 0xD800U && first <= 0xDBFFU) {
            omc_u32 second;
            if (a + 1U >= bytes.size) {
                return 0;
            }
            second = (omc_u32)bytes.data[a]
                     | ((omc_u32)bytes.data[a + 1U] << 8U);
            if (second < 0xDC00U || second > 0xDFFFU) {
                return 0;
            }
            a += 2U;
            code = 0x10000U + ((first - 0xD800U) << 10U)
                   + (second - 0xDC00U);
        } else if (first >= 0xDC00U && first <= 0xDFFFU) {
            return 0;
        } else {
            code = first;
        }
        {
            omc_size old = b;
            omc_u32 actual;
            if (!omc_exif_text_next_utf8(utf8, &b, &actual) || actual != code) {
                return 0;
            }
            if (b == old) {
                return 0;
            }
        }
    }
    return b == utf8.size;
}

static void
omc_exif_text_count_native(const omc_store *store,
                           omc_exif_text_plan *plans)
{
    omc_size i;
    omc_size field;
    for (i = 0U; i < store->entry_count; ++i) {
        for (field = 0U; field < OMC_EXIF_TEXT_FIELD_COUNT; ++field) {
            if (omc_exif_text_native_key(store, &store->entries[i],
                                         omc_exif_text_fields[field].tag)) {
                ++plans[field].native_count;
            }
        }
    }
}

static omc_status
omc_exif_text_append(omc_edit *edit, const omc_u8 *data, omc_size size,
                     omc_byte_ref *ref)
{
    return omc_arena_append(&edit->arena, data, size, ref);
}

void
omc_exif_text_translation_opts_init(omc_exif_text_translation_opts *opts)
{
    if (opts == (omc_exif_text_translation_opts *)0) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->mappings = OMC_EXIF_TEXT_TRANSLATE_ALL;
    opts->all_sources = 0;
    opts->conflict = OMC_TRANSLATION_FAIL;
    opts->max_source_properties = OMC_EXIF_TEXT_DEFAULT_SOURCE_LIMIT;
    opts->max_added_entries = OMC_EXIF_TEXT_DEFAULT_ADDITION_LIMIT;
    opts->max_operations = OMC_EXIF_TEXT_DEFAULT_OPERATION_LIMIT;
    opts->max_text_bytes_per_property = OMC_EXIF_TEXT_DEFAULT_PROPERTY_LIMIT;
    opts->max_total_text_bytes = OMC_EXIF_TEXT_DEFAULT_TOTAL_LIMIT;
}

omc_exif_text_translation_res
omc_translate_xmp_exif_text(const omc_store *source, omc_store *out,
                            const omc_exif_text_translation_opts *opts_in)
{
    omc_exif_text_translation_opts opts;
    omc_exif_text_plan plans[OMC_EXIF_TEXT_FIELD_COUNT];
    omc_exif_text_translation_res result;
    omc_u64 total_text = 0U;
    omc_u32 effective_version = 0U;
    omc_u32 old_version = 0U;
    int old_version_valid;
    omc_size i;
    omc_size field;
    omc_edit edit;
    omc_status status;
    omc_u32 additions = 0U;
    omc_u32 operations = 0U;

    memset(&result, 0, sizeof(result));
    result.failed_source = OMC_INVALID_ENTRY_ID;
    if (opts_in == (const omc_exif_text_translation_opts *)0) {
        omc_exif_text_translation_opts_init(&opts);
    } else {
        opts = *opts_in;
    }
    if (source == (const omc_store *)0 || out == (omc_store *)0
        || source == out || !omc_store_shape_valid(source)
        || !omc_store_shape_valid(out)
        || (opts.mappings & ~OMC_EXIF_TEXT_TRANSLATE_ALL) != 0U
        || opts.mappings == 0U
        || opts.conflict < OMC_TRANSLATION_PRESERVE
        || opts.conflict > OMC_TRANSLATION_REPLACE
        || opts.max_source_properties == 0U
        || opts.max_added_entries == 0U || opts.max_operations == 0U
        || opts.max_text_bytes_per_property == 0U
        || opts.max_total_text_bytes == 0U) {
        return omc_exif_text_error(OMC_EXIF_TEXT_TRANSLATION_INVALID_OPTIONS,
                                   0U, OMC_INVALID_ENTRY_ID);
    }
    memset(plans, 0, sizeof(plans));
    for (field = 0U; field < OMC_EXIF_TEXT_FIELD_COUNT; ++field) {
        plans[field].source = OMC_INVALID_ENTRY_ID;
    }

    for (i = 0U; i < source->entry_count; ++i) {
        const omc_entry *entry = &source->entries[i];
        int shape;
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        for (field = 0U; field < OMC_EXIF_TEXT_FIELD_COUNT; ++field) {
            omc_const_bytes text;
            omc_u32 parsed;
            if ((opts.mappings & omc_exif_text_fields[field].mapping) == 0U
                || !omc_exif_text_key_matches(source, entry,
                                              &omc_exif_text_fields[field],
                                              &shape)) {
                continue;
            }
            if (shape < 0) {
                return omc_exif_text_error(
                    OMC_EXIF_TEXT_TRANSLATION_UNSUPPORTED_SOURCE_SHAPE,
                    omc_exif_text_fields[field].mapping,
                    (omc_entry_id)i);
            }
            if ((entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U
                && !opts.all_sources) {
                continue;
            }
            if (plans[field].present) {
                return omc_exif_text_error(
                    OMC_EXIF_TEXT_TRANSLATION_AMBIGUOUS_SOURCE,
                    omc_exif_text_fields[field].mapping,
                    (omc_entry_id)i);
            }
            plans[field].present = 1;
            plans[field].source = (omc_entry_id)i;
            plans[field].deleted =
                (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U;
            ++result.source_properties;
            if (result.source_properties > opts.max_source_properties) {
                return omc_exif_text_error(
                    OMC_EXIF_TEXT_TRANSLATION_SOURCE_LIMIT,
                    omc_exif_text_fields[field].mapping,
                    (omc_entry_id)i);
            }
            if (plans[field].deleted) {
                continue;
            }
            if (entry->value.kind != OMC_VAL_TEXT
                || !omc_value_shape_valid(&entry->value, &source->arena)) {
                return omc_exif_text_error(
                    OMC_EXIF_TEXT_TRANSLATION_INVALID_SOURCE,
                    omc_exif_text_fields[field].mapping,
                    (omc_entry_id)i);
            }
            text = omc_arena_view(&source->arena, entry->value.u.ref);
            if (text.size > opts.max_text_bytes_per_property
                || text.size > opts.max_total_text_bytes
                || total_text > opts.max_total_text_bytes - text.size) {
                return omc_exif_text_error(
                    text.size > opts.max_text_bytes_per_property
                        ? OMC_EXIF_TEXT_TRANSLATION_VALUE_TOO_LONG
                        : OMC_EXIF_TEXT_TRANSLATION_SOURCE_LIMIT,
                    omc_exif_text_fields[field].mapping,
                    (omc_entry_id)i);
            }
            if (!omc_exif_text_utf8(text.data, text.size)) {
                return omc_exif_text_error(
                    OMC_EXIF_TEXT_TRANSLATION_INVALID_SOURCE,
                    omc_exif_text_fields[field].mapping,
                    (omc_entry_id)i);
            }
            plans[field].text = text;
            plans[field].ascii = 1;
            for (parsed = 0U; parsed < text.size; ++parsed) {
                if (text.data[parsed] >= 0x80U) {
                    plans[field].ascii = 0;
                    break;
                }
            }
            total_text += text.size;
            if (field < 2U) {
                if (!omc_exif_text_digits(text, &plans[field].version)
                    || (field == 1U && plans[field].version != 100U)) {
                    return omc_exif_text_error(
                        OMC_EXIF_TEXT_TRANSLATION_INVALID_SOURCE,
                        omc_exif_text_fields[field].mapping,
                        (omc_entry_id)i);
                }
            }
        }
    }

    omc_exif_text_count_native(source, plans);
    old_version_valid = omc_exif_text_native_version(source, &old_version);
    effective_version = old_version_valid ? old_version : 0U;
    if (plans[0].present && !plans[0].deleted) {
        effective_version = plans[0].version;
    } else if (plans[0].present && plans[0].deleted) {
        effective_version = 0U;
    }
    for (field = 0U; field < OMC_EXIF_TEXT_FIELD_COUNT; ++field) {
        omc_exif_text_plan *plan = &plans[field];
        if (!plan->present) {
            continue;
        }
        result.failed_mapping = omc_exif_text_fields[field].mapping;
        result.failed_source = plan->source;
        if (opts.conflict == OMC_TRANSLATION_PRESERVE
            && plan->native_count != 0U) {
            ++result.groups_preserved;
        } else if (!plan->deleted && plan->native_count == 1U
                   && omc_exif_text_native_value_equal(
                       source, plan, omc_exif_text_fields[field].tag)) {
            ++result.groups_unchanged;
        } else if (opts.conflict == OMC_TRANSLATION_FAIL
                   && plan->native_count != 0U) {
            return omc_exif_text_error(
                OMC_EXIF_TEXT_TRANSLATION_NATIVE_CONFLICT,
                omc_exif_text_fields[field].mapping, plan->source);
        } else {
            plan->apply = 1;
        }
        if (!plan->apply || plan->deleted) {
            continue;
        }
        if (field == 3U && effective_version < 300U) {
            return omc_exif_text_error(
                OMC_EXIF_TEXT_TRANSLATION_UNSUPPORTED_VERSION,
                omc_exif_text_fields[field].mapping, plan->source);
        }
        if (field == 2U && effective_version == 0U) {
            return omc_exif_text_error(
                OMC_EXIF_TEXT_TRANSLATION_INCOMPLETE_SOURCE,
                omc_exif_text_fields[field].mapping, plan->source);
        }
    }
    if (plans[0].apply && old_version_valid
        && ((old_version >= 300U) != (effective_version >= 300U))) {
        if (plans[2].native_count != 0U && !plans[2].present) {
            return omc_exif_text_error(
                OMC_EXIF_TEXT_TRANSLATION_NATIVE_CONFLICT,
                OMC_EXIF_TEXT_TRANSLATE_EXIF_VERSION,
                plans[0].source);
        }
    }
    for (field = 0U; field < OMC_EXIF_TEXT_FIELD_COUNT; ++field) {
        if (!plans[field].apply) {
            continue;
        }
        operations += plans[field].native_count != 0U
                          ? plans[field].native_count
                          : (plans[field].deleted ? 0U : 1U);
        if (!plans[field].deleted && plans[field].native_count == 0U) {
            ++additions;
        }
    }
    if (additions > opts.max_added_entries) {
        return omc_exif_text_error(OMC_EXIF_TEXT_TRANSLATION_ENTRY_LIMIT,
                                   0U, OMC_INVALID_ENTRY_ID);
    }
    if (operations > opts.max_operations) {
        return omc_exif_text_error(OMC_EXIF_TEXT_TRANSLATION_OPERATION_LIMIT,
                                   0U, OMC_INVALID_ENTRY_ID);
    }

    omc_edit_init(&edit);
    status = omc_edit_reserve_ops(&edit, operations);
    if (status != OMC_STATUS_OK) {
        omc_edit_fini(&edit);
        return omc_exif_text_error(OMC_EXIF_TEXT_TRANSLATION_NO_MEMORY,
                                   0U, OMC_INVALID_ENTRY_ID);
    }
    for (field = 0U; field < OMC_EXIF_TEXT_FIELD_COUNT; ++field) {
        omc_exif_text_plan *plan = &plans[field];
        omc_val value;
        omc_byte_ref value_ref;
        omc_size native_seen = 0U;
        int written = 0;
        if (!plan->apply) {
            continue;
        }
        if (!plan->deleted) {
            if (field == 2U) {
                status = omc_exif_text_user_value(&edit, plan,
                                                  effective_version,
                                                  &value_ref);
                if (status == OMC_STATUS_OK) {
                    omc_val_make_bytes(&value, value_ref);
                }
            } else {
                status = omc_exif_text_append(&edit, plan->text.data,
                                              plan->text.size, &value_ref);
                if (status == OMC_STATUS_OK) {
                    if (field < 2U) {
                        omc_val_make_bytes(&value, value_ref);
                    } else {
                        omc_val_make_text(&value, value_ref,
                                          plan->ascii ? OMC_TEXT_ASCII
                                                      : OMC_TEXT_UTF8);
                    }
                }
            }
            if (status != OMC_STATUS_OK) {
                omc_edit_fini(&edit);
                return omc_exif_text_error(
                    OMC_EXIF_TEXT_TRANSLATION_NO_MEMORY,
                    omc_exif_text_fields[field].mapping, plan->source);
            }
        }
        for (i = 0U; i < source->entry_count; ++i) {
            const omc_entry *entry = &source->entries[i];
            if (!omc_exif_text_native_key(source, entry,
                                         omc_exif_text_fields[field].tag)) {
                continue;
            }
            ++native_seen;
            if (!plan->deleted && !written) {
                status = omc_edit_set_value(&edit, (omc_entry_id)i, &value);
                if (status != OMC_STATUS_OK) {
                    omc_edit_fini(&edit);
                    return omc_exif_text_error(
                        OMC_EXIF_TEXT_TRANSLATION_NO_MEMORY,
                        omc_exif_text_fields[field].mapping, plan->source);
                }
                ++result.entries_updated;
                written = 1;
            } else {
                status = omc_edit_tombstone(&edit, (omc_entry_id)i);
                if (status != OMC_STATUS_OK) {
                    omc_edit_fini(&edit);
                    return omc_exif_text_error(
                        OMC_EXIF_TEXT_TRANSLATION_NO_MEMORY,
                        omc_exif_text_fields[field].mapping, plan->source);
                }
                ++result.entries_removed;
            }
        }
        if (!plan->deleted && !written) {
            omc_entry entry;
            omc_byte_ref ifd_ref;
            memset(&entry, 0, sizeof(entry));
            status = omc_exif_text_append(&edit, (const omc_u8 *)"exififd",
                                          strlen("exififd"), &ifd_ref);
            if (status == OMC_STATUS_OK) {
                omc_key_make_exif_tag(&entry.key, ifd_ref,
                                      omc_exif_text_fields[field].tag);
                entry.value = value;
                entry.origin.block = OMC_INVALID_BLOCK_ID;
                entry.origin.wire_type.family = OMC_WIRE_TIFF;
                entry.origin.wire_type.code =
                    field < 3U ? 7U : (plan->ascii ? 2U : 129U);
                entry.origin.wire_count =
                    field == 2U ? value.count : value.count + (field == 3U ? 1U : 0U);
                entry.flags = OMC_ENTRY_FLAG_DIRTY;
                status = omc_edit_add_entry(&edit, &entry);
            }
            if (status != OMC_STATUS_OK) {
                omc_edit_fini(&edit);
                return omc_exif_text_error(
                    OMC_EXIF_TEXT_TRANSLATION_NO_MEMORY,
                    omc_exif_text_fields[field].mapping, plan->source);
            }
            ++result.entries_added;
        }
        ++result.groups_translated;
        (void)native_seen;
    }
    status = omc_edit_commit(source, &edit, 1U, out);
    omc_edit_fini(&edit);
    if (status != OMC_STATUS_OK) {
        return omc_exif_text_error(OMC_EXIF_TEXT_TRANSLATION_INTERNAL,
                                   0U, OMC_INVALID_ENTRY_ID);
    }
    result.status = OMC_EXIF_TEXT_TRANSLATION_OK;
    result.failed_mapping = 0U;
    result.failed_source = OMC_INVALID_ENTRY_ID;
    return result;
}
