#include "omc/omc_translation.h"
#include "../core/omc_value_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct omc_iptc_date_pair_descriptor {
    omc_u16 date_dataset;
    omc_u16 time_dataset;
    omc_u32 mapping;
    const char *schema;
    const char *path;
} omc_iptc_date_pair_descriptor;

typedef struct omc_iptc_date_parts {
    omc_u16 year;
    omc_u8 month;
    omc_u8 day;
    omc_u8 precision;
} omc_iptc_date_parts;

typedef struct omc_iptc_time_parts {
    omc_u8 hour;
    omc_u8 minute;
    omc_u8 second;
    int has_offset;
    char offset_sign;
    omc_u8 offset_hour;
    omc_u8 offset_minute;
} omc_iptc_time_parts;

typedef enum omc_iptc_date_action {
    OMC_IPTC_DATE_ACTION_NONE = 0,
    OMC_IPTC_DATE_ACTION_ADD,
    OMC_IPTC_DATE_ACTION_UPDATE,
    OMC_IPTC_DATE_ACTION_REMOVE,
    OMC_IPTC_DATE_ACTION_PRESERVE
} omc_iptc_date_action;

typedef struct omc_iptc_date_pair_plan {
    const omc_iptc_date_pair_descriptor *descriptor;
    int enabled;
    int selected;
    int date_active;
    int time_active;
    int date_tombstone;
    int time_tombstone;
    omc_entry_id date_id;
    omc_entry_id time_id;
    omc_block_id date_block;
    omc_u32 date_order;
    omc_const_bytes date_text;
    omc_const_bytes time_text;
    omc_text_encoding date_encoding;
    omc_text_encoding time_encoding;
    int destination_root;
    int destination_present;
    omc_entry_id destination_id;
    omc_const_bytes destination_text;
    omc_text_encoding destination_encoding;
    int has_output;
    char output[25];
    omc_size output_size;
    omc_iptc_date_action action;
} omc_iptc_date_pair_plan;

static const char omc_iptc_date_photoshop_ns[] =
    "http://ns.adobe.com/photoshop/1.0/";
static const char omc_iptc_date_xmp_ns[] = "http://ns.adobe.com/xap/1.0/";
static const omc_iptc_date_pair_descriptor omc_iptc_date_pairs[2] = {
    {55U, 60U, OMC_TRANSLATE_DATE_CREATED, omc_iptc_date_photoshop_ns,
     "DateCreated"},
    {62U, 63U, OMC_TRANSLATE_CREATE_IPTC, omc_iptc_date_xmp_ns, "CreateDate"}
};

static omc_iptc_date_to_xmp_translation_res
omc_iptc_date_failure(omc_iptc_date_to_xmp_translation_status status,
                      omc_u32 mapping, omc_entry_id source)
{
    omc_iptc_date_to_xmp_translation_res result;
    memset(&result, 0, sizeof(result));
    result.status = status;
    result.failed_mapping = mapping;
    result.failed_source = source;
    return result;
}

static int
omc_iptc_date_options_valid(const omc_iptc_date_to_xmp_translation_opts *opts)
{
    return (opts->created == 0 || opts->created == 1) &&
           (opts->digital_creation == 0 || opts->digital_creation == 1) &&
           (opts->all_sources == 0 || opts->all_sources == 1) &&
           (opts->conflict == OMC_TRANSLATION_PRESERVE ||
            opts->conflict == OMC_TRANSLATION_FAIL ||
            opts->conflict == OMC_TRANSLATION_REPLACE) &&
           opts->max_source_properties > 0U &&
           opts->max_source_properties <=
               OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_SOURCE_PROPERTIES &&
           opts->max_added_entries > 0U &&
           opts->max_added_entries <=
               OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_ADDED_ENTRIES &&
           opts->max_operations > 0U &&
           opts->max_operations <=
               OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_OPERATIONS &&
           opts->max_total_text_bytes > 0U &&
           opts->max_total_text_bytes <=
               OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES;
}

static int
omc_iptc_date_find_pair(const omc_entry *entry,
                        omc_iptc_date_pair_plan plans[2],
                        omc_u32 *pair_index, int *is_date)
{
    omc_u32 i;
    omc_u16 dataset;
    if (entry->key.kind != OMC_KEY_IPTC_DATASET ||
        entry->key.u.iptc_dataset.record != 2U)
        return 0;
    dataset = entry->key.u.iptc_dataset.dataset;
    for (i = 0U; i < 2U; ++i) {
        if (!plans[i].enabled)
            continue;
        if (dataset == plans[i].descriptor->date_dataset) {
            *pair_index = i;
            *is_date = 1;
            return 1;
        }
        if (dataset == plans[i].descriptor->time_dataset) {
            *pair_index = i;
            *is_date = 0;
            return 1;
        }
    }
    return 0;
}

static int
omc_iptc_date_add_budget(omc_u64 *total, omc_size amount,
                         const omc_iptc_date_to_xmp_translation_opts *opts)
{
    omc_u64 bytes;
    bytes = (omc_u64)amount;
    if (bytes > opts->max_total_text_bytes ||
        *total > opts->max_total_text_bytes - bytes ||
        bytes > OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES ||
        *total > OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES - bytes)
        return 0;
    *total += bytes;
    return 1;
}

static int
omc_iptc_date_source_flags_valid(const omc_entry *entry)
{
    const omc_entry_flags known = OMC_ENTRY_FLAG_DERIVED |
                                  OMC_ENTRY_FLAG_TRUNCATED |
                                  OMC_ENTRY_FLAG_UNREADABLE |
                                  OMC_ENTRY_FLAG_DIRTY |
                                  OMC_ENTRY_FLAG_DELETED |
                                  OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
    return (entry->flags & ~known) == 0U;
}

static omc_iptc_date_to_xmp_translation_status
omc_iptc_date_read_active(const omc_store *source, const omc_entry *entry,
                          omc_const_bytes *text,
                          omc_text_encoding *encoding)
{
    omc_byte_ref ref;
    if ((entry->flags & (OMC_ENTRY_FLAG_TRUNCATED |
                         OMC_ENTRY_FLAG_UNREADABLE)) != 0U)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
    if (entry->origin.block != OMC_INVALID_BLOCK_ID &&
        entry->origin.block >= source->block_count)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
    if (entry->value.kind != OMC_VAL_BYTES && entry->value.kind != OMC_VAL_TEXT)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
    if (entry->value.kind == OMC_VAL_TEXT &&
        entry->value.text_encoding != OMC_TEXT_ASCII &&
        entry->value.text_encoding != OMC_TEXT_UTF8)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
    ref = entry->value.u.ref;
    if (!omc_ref_valid(&source->arena, ref) || entry->value.count != ref.size)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
    text->data = ref.size == 0U ? NULL : source->arena.data + ref.offset;
    text->size = ref.size;
    *encoding = entry->value.kind == OMC_VAL_TEXT
                    ? entry->value.text_encoding
                    : OMC_TEXT_UNKNOWN;
    return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
}

static int
omc_iptc_date_parse_two(omc_const_bytes text, omc_size offset, omc_u8 *out)
{
    omc_u8 a;
    omc_u8 b;
    if (offset + 1U >= text.size)
        return 0;
    a = text.data[offset];
    b = text.data[offset + 1U];
    if (a < (omc_u8)'0' || a > (omc_u8)'9' ||
        b < (omc_u8)'0' || b > (omc_u8)'9')
        return 0;
    *out = (omc_u8)(((omc_u32)(a - (omc_u8)'0') * 10U) +
                    (omc_u32)(b - (omc_u8)'0'));
    return 1;
}

static int
omc_iptc_date_parse_four(omc_const_bytes text, omc_size offset, omc_u16 *out)
{
    omc_size i;
    omc_u16 value;
    omc_u8 digit;
    if (offset + 3U >= text.size)
        return 0;
    value = 0U;
    for (i = 0U; i < 4U; ++i) {
        digit = text.data[offset + i];
        if (digit < (omc_u8)'0' || digit > (omc_u8)'9')
            return 0;
        value = (omc_u16)(value * 10U + (omc_u16)(digit - (omc_u8)'0'));
    }
    *out = value;
    return 1;
}

static omc_u8
omc_iptc_date_days_in_month(omc_u16 year, omc_u8 month)
{
    switch (month) {
    case 1U: case 3U: case 5U: case 7U: case 8U: case 10U: case 12U:
        return 31U;
    case 4U: case 6U: case 9U: case 11U:
        return 30U;
    case 2U:
        return ((year % 400U) == 0U ||
                ((year % 4U) == 0U && (year % 100U) != 0U)) ? 29U : 28U;
    default:
        return 0U;
    }
}

static omc_iptc_date_to_xmp_translation_status
omc_iptc_date_parse_date(omc_const_bytes text, omc_iptc_date_parts *date)
{
    if (text.size != 8U || !omc_iptc_date_parse_four(text, 0U, &date->year) ||
        !omc_iptc_date_parse_two(text, 4U, &date->month) ||
        !omc_iptc_date_parse_two(text, 6U, &date->day))
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME;
    if (date->month > 12U || date->day > 31U)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME;
    if (date->year == 0U)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION;
    if (date->month == 0U) {
        if (date->day != 0U)
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION;
        date->precision = 1U;
    } else if (date->day == 0U) {
        date->precision = 2U;
    } else {
        if (date->day > omc_iptc_date_days_in_month(date->year, date->month))
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME;
        date->precision = 3U;
    }
    return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
}

static omc_iptc_date_to_xmp_translation_status
omc_iptc_date_parse_time(omc_const_bytes text, omc_iptc_time_parts *time)
{
    if ((text.size != 6U && text.size != 11U) ||
        !omc_iptc_date_parse_two(text, 0U, &time->hour) ||
        !omc_iptc_date_parse_two(text, 2U, &time->minute) ||
        !omc_iptc_date_parse_two(text, 4U, &time->second))
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME;
    if (time->hour > 23U || time->minute > 59U || time->second > 60U)
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME;
    if (text.size == 11U) {
        time->offset_sign = (char)text.data[6];
        if ((time->offset_sign != '+' && time->offset_sign != '-') ||
            !omc_iptc_date_parse_two(text, 7U, &time->offset_hour) ||
            !omc_iptc_date_parse_two(text, 9U, &time->offset_minute) ||
            time->offset_hour > 23U || time->offset_minute > 59U)
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_DATETIME;
        time->has_offset = 1;
    }
    return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
}

static int
omc_iptc_date_ascii_hint_valid(omc_const_bytes text,
                               omc_text_encoding encoding)
{
    omc_size i;
    if (encoding != OMC_TEXT_ASCII)
        return 1;
    for (i = 0U; i < text.size; ++i)
        if (text.data[i] >= 0x80U)
            return 0;
    return 1;
}

static void
omc_iptc_date_append_two(char out[25], omc_size *size, omc_u8 value)
{
    out[*size] = (char)('0' + value / 10U);
    ++*size;
    out[*size] = (char)('0' + value % 10U);
    ++*size;
}

static void
omc_iptc_date_append_four(char out[25], omc_size *size, omc_u16 value)
{
    out[*size] = (char)('0' + (value / 1000U) % 10U);
    ++*size;
    out[*size] = (char)('0' + (value / 100U) % 10U);
    ++*size;
    out[*size] = (char)('0' + (value / 10U) % 10U);
    ++*size;
    out[*size] = (char)('0' + value % 10U);
    ++*size;
}

static void
omc_iptc_date_format(omc_iptc_date_pair_plan *plan,
                     const omc_iptc_date_parts *date,
                     const omc_iptc_time_parts *time)
{
    omc_size size;
    size = 0U;
    omc_iptc_date_append_four(plan->output, &size, date->year);
    if (date->precision >= 2U) {
        plan->output[size++] = '-';
        omc_iptc_date_append_two(plan->output, &size, date->month);
    }
    if (date->precision == 3U) {
        plan->output[size++] = '-';
        omc_iptc_date_append_two(plan->output, &size, date->day);
    }
    if (time != NULL) {
        plan->output[size++] = 'T';
        omc_iptc_date_append_two(plan->output, &size, time->hour);
        plan->output[size++] = ':';
        omc_iptc_date_append_two(plan->output, &size, time->minute);
        plan->output[size++] = ':';
        omc_iptc_date_append_two(plan->output, &size, time->second);
        if (time->has_offset) {
            plan->output[size++] = time->offset_sign;
            omc_iptc_date_append_two(plan->output, &size, time->offset_hour);
            plan->output[size++] = ':';
            omc_iptc_date_append_two(plan->output, &size,
                                     time->offset_minute);
        }
    }
    plan->output_size = size;
    plan->has_output = 1;
}

static omc_iptc_date_to_xmp_translation_status
omc_iptc_date_prepare_output(omc_iptc_date_pair_plan *plan,
                             omc_entry_id *failed_source)
{
    omc_iptc_date_parts date;
    omc_iptc_time_parts time;
    omc_iptc_date_to_xmp_translation_status status;
    const omc_iptc_time_parts *time_ptr;
    if (!plan->date_active) {
        if (plan->time_active) {
            *failed_source = plan->time_id;
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INCOMPLETE_SOURCE;
        }
        if (plan->date_tombstone) {
            plan->has_output = 0;
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
        }
        if (plan->time_tombstone) {
            *failed_source = plan->time_id;
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INCOMPLETE_SOURCE;
        }
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INCOMPLETE_SOURCE;
    }
    if (!omc_iptc_date_ascii_hint_valid(plan->date_text,
                                       plan->date_encoding)) {
        *failed_source = plan->date_id;
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
    }
    status = omc_iptc_date_parse_date(plan->date_text, &date);
    if (status != OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK) {
        *failed_source = plan->date_id;
        return status;
    }
    time_ptr = NULL;
    if (plan->time_active) {
        memset(&time, 0, sizeof(time));
        if (!omc_iptc_date_ascii_hint_valid(plan->time_text,
                                           plan->time_encoding)) {
            *failed_source = plan->time_id;
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE;
        }
        status = omc_iptc_date_parse_time(plan->time_text, &time);
        if (status != OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK) {
            *failed_source = plan->time_id;
            return status;
        }
        if (date.precision != 3U) {
            *failed_source = plan->time_id;
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_PRECISION;
        }
        time_ptr = &time;
    }
    omc_iptc_date_format(plan, &date, time_ptr);
    return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
}

static int
omc_iptc_date_schema_matches(const omc_store *store, const omc_entry *entry,
                             const omc_iptc_date_pair_descriptor *descriptor)
{
    omc_const_bytes schema;
    omc_size size;
    if (entry->key.kind != OMC_KEY_XMP_PROPERTY ||
        !omc_ref_valid(&store->arena, entry->key.u.xmp_property.schema_ns))
        return 0;
    schema = omc_arena_view(&store->arena,
                            entry->key.u.xmp_property.schema_ns);
    size = strlen(descriptor->schema);
    return schema.size == size &&
           (size == 0U || memcmp(schema.data, descriptor->schema, size) == 0);
}

static int
omc_iptc_date_path_match(omc_const_bytes path, const char *root, int *exact)
{
    omc_size root_size;
    root_size = strlen(root);
    if (path.size < root_size || memcmp(path.data, root, root_size) != 0)
        return 0;
    *exact = path.size == root_size;
    if (*exact)
        return 1;
    return path.data[root_size] == '[' || path.data[root_size] == '/';
}

static omc_u32
omc_iptc_date_representative_source(const omc_iptc_date_pair_plan *plan)
{
    if (plan->date_active)
        return plan->date_id;
    if (plan->time_active)
        return plan->time_id;
    if (plan->date_tombstone)
        return plan->date_id;
    if (plan->time_tombstone)
        return plan->time_id;
    return OMC_INVALID_ENTRY_ID;
}

static int
omc_iptc_date_logical_text_valid(omc_const_bytes text, int ascii)
{
    omc_size i;
    if (!omc_utf8_valid(text.data, text.size, ascii))
        return 0;
    for (i = 0U; i < text.size; ++i) {
        if (text.data[i] < 0x20U && text.data[i] != 0x09U &&
            text.data[i] != 0x0AU && text.data[i] != 0x0DU)
            return 0;
    }
    if (text.size >= 3U) {
        for (i = 0U; i + 2U < text.size; ++i)
            if (text.data[i] == 0xEFU && text.data[i + 1U] == 0xBFU &&
                (text.data[i + 2U] == 0xBEU || text.data[i + 2U] == 0xBFU))
                return 0;
    }
    return 1;
}

static omc_iptc_date_to_xmp_translation_status
omc_iptc_date_inspect_destinations(
    const omc_store *source, omc_iptc_date_pair_plan plans[2],
    const omc_iptc_date_to_xmp_translation_opts *opts, omc_u64 *text_budget,
    omc_u32 *failed_mapping, omc_entry_id *failed_source)
{
    omc_size i;
    omc_u32 p;
    omc_entry *entry;
    omc_byte_ref ref;
    omc_const_bytes path;
    omc_const_bytes text;
    int exact;
    for (i = 0U; i < source->entry_count; ++i) {
        entry = &source->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        for (p = 0U; p < 2U; ++p) {
            omc_iptc_date_pair_plan *plan = &plans[p];
            if (!plan->selected ||
                !omc_iptc_date_schema_matches(source, entry,
                                               plan->descriptor))
                continue;
            ref = entry->key.u.xmp_property.property_path;
            if (!omc_ref_valid(&source->arena, ref)) {
                *failed_mapping = plan->descriptor->mapping;
                *failed_source = omc_iptc_date_representative_source(plan);
                return OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
            }
            path = omc_arena_view(&source->arena, ref);
            if (!omc_iptc_date_path_match(path, plan->descriptor->path, &exact))
                continue;
            plan->destination_root = 1;
            if (opts->conflict == OMC_TRANSLATION_PRESERVE)
                continue;
            if (!exact || plan->destination_present) {
                *failed_mapping = plan->descriptor->mapping;
                *failed_source = omc_iptc_date_representative_source(plan);
                return OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
            }
            plan->destination_present = 1;
            plan->destination_id = (omc_entry_id)i;
            if (!omc_iptc_date_source_flags_valid(entry) ||
                (entry->flags & (OMC_ENTRY_FLAG_TRUNCATED |
                                 OMC_ENTRY_FLAG_UNREADABLE)) != 0U ||
                entry->value.kind != OMC_VAL_TEXT ||
                (entry->value.text_encoding != OMC_TEXT_ASCII &&
                 entry->value.text_encoding != OMC_TEXT_UTF8) ||
                !omc_ref_valid(&source->arena, entry->value.u.ref) ||
                entry->value.count != entry->value.u.ref.size) {
                *failed_mapping = plan->descriptor->mapping;
                *failed_source = omc_iptc_date_representative_source(plan);
                return OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE;
            }
            ref = entry->value.u.ref;
            if (!omc_iptc_date_add_budget(text_budget, ref.size, opts)) {
                *failed_mapping = plan->descriptor->mapping;
                *failed_source = omc_iptc_date_representative_source(plan);
                return OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT;
            }
            text.data = ref.size == 0U ? NULL :
                        source->arena.data + ref.offset;
            text.size = ref.size;
            plan->destination_text = text;
            plan->destination_encoding = entry->value.text_encoding;
        }
    }
    return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
}

static omc_iptc_date_to_xmp_translation_status
omc_iptc_date_plan_action(
    omc_iptc_date_pair_plan *plan,
    const omc_iptc_date_to_xmp_translation_opts *opts,
    omc_u32 *failed_mapping, omc_entry_id *failed_source)
{
    if (opts->conflict == OMC_TRANSLATION_PRESERVE &&
        plan->destination_root) {
        plan->action = OMC_IPTC_DATE_ACTION_PRESERVE;
        return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
    }
    if (plan->has_output && plan->destination_present) {
        if (plan->output_size == plan->destination_text.size &&
            memcmp(plan->output, plan->destination_text.data,
                   plan->output_size) == 0) {
            plan->action = OMC_IPTC_DATE_ACTION_NONE;
        } else if (opts->conflict == OMC_TRANSLATION_FAIL) {
            *failed_mapping = plan->descriptor->mapping;
            *failed_source =
                omc_iptc_date_representative_source(plan);
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_DESTINATION_CONFLICT;
        } else {
            plan->action = OMC_IPTC_DATE_ACTION_UPDATE;
        }
    } else if (plan->has_output) {
        plan->action = OMC_IPTC_DATE_ACTION_ADD;
    } else if (plan->destination_present) {
        if (opts->conflict == OMC_TRANSLATION_FAIL) {
            *failed_mapping = plan->descriptor->mapping;
            *failed_source =
                omc_iptc_date_representative_source(plan);
            return OMC_IPTC_DATE_TO_XMP_TRANSLATION_DESTINATION_CONFLICT;
        }
        plan->action = OMC_IPTC_DATE_ACTION_REMOVE;
    }
    return OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
}

static omc_status
omc_iptc_date_snapshot(const omc_store *source, omc_store *built,
                       omc_size entry_capacity, omc_size arena_capacity)
{
    omc_status status;
    omc_store_init(built);
    status = omc_store_reserve_entries(built, entry_capacity);
    if (status != OMC_STATUS_OK)
        return status;
    status = omc_store_reserve_blocks(built, source->block_count);
    if (status != OMC_STATUS_OK)
        return status;
    status = omc_arena_reserve(&built->arena, arena_capacity);
    if (status != OMC_STATUS_OK)
        return status;
    if (source->entry_count != 0U)
        memcpy(built->entries, source->entries,
               source->entry_count * sizeof(*source->entries));
    if (source->block_count != 0U)
        memcpy(built->blocks, source->blocks,
               source->block_count * sizeof(*source->blocks));
    if (source->arena.size != 0U)
        memcpy(built->arena.data, source->arena.data, source->arena.size);
    built->entry_count = source->entry_count;
    built->block_count = source->block_count;
    built->arena.size = source->arena.size;
    return OMC_STATUS_OK;
}

static omc_status
omc_iptc_date_apply_action(omc_store *built,
                           const omc_iptc_date_pair_plan *plan)
{
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref path_ref;
    omc_byte_ref value_ref;
    omc_entry_id added_id;
    omc_status status;
    if (plan->action == OMC_IPTC_DATE_ACTION_NONE ||
        plan->action == OMC_IPTC_DATE_ACTION_PRESERVE)
        return OMC_STATUS_OK;
    if (plan->action == OMC_IPTC_DATE_ACTION_REMOVE) {
        built->entries[plan->destination_id].flags |=
            OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
        return OMC_STATUS_OK;
    }
    if (plan->action == OMC_IPTC_DATE_ACTION_UPDATE) {
        status = omc_arena_append(&built->arena, plan->output,
                                  plan->output_size, &value_ref);
        if (status != OMC_STATUS_OK)
            return status;
        omc_val_make_text(&built->entries[plan->destination_id].value,
                          value_ref, OMC_TEXT_UTF8);
        built->entries[plan->destination_id].flags |= OMC_ENTRY_FLAG_DIRTY;
        return OMC_STATUS_OK;
    }
    status = omc_arena_append(&built->arena, plan->descriptor->schema,
                              strlen(plan->descriptor->schema), &schema_ref);
    if (status != OMC_STATUS_OK)
        return status;
    status = omc_arena_append(&built->arena, plan->descriptor->path,
                              strlen(plan->descriptor->path), &path_ref);
    if (status != OMC_STATUS_OK)
        return status;
    status = omc_arena_append(&built->arena, plan->output, plan->output_size,
                              &value_ref);
    if (status != OMC_STATUS_OK)
        return status;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, path_ref);
    omc_val_make_text(&entry.value, value_ref, OMC_TEXT_UTF8);
    entry.origin.block = plan->date_block;
    entry.origin.order_in_block = plan->date_order;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    return omc_store_add_entry(built, &entry, &added_id);
}

void
omc_iptc_date_to_xmp_translation_opts_init(
    omc_iptc_date_to_xmp_translation_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->created = 1;
    opts->digital_creation = 1;
    opts->all_sources = 1;
    opts->conflict = OMC_TRANSLATION_FAIL;
    opts->max_source_properties =
        OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_SOURCE_PROPERTIES;
    opts->max_added_entries =
        OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_ADDED_ENTRIES;
    opts->max_operations = OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_OPERATIONS;
    opts->max_total_text_bytes =
        OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_TOTAL_TEXT_BYTES;
}

omc_iptc_date_to_xmp_translation_res
omc_translate_iptc_dates_to_xmp(
    const omc_store *source, omc_store *out,
    const omc_iptc_date_to_xmp_translation_opts *options)
{
    omc_iptc_date_to_xmp_translation_res result;
    omc_iptc_date_to_xmp_translation_opts opts;
    omc_iptc_date_pair_plan plans[2];
    omc_store built;
    omc_status status;
    omc_size i;
    omc_size extra_arena;
    omc_size final_arena;
    omc_size final_entries;
    omc_u32 p;
    omc_u32 pair_index;
    omc_u32 source_properties;
    omc_u32 operation_count;
    omc_u32 additions;
    omc_u32 updates;
    omc_u32 removals;
    omc_u64 text_budget;
    omc_u32 failed_mapping;
    omc_entry_id failed_source;
    int is_date;
    int deleted;
    int dirty;
    omc_iptc_date_to_xmp_translation_status date_status;
    int built_moved;

    result = omc_iptc_date_failure(OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK, 0U,
                                  OMC_INVALID_ENTRY_ID);
    if (out == NULL)
        return omc_iptc_date_failure(
            OMC_IPTC_DATE_TO_XMP_TRANSLATION_NULL_OUTPUT, 0U,
            OMC_INVALID_ENTRY_ID);
    if (source == NULL || !omc_store_shape_valid(source))
        return omc_iptc_date_failure(
            OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE, 0U,
            OMC_INVALID_ENTRY_ID);
    if (!omc_store_shape_valid(out))
        return omc_iptc_date_failure(OMC_IPTC_DATE_TO_XMP_TRANSLATION_INTERNAL,
                                     0U, OMC_INVALID_ENTRY_ID);
    omc_iptc_date_to_xmp_translation_opts_init(&opts);
    if (options != NULL)
        opts = *options;
    if (!omc_iptc_date_options_valid(&opts))
        return omc_iptc_date_failure(
            OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_OPTIONS, 0U,
            OMC_INVALID_ENTRY_ID);

    memset(plans, 0, sizeof(plans));
    for (p = 0U; p < 2U; ++p) {
        plans[p].descriptor = &omc_iptc_date_pairs[p];
        plans[p].enabled = p == 0U ? opts.created : opts.digital_creation;
        plans[p].date_id = OMC_INVALID_ENTRY_ID;
        plans[p].time_id = OMC_INVALID_ENTRY_ID;
        plans[p].destination_id = OMC_INVALID_ENTRY_ID;
    }

    for (i = 0U; i < source->entry_count; ++i) {
        if (!omc_iptc_date_find_pair(&source->entries[i], plans,
                                     &pair_index, &is_date))
            continue;
        deleted = (source->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U;
        dirty = (source->entries[i].flags & OMC_ENTRY_FLAG_DIRTY) != 0U;
        if (dirty || (opts.all_sources && !deleted))
            plans[pair_index].selected = 1;
        (void)is_date;
    }

    source_properties = 0U;
    text_budget = 0U;
    for (i = 0U; i < source->entry_count; ++i) {
        omc_iptc_date_pair_plan *plan;
        omc_entry *entry;
        omc_const_bytes active_text;
        omc_text_encoding active_encoding;
        entry = &source->entries[i];
        if (!omc_iptc_date_find_pair(entry, plans, &pair_index, &is_date))
            continue;
        plan = &plans[pair_index];
        if (!plan->selected)
            continue;
        deleted = (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U;
        dirty = (entry->flags & OMC_ENTRY_FLAG_DIRTY) != 0U;
        if (deleted && !dirty)
            continue;
        if (source_properties >= opts.max_source_properties)
            return omc_iptc_date_failure(
                OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT,
                plan->descriptor->mapping, (omc_entry_id)i);
        ++source_properties;
        if (!omc_iptc_date_source_flags_valid(entry))
            return omc_iptc_date_failure(
                OMC_IPTC_DATE_TO_XMP_TRANSLATION_INVALID_SOURCE_VALUE,
                plan->descriptor->mapping, (omc_entry_id)i);
        if (deleted) {
            if (is_date) {
                plan->date_tombstone = 1;
                if (plan->date_id == OMC_INVALID_ENTRY_ID)
                    plan->date_id = (omc_entry_id)i;
            } else {
                plan->time_tombstone = 1;
                if (plan->time_id == OMC_INVALID_ENTRY_ID)
                    plan->time_id = (omc_entry_id)i;
            }
            continue;
        }
        if ((is_date && plan->date_active) || (!is_date && plan->time_active))
            return omc_iptc_date_failure(
                OMC_IPTC_DATE_TO_XMP_TRANSLATION_AMBIGUOUS_SOURCE,
                plan->descriptor->mapping, (omc_entry_id)i);
        date_status = omc_iptc_date_read_active(source, entry, &active_text,
                                                 &active_encoding);
        if (date_status != OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK)
            return omc_iptc_date_failure(date_status, plan->descriptor->mapping,
                                         (omc_entry_id)i);
        if (!omc_iptc_date_add_budget(&text_budget, active_text.size, &opts))
            return omc_iptc_date_failure(
                OMC_IPTC_DATE_TO_XMP_TRANSLATION_SOURCE_LIMIT,
                plan->descriptor->mapping, (omc_entry_id)i);
        if (is_date) {
            plan->date_active = 1;
            plan->date_id = (omc_entry_id)i;
            plan->date_block = entry->origin.block;
            plan->date_order = entry->origin.order_in_block;
            plan->date_text = active_text;
            plan->date_encoding = active_encoding;
        } else {
            plan->time_active = 1;
            plan->time_id = (omc_entry_id)i;
            plan->time_text = active_text;
            plan->time_encoding = active_encoding;
        }
    }

    for (p = 0U; p < 2U; ++p) {
        if (!plans[p].selected)
            continue;
        if (plans[p].date_active && plans[p].time_active &&
            plans[p].date_block != source->entries[plans[p].time_id].origin.block)
            return omc_iptc_date_failure(
                OMC_IPTC_DATE_TO_XMP_TRANSLATION_AMBIGUOUS_SOURCE,
                plans[p].descriptor->mapping, plans[p].time_id);
    }

    failed_mapping = 0U;
    failed_source = OMC_INVALID_ENTRY_ID;
    date_status = omc_iptc_date_inspect_destinations(
        source, plans, &opts, &text_budget, &failed_mapping, &failed_source);
    if (date_status != OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK)
        return omc_iptc_date_failure(date_status, failed_mapping, failed_source);

    for (p = 0U; p < 2U; ++p) {
        if (!plans[p].selected)
            continue;
        failed_source = OMC_INVALID_ENTRY_ID;
        date_status = omc_iptc_date_prepare_output(&plans[p], &failed_source);
        if (date_status != OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK)
            return omc_iptc_date_failure(date_status,
                                         plans[p].descriptor->mapping,
                                         failed_source);
        if (plans[p].destination_present &&
            !omc_iptc_date_logical_text_valid(
                plans[p].destination_text,
                plans[p].destination_encoding == OMC_TEXT_ASCII))
            return omc_iptc_date_failure(
                OMC_IPTC_DATE_TO_XMP_TRANSLATION_UNSUPPORTED_DESTINATION_SHAPE,
                plans[p].descriptor->mapping,
                omc_iptc_date_representative_source(&plans[p]));
        failed_mapping = 0U;
        failed_source = OMC_INVALID_ENTRY_ID;
        date_status = omc_iptc_date_plan_action(
            &plans[p], &opts, &failed_mapping, &failed_source);
        if (date_status != OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK)
            return omc_iptc_date_failure(date_status, failed_mapping,
                                         failed_source);
    }

    additions = 0U;
    updates = 0U;
    removals = 0U;
    operation_count = 0U;
    extra_arena = 0U;
    for (p = 0U; p < 2U; ++p) {
        switch (plans[p].action) {
        case OMC_IPTC_DATE_ACTION_ADD:
            ++additions;
            ++operation_count;
            extra_arena += strlen(plans[p].descriptor->schema);
            extra_arena += strlen(plans[p].descriptor->path);
            extra_arena += plans[p].output_size;
            break;
        case OMC_IPTC_DATE_ACTION_UPDATE:
            ++updates;
            ++operation_count;
            extra_arena += plans[p].output_size;
            break;
        case OMC_IPTC_DATE_ACTION_REMOVE:
            ++removals;
            ++operation_count;
            break;
        case OMC_IPTC_DATE_ACTION_NONE:
        case OMC_IPTC_DATE_ACTION_PRESERVE:
            break;
        }
    }
    if (additions > opts.max_added_entries ||
        additions > OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_ADDED_ENTRIES)
        return omc_iptc_date_failure(
            OMC_IPTC_DATE_TO_XMP_TRANSLATION_ENTRY_LIMIT, 0U,
            OMC_INVALID_ENTRY_ID);
    if (operation_count > opts.max_operations ||
        operation_count > OMC_IPTC_DATE_TO_XMP_TRANSLATION_MAX_OPERATIONS)
        return omc_iptc_date_failure(
            OMC_IPTC_DATE_TO_XMP_TRANSLATION_OPERATION_LIMIT, 0U,
            OMC_INVALID_ENTRY_ID);
    if ((omc_u64)source->entry_count + additions >
            (omc_u64)0xFFFFFFFFU ||
        source->entry_count > (omc_size)-1 - additions)
        return omc_iptc_date_failure(
            OMC_IPTC_DATE_TO_XMP_TRANSLATION_ENTRY_LIMIT, 0U,
            OMC_INVALID_ENTRY_ID);
    final_entries = source->entry_count + additions;
    if (final_entries > (omc_size)-1 / sizeof(*built.entries) ||
        source->block_count > (omc_size)-1 / sizeof(*built.blocks) ||
        (omc_u64)source->arena.size > (omc_u64)0xFFFFFFFFU ||
        (omc_u64)extra_arena >
            (omc_u64)0xFFFFFFFFU - (omc_u64)source->arena.size ||
        source->arena.size > (omc_size)-1 - extra_arena)
        return omc_iptc_date_failure(
            OMC_IPTC_DATE_TO_XMP_TRANSLATION_INTERNAL, 0U,
            OMC_INVALID_ENTRY_ID);
    final_arena = source->arena.size + extra_arena;

    built_moved = 0;
    omc_store_init(&built);
    status = omc_iptc_date_snapshot(source, &built, final_entries, final_arena);
    if (status != OMC_STATUS_OK) {
        omc_store_fini(&built);
        return omc_iptc_date_failure(
            status == OMC_STATUS_NO_MEMORY
                ? OMC_IPTC_DATE_TO_XMP_TRANSLATION_NO_MEMORY
                : OMC_IPTC_DATE_TO_XMP_TRANSLATION_INTERNAL,
            0U, OMC_INVALID_ENTRY_ID);
    }
    for (p = 0U; p < 2U; ++p) {
        status = omc_iptc_date_apply_action(&built, &plans[p]);
        if (status != OMC_STATUS_OK) {
            result = omc_iptc_date_failure(
                status == OMC_STATUS_NO_MEMORY
                    ? OMC_IPTC_DATE_TO_XMP_TRANSLATION_NO_MEMORY
                    : OMC_IPTC_DATE_TO_XMP_TRANSLATION_INTERNAL,
                plans[p].descriptor->mapping, OMC_INVALID_ENTRY_ID);
            goto done;
        }
    }
    omc_store_fini(out);
    *out = built;
    built_moved = 1;

    result.status = OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK;
    result.source_properties = source_properties;
    result.entries_added = additions;
    result.entries_updated = updates;
    result.entries_removed = removals;
    for (p = 0U; p < 2U; ++p) {
        if (!plans[p].selected)
            continue;
        if (plans[p].action == OMC_IPTC_DATE_ACTION_PRESERVE)
            ++result.groups_preserved;
        else if (plans[p].action == OMC_IPTC_DATE_ACTION_NONE)
            ++result.groups_unchanged;
        else
            ++result.groups_translated;
    }

done:
    if (!built_moved)
        omc_store_fini(&built);
    return result;
}
