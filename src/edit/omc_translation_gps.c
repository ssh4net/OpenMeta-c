#include "omc/omc_translation.h"
#include "../core/omc_value_internal.h"

#include <stdlib.h>
#include <string.h>

/* Primary GPS writeback follows the C++ 0.5.10 contract.  The implementation
 * keeps the source scan and the edit plan separate, so no output store is
 * changed until all groups, native conflicts and resource limits pass. */

static const char omc_gps_ns_exif[] = "http://ns.adobe.com/exif/1.0/";
static const char omc_gps_ifd[] = "gpsifd";

typedef struct omc_gps_source_property {
    omc_entry_id first;
    omc_entry_id duplicate;
    int dirty;
} omc_gps_source_property;

typedef struct omc_gps_group {
    omc_u32 mapping;
    omc_u16 tags[2];
    omc_entry_id source[2];
    omc_entry_id native[2];
    omc_u32 native_count[2];
    int matches[2];
    int selected;
    int present;
    int apply;
    omc_urational components[3];
    omc_urational magnitude;
    omc_u8 altitude_ref;
    char reference;
} omc_gps_group;

typedef struct omc_gps_context {
    const omc_store *source;
    omc_gps_translation_opts opts;
    omc_gps_translation_res res;
    omc_edit edit;
    omc_gps_group groups[3];
    omc_u8 version[4];
    omc_entry_id version_entry;
    omc_u32 version_count;
    omc_u64 text_bytes;
    omc_u32 added;
    omc_u64 operations;
    int need_version;
    int removed_group;
    omc_entry_id version_source;
} omc_gps_context;

static omc_gps_translation_res
omc_gps_error(omc_gps_context *context, omc_gps_translation_status status,
              omc_u32 mapping, omc_entry_id source)
{
    context->res.status = status;
    context->res.failed_mapping = mapping;
    context->res.failed_source = source;
    return context->res;
}

static int
omc_gps_ref_equal(const omc_arena *arena, omc_byte_ref ref, const char *text)
{
    omc_const_bytes bytes;
    omc_size size;
    size = strlen(text);
    bytes = omc_arena_view(arena, ref);
    return bytes.size == size &&
           (size == 0U || (bytes.data != NULL && memcmp(bytes.data, text, size) == 0));
}

static int
omc_gps_text_value(const omc_gps_context *context, omc_entry_id id,
                   omc_const_bytes *out)
{
    const omc_val *value;
    value = &context->source->entries[id].value;
    if ((value->kind != OMC_VAL_TEXT ||
         (value->text_encoding != OMC_TEXT_ASCII &&
          value->text_encoding != OMC_TEXT_UTF8)) ||
        !omc_value_shape_valid(value, &context->source->arena))
        return 0;
    *out = omc_arena_view(&context->source->arena, value->u.ref);
    return 1;
}

static omc_gps_translation_status
omc_gps_digits(const omc_u8 *bytes, omc_size size, omc_u64 *out)
{
    omc_size i;
    omc_u64 value;
    omc_u64 digit;
    value = 0U;
    if (size == 0U)
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    for (i = 0U; i < size; ++i) {
        if (bytes[i] < (omc_u8)'0' || bytes[i] > (omc_u8)'9')
            return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        digit = (omc_u64)(bytes[i] - (omc_u8)'0');
        if (value > (~(omc_u64)0 - digit) / 10U)
            return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
        value = value * 10U + digit;
    }
    *out = value;
    return OMC_GPS_TRANSLATION_OK;
}

static omc_gps_translation_status
omc_gps_decimal(const omc_u8 *bytes, omc_size size, omc_u64 *numer,
                omc_u64 *denom)
{
    omc_size i;
    omc_size dot;
    omc_size fraction_size;
    omc_u64 whole;
    omc_u64 fraction;
    omc_u64 multiplier;
    omc_gps_translation_status status;
    dot = size;
    for (i = 0U; i < size; ++i) {
        if (bytes[i] == (omc_u8)'.') {
            if (dot != size)
                return OMC_GPS_TRANSLATION_INVALID_SOURCE;
            dot = i;
        }
    }
    if (dot == size) {
        status = omc_gps_digits(bytes, size, numer);
        if (status == OMC_GPS_TRANSLATION_OK)
            *denom = 1U;
        return status;
    }
    status = omc_gps_digits(bytes, dot, &whole);
    if (status != OMC_GPS_TRANSLATION_OK)
        return status;
    fraction_size = size - dot - 1U;
    if (fraction_size == 0U)
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    for (i = dot + 1U; i < size; ++i) {
        if (bytes[i] < (omc_u8)'0' || bytes[i] > (omc_u8)'9')
            return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    }
    while (fraction_size != 0U && bytes[dot + fraction_size] == (omc_u8)'0')
        --fraction_size;
    fraction = 0U;
    multiplier = 1U;
    for (i = 0U; i < fraction_size; ++i) {
        if (multiplier > ~(omc_u64)0 / 10U)
            return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
        multiplier *= 10U;
    }
    if (fraction_size != 0U) {
        status = omc_gps_digits(bytes + dot + 1U, fraction_size, &fraction);
        if (status != OMC_GPS_TRANSLATION_OK)
            return status;
    }
    if (whole > (~(omc_u64)0 - fraction) / multiplier)
        return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
    *numer = whole * multiplier + fraction;
    *denom = multiplier;
    return OMC_GPS_TRANSLATION_OK;
}

static omc_gps_translation_status
omc_gps_reduce_rational(omc_u64 numer, omc_u64 denom, omc_urational *out)
{
    omc_u64 a;
    omc_u64 b;
    omc_u64 remainder;
    if (denom == 0U)
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    a = numer;
    b = denom;
    while (b != 0U) {
        remainder = a % b;
        a = b;
        b = remainder;
    }
    if (a == 0U)
        a = 1U;
    numer /= a;
    denom /= a;
    if (numer > 0xFFFFFFFFU || denom > 0xFFFFFFFFU)
        return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
    out->numer = (omc_u32)numer;
    out->denom = (omc_u32)denom;
    return OMC_GPS_TRANSLATION_OK;
}

static omc_gps_translation_status
omc_gps_coordinate(const omc_gps_context *context, omc_entry_id id, int latitude,
                   omc_gps_group *group)
{
    omc_const_bytes bytes;
    const omc_u8 *input;
    omc_size size;
    omc_size first;
    omc_size second;
    omc_u64 degrees;
    omc_u64 minutes;
    omc_u64 seconds_numer;
    omc_u64 seconds_denom;
    omc_u64 divisor;
    omc_u64 multiplier;
    omc_gps_translation_status status;

    if (!omc_gps_text_value(context, id, &bytes) || bytes.size == 0U)
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    input = bytes.data;
    size = bytes.size;
    group->reference = (char)input[size - 1U];
    if (latitude ? (group->reference != 'N' && group->reference != 'S')
                 : (group->reference != 'E' && group->reference != 'W'))
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    --size;
    first = size;
    second = size;
    {
        omc_size i;
        for (i = 0U; i < size; ++i) {
            if (input[i] == (omc_u8)',') {
                first = i;
                break;
            }
        }
    }
    if (first == size)
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    status = omc_gps_digits(input, first, &degrees);
    if (status != OMC_GPS_TRANSLATION_OK)
        return status;
    {
        omc_size i;
        for (i = first + 1U; i < size; ++i) {
            if (input[i] == (omc_u8)',') {
                second = i;
                break;
            }
        }
    }
    if (second == size) {
        status = omc_gps_decimal(input + first + 1U, size - first - 1U,
                                 &minutes, &seconds_denom);
        if (status != OMC_GPS_TRANSLATION_OK)
            return status;
        seconds_numer = minutes % seconds_denom;
        minutes /= seconds_denom;
        divisor = 60U;
        {
            omc_u64 remainder;
            omc_u64 a;
            omc_u64 b;
            a = divisor;
            b = seconds_denom;
            while (b != 0U) {
                remainder = a % b;
                a = b;
                b = remainder;
            }
            divisor = a == 0U ? 1U : a;
        }
        seconds_denom /= divisor;
        multiplier = 60U / divisor;
        if (seconds_numer > ~(omc_u64)0 / multiplier)
            return OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION;
        seconds_numer *= multiplier;
    } else {
        status = omc_gps_digits(input + first + 1U, second - first - 1U,
                                 &minutes);
        if (status != OMC_GPS_TRANSLATION_OK)
            return status;
        status = omc_gps_decimal(input + second + 1U, size - second - 1U,
                                 &seconds_numer, &seconds_denom);
        if (status != OMC_GPS_TRANSLATION_OK)
            return status;
    }
    if (degrees > (latitude ? 90U : 180U) || minutes >= 60U ||
        seconds_numer / seconds_denom >= 60U ||
        (degrees == (latitude ? 90U : 180U) &&
         (minutes != 0U || seconds_numer != 0U)))
        return OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE;
    group->components[0].numer = (omc_u32)degrees;
    group->components[0].denom = 1U;
    group->components[1].numer = (omc_u32)minutes;
    group->components[1].denom = 1U;
    return omc_gps_reduce_rational(seconds_numer, seconds_denom,
                                   &group->components[2]);
}

static int
omc_gps_integer(const omc_val *value, omc_u64 *out)
{
    if (value->kind != OMC_VAL_SCALAR || value->count != 1U)
        return 0;
    switch (value->elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_U16:
    case OMC_ELEM_U32:
    case OMC_ELEM_U64:
        *out = value->u.u64;
        return 1;
    case OMC_ELEM_I8:
    case OMC_ELEM_I16:
    case OMC_ELEM_I32:
    case OMC_ELEM_I64:
        if (value->u.i64 >= 0) {
            *out = (omc_u64)value->u.i64;
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static omc_gps_translation_status
omc_gps_unsigned_rational(const omc_gps_context *context, omc_entry_id id,
                           omc_urational *out)
{
    const omc_val *value;
    omc_const_bytes bytes;
    omc_u64 numer;
    omc_u64 denom;
    omc_size slash;
    omc_size i;
    omc_gps_translation_status status;
    value = &context->source->entries[id].value;
    if (!omc_value_shape_valid(value, &context->source->arena))
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    if (value->kind == OMC_VAL_SCALAR) {
        if (value->elem_type == OMC_ELEM_URATIONAL)
            return omc_gps_reduce_rational(value->u.ur.numer,
                                            value->u.ur.denom, out);
        if (!omc_gps_integer(value, &numer))
            return OMC_GPS_TRANSLATION_INVALID_SOURCE;
        return omc_gps_reduce_rational(numer, 1U, out);
    }
    if (value->kind != OMC_VAL_TEXT ||
        (value->text_encoding != OMC_TEXT_ASCII &&
         value->text_encoding != OMC_TEXT_UTF8))
        return OMC_GPS_TRANSLATION_INVALID_SOURCE;
    bytes = omc_arena_view(&context->source->arena, value->u.ref);
    slash = bytes.size;
    for (i = 0U; i < bytes.size; ++i) {
        if (bytes.data[i] == (omc_u8)'/') {
            if (slash != bytes.size)
                return OMC_GPS_TRANSLATION_INVALID_SOURCE;
            slash = i;
        }
    }
    if (slash == bytes.size) {
        status = omc_gps_decimal(bytes.data, bytes.size, &numer, &denom);
    } else {
        status = omc_gps_digits(bytes.data, slash, &numer);
        if (status == OMC_GPS_TRANSLATION_OK)
            status = omc_gps_digits(bytes.data + slash + 1U,
                                    bytes.size - slash - 1U, &denom);
    }
    if (status != OMC_GPS_TRANSLATION_OK)
        return status;
    return omc_gps_reduce_rational(numer, denom, out);
}

static int
omc_gps_native_tag(const omc_gps_context *context, const omc_entry *entry,
                   omc_u16 tag)
{
    return entry->key.kind == OMC_KEY_EXIF_TAG &&
           entry->key.u.exif_tag.tag == tag &&
           omc_gps_ref_equal(&context->source->arena,
                             entry->key.u.exif_tag.ifd, omc_gps_ifd);
}

static int
omc_gps_ratio_equal(omc_urational a, omc_urational b)
{
    return a.denom != 0U && b.denom != 0U &&
           (omc_u64)a.numer * (omc_u64)b.denom ==
               (omc_u64)b.numer * (omc_u64)a.denom;
}

static int
omc_gps_native_matches(const omc_gps_context *context, const omc_val *value,
                       const omc_gps_group *group, omc_size member)
{
    omc_const_bytes bytes;
    omc_u32 i;
    omc_urational actual;
    if (group->mapping == OMC_GPS_TRANSLATE_ALTITUDE && member == 0U)
        return value->kind == OMC_VAL_SCALAR && value->count == 1U &&
               value->elem_type == OMC_ELEM_U8 &&
               value->u.u64 == group->altitude_ref;
    if (group->mapping == OMC_GPS_TRANSLATE_ALTITUDE && member == 1U)
        return value->kind == OMC_VAL_SCALAR && value->count == 1U &&
               value->elem_type == OMC_ELEM_URATIONAL &&
               omc_gps_ratio_equal(value->u.ur, group->magnitude);
    if (member == 0U) {
        if (value->kind != OMC_VAL_TEXT ||
            (value->text_encoding != OMC_TEXT_ASCII &&
             value->text_encoding != OMC_TEXT_UTF8))
            return 0;
        bytes = omc_arena_view(&context->source->arena, value->u.ref);
        if (bytes.size == 2U && bytes.data[1] == 0U)
            return bytes.data[0] == (omc_u8)group->reference;
        return bytes.size == 1U && bytes.data[0] == (omc_u8)group->reference;
    }
    if (value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_URATIONAL ||
        value->count != 3U || !omc_value_shape_valid(value, &context->source->arena))
        return 0;
    bytes = omc_arena_view(&context->source->arena, value->u.ref);
    if (bytes.size != 3U * sizeof(omc_urational))
        return 0;
    for (i = 0U; i < 3U; ++i) {
        memcpy(&actual, bytes.data + (omc_size)i * sizeof(actual), sizeof(actual));
        if (!omc_gps_ratio_equal(actual, group->components[i]))
            return 0;
    }
    return 1;
}

static omc_status
omc_gps_make_value(omc_gps_context *context, const omc_gps_group *group,
                   omc_size member, omc_val *value)
{
    omc_byte_ref ref;
    omc_urational components[3];
    const char *reference;
    if (group->mapping == OMC_GPS_TRANSLATE_ALTITUDE) {
        if (member == 0U)
            omc_val_make_u8(value, group->altitude_ref);
        else
            omc_val_make_urational(value, group->magnitude);
        return OMC_STATUS_OK;
    }
    if (member == 0U) {
        reference = &group->reference;
        if (omc_arena_append(&context->edit.arena, reference, 1U, &ref) !=
            OMC_STATUS_OK)
            return OMC_STATUS_NO_MEMORY;
        omc_val_make_text(value, ref, OMC_TEXT_ASCII);
        return OMC_STATUS_OK;
    }
    components[0] = group->components[0];
    components[1] = group->components[1];
    components[2] = group->components[2];
    if (omc_arena_append(&context->edit.arena, components, sizeof(components),
                         &ref) != OMC_STATUS_OK)
        return OMC_STATUS_NO_MEMORY;
    omc_val_make_array(value, OMC_ELEM_URATIONAL, 3U, ref,
                       OMC_BYTE_ORDER_NATIVE);
    return OMC_STATUS_OK;
}

static omc_status
omc_gps_append_entry(omc_gps_context *context, const omc_gps_group *group,
                     omc_size member, omc_val *value)
{
    omc_entry entry;
    omc_byte_ref ifd;
    omc_const_bytes bytes;
    omc_status status;
    memset(&entry, 0, sizeof(entry));
    status = omc_arena_append(&context->edit.arena, omc_gps_ifd,
                              sizeof(omc_gps_ifd) - 1U, &ifd);
    if (status != OMC_STATUS_OK)
        return status;
    omc_key_make_exif_tag(&entry.key, ifd, group->tags[member]);
    entry.value = *value;
    entry.origin = context->source->entries[group->source[member]].origin;
    bytes = omc_arena_view(&context->source->arena,
                           entry.origin.wire_type_name);
    entry.origin.wire_type_name.offset = 0U;
    entry.origin.wire_type_name.size = 0U;
    if (bytes.size != 0U) {
        status = omc_arena_append(&context->edit.arena, bytes.data, bytes.size,
                                  &entry.origin.wire_type_name);
        if (status != OMC_STATUS_OK)
            return status;
    }
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    return omc_edit_add_entry(&context->edit, &entry);
}

static omc_gps_translation_status
omc_gps_scan_version(omc_gps_context *context, int validate)
{
    omc_size i;
    const omc_entry *entry;
    omc_const_bytes bytes;
    context->version_entry = OMC_INVALID_ENTRY_ID;
    context->version_count = 0U;
    context->version[0] = 2U;
    context->version[1] = 3U;
    context->version[2] = 0U;
    context->version[3] = 0U;
    for (i = 0U; i < context->source->entry_count; ++i) {
        entry = &context->source->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            !omc_gps_native_tag(context, entry, 0U))
            continue;
        ++context->version_count;
        /* The C++ reference selects the last active version entry after it
         * counts duplicates.  Keep the same entry for the single-version
         * validation path and diagnostics. */
        context->version_entry = (omc_entry_id)i;
    }
    if (context->version_count == 0U || !validate)
        return OMC_GPS_TRANSLATION_OK;
    if (context->version_count != 1U)
        return OMC_GPS_TRANSLATION_NATIVE_CONFLICT;
    entry = &context->source->entries[context->version_entry];
    if (entry->value.kind != OMC_VAL_ARRAY || entry->value.elem_type != OMC_ELEM_U8 ||
        entry->value.count != 4U ||
        !omc_value_shape_valid(&entry->value, &context->source->arena))
        return OMC_GPS_TRANSLATION_NATIVE_CONFLICT;
    bytes = omc_arena_view(&context->source->arena, entry->value.u.ref);
    if (bytes.size != 4U)
        return OMC_GPS_TRANSLATION_NATIVE_CONFLICT;
    memcpy(context->version, bytes.data, sizeof(context->version));
    if (context->version[0] != 2U || context->version[1] > 4U ||
        context->version[2] != 0U || context->version[3] != 0U)
        return OMC_GPS_TRANSLATION_UNSUPPORTED_VERSION;
    return OMC_GPS_TRANSLATION_OK;
}

static int
omc_gps_group_removed_tag(const omc_gps_context *context, omc_u16 tag)
{
    omc_size i;
    for (i = 0U; i < 3U; ++i) {
        if (!context->groups[i].apply || context->groups[i].present)
            continue;
        if (context->groups[i].tags[0] == tag ||
            context->groups[i].tags[1] == tag)
            return 1;
    }
    return 0;
}

static omc_gps_translation_status
omc_gps_prepare_groups(omc_gps_context *context)
{
    omc_size i;
    omc_size j;
    omc_size k;
    omc_u32 added;
    omc_u64 operations;
    int existing;
    int exact;
    const omc_entry *entry;
    omc_status status;
    omc_u32 native_tag;
    omc_gps_group *group;
    context->need_version = 0;
    context->removed_group = 0;
    context->version_source = OMC_INVALID_ENTRY_ID;
    added = 0U;
    operations = 0U;
    for (i = 0U; i < 3U; ++i) {
        group = &context->groups[i];
        if (!group->selected)
            continue;
        group->native[0] = OMC_INVALID_ENTRY_ID;
        group->native[1] = OMC_INVALID_ENTRY_ID;
        group->native_count[0] = 0U;
        group->native_count[1] = 0U;
        group->matches[0] = 0;
        group->matches[1] = 0;
        for (j = 0U; j < (i == 2U ? 2U : 2U); ++j) {
            native_tag = group->tags[j];
            for (k = 0U; k < context->source->entry_count; ++k) {
                entry = &context->source->entries[k];
                if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
                    !omc_gps_native_tag(context, entry, (omc_u16)native_tag))
                    continue;
                if (group->native_count[j] >= context->opts.max_operations)
                    return OMC_GPS_TRANSLATION_OPERATION_LIMIT;
                if (group->native_count[j] == 0U) {
                    group->native[j] = (omc_entry_id)k;
                    group->matches[j] = group->present &&
                        omc_gps_native_matches(context, &entry->value, group, j);
                }
                ++group->native_count[j];
            }
        }
        existing = group->native_count[0] != 0U || group->native_count[1] != 0U;
        exact = group->present ?
            (group->native_count[0] == 1U && group->matches[0] &&
             group->native_count[1] == 1U && group->matches[1]) : !existing;
        if (context->opts.conflict == OMC_TRANSLATION_PRESERVE && existing) {
            ++context->res.groups_preserved;
            continue;
        }
        if (context->opts.conflict == OMC_TRANSLATION_FAIL && existing && !exact)
            return OMC_GPS_TRANSLATION_NATIVE_CONFLICT;
        if (exact)
            ++context->res.groups_unchanged;
        else
            group->apply = 1;
        if (group->present) {
            context->need_version = 1;
            context->version_source = group->mapping == OMC_GPS_TRANSLATE_ALTITUDE
                                          ? group->source[1]
                                          : group->source[0];
        } else if (group->apply) {
            context->removed_group = 1;
        }
        if (!group->apply)
            continue;
        for (j = 0U; j < 2U; ++j) {
            if (!group->present)
                operations += group->native_count[j];
            else if (group->native_count[j] == 0U) {
                ++added;
                ++operations;
            } else {
                operations += group->native_count[j] - 1U;
                if (!group->matches[j])
                    ++operations;
            }
        }
    }
    if (context->removed_group) {
        int remove_version;
        remove_version = 1;
        for (i = 0U; i < context->source->entry_count; ++i) {
            entry = &context->source->entries[i];
            if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
                entry->key.kind != OMC_KEY_EXIF_TAG ||
                !omc_gps_ref_equal(&context->source->arena,
                                   entry->key.u.exif_tag.ifd, omc_gps_ifd) ||
                entry->key.u.exif_tag.tag == 0U)
                continue;
            if (!omc_gps_group_removed_tag(context,
                                           entry->key.u.exif_tag.tag)) {
                remove_version = 0;
                break;
            }
        }
        if (context->need_version)
            remove_version = 0;
        if (remove_version) {
            if (context->version_count > context->opts.max_operations)
                return OMC_GPS_TRANSLATION_OPERATION_LIMIT;
            operations += context->version_count;
        }
    }
    if (context->need_version && context->version_count == 0U) {
        ++added;
        ++operations;
    }
    context->added = added;
    context->operations = operations;
    if (added > context->opts.max_added_entries || operations > context->opts.max_operations)
        return added > context->opts.max_added_entries
                   ? OMC_GPS_TRANSLATION_ENTRY_LIMIT
                   : OMC_GPS_TRANSLATION_OPERATION_LIMIT;
    if (operations > (omc_u64)(~(omc_size)0))
        return OMC_GPS_TRANSLATION_OPERATION_LIMIT;
    status = omc_edit_reserve_ops(&context->edit, (omc_size)operations);
    return status == OMC_STATUS_OK ? OMC_GPS_TRANSLATION_OK
                                   : (status == OMC_STATUS_NO_MEMORY
                                          ? OMC_GPS_TRANSLATION_NO_MEMORY
                                          : OMC_GPS_TRANSLATION_INTERNAL);
}

static omc_gps_translation_status
omc_gps_apply(omc_gps_context *context, omc_store *out)
{
    omc_size i;
    omc_size j;
    omc_size k;
    omc_entry *entry;
    omc_val value;
    omc_status status;
    int remove_version;
    for (i = 0U; i < 3U; ++i) {
        omc_gps_group *group;
        group = &context->groups[i];
        if (!group->apply)
            continue;
        for (j = 0U; j < 2U; ++j) {
            for (k = 0U; k < context->source->entry_count; ++k) {
                entry = &context->source->entries[k];
                if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
                    !omc_gps_native_tag(context, entry, group->tags[j]))
                    continue;
                if (!group->present || (omc_entry_id)k != group->native[j]) {
                    status = omc_edit_tombstone(&context->edit, (omc_entry_id)k);
                    if (status != OMC_STATUS_OK)
                        return status == OMC_STATUS_NO_MEMORY
                                   ? OMC_GPS_TRANSLATION_NO_MEMORY
                                   : OMC_GPS_TRANSLATION_INTERNAL;
                    ++context->res.entries_removed;
                } else if (!group->matches[j]) {
                    status = omc_gps_make_value(context, group, j, &value);
                    if (status != OMC_STATUS_OK)
                        return status == OMC_STATUS_NO_MEMORY
                                   ? OMC_GPS_TRANSLATION_NO_MEMORY
                                   : OMC_GPS_TRANSLATION_INTERNAL;
                    status = omc_edit_set_value(&context->edit, (omc_entry_id)k,
                                                &value);
                    if (status != OMC_STATUS_OK)
                        return status == OMC_STATUS_NO_MEMORY
                                   ? OMC_GPS_TRANSLATION_NO_MEMORY
                                   : OMC_GPS_TRANSLATION_INTERNAL;
                    ++context->res.entries_updated;
                }
            }
            if (group->present && group->native_count[j] == 0U) {
                status = omc_gps_make_value(context, group, j, &value);
                if (status != OMC_STATUS_OK)
                    return status == OMC_STATUS_NO_MEMORY
                               ? OMC_GPS_TRANSLATION_NO_MEMORY
                               : OMC_GPS_TRANSLATION_INTERNAL;
                status = omc_gps_append_entry(context, group, j, &value);
                if (status != OMC_STATUS_OK)
                    return status == OMC_STATUS_NO_MEMORY
                               ? OMC_GPS_TRANSLATION_NO_MEMORY
                               : OMC_GPS_TRANSLATION_INTERNAL;
                ++context->res.entries_added;
            }
        }
        ++context->res.groups_translated;
    }
    if (context->need_version && context->version_count == 0U) {
        omc_byte_ref ref;
        omc_entry entry_added;
        omc_const_bytes bytes;
        if (omc_arena_append(&context->edit.arena, context->version,
                             sizeof(context->version), &ref) != OMC_STATUS_OK)
            return OMC_GPS_TRANSLATION_NO_MEMORY;
        omc_val_make_array(&value, OMC_ELEM_U8, 4U, ref, OMC_BYTE_ORDER_NATIVE);
        memset(&entry_added, 0, sizeof(entry_added));
        status = omc_arena_append(&context->edit.arena, omc_gps_ifd,
                                  sizeof(omc_gps_ifd) - 1U, &ref);
        if (status != OMC_STATUS_OK)
            return OMC_GPS_TRANSLATION_NO_MEMORY;
        omc_key_make_exif_tag(&entry_added.key, ref, 0U);
        entry_added.value = value;
        entry_added.origin = context->source->entries[context->version_source].origin;
        bytes = omc_arena_view(&context->source->arena,
                               entry_added.origin.wire_type_name);
        entry_added.origin.wire_type_name.offset = 0U;
        entry_added.origin.wire_type_name.size = 0U;
        if (bytes.size != 0U && omc_arena_append(&context->edit.arena, bytes.data,
                                                 bytes.size,
                                                 &entry_added.origin.wire_type_name) !=
                                    OMC_STATUS_OK)
            return OMC_GPS_TRANSLATION_NO_MEMORY;
        entry_added.flags = OMC_ENTRY_FLAG_DIRTY;
        status = omc_edit_add_entry(&context->edit, &entry_added);
        if (status != OMC_STATUS_OK)
            return status == OMC_STATUS_NO_MEMORY ? OMC_GPS_TRANSLATION_NO_MEMORY
                                                  : OMC_GPS_TRANSLATION_INTERNAL;
        ++context->res.entries_added;
    }
    remove_version = 0;
    if (context->removed_group) {
        remove_version = 1;
        for (i = 0U; i < context->source->entry_count; ++i) {
            entry = &context->source->entries[i];
            if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
                entry->key.kind != OMC_KEY_EXIF_TAG ||
                !omc_gps_ref_equal(&context->source->arena,
                                   entry->key.u.exif_tag.ifd, omc_gps_ifd) ||
                entry->key.u.exif_tag.tag == 0U)
                continue;
            if (!omc_gps_group_removed_tag(context,
                                           entry->key.u.exif_tag.tag)) {
                remove_version = 0;
                break;
            }
        }
        if (context->need_version)
            remove_version = 0;
    }
    if (remove_version) {
        for (i = 0U; i < context->source->entry_count; ++i) {
            entry = &context->source->entries[i];
            if ((entry->flags & OMC_ENTRY_FLAG_DELETED) == 0U &&
                omc_gps_native_tag(context, entry, 0U)) {
                status = omc_edit_tombstone(&context->edit, (omc_entry_id)i);
                if (status != OMC_STATUS_OK)
                    return status == OMC_STATUS_NO_MEMORY
                               ? OMC_GPS_TRANSLATION_NO_MEMORY
                               : OMC_GPS_TRANSLATION_INTERNAL;
                ++context->res.entries_removed;
            }
        }
    }
    if (context->edit.op_count != (omc_size)context->operations)
        return OMC_GPS_TRANSLATION_INTERNAL;
    status = omc_edit_commit(context->source, &context->edit, 1U, out);
    if (status != OMC_STATUS_OK)
        return status == OMC_STATUS_NO_MEMORY ? OMC_GPS_TRANSLATION_NO_MEMORY
                                              : OMC_GPS_TRANSLATION_INTERNAL;
    return OMC_GPS_TRANSLATION_OK;
}

void
omc_gps_translation_opts_init(omc_gps_translation_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->mappings = OMC_GPS_TRANSLATE_ALL;
    opts->conflict = OMC_TRANSLATION_FAIL;
    opts->max_source_properties = 1024U;
    opts->max_added_entries = 7U;
    opts->max_operations = 1024U;
    opts->max_text_bytes_per_property = 128U;
    opts->max_total_text_bytes = 512U;
}

omc_gps_translation_res
omc_translate_xmp_gps(const omc_store *source, omc_store *out,
                      const omc_gps_translation_opts *options)
{
    static const char *const paths[4] = {
        "GPSLatitude", "GPSLongitude", "GPSAltitude", "GPSAltitudeRef"
    };
    omc_gps_context context;
    omc_gps_source_property properties[4];
    omc_size i;
    omc_size j;
    omc_size count;
    omc_entry *entry;
    omc_const_bytes path;
    omc_const_bytes value_bytes;
    omc_gps_translation_status gps_status;
    memset(&context, 0, sizeof(context));
    context.source = source;
    context.res.failed_source = OMC_INVALID_ENTRY_ID;
    context.version_entry = OMC_INVALID_ENTRY_ID;
    for (i = 0U; i < 4U; ++i) {
        properties[i].first = OMC_INVALID_ENTRY_ID;
        properties[i].duplicate = OMC_INVALID_ENTRY_ID;
        properties[i].dirty = 0;
    }
    omc_gps_translation_opts_init(&context.opts);
    if (options != NULL)
        context.opts = *options;
    if (out == NULL)
        return omc_gps_error(&context, OMC_GPS_TRANSLATION_NULL_OUTPUT, 0U,
                             OMC_INVALID_ENTRY_ID);
    if (source == NULL || source == out || !omc_store_shape_valid(source) ||
        !omc_store_shape_valid(out) ||
        (context.opts.mappings & ~(OMC_GPS_TRANSLATE_ALL |
                                   OMC_GPS_TRANSLATE_VERSION)) != 0U ||
        (context.opts.mappings & OMC_GPS_TRANSLATE_ALL) == 0U ||
        context.opts.conflict < OMC_TRANSLATION_PRESERVE ||
        context.opts.conflict > OMC_TRANSLATION_REPLACE ||
        context.opts.max_source_properties == 0U ||
        context.opts.max_source_properties > 65536U ||
        context.opts.max_added_entries == 0U ||
        context.opts.max_added_entries > 7U ||
        context.opts.max_operations == 0U ||
        context.opts.max_operations > 1024U ||
        context.opts.max_text_bytes_per_property == 0U ||
        context.opts.max_text_bytes_per_property > 128U ||
        context.opts.max_total_text_bytes == 0U ||
        context.opts.max_total_text_bytes > 512U ||
        (context.opts.all_sources != 0 && context.opts.all_sources != 1))
        return omc_gps_error(&context, OMC_GPS_TRANSLATION_INVALID_OPTIONS, 0U,
                             OMC_INVALID_ENTRY_ID);
    for (i = 0U; i < source->entry_count; ++i) {
        entry = &source->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY ||
            !omc_gps_ref_equal(&source->arena,
                               entry->key.u.xmp_property.schema_ns,
                               omc_gps_ns_exif) ||
            ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U &&
             (entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U))
            continue;
        path = omc_arena_view(&source->arena,
                              entry->key.u.xmp_property.property_path);
        for (j = 0U; j < 4U; ++j) {
            if (path.size != strlen(paths[j]) ||
                memcmp(path.data, paths[j], path.size) != 0)
                continue;
            if (properties[j].first == OMC_INVALID_ENTRY_ID)
                properties[j].first = (omc_entry_id)i;
            else
                properties[j].duplicate = (omc_entry_id)i;
            if ((entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U)
                properties[j].dirty = 1;
            break;
        }
    }
    memset(context.groups, 0, sizeof(context.groups));
    for (i = 0U; i < 3U; ++i) {
        omc_gps_group *group;
        int enabled;
        int found;
        int dirty;
        group = &context.groups[i];
        group->mapping = i == 0U ? OMC_GPS_TRANSLATE_LATITUDE
                                 : i == 1U ? OMC_GPS_TRANSLATE_LONGITUDE
                                            : OMC_GPS_TRANSLATE_ALTITUDE;
        group->tags[0] = i == 0U ? 1U : i == 1U ? 3U : 5U;
        group->tags[1] = i == 0U ? 2U : i == 1U ? 4U : 6U;
        group->source[0] = OMC_INVALID_ENTRY_ID;
        group->source[1] = OMC_INVALID_ENTRY_ID;
        enabled = (context.opts.mappings & group->mapping) != 0U;
        found = properties[i].first != OMC_INVALID_ENTRY_ID;
        dirty = properties[i].dirty;
        if (i == 2U) {
            found = found || properties[3].first != OMC_INVALID_ENTRY_ID;
            dirty = dirty || properties[3].dirty;
        }
        if (!enabled || !found || (!context.opts.all_sources && !dirty))
            continue;
        group->selected = 1;
        if (properties[i].duplicate != OMC_INVALID_ENTRY_ID)
            return omc_gps_error(&context, OMC_GPS_TRANSLATION_AMBIGUOUS_SOURCE,
                                 group->mapping, properties[i].duplicate);
        if (properties[i].first == OMC_INVALID_ENTRY_ID)
            return omc_gps_error(&context, OMC_GPS_TRANSLATION_INCOMPLETE_SOURCE,
                                 group->mapping, properties[3].first);
        if (i == 2U) {
            if (properties[3].duplicate != OMC_INVALID_ENTRY_ID)
                return omc_gps_error(&context, OMC_GPS_TRANSLATION_AMBIGUOUS_SOURCE,
                                     group->mapping, properties[3].duplicate);
            if (properties[3].first == OMC_INVALID_ENTRY_ID)
                return omc_gps_error(&context, OMC_GPS_TRANSLATION_INCOMPLETE_SOURCE,
                                     group->mapping, properties[i].first);
            group->source[0] = properties[3].first;
            group->source[1] = properties[i].first;
            ++context.res.source_properties;
        } else {
            group->source[0] = properties[i].first;
            group->source[1] = properties[i].first;
        }
        ++context.res.source_properties;
        if ((source->entries[group->source[0]].flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            group->present = 0;
        else
            group->present = 1;
        if (i == 2U &&
            (((source->entries[group->source[1]].flags & OMC_ENTRY_FLAG_DELETED) != 0U) !=
             (!group->present)))
            return omc_gps_error(&context, OMC_GPS_TRANSLATION_INCOMPLETE_SOURCE,
                                 group->mapping, group->source[1]);
        if (!group->present)
            continue;
        if (i < 2U)
            gps_status = omc_gps_coordinate(&context, group->source[0], i == 0U,
                                            group);
        else {
            gps_status = omc_gps_unsigned_rational(&context, group->source[1],
                                                   &group->magnitude);
            if (gps_status == OMC_GPS_TRANSLATION_OK) {
                const omc_val *ref_value;
                omc_u64 reference;
                ref_value = &source->entries[group->source[0]].value;
                reference = 0U;
                if (ref_value->kind == OMC_VAL_TEXT &&
                    (ref_value->text_encoding == OMC_TEXT_ASCII ||
                     ref_value->text_encoding == OMC_TEXT_UTF8)) {
                    value_bytes = omc_arena_view(&source->arena, ref_value->u.ref);
                    if (value_bytes.size == 1U &&
                        (value_bytes.data[0] == (omc_u8)'0' ||
                         value_bytes.data[0] == (omc_u8)'1'))
                        reference = (omc_u64)(value_bytes.data[0] - (omc_u8)'0');
                    else
                        gps_status = OMC_GPS_TRANSLATION_INVALID_SOURCE;
                } else if (!omc_gps_integer(ref_value, &reference) || reference > 1U)
                    gps_status = OMC_GPS_TRANSLATION_INVALID_SOURCE;
                group->altitude_ref = (omc_u8)reference;
            }
        }
        if (gps_status != OMC_GPS_TRANSLATION_OK)
            return omc_gps_error(&context, gps_status, group->mapping,
                                 i == 2U ? group->source[1] : group->source[0]);
    }
    /* Count and validate selected source text after the group shape is known. */
    context.res.source_properties = 0U;
    context.text_bytes = 0U;
    for (i = 0U; i < 3U; ++i) {
        omc_gps_group *group;
        group = &context.groups[i];
        if (!group->selected)
            continue;
        count = i == 2U ? 2U : 1U;
        for (j = 0U; j < count; ++j) {
            const omc_val *source_value;
            source_value = &source->entries[group->source[j]].value;
            ++context.res.source_properties;
            if (!group->present || source_value->kind != OMC_VAL_TEXT)
                continue;
            if (source_value->u.ref.size > context.opts.max_text_bytes_per_property)
                return omc_gps_error(&context, OMC_GPS_TRANSLATION_VALUE_TOO_LONG,
                                     group->mapping, group->source[j]);
            if (context.text_bytes > context.opts.max_total_text_bytes -
                                         source_value->u.ref.size)
                return omc_gps_error(&context, OMC_GPS_TRANSLATION_SOURCE_LIMIT,
                                     group->mapping, group->source[j]);
            context.text_bytes += source_value->u.ref.size;
        }
    }
    {
        int selected_group;
        int active_selected;
        selected_group = 0;
        active_selected = 0;
        context.need_version = 0;
        for (i = 0U; i < 3U; ++i) {
            if (context.groups[i].selected)
                selected_group = 1;
            if (context.groups[i].selected && context.groups[i].present)
                active_selected = 1;
            if (context.groups[i].selected && context.groups[i].present)
                context.need_version = 1;
        }
        gps_status = selected_group ? omc_gps_scan_version(&context,
                                                            active_selected)
                                    : OMC_GPS_TRANSLATION_OK;
    }
    if (gps_status != OMC_GPS_TRANSLATION_OK) {
        return omc_gps_error(&context, gps_status, OMC_GPS_TRANSLATE_VERSION,
                             context.version_entry);
    }
    /* GPS 2.4 stores altitude reference as 2/3.  The XMP contract remains
     * 0/1, so adjust only after the native version has been validated. */
    for (i = 0U; i < 3U; ++i) {
        if (context.groups[i].selected && context.groups[i].present &&
            context.groups[i].mapping == OMC_GPS_TRANSLATE_ALTITUDE &&
            context.version[1] == 4U)
            context.groups[i].altitude_ref =
                (omc_u8)(context.groups[i].altitude_ref + 2U);
    }
    omc_edit_init(&context.edit);
    gps_status = omc_gps_prepare_groups(&context);
    if (gps_status != OMC_GPS_TRANSLATION_OK) {
        omc_edit_fini(&context.edit);
        return omc_gps_error(&context, gps_status, 0U,
                             OMC_INVALID_ENTRY_ID);
    }
    gps_status = omc_gps_apply(&context, out);
    omc_edit_fini(&context.edit);
    if (gps_status != OMC_GPS_TRANSLATION_OK)
        return omc_gps_error(&context, gps_status, 0U,
                             OMC_INVALID_ENTRY_ID);
    context.res.status = OMC_GPS_TRANSLATION_OK;
    context.res.failed_mapping = 0U;
    context.res.failed_source = OMC_INVALID_ENTRY_ID;
    return context.res;
}
