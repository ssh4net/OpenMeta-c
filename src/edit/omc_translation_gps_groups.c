#include "omc/omc_translation.h"
#include "../core/omc_value_internal.h"

#include <string.h>

static const char k_gg_ns[] = "http://ns.adobe.com/exif/1.0/";
static const char k_gg_ifd[] = "gpsifd";

typedef enum omc_gg_kind {
    OMC_GG_COORDINATE,
    OMC_GG_TIMESTAMP,
    OMC_GG_RATIONAL_PAIR,
    OMC_GG_SINGLE_TEXT,
    OMC_GG_SINGLE_U16,
    OMC_GG_PLAIN_TEXT,
    OMC_GG_ENCODED_TEXT
} omc_gg_kind;

typedef struct omc_gg_prop {
    omc_entry_id first;
    omc_entry_id duplicate;
    int dirty;
} omc_gg_prop;

typedef struct omc_gg_group {
    omc_gg_kind kind;
    omc_u32 mapping;
    const char *paths[2];
    omc_u16 tags[2];
    omc_u32 source_count;
    omc_u32 native_count;
    omc_entry_id source[2];
    omc_entry_id native[2];
    omc_u32 native_seen[2];
    int matches[2];
    int selected;
    int present;
    int apply;
    omc_urational components[3];
    omc_urational magnitude;
    omc_u8 short_value;
    char reference;
    char text[256];
    omc_size text_size;
    char date[11];
} omc_gg_group;

typedef struct omc_gg_context {
    const omc_store *source;
    omc_store *out;
    omc_gps_translation_opts opts;
    omc_gps_translation_res result;
    omc_gg_group *groups;
    omc_u32 group_count;
    omc_edit edit;
    omc_u8 version[4];
    omc_u32 version_count;
    omc_entry_id version_entry;
    omc_entry_id version_source;
    omc_u32 added;
    omc_u64 operations;
    omc_u64 text_bytes;
    int need_version;
    int removed_group;
} omc_gg_context;

static int
gg_ref_equal(const omc_arena *arena, omc_byte_ref ref, const char *literal)
{
    omc_const_bytes bytes = omc_arena_view(arena, ref);
    omc_size size = strlen(literal);
    return bytes.size == size && (size == 0U || memcmp(bytes.data, literal, size) == 0);
}

static omc_const_bytes
gg_value_text(const omc_store *store, const omc_val *value)
{
    omc_const_bytes empty;
    empty.data = (const omc_u8 *)0; empty.size = 0U;
    if (value == (const omc_val *)0 || value->kind != OMC_VAL_TEXT
        || (value->text_encoding != OMC_TEXT_ASCII
            && value->text_encoding != OMC_TEXT_UTF8)
        || !omc_value_shape_valid(value, &store->arena))
        return empty;
    return omc_arena_view(&store->arena, value->u.ref);
}

static omc_gps_translation_status
gg_digits(const omc_u8 *bytes, omc_size size, omc_u64 *out)
{
    omc_size i;
    omc_u64 number = 0U;
    if (size == 0U) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    for (i = 0U; i < size; ++i) {
        if (bytes[i] < '0' || bytes[i] > '9') return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        if (number > (~(omc_u64)0U - (omc_u64)(bytes[i] - '0')) / 10U)
            return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
        number = number * 10U + (omc_u64)(bytes[i] - '0');
    }
    *out = number;
    return OMC_GPS_TRANSLATION_OK;
}

static omc_gps_translation_status
gg_decimal(const omc_u8 *bytes, omc_size size, omc_u64 *numer,
           omc_u64 *denom)
{
    omc_size dot = size;
    omc_size i;
    omc_u64 whole;
    omc_u64 fraction = 0U;
    omc_u64 scale = 1U;
    omc_gps_translation_status status;
    for (i = 0U; i < size; ++i) {
        if (bytes[i] == '.') {
            if (dot != size) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
            dot = i;
        }
    }
    if (dot == size) {
        status = gg_digits(bytes, size, numer);
        if (status == OMC_GPS_TRANSLATION_OK) *denom = 1U;
        return status;
    }
    status = gg_digits(bytes, dot, &whole);
    if (status != OMC_GPS_TRANSLATION_OK || dot + 1U == size)
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    for (i = dot + 1U; i < size; ++i)
        if (bytes[i] < '0' || bytes[i] > '9') return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    while (size > dot + 1U && bytes[size - 1U] == '0') --size;
    for (i = dot + 1U; i < size; ++i) {
        if (scale > ~(omc_u64)0U / 10U) return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
        scale *= 10U;
    }
    if (size != dot + 1U) {
        status = gg_digits(bytes + dot + 1U, size - dot - 1U, &fraction);
        if (status != OMC_GPS_TRANSLATION_OK) return status;
    }
    if (whole > (~(omc_u64)0U - fraction) / scale)
        return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
    *numer = whole * scale + fraction; *denom = scale;
    return OMC_GPS_TRANSLATION_OK;
}

static omc_gps_translation_status
gg_rational(const omc_store *store, const omc_val *value, omc_urational *out)
{
    omc_const_bytes text;
    omc_u64 numer;
    omc_u64 denom;
    omc_size slash;
    omc_u64 a;
    omc_u64 b;
    if (!omc_value_shape_valid(value, &store->arena))
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    if (value->kind == OMC_VAL_SCALAR && value->count == 1U) {
        if (value->elem_type == OMC_ELEM_URATIONAL) {
            numer = value->u.ur.numer; denom = value->u.ur.denom;
        } else if (value->elem_type == OMC_ELEM_U8 || value->elem_type == OMC_ELEM_U16
                   || value->elem_type == OMC_ELEM_U32 || value->elem_type == OMC_ELEM_U64) {
            numer = value->u.u64; denom = 1U;
        } else return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    } else if (value->kind == OMC_VAL_TEXT) {
        text = gg_value_text(store, value);
        slash = text.size;
        for (a = 0U; a < text.size; ++a) {
            if (text.data[a] == '/') {
                if (slash != text.size) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
                slash = (omc_size)a;
            }
        }
        if (slash == text.size) {
            omc_gps_translation_status s = gg_decimal(text.data, text.size, &numer, &denom);
            if (s != OMC_GPS_TRANSLATION_OK) return s;
        } else {
            omc_gps_translation_status s = gg_digits(text.data, slash, &numer);
            if (s != OMC_GPS_TRANSLATION_OK) return s;
            s = gg_digits(text.data + slash + 1U, text.size - slash - 1U, &denom);
            if (s != OMC_GPS_TRANSLATION_OK) return s;
        }
    } else return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    if (denom == 0U) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    a = numer; b = denom;
    while (b != 0U) { omc_u64 r = a % b; a = b; b = r; }
    if (a == 0U) a = 1U;
    numer /= a; denom /= a;
    if (numer > 0xFFFFFFFFU || denom > 0xFFFFFFFFU)
        return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
    out->numer = (omc_u32)numer; out->denom = (omc_u32)denom;
    return OMC_GPS_TRANSLATION_OK;
}

static int
gg_ratio_equal(omc_urational a, omc_urational b)
{ return a.denom != 0U && b.denom != 0U && (omc_u64)a.numer * b.denom == (omc_u64)b.numer * a.denom; }

static int
gg_array_equal(const omc_store *store, const omc_val *value,
               const omc_urational *expected, omc_u32 count)
{
    omc_u32 i;
    omc_val scalar;
    if (value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_URATIONAL
        || value->count != count || !omc_value_shape_valid(value, &store->arena))
        return 0;
    for (i = 0U; i < count; ++i) {
        omc_value_array_scalar(value, &store->arena, i, &scalar);
        if (!gg_ratio_equal(scalar.u.ur, expected[i]))
            return 0;
    }
    return 1;
}

static omc_gps_translation_status
gg_coordinate(const omc_store *store, const omc_val *value, int latitude,
              omc_gg_group *group)
{
    omc_const_bytes text = gg_value_text(store, value);
    omc_size end;
    omc_size first = 0U;
    omc_size second = 0U;
    omc_u64 degrees;
    omc_u64 minutes;
    omc_u64 sec_numer;
    omc_u64 sec_denom;
    omc_gps_translation_status status;
    if (text.size < 3U) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    group->reference = (char)text.data[text.size - 1U];
    if (latitude ? (group->reference != 'N' && group->reference != 'S')
                 : (group->reference != 'E' && group->reference != 'W'))
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    end = text.size - 1U;
    while (first < end && text.data[first] != ',') ++first;
    if (first == end) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    status = gg_digits(text.data, first, &degrees);
    if (status != OMC_GPS_TRANSLATION_OK) return status;
    second = first + 1U;
    while (second < end && text.data[second] != ',') ++second;
    if (second == end) {
        omc_u64 minute_numer;
        omc_u64 minute_denom;
        status = gg_decimal(text.data + first + 1U, end - first - 1U,
                            &minute_numer, &minute_denom);
        if (status != OMC_GPS_TRANSLATION_OK) return status;
        minutes = minute_numer / minute_denom;
        sec_numer = minute_numer % minute_denom;
        sec_denom = minute_denom;
        {
            omc_u64 a = 60U; omc_u64 b = sec_denom;
            while (b != 0U) { omc_u64 r = a % b; a = b; b = r; }
            if (a == 0U) a = 1U;
            sec_denom /= a; sec_numer *= 60U / a;
        }
    } else {
        status = gg_digits(text.data + first + 1U, second - first - 1U, &minutes);
        if (status != OMC_GPS_TRANSLATION_OK) return status;
        status = gg_decimal(text.data + second + 1U, end - second - 1U,
                            &sec_numer, &sec_denom);
        if (status != OMC_GPS_TRANSLATION_OK) return status;
    }
    if (degrees > (latitude ? 90U : 180U) || minutes >= 60U
        || sec_numer / sec_denom >= 60U
        || (degrees == (latitude ? 90U : 180U)
            && (minutes != 0U || sec_numer != 0U)))
        return OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE;
    group->components[0].numer = (omc_u32)degrees; group->components[0].denom = 1U;
    group->components[1].numer = (omc_u32)minutes; group->components[1].denom = 1U;
    group->components[2].numer = (omc_u32)sec_numer;
    group->components[2].denom = (omc_u32)sec_denom;
    return OMC_GPS_TRANSLATION_OK;
}

static int
gg_copy_text(omc_gg_group *group, const omc_u8 *bytes, omc_size size)
{
    if (size > sizeof(group->text)) return 0;
    memcpy(group->text, bytes, size); group->text_size = size; return 1;
}

static omc_gps_translation_status
gg_parse_group(omc_gg_context *context, omc_gg_group *group)
{
    const omc_store *store = context->source;
    const omc_val *value;
    omc_const_bytes text;
    omc_u64 number;
    omc_u64 h;
    omc_u64 hour;
    omc_u64 m;
    omc_gps_translation_status status;
    value = &store->entries[group->source[0]].value;
    if (group->kind == OMC_GG_COORDINATE)
        return gg_coordinate(store, value, group->mapping == OMC_GPS_DEST_TRANSLATE_LATITUDE, group);
    if (group->kind == OMC_GG_RATIONAL_PAIR && group->source_count == 1U)
        return gg_rational(store, value, &group->magnitude);
    if (group->kind == OMC_GG_RATIONAL_PAIR) {
        text = gg_value_text(store, value);
        if (group->mapping == OMC_GPS_NAV_TRANSLATE_SPEED) {
            if ((text.size == 1U && (text.data[0] == 'K' || text.data[0] == 'M' || text.data[0] == 'N'))
                || (text.size == 4U && memcmp(text.data, "km/h", 4U) == 0))
                group->reference = text.data[0] == 'k' ? 'K' : (char)text.data[0];
            else if (text.size == 3U && memcmp(text.data, "mph", 3U) == 0) group->reference = 'M';
            else if (text.size == 5U && memcmp(text.data, "knots", 5U) == 0) group->reference = 'N';
            else return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        } else if (group->mapping == OMC_GPS_NAV_TRANSLATE_TRACK
                   || group->mapping == OMC_GPS_DEST_TRANSLATE_BEARING) {
            if (text.size == 1U && (text.data[0] == 'T' || text.data[0] == 'M')) group->reference = (char)text.data[0];
            else if (text.size == 10U && memcmp(text.data, "True North", 10U) == 0) group->reference = 'T';
            else if (text.size == 14U && memcmp(text.data, "Magnetic North", 14U) == 0) group->reference = 'M';
            else return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        } else {
            if (text.size == 1U && (text.data[0] == 'K' || text.data[0] == 'M' || text.data[0] == 'N')) group->reference = (char)text.data[0];
            else if (text.size == 14U && memcmp(text.data, "Nautical miles", 14U) == 0) group->reference = 'N';
            else return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        }
        return gg_rational(store, &store->entries[group->source[1]].value,
                           &group->magnitude);
    }
    if (group->kind == OMC_GG_SINGLE_U16) {
        if (value->kind == OMC_VAL_TEXT) {
            text = gg_value_text(store, value);
            if (text.size != 1U || (text.data[0] != '0' && text.data[0] != '1'))
                return OMC_GPS_TRANSLATION_INVALID_SOURCE;
            group->short_value = (omc_u8)(text.data[0] - '0');
        } else if (value->kind == OMC_VAL_SCALAR && value->count == 1U
                   && (value->elem_type == OMC_ELEM_U8 || value->elem_type == OMC_ELEM_U16)
                   && value->u.u64 <= 1U)
            group->short_value = (omc_u8)value->u.u64;
        else return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        return OMC_GPS_TRANSLATION_OK;
    }
    if (group->kind == OMC_GG_SINGLE_TEXT || group->kind == OMC_GG_PLAIN_TEXT
        || group->kind == OMC_GG_ENCODED_TEXT) {
        text = gg_value_text(store, value);
        if (group->kind == OMC_GG_ENCODED_TEXT) {
            if (value->kind == OMC_VAL_BYTES) {
                text = omc_arena_view(&store->arena, value->u.ref);
                if (text.size >= 8U && memcmp(text.data, "ASCII", 5U) == 0) {
                    text.data += 8U; text.size -= 8U;
                }
            } else if (value->kind != OMC_VAL_TEXT) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        }
        if (text.size == 0U || !gg_copy_text(group, text.data, text.size))
            return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        if (group->kind == OMC_GG_SINGLE_TEXT) {
            if (group->mapping == OMC_GPS_QUALITY_TRANSLATE_STATUS) {
                if (text.size == 1U && (text.data[0] == 'A' || text.data[0] == 'V')) {
                    group->text[0] = (char)text.data[0]; group->text_size = 1U;
                } else if (text.size == 18U && memcmp(text.data, "Measurement Active", 18U) == 0) {
                    group->text[0] = 'A'; group->text_size = 1U;
                } else if (text.size == 16U && memcmp(text.data, "Measurement Void", 16U) == 0) {
                    group->text[0] = 'V'; group->text_size = 1U;
                } else return OMC_GPS_TRANSLATION_INVALID_SOURCE;
            }
            if (group->mapping == OMC_GPS_QUALITY_TRANSLATE_MEASURE_MODE
                && (group->text_size != 1U || (group->text[0] != '2' && group->text[0] != '3')))
                return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        }
        return OMC_GPS_TRANSLATION_OK;
    }
    if (group->kind == OMC_GG_TIMESTAMP) {
        text = gg_value_text(store, value);
        if (text.size < 20U || text.data[4] != '-' || text.data[7] != '-'
            || text.data[10] != 'T' || text.data[13] != ':' || text.data[16] != ':')
            return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        status = gg_digits(text.data + 11U, 2U, &hour);
        if (status != OMC_GPS_TRANSLATION_OK) return status;
        status = gg_digits(text.data + 14U, 2U, &m);
        if (status != OMC_GPS_TRANSLATION_OK) return status;
        if (hour > 23U || m > 59U) return OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE;
        status = gg_decimal(text.data + 17U, text.size - 17U - (text.data[text.size - 1U] == 'Z' ? 1U : 0U),
                            &number, &h);
        if (status != OMC_GPS_TRANSLATION_OK || h == 0U || number / h >= 60U)
            return OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE;
        {
            omc_u64 a = number; omc_u64 b = h;
            while (b != 0U) { omc_u64 r = a % b; a = b; b = r; }
            if (a == 0U) a = 1U;
            group->components[2].numer = (omc_u32)(number / a);
            group->components[2].denom = (omc_u32)(h / a);
        }
        group->components[0].numer = (omc_u32)hour; group->components[0].denom = 1U;
        group->components[1].numer = (omc_u32)m; group->components[1].denom = 1U;
        if (text.size < 20U || text.data[text.size - 1U] != 'Z') return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        if (text.size < 10U || !gg_copy_text((omc_gg_group *)group, text.data, 10U)) return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        memcpy(group->date, text.data, 4U); group->date[4] = ':';
        group->date[5] = (char)text.data[5]; group->date[6] = (char)text.data[6]; group->date[7] = ':';
        group->date[8] = (char)text.data[8]; group->date[9] = (char)text.data[9]; group->date[10] = 0;
        return OMC_GPS_TRANSLATION_OK;
    }
    return OMC_GPS_TRANSLATION_INVALID_SOURCE;
}

static int
gg_native_tag(const omc_gg_context *context, const omc_entry *entry, omc_u16 tag)
{ return entry->key.kind == OMC_KEY_EXIF_TAG && entry->key.u.exif_tag.tag == tag && gg_ref_equal(&context->source->arena, entry->key.u.exif_tag.ifd, k_gg_ifd); }

static int
gg_native_match(const omc_gg_context *context, const omc_gg_group *group,
                const omc_val *value, omc_size member)
{
    omc_const_bytes text;
    if (group->kind == OMC_GG_COORDINATE || group->kind == OMC_GG_TIMESTAMP) {
        if (member == 0U) {
            if (group->kind == OMC_GG_TIMESTAMP)
                return gg_array_equal(context->source, value,
                                      group->components, 3U);
            text = gg_value_text(context->source, value);
            return text.size == 1U && text.data[0] == (omc_u8)group->reference;
        }
        if (group->kind == OMC_GG_TIMESTAMP) {
            text = gg_value_text(context->source, value);
            return text.size == 10U && memcmp(text.data, group->date, 10U) == 0;
        }
        return gg_array_equal(context->source, value, group->components, 3U);
    }
    if (group->kind == OMC_GG_RATIONAL_PAIR && group->source_count == 1U)
        return value->kind == OMC_VAL_SCALAR && value->elem_type == OMC_ELEM_URATIONAL
               && gg_ratio_equal(value->u.ur, group->magnitude);
    if (group->kind == OMC_GG_RATIONAL_PAIR)
        return member == 0U ? (value->kind == OMC_VAL_TEXT && gg_value_text(context->source, value).size == 1U && gg_value_text(context->source, value).data[0] == (omc_u8)group->reference)
                            : (value->kind == OMC_VAL_SCALAR && value->elem_type == OMC_ELEM_URATIONAL && gg_ratio_equal(value->u.ur, group->magnitude));
    if (group->kind == OMC_GG_SINGLE_U16)
        return value->kind == OMC_VAL_SCALAR && (value->elem_type == OMC_ELEM_U8 || value->elem_type == OMC_ELEM_U16) && value->u.u64 == group->short_value;
    if (group->kind == OMC_GG_PLAIN_TEXT || group->kind == OMC_GG_SINGLE_TEXT) {
        text = gg_value_text(context->source, value);
        return text.size == group->text_size && memcmp(text.data, group->text, text.size) == 0;
    }
    if (value->kind == OMC_VAL_BYTES) {
        text = omc_arena_view(&context->source->arena, value->u.ref);
        if (text.size >= 8U && memcmp(text.data, "ASCII", 5U) == 0) {
            text.data += 8U;
            text.size -= 8U;
        }
        return text.size == group->text_size
               && memcmp(text.data, group->text, text.size) == 0;
    }
    text = gg_value_text(context->source, value);
    return text.size == group->text_size
           && memcmp(text.data, group->text, text.size) == 0;
}

static omc_status
gg_make_value(omc_gg_context *context, const omc_gg_group *group,
              omc_size member, omc_val *value)
{
    omc_byte_ref ref;
    omc_urational components[3];
    omc_u8 prefix[8] = { 'A','S','C','I','I',0U,0U,0U };
    if (group->kind == OMC_GG_COORDINATE) {
        if (member == 0U) {
            if (omc_arena_append(&context->edit.arena, &group->reference, 1U, &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
            omc_val_make_text(value, ref, OMC_TEXT_ASCII); return OMC_STATUS_OK;
        }
        memcpy(components, group->components, sizeof(components));
        if (omc_arena_append(&context->edit.arena, components, sizeof(components), &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
        omc_val_make_array(value, OMC_ELEM_URATIONAL, 3U, ref, OMC_BYTE_ORDER_NATIVE); return OMC_STATUS_OK;
    }
    if (group->kind == OMC_GG_TIMESTAMP) {
        if (member == 0U) {
            memcpy(components, group->components, sizeof(components));
            if (omc_arena_append(&context->edit.arena, components, sizeof(components), &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
            omc_val_make_array(value, OMC_ELEM_URATIONAL, 3U, ref, OMC_BYTE_ORDER_NATIVE); return OMC_STATUS_OK;
        }
        if (omc_arena_append(&context->edit.arena, group->date, 10U, &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
        omc_val_make_text(value, ref, OMC_TEXT_ASCII); return OMC_STATUS_OK;
    }
    if (group->kind == OMC_GG_RATIONAL_PAIR && group->source_count == 1U) {
        omc_val_make_urational(value, group->magnitude);
        return OMC_STATUS_OK;
    }
    if (group->kind == OMC_GG_RATIONAL_PAIR) {
        if (member == 0U) {
            if (omc_arena_append(&context->edit.arena, &group->reference, 1U, &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
            omc_val_make_text(value, ref, OMC_TEXT_ASCII);
        } else omc_val_make_urational(value, group->magnitude);
        return OMC_STATUS_OK;
    }
    if (group->kind == OMC_GG_SINGLE_U16) { omc_val_make_u16(value, group->short_value); return OMC_STATUS_OK; }
    if (group->kind == OMC_GG_PLAIN_TEXT || group->kind == OMC_GG_SINGLE_TEXT) {
        if (omc_arena_append(&context->edit.arena, group->text, group->text_size, &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
        omc_val_make_text(value, ref, OMC_TEXT_ASCII); return OMC_STATUS_OK;
    }
    if (omc_arena_append(&context->edit.arena, prefix, sizeof(prefix), &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
    if (omc_arena_append(&context->edit.arena, group->text, group->text_size, &ref) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
    ref.offset -= 8U; ref.size += 8U; omc_val_make_bytes(value, ref); return OMC_STATUS_OK;
}

static omc_status
gg_append(omc_gg_context *context, const omc_gg_group *group, omc_size member,
          omc_val *value)
{
    omc_entry entry;
    omc_byte_ref ifd;
    omc_const_bytes wire;
    memset(&entry, 0, sizeof(entry));
    if (omc_arena_append(&context->edit.arena, k_gg_ifd, sizeof(k_gg_ifd)-1U, &ifd) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
    omc_key_make_exif_tag(&entry.key, ifd, group->tags[member]); entry.value = *value;
    entry.origin = context->source->entries[group->source[member < group->source_count ? member : 0U]].origin;
    wire = omc_arena_view(&context->source->arena, entry.origin.wire_type_name);
    entry.origin.wire_type_name.offset = 0U; entry.origin.wire_type_name.size = 0U;
    if (wire.size != 0U && omc_arena_append(&context->edit.arena, wire.data, wire.size, &entry.origin.wire_type_name) != OMC_STATUS_OK) return OMC_STATUS_NO_MEMORY;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    return omc_edit_add_entry(&context->edit, &entry);
}

static omc_gps_translation_res
gg_apply(omc_gg_context *context)
{
    omc_size i;
    omc_size j;
    omc_size k;
    omc_entry *entry;
    omc_val value;
    omc_status status;
    for (i = 0U; i < context->group_count; ++i) {
        omc_gg_group *selected = &context->groups[i];
        if (!selected->selected || !selected->present)
            continue;
        if (selected->kind == OMC_GG_TIMESTAMP
            || selected->mapping == OMC_GPS_QUALITY_TRANSLATE_DIFFERENTIAL
            || selected->mapping == OMC_GPS_QUALITY_TRANSLATE_ERROR
            || selected->kind == OMC_GG_ENCODED_TEXT) {
            if (context->version[0] != 2U || context->version[1] > 4U
                || context->version[2] != 0U || context->version[3] != 0U
                || (selected->mapping == OMC_GPS_QUALITY_TRANSLATE_ERROR
                    && context->version[1] < 3U)
                || (selected->mapping != OMC_GPS_QUALITY_TRANSLATE_ERROR
                    && context->version[1] < 2U)) {
                context->result.status = OMC_GPS_TRANSLATION_UNSUPPORTED_VERSION;
                context->result.failed_mapping = OMC_GPS_TRANSLATE_VERSION;
                return context->result;
            }
        }
        if (context->version_source == OMC_INVALID_ENTRY_ID)
            context->version_source = selected->source[0];
    }
    for (i = 0U; i < context->group_count; ++i) {
        omc_gg_group *group = &context->groups[i];
        if (!group->selected) continue;
        group->native_count = group->kind == OMC_GG_COORDINATE
                                  || group->kind == OMC_GG_TIMESTAMP
                                  || (group->kind == OMC_GG_RATIONAL_PAIR
                                      && group->source_count > 1U) ? 2U : 1U;
        for (j = 0U; j < group->native_count; ++j) {
            group->native[j] = OMC_INVALID_ENTRY_ID;
            for (k = 0U; k < context->source->entry_count; ++k) {
                entry = &context->source->entries[k];
                if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U || !gg_native_tag(context, entry, group->tags[j])) continue;
                if (group->native_seen[j]++ == 0U) { group->native[j] = (omc_entry_id)k; group->matches[j] = group->present && gg_native_match(context, group, &entry->value, j); }
            }
        }
        if (context->opts.conflict == OMC_TRANSLATION_PRESERVE && (group->native_seen[0] != 0U || group->native_seen[1] != 0U)) { ++context->result.groups_preserved; continue; }
        if (context->opts.conflict == OMC_TRANSLATION_FAIL && (group->native_seen[0] != 0U || group->native_seen[1] != 0U) && !(group->present && group->native_seen[0] == 1U && group->matches[0] && (group->native_count == 1U || (group->native_seen[1] == 1U && group->matches[1])))) {
            context->result.status = OMC_GPS_TRANSLATION_NATIVE_CONFLICT;
            context->result.failed_mapping = group->mapping;
            context->result.failed_source = group->source[0];
            return context->result;
        }
        if (group->present && group->native_seen[0] == 1U && group->matches[0] && (group->native_count == 1U || (group->native_seen[1] == 1U && group->matches[1]))) { ++context->result.groups_unchanged; continue; }
        group->apply = 1;
        if (group->present) context->need_version = context->need_version || group->kind == OMC_GG_TIMESTAMP || group->mapping == OMC_GPS_QUALITY_TRANSLATE_DIFFERENTIAL || group->mapping == OMC_GPS_QUALITY_TRANSLATE_ERROR || group->kind == OMC_GG_ENCODED_TEXT;
        else context->removed_group = 1;
        for (j = 0U; j < group->native_count; ++j) {
            if (!group->present) context->operations += group->native_seen[j];
            else if (group->native_seen[j] == 0U) { ++context->added; ++context->operations; }
            else context->operations += (omc_u64)group->native_seen[j] - 1U + (group->matches[j] ? 0U : 1U);
        }
        ++context->result.groups_translated;
    }
    if (context->need_version && context->version_count == 0U) { ++context->added; ++context->operations; }
    if (context->added > context->opts.max_added_entries) return (context->result.status = OMC_GPS_TRANSLATION_ENTRY_LIMIT, context->result);
    if (context->operations > context->opts.max_operations) return (context->result.status = OMC_GPS_TRANSLATION_OPERATION_LIMIT, context->result);
    status = omc_edit_reserve_ops(&context->edit, (omc_size)context->operations);
    if (status != OMC_STATUS_OK) return (context->result.status = OMC_GPS_TRANSLATION_NO_MEMORY, context->result);
    for (i = 0U; i < context->group_count; ++i) {
        omc_gg_group *group = &context->groups[i];
        if (!group->apply) continue;
        for (j = 0U; j < group->native_count; ++j) {
            for (k = 0U; k < context->source->entry_count; ++k) {
                entry = &context->source->entries[k];
                if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U || !gg_native_tag(context, entry, group->tags[j])) continue;
                if (!group->present || (omc_entry_id)k != group->native[j]) { status = omc_edit_tombstone(&context->edit, (omc_entry_id)k); ++context->result.entries_removed; }
                else if (!group->matches[j]) { status = gg_make_value(context, group, j, &value); if (status == OMC_STATUS_OK) status = omc_edit_set_value(&context->edit, (omc_entry_id)k, &value); ++context->result.entries_updated; }
                if (status != OMC_STATUS_OK) { context->result.status = OMC_GPS_TRANSLATION_NO_MEMORY; return context->result; }
            }
            if (group->present && group->native_seen[j] == 0U) { status = gg_make_value(context, group, j, &value); if (status == OMC_STATUS_OK) status = gg_append(context, group, j, &value); ++context->result.entries_added; if (status != OMC_STATUS_OK) { context->result.status = OMC_GPS_TRANSLATION_NO_MEMORY; return context->result; } }
        }
    }
    if (context->need_version && context->version_count == 0U) {
        omc_gg_group version_group; memset(&version_group, 0, sizeof(version_group)); version_group.source[0] = context->version_source; version_group.source_count = 1U; version_group.tags[0] = 0U; version_group.kind = OMC_GG_PLAIN_TEXT;
        {
            omc_byte_ref ref; omc_entry added; omc_const_bytes wire;
            if (omc_arena_append(&context->edit.arena, context->version, 4U, &ref) != OMC_STATUS_OK) return (context->result.status = OMC_GPS_TRANSLATION_NO_MEMORY, context->result);
            omc_val_make_array(&value, OMC_ELEM_U8, 4U, ref, OMC_BYTE_ORDER_NATIVE); memset(&added, 0, sizeof(added));
            if (omc_arena_append(&context->edit.arena, k_gg_ifd, sizeof(k_gg_ifd)-1U, &ref) != OMC_STATUS_OK) return (context->result.status = OMC_GPS_TRANSLATION_NO_MEMORY, context->result);
            omc_key_make_exif_tag(&added.key, ref, 0U); added.value = value; added.origin = context->source->entries[context->version_source].origin; wire = omc_arena_view(&context->source->arena, added.origin.wire_type_name); added.origin.wire_type_name.offset = 0U; added.origin.wire_type_name.size = 0U; if (wire.size != 0U && omc_arena_append(&context->edit.arena, wire.data, wire.size, &added.origin.wire_type_name) != OMC_STATUS_OK) return (context->result.status = OMC_GPS_TRANSLATION_NO_MEMORY, context->result); added.flags = OMC_ENTRY_FLAG_DIRTY; if (omc_edit_add_entry(&context->edit, &added) != OMC_STATUS_OK) return (context->result.status = OMC_GPS_TRANSLATION_NO_MEMORY, context->result); ++context->result.entries_added;
        }
    }
    if (context->edit.op_count != (omc_size)context->operations) return (context->result.status = OMC_GPS_TRANSLATION_INTERNAL, context->result);
    if (omc_edit_commit(context->source, &context->edit, 1U, context->out) != OMC_STATUS_OK) return (context->result.status = OMC_GPS_TRANSLATION_INTERNAL, context->result);
    context->result.status = OMC_GPS_TRANSLATION_OK; return context->result;
}

static omc_gps_translation_res
gg_translate(const omc_store *source, omc_store *out,
             const omc_gps_translation_opts *options,
             omc_gg_group *groups, omc_u32 count, omc_u32 mappings)
{
    omc_gg_context context;
    omc_gps_translation_opts defaults;
    omc_u32 i;
    omc_size j;
    memset(&context, 0, sizeof(context));
    context.source = source; context.out = out; context.groups = groups; context.group_count = count; context.version_entry = OMC_INVALID_ENTRY_ID;
    omc_gps_translation_opts_init(&defaults); if (options != (const omc_gps_translation_opts *)0) defaults = *options; context.opts = defaults;
    context.result.status = OMC_GPS_TRANSLATION_INTERNAL; context.result.failed_source = OMC_INVALID_ENTRY_ID;
    if (source == (const omc_store *)0 || out == (omc_store *)0 || source == out || !omc_store_shape_valid(source) || !omc_store_shape_valid(out) || (context.opts.mappings & ~mappings) != 0U || (context.opts.mappings & mappings) == 0U || context.opts.conflict < OMC_TRANSLATION_PRESERVE || context.opts.conflict > OMC_TRANSLATION_REPLACE || context.opts.max_added_entries == 0U || context.opts.max_operations == 0U || context.opts.max_source_properties == 0U || context.opts.max_text_bytes_per_property == 0U || context.opts.max_total_text_bytes == 0U)
        return (context.result.status = OMC_GPS_TRANSLATION_INVALID_OPTIONS, context.result);
    for (i = 0U; i < count; ++i) {
        omc_gg_group *group = &groups[i];
        omc_gg_prop props[2];
        int dirty = 0; int found = 0;
        memset(props, 0, sizeof(props)); props[0].first = OMC_INVALID_ENTRY_ID; props[0].duplicate = OMC_INVALID_ENTRY_ID; props[1].first = OMC_INVALID_ENTRY_ID; props[1].duplicate = OMC_INVALID_ENTRY_ID;
        for (j = 0U; j < source->entry_count; ++j) {
            const omc_entry *entry = &source->entries[j];
            omc_const_bytes path;
            omc_u32 p;
            if (entry->key.kind != OMC_KEY_XMP_PROPERTY || !gg_ref_equal(&source->arena, entry->key.u.xmp_property.schema_ns, k_gg_ns) || ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U && (entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U)) continue;
            path = omc_arena_view(&source->arena, entry->key.u.xmp_property.property_path);
            for (p = 0U; p < group->source_count; ++p) if (path.size == strlen(group->paths[p]) && memcmp(path.data, group->paths[p], path.size) == 0) { if (props[p].first == OMC_INVALID_ENTRY_ID) props[p].first = (omc_entry_id)j; else props[p].duplicate = (omc_entry_id)j; props[p].dirty = props[p].dirty || ((entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U); }
        }
        for (j = 0U; j < group->source_count; ++j) { found = found || props[j].first != OMC_INVALID_ENTRY_ID; dirty = dirty || props[j].dirty; }
        if ((context.opts.mappings & group->mapping) == 0U || !found || (!context.opts.all_sources && !dirty)) continue;
        group->selected = 1; for (j = 0U; j < group->source_count; ++j) { if (props[j].duplicate != OMC_INVALID_ENTRY_ID) { context.result.status = OMC_GPS_TRANSLATION_AMBIGUOUS_SOURCE; context.result.failed_mapping = group->mapping; context.result.failed_source = props[j].duplicate; return context.result; } if (props[j].first == OMC_INVALID_ENTRY_ID) { context.result.status = OMC_GPS_TRANSLATION_INCOMPLETE_SOURCE; context.result.failed_mapping = group->mapping; return context.result; } group->source[j] = props[j].first; ++context.result.source_properties; if (source->entries[group->source[j]].value.kind == OMC_VAL_TEXT) { omc_size size = source->entries[group->source[j]].value.u.ref.size; if (size > context.opts.max_text_bytes_per_property || size > context.opts.max_total_text_bytes || context.text_bytes > context.opts.max_total_text_bytes - size) { context.result.status = OMC_GPS_TRANSLATION_VALUE_TOO_LONG; context.result.failed_mapping = group->mapping; context.result.failed_source = group->source[j]; return context.result; } context.text_bytes += size; } }
        group->present = (source->entries[group->source[0]].flags & OMC_ENTRY_FLAG_DELETED) == 0U;
        for (j = 1U; j < group->source_count; ++j) if (((source->entries[group->source[j]].flags & OMC_ENTRY_FLAG_DELETED) == 0U) != group->present) { context.result.status = OMC_GPS_TRANSLATION_INCOMPLETE_SOURCE; context.result.failed_mapping = group->mapping; context.result.failed_source = group->source[j]; return context.result; }
        if (group->present) { omc_gps_translation_status s = gg_parse_group(&context, group); if (s != OMC_GPS_TRANSLATION_OK) { context.result.status = s; context.result.failed_mapping = group->mapping; context.result.failed_source = group->source[0]; return context.result; } }
    }
    for (j = 0U; j < source->entry_count; ++j) if ((source->entries[j].flags & OMC_ENTRY_FLAG_DELETED) == 0U && gg_native_tag(&context, &source->entries[j], 0U)) { ++context.version_count; context.version_entry = (omc_entry_id)j; }
    context.version[0] = 2U; context.version[1] = 3U; context.version[2] = 0U; context.version[3] = 0U;
    if (context.version_count != 0U) { const omc_val *value = &source->entries[context.version_entry].value; omc_const_bytes bytes = omc_arena_view(&source->arena, value->u.ref); if (context.version_count != 1U || value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_U8 || value->count != 4U || bytes.size != 4U) { context.result.status = OMC_GPS_TRANSLATION_NATIVE_CONFLICT; context.result.failed_mapping = OMC_GPS_TRANSLATE_VERSION; return context.result; } memcpy(context.version, bytes.data, 4U); }
    omc_edit_init(&context.edit); context.result = gg_apply(&context); omc_edit_fini(&context.edit); return context.result;
}

omc_gps_translation_res
omc_translate_xmp_gps_navigation(const omc_store *source, omc_store *out, const omc_gps_translation_opts *opts)
{
    omc_gps_translation_opts local;
    omc_gg_group groups[4];
    memset(groups, 0, sizeof(groups));
    if (opts != (const omc_gps_translation_opts *)0) { local = *opts; if (local.max_added_entries == 7U) local.max_added_entries = 9U; opts = &local; }
    groups[0].kind = OMC_GG_TIMESTAMP; groups[0].mapping = OMC_GPS_NAV_TRANSLATE_TIMESTAMP; groups[0].paths[0] = "GPSTimeStamp"; groups[0].source_count = 1U; groups[0].tags[0] = 7U; groups[0].tags[1] = 29U;
    groups[1].kind = OMC_GG_RATIONAL_PAIR; groups[1].mapping = OMC_GPS_NAV_TRANSLATE_SPEED; groups[1].paths[0] = "GPSSpeedRef"; groups[1].paths[1] = "GPSSpeed"; groups[1].source_count = 2U; groups[1].tags[0] = 12U; groups[1].tags[1] = 13U;
    groups[2].kind = OMC_GG_RATIONAL_PAIR; groups[2].mapping = OMC_GPS_NAV_TRANSLATE_TRACK; groups[2].paths[0] = "GPSTrackRef"; groups[2].paths[1] = "GPSTrack"; groups[2].source_count = 2U; groups[2].tags[0] = 23U; groups[2].tags[1] = 24U;
    groups[3].kind = OMC_GG_RATIONAL_PAIR; groups[3].mapping = OMC_GPS_NAV_TRANSLATE_DIRECTION; groups[3].paths[0] = "GPSImgDirectionRef"; groups[3].paths[1] = "GPSImgDirection"; groups[3].source_count = 2U; groups[3].tags[0] = 16U; groups[3].tags[1] = 17U;
    return gg_translate(source, out, opts, groups, 4U, OMC_GPS_NAV_TRANSLATE_ALL);
}

omc_gps_translation_res
omc_translate_xmp_gps_destination(const omc_store *source, omc_store *out, const omc_gps_translation_opts *opts)
{
    omc_gps_translation_opts local;
    omc_gg_group groups[4];
    memset(groups, 0, sizeof(groups));
    if (opts != (const omc_gps_translation_opts *)0) { local = *opts; if (local.max_added_entries == 7U) local.max_added_entries = 9U; opts = &local; }
    groups[0].kind = OMC_GG_COORDINATE; groups[0].mapping = OMC_GPS_DEST_TRANSLATE_LATITUDE; groups[0].paths[0] = "GPSDestLatitude"; groups[0].source_count = 1U; groups[0].tags[0] = 19U; groups[0].tags[1] = 20U;
    groups[1].kind = OMC_GG_COORDINATE; groups[1].mapping = OMC_GPS_DEST_TRANSLATE_LONGITUDE; groups[1].paths[0] = "GPSDestLongitude"; groups[1].source_count = 1U; groups[1].tags[0] = 21U; groups[1].tags[1] = 22U;
    groups[2].kind = OMC_GG_RATIONAL_PAIR; groups[2].mapping = OMC_GPS_DEST_TRANSLATE_BEARING; groups[2].paths[0] = "GPSDestBearingRef"; groups[2].paths[1] = "GPSDestBearing"; groups[2].source_count = 2U; groups[2].tags[0] = 23U; groups[2].tags[1] = 24U;
    groups[3].kind = OMC_GG_RATIONAL_PAIR; groups[3].mapping = OMC_GPS_DEST_TRANSLATE_DISTANCE; groups[3].paths[0] = "GPSDestDistanceRef"; groups[3].paths[1] = "GPSDestDistance"; groups[3].source_count = 2U; groups[3].tags[0] = 25U; groups[3].tags[1] = 26U;
    return gg_translate(source, out, opts, groups, 4U, OMC_GPS_DEST_TRANSLATE_ALL);
}

omc_gps_translation_res
omc_translate_xmp_gps_quality(const omc_store *source, omc_store *out, const omc_gps_translation_opts *opts)
{
    omc_gps_translation_opts local;
    omc_gg_group groups[5];
    memset(groups, 0, sizeof(groups));
    if (opts != (const omc_gps_translation_opts *)0) { local = *opts; if (local.max_added_entries == 7U) local.max_added_entries = 6U; opts = &local; }
    groups[0].kind = OMC_GG_SINGLE_TEXT; groups[0].mapping = OMC_GPS_QUALITY_TRANSLATE_STATUS; groups[0].paths[0] = "GPSStatus"; groups[0].source_count = 1U; groups[0].tags[0] = 9U;
    groups[1].kind = OMC_GG_SINGLE_TEXT; groups[1].mapping = OMC_GPS_QUALITY_TRANSLATE_MEASURE_MODE; groups[1].paths[0] = "GPSMeasureMode"; groups[1].source_count = 1U; groups[1].tags[0] = 10U;
    groups[2].kind = OMC_GG_RATIONAL_PAIR; groups[2].mapping = OMC_GPS_QUALITY_TRANSLATE_DOP; groups[2].paths[0] = "GPSDOP"; groups[2].source_count = 1U; groups[2].tags[0] = 11U;
    groups[3].kind = OMC_GG_SINGLE_U16; groups[3].mapping = OMC_GPS_QUALITY_TRANSLATE_DIFFERENTIAL; groups[3].paths[0] = "GPSDifferential"; groups[3].source_count = 1U; groups[3].tags[0] = 30U;
    groups[4].kind = OMC_GG_RATIONAL_PAIR; groups[4].mapping = OMC_GPS_QUALITY_TRANSLATE_ERROR; groups[4].paths[0] = "GPSHPositioningError"; groups[4].source_count = 1U; groups[4].tags[0] = 31U;
    return gg_translate(source, out, opts, groups, 5U, OMC_GPS_QUALITY_TRANSLATE_ALL);
}

omc_gps_translation_res
omc_translate_xmp_gps_text(const omc_store *source, omc_store *out, const omc_gps_translation_opts *opts)
{
    omc_gps_translation_opts local;
    omc_gg_group groups[4];
    memset(groups, 0, sizeof(groups));
    if (opts != (const omc_gps_translation_opts *)0) { local = *opts; if (local.max_added_entries == 7U) local.max_added_entries = 5U; opts = &local; }
    groups[0].kind = OMC_GG_PLAIN_TEXT; groups[0].mapping = OMC_GPS_TEXT_TRANSLATE_SATELLITES; groups[0].paths[0] = "GPSSatellites"; groups[0].source_count = 1U; groups[0].tags[0] = 8U;
    groups[1].kind = OMC_GG_PLAIN_TEXT; groups[1].mapping = OMC_GPS_TEXT_TRANSLATE_MAP_DATUM; groups[1].paths[0] = "GPSMapDatum"; groups[1].source_count = 1U; groups[1].tags[0] = 18U;
    groups[2].kind = OMC_GG_ENCODED_TEXT; groups[2].mapping = OMC_GPS_TEXT_TRANSLATE_PROCESSING_METHOD; groups[2].paths[0] = "GPSProcessingMethod"; groups[2].source_count = 1U; groups[2].tags[0] = 27U;
    groups[3].kind = OMC_GG_ENCODED_TEXT; groups[3].mapping = OMC_GPS_TEXT_TRANSLATE_AREA_INFORMATION; groups[3].paths[0] = "GPSAreaInformation"; groups[3].source_count = 1U; groups[3].tags[0] = 28U;
    return gg_translate(source, out, opts, groups, 4U, OMC_GPS_TEXT_TRANSLATE_ALL);
}
