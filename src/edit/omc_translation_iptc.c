#include "omc/omc_translation.h"
#include "../core/omc_value_internal.h"

#include <stdlib.h>
#include <string.h>

static const char omc_iptc_ns_dc[] = "http://purl.org/dc/elements/1.1/";
static const char omc_iptc_ns_ps[] = "http://ns.adobe.com/photoshop/1.0/";
static const char omc_iptc_ns_core[] = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";

typedef struct omc_iptc_mapping {
    const char *ns;
    const char *path;
    omc_u32 bit;
    omc_u16 dataset;
    omc_u16 max_bytes;
    int repeated;
} omc_iptc_mapping;

static const omc_iptc_mapping omc_iptc_mappings[] = {
    {omc_iptc_ns_dc, "title[@xml:lang=x-default]", OMC_IPTC_TRANSLATE_TITLE, 5U, 64U,
     0},
    {omc_iptc_ns_dc, "description[@xml:lang=x-default]", OMC_IPTC_TRANSLATE_DESCRIPTION,
     120U, 2000U, 0},
    {omc_iptc_ns_dc, "creator", OMC_IPTC_TRANSLATE_CREATORS, 80U, 32U, 1},
    {omc_iptc_ns_dc, "subject", OMC_IPTC_TRANSLATE_KEYWORDS, 25U, 64U, 1},
    {omc_iptc_ns_dc, "rights[@xml:lang=x-default]", OMC_IPTC_TRANSLATE_RIGHTS, 116U,
     128U, 0},
    {omc_iptc_ns_ps, "Credit", OMC_IPTC_TRANSLATE_CREDIT, 110U, 32U, 0},
    {omc_iptc_ns_ps, "Source", OMC_IPTC_TRANSLATE_SOURCE, 115U, 32U, 0},
    {omc_iptc_ns_ps, "City", OMC_IPTC_TRANSLATE_CITY, 90U, 32U, 0},
    {omc_iptc_ns_core, "Location", OMC_IPTC_TRANSLATE_SUBLOCATION, 92U, 32U, 0},
    {omc_iptc_ns_ps, "State", OMC_IPTC_TRANSLATE_STATE, 95U, 32U, 0},
    {omc_iptc_ns_ps, "Country", OMC_IPTC_TRANSLATE_COUNTRY, 101U, 64U, 0},
    {omc_iptc_ns_core, "CountryCode", OMC_IPTC_TRANSLATE_COUNTRY_CODE, 100U, 3U, 0},
    {omc_iptc_ns_ps, "Headline", OMC_IPTC_TRANSLATE_HEADLINE, 105U, 256U, 0},
    {omc_iptc_ns_ps, "Instructions", OMC_IPTC_TRANSLATE_INSTRUCTIONS, 40U, 256U, 0},
    {omc_iptc_ns_ps, "TransmissionReference", OMC_IPTC_TRANSLATE_TRANSMISSION_REFERENCE,
     103U, 32U, 0},
    {omc_iptc_ns_ps, "AuthorsPosition", OMC_IPTC_TRANSLATE_AUTHORS_POSITION, 85U, 32U,
     0},
    {omc_iptc_ns_ps, "CaptionWriter", OMC_IPTC_TRANSLATE_CAPTION_WRITER, 122U, 32U, 0},
    {omc_iptc_ns_ps, "Category", OMC_IPTC_TRANSLATE_CATEGORY, 15U, 3U, 0},
    {omc_iptc_ns_ps, "SupplementalCategories",
     OMC_IPTC_TRANSLATE_SUPPLEMENTAL_CATEGORIES, 20U, 32U, 1},
    {omc_iptc_ns_ps, "Urgency", OMC_IPTC_TRANSLATE_URGENCY, 10U, 1U, 0}};

typedef struct omc_iptc_source_value {
    omc_entry_id id;
    omc_u32 index;
    omc_const_bytes text;
} omc_iptc_source_value;

typedef struct omc_iptc_native {
    omc_entry_id id;
    omc_u32 order;
} omc_iptc_native;

typedef struct omc_iptc_plan {
    omc_iptc_source_value single;
    omc_iptc_source_value *values;
    omc_iptc_native first_native;
    omc_iptc_native *natives;
    omc_u32 value_count;
    omc_u32 native_count;
    int eligible;
    int preserved;
    int apply;
} omc_iptc_plan;

/* Scratch is allocated only for eligible repeated groups and freed before
 * return. Singleton plans, including the location API, use fixed records. */
typedef struct omc_iptc_scratch {
    omc_iptc_source_value *values;
    omc_iptc_native *natives;
    omc_u32 value_used;
    omc_u32 native_used;
    omc_u32 value_capacity;
    omc_u32 native_capacity;
} omc_iptc_scratch;

void
omc_iptc_translation_opts_init(omc_iptc_translation_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->mappings = OMC_IPTC_TRANSLATE_ALL;
    opts->conflict = OMC_TRANSLATION_FAIL;
    opts->max_source_properties = 1024U;
    opts->max_added_entries = 1025U;
    opts->max_operations = 4096U;
    opts->max_total_text_bytes = 8U * 1024U * 1024U;
}

void
omc_location_translation_opts_init(omc_location_translation_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->mappings = OMC_TRANSLATE_LOCATION;
    opts->conflict = OMC_TRANSLATION_FAIL;
    opts->max_source_properties = 1024U;
    opts->max_added_entries = 6U;
    opts->max_operations = 4096U;
    opts->max_total_text_bytes = 8U * 1024U * 1024U;
}

static int
omc_iptc_ref_equal(const omc_arena *arena, omc_byte_ref ref, const char *text)
{
    omc_size n;
    n = strlen(text);
    return omc_ref_valid(arena, ref) && ref.size == n &&
           memcmp(arena->data + ref.offset, text, n) == 0;
}

static int
omc_iptc_native_matches(const omc_entry *e, omc_u16 record, omc_u16 dataset)
{
    return e->key.kind == OMC_KEY_IPTC_DATASET &&
           (e->flags & OMC_ENTRY_FLAG_DELETED) == 0U &&
           e->key.u.iptc_dataset.record == record &&
           e->key.u.iptc_dataset.dataset == dataset;
}

static omc_const_bytes
omc_iptc_bytes(const omc_store *s, const omc_entry *e)
{
    omc_const_bytes bytes;
    bytes.data = NULL;
    bytes.size = 0U;
    if ((e->value.kind == OMC_VAL_TEXT || e->value.kind == OMC_VAL_BYTES) &&
        omc_value_shape_valid(&e->value, &s->arena))
        bytes = omc_arena_view(&s->arena, e->value.u.ref);
    return bytes;
}

static int
omc_iptc_equal(const omc_store *s, omc_entry_id id, omc_const_bytes text)
{
    omc_const_bytes bytes;
    if (id == OMC_INVALID_ENTRY_ID)
        return 0;
    bytes = omc_iptc_bytes(s, &s->entries[id]);
    return bytes.size == text.size && bytes.data != NULL &&
           memcmp(bytes.data, text.data, text.size) == 0;
}

static int
omc_iptc_text_valid(const omc_store *s, omc_entry_id id, omc_const_bytes bytes)
{
    omc_size i;
    if (bytes.size == 0U ||
        (!omc_utf8_valid(bytes.data, bytes.size,
                         s->entries[id].value.text_encoding == OMC_TEXT_ASCII)))
        return 0;
    /* The shared validator checks UTF-8. XML 1.0 additionally excludes these
     * controls and U+FFFE/U+FFFF. Tabs and line breaks remain valid text. */
    for (i = 0U; i < bytes.size; ++i) {
        if (bytes.data[i] < 32U && bytes.data[i] != 9U && bytes.data[i] != 10U &&
            bytes.data[i] != 13U)
            return 0;
        if (bytes.size - i >= 3U && bytes.data[i] == 0xEFU &&
            bytes.data[i + 1U] == 0xBFU && bytes.data[i + 2U] >= 0xBEU)
            return 0;
    }
    return 1;
}

static int
omc_iptc_source_matches(const omc_store *s, const omc_entry *e,
                        const omc_iptc_mapping *m, omc_u32 *index)
{
    omc_const_bytes path;
    omc_size n;
    omc_size i;
    omc_u32 digit;
    if (e->key.kind != OMC_KEY_XMP_PROPERTY ||
        !omc_iptc_ref_equal(&s->arena, e->key.u.xmp_property.schema_ns, m->ns) ||
        ((e->flags & OMC_ENTRY_FLAG_DELETED) != 0U &&
         (e->flags & OMC_ENTRY_FLAG_DIRTY) == 0U))
        return 0;
    *index = 0U;
    if (!m->repeated)
        return omc_iptc_ref_equal(&s->arena, e->key.u.xmp_property.property_path,
                                  m->path);
    path = omc_arena_view(&s->arena, e->key.u.xmp_property.property_path);
    n = strlen(m->path);
    if (path.size <= n + 2U || memcmp(path.data, m->path, n) != 0 ||
        path.data[n] != '[' || path.data[path.size - 1U] != ']')
        return 0;
    for (i = n + 1U; i + 1U < path.size; ++i) {
        if (path.data[i] < '0' || path.data[i] > '9')
            return 0;
        digit = (omc_u32)(path.data[i] - '0');
        if (*index > (0xFFFFFFFFU - digit) / 10U)
            return 0;
        *index = *index * 10U + digit;
    }
    return *index != 0U;
}

static int
omc_iptc_source_compare(const void *a, const void *b)
{
    const omc_iptc_source_value *x;
    const omc_iptc_source_value *y;
    x = (const omc_iptc_source_value *)a;
    y = (const omc_iptc_source_value *)b;
    if (x->index != y->index)
        return x->index < y->index ? -1 : 1;
    return x->id < y->id ? -1 : x->id != y->id;
}

static int
omc_iptc_native_compare(const void *a, const void *b)
{
    const omc_iptc_native *x;
    const omc_iptc_native *y;
    x = (const omc_iptc_native *)a;
    y = (const omc_iptc_native *)b;
    if (x->order != y->order)
        return x->order < y->order ? -1 : 1;
    return x->id < y->id ? -1 : x->id != y->id;
}

static omc_const_bytes
omc_iptc_urgency_scalar(const omc_store *s, const omc_val *v)
{
    static const omc_u8 digits[] = "12345678";
    omc_const_bytes bytes;
    omc_u64 n;
    bytes.data = NULL;
    bytes.size = 0U;
    if (v->kind != OMC_VAL_SCALAR || !omc_value_shape_valid(v, &s->arena))
        return bytes;
    switch (v->elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_U16:
    case OMC_ELEM_U32:
    case OMC_ELEM_U64:
        n = v->u.u64;
        break;
    case OMC_ELEM_I8:
    case OMC_ELEM_I16:
    case OMC_ELEM_I32:
    case OMC_ELEM_I64:
        if (v->u.i64 < 1)
            return bytes;
        n = (omc_u64)v->u.i64;
        break;
    default:
        return bytes;
    }
    if (n >= 1U && n <= 8U) {
        bytes.data = digits + (omc_size)(n - 1U);
        bytes.size = 1U;
    }
    return bytes;
}

static omc_translation_status
omc_iptc_collect(const omc_store *s, const omc_iptc_translation_opts *opts,
                 const omc_iptc_mapping *m, omc_iptc_plan *p, omc_iptc_scratch *scratch,
                 omc_u32 *matched, omc_u64 *total, omc_entry_id *failed)
{
    omc_size i;
    omc_size j;
    omc_u32 count;
    omc_u32 index;
    omc_entry_id second;
    int dirty;
    const omc_entry *e;
    omc_iptc_source_value *value;
    omc_const_bytes bytes;
    count = 0U;
    dirty = opts->all_sources;
    p->values = &p->single;
    p->first_native.id = OMC_INVALID_ENTRY_ID;
    for (i = 0U; i < s->entry_count; ++i) {
        e = &s->entries[i];
        if (!omc_iptc_source_matches(s, e, m, &index))
            continue;
        *failed = (omc_entry_id)i;
        if (count == opts->max_source_properties)
            return OMC_TRANSLATION_LIMIT;
        ++count;
        if ((e->flags & OMC_ENTRY_FLAG_DIRTY) != 0U)
            dirty = 1;
    }
    if (count == 0U || !dirty)
        return OMC_TRANSLATION_OK;
    if (*matched > opts->max_source_properties - count)
        return OMC_TRANSLATION_LIMIT;
    *matched += count;
    p->eligible = 1;
    if (m->repeated) {
        if (scratch->values == NULL) {
            scratch->values = (omc_iptc_source_value *)calloc(scratch->value_capacity,
                                                              sizeof(*scratch->values));
            if (scratch->values == NULL)
                return OMC_TRANSLATION_NO_MEMORY;
        }
        p->values = scratch->values + scratch->value_used;
    }
    second = OMC_INVALID_ENTRY_ID;
    for (i = 0U; i < s->entry_count; ++i) {
        e = &s->entries[i];
        if (!omc_iptc_source_matches(s, e, m, &index) ||
            (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        *failed = (omc_entry_id)i;
        if (e->value.kind == OMC_VAL_TEXT) {
            bytes = omc_iptc_bytes(s, e);
            if ((e->value.text_encoding != OMC_TEXT_ASCII &&
                 e->value.text_encoding != OMC_TEXT_UTF8) ||
                !omc_iptc_text_valid(s, (omc_entry_id)i, bytes))
                return OMC_TRANSLATION_INVALID_SOURCE;
        } else if (m->dataset == 10U) {
            bytes = omc_iptc_urgency_scalar(s, &e->value);
            if (bytes.size == 0U)
                return OMC_TRANSLATION_INVALID_SOURCE;
        } else
            return OMC_TRANSLATION_INVALID_SOURCE;
        if (bytes.size > m->max_bytes)
            return OMC_TRANSLATION_VALUE_TOO_LONG;
        if (m->dataset == 15U || m->dataset == 100U) {
            if (m->dataset == 100U && bytes.size < 2U)
                return OMC_TRANSLATION_INVALID_SOURCE;
            for (j = 0U; j < bytes.size; ++j)
                if (!(bytes.data[j] >= 'A' && bytes.data[j] <= 'Z') &&
                    !(m->dataset == 15U && bytes.data[j] >= 'a' &&
                      bytes.data[j] <= 'z'))
                    return OMC_TRANSLATION_INVALID_SOURCE;
        }
        if (m->dataset == 10U && (bytes.data[0] < '1' || bytes.data[0] > '8'))
            return OMC_TRANSLATION_INVALID_SOURCE;
        if (bytes.size > opts->max_total_text_bytes ||
            *total > opts->max_total_text_bytes - bytes.size)
            return OMC_TRANSLATION_LIMIT;
        *total += bytes.size;
        if (m->repeated || p->value_count == 0U) {
            value = &p->values[p->value_count];
            value->id = (omc_entry_id)i;
            value->index = index;
            value->text = bytes;
        } else if (p->value_count == 1U)
            second = (omc_entry_id)i;
        ++p->value_count;
    }
    if (!m->repeated && p->value_count > 1U) {
        *failed = second;
        return OMC_TRANSLATION_AMBIGUOUS_SOURCE;
    }
    if (m->repeated) {
        scratch->value_used += p->value_count;
        qsort(p->values, p->value_count, sizeof(*p->values), omc_iptc_source_compare);
        for (i = 1U; i < p->value_count; ++i)
            if (p->values[i - 1U].index == p->values[i].index) {
                *failed = p->values[i].id;
                return OMC_TRANSLATION_AMBIGUOUS_SOURCE;
            }
    }
    return OMC_TRANSLATION_OK;
}

static omc_translation_status
omc_iptc_analyze_native(const omc_store *s, const omc_iptc_translation_opts *opts,
                        const omc_iptc_mapping *m, omc_iptc_plan *p,
                        omc_iptc_scratch *scratch, omc_u32 *inspected)
{
    omc_size i;
    omc_iptc_native native;
    for (i = 0U; i < s->entry_count; ++i) {
        if (!omc_iptc_native_matches(&s->entries[i], 2U, m->dataset))
            continue;
        if (*inspected == opts->max_operations)
            return OMC_TRANSLATION_LIMIT;
        ++*inspected;
        native.id = (omc_entry_id)i;
        native.order = s->entries[i].origin.order_in_block;
        if (m->repeated) {
            if (scratch->natives == NULL) {
                scratch->natives = (omc_iptc_native *)calloc(scratch->native_capacity,
                                                             sizeof(*scratch->natives));
                if (scratch->natives == NULL)
                    return OMC_TRANSLATION_NO_MEMORY;
            }
            if (p->native_count == 0U)
                p->natives = scratch->natives + scratch->native_used;
            scratch->natives[scratch->native_used++] = native;
        } else if (p->native_count == 0U || native.order < p->first_native.order)
            p->first_native = native;
        ++p->native_count;
    }
    if (m->repeated && p->native_count != 0U)
        qsort(p->natives, p->native_count, sizeof(*p->natives),
              omc_iptc_native_compare);
    return OMC_TRANSLATION_OK;
}

/* Only overlap ordinals are requested for singleton groups. */
static omc_iptc_native
omc_iptc_native_at(const omc_iptc_plan *p, omc_u32 i)
{
    return p->natives != NULL ? p->natives[i] : p->first_native;
}

static omc_status
omc_iptc_add(omc_edit *edit, const omc_store *s, omc_entry_id id, omc_u16 record,
             omc_u16 dataset, omc_const_bytes bytes, int set_order, omc_u32 order)
{
    omc_entry e;
    omc_byte_ref ref;
    omc_status status;
    memset(&e, 0, sizeof(e));
    status = omc_arena_append(&edit->arena, bytes.data, bytes.size, &ref);
    if (status != OMC_STATUS_OK)
        return status;
    omc_key_make_iptc_dataset(&e.key, record, dataset);
    omc_val_make_bytes(&e.value, ref);
    e.origin = s->entries[id].origin;
    if (!omc_ref_valid(&s->arena, e.origin.wire_type_name))
        return OMC_STATUS_INVALID_ARGUMENT;
    bytes = omc_arena_view(&s->arena, e.origin.wire_type_name);
    status = omc_arena_append(&edit->arena, bytes.data, bytes.size,
                              &e.origin.wire_type_name);
    if (status != OMC_STATUS_OK)
        return status;
    if (set_order)
        e.origin.order_in_block = order;
    e.flags = OMC_ENTRY_FLAG_DIRTY;
    return omc_edit_add_entry(edit, &e);
}

static omc_translation_res
omc_iptc_translate(const omc_store *s, omc_store *out,
                   const omc_iptc_translation_opts *opts,
                   const omc_iptc_mapping *mappings, omc_iptc_plan *plans,
                   omc_u32 count)
{
    static const omc_u8 utf8[] = {0x1BU, 0x25U, 0x47U};
    omc_translation_res res;
    omc_iptc_scratch scratch;
    omc_iptc_plan *p;
    omc_iptc_native native;
    omc_size i;
    omc_size j;
    omc_u32 k;
    omc_u32 matched;
    omc_u32 inspected;
    omc_u32 additions;
    omc_u32 operations;
    omc_u32 overlap;
    omc_u32 order;
    omc_u64 total;
    omc_entry_id utf8_source;
    omc_entry_id charset;
    omc_const_bytes bytes;
    omc_byte_ref ref;
    omc_val value;
    omc_edit edit;
    omc_status status;
    int exact;
    int owned;
    int add_charset;
    memset(&res, 0, sizeof(res));
    res.failed_source = OMC_INVALID_ENTRY_ID;
    if (!omc_store_shape_valid(s) || !omc_store_shape_valid(out) || s == out ||
        s->entry_count > 200000U || opts->mappings == 0U ||
        (opts->mappings & ~OMC_IPTC_TRANSLATE_ALL) != 0U ||
        (opts->all_sources != 0 && opts->all_sources != 1) ||
        opts->conflict < OMC_TRANSLATION_PRESERVE ||
        opts->conflict > OMC_TRANSLATION_REPLACE || opts->max_source_properties == 0U ||
        opts->max_source_properties > 1024U || opts->max_added_entries == 0U ||
        opts->max_added_entries > 1025U || opts->max_operations == 0U ||
        opts->max_operations > 4096U || opts->max_total_text_bytes == 0U ||
        opts->max_total_text_bytes > 8U * 1024U * 1024U) {
        res.status = OMC_TRANSLATION_INVALID_OPTIONS;
        return res;
    }
    memset(plans, 0, count * sizeof(*plans));
    memset(&scratch, 0, sizeof(scratch));
    scratch.value_capacity = opts->max_source_properties;
    if (s->entry_count < scratch.value_capacity)
        scratch.value_capacity = (omc_u32)s->entry_count;
    scratch.native_capacity = opts->max_operations;
    if (s->entry_count < scratch.native_capacity)
        scratch.native_capacity = (omc_u32)s->entry_count;
    omc_edit_init(&edit);
    matched = 0U;
    total = 0U;
    for (k = 0U; k < count; ++k) {
        if ((opts->mappings & mappings[k].bit) == 0U)
            continue;
        res.status = omc_iptc_collect(s, opts, &mappings[k], &plans[k], &scratch,
                                      &matched, &total, &res.failed_source);
        if (res.status != OMC_TRANSLATION_OK) {
            res.failed_mapping = mappings[k].bit;
            goto done;
        }
    }
    res.failed_source = OMC_INVALID_ENTRY_ID;
    inspected = 0U;
    additions = 0U;
    operations = 0U;
    utf8_source = OMC_INVALID_ENTRY_ID;
    for (k = 0U; k < count; ++k) {
        p = &plans[k];
        if (!p->eligible)
            continue;
        res.status =
            omc_iptc_analyze_native(s, opts, &mappings[k], p, &scratch, &inspected);
        if (res.status != OMC_TRANSLATION_OK) {
            res.failed_mapping = mappings[k].bit;
            goto done;
        }
        exact = p->native_count == p->value_count;
        for (i = 0U; exact && i < p->value_count; ++i) {
            native = omc_iptc_native_at(p, (omc_u32)i);
            exact = omc_iptc_equal(s, native.id, p->values[i].text);
        }
        if (p->native_count != 0U && opts->conflict == OMC_TRANSLATION_PRESERVE) {
            p->preserved = 1;
            ++res.groups_preserved;
            continue;
        }
        if (p->native_count != 0U && !exact && opts->conflict == OMC_TRANSLATION_FAIL) {
            res.status = OMC_TRANSLATION_NATIVE_CONFLICT;
            res.failed_mapping = mappings[k].bit;
            res.failed_source = p->value_count ? p->values[0].id : OMC_INVALID_ENTRY_ID;
            goto done;
        }
        for (i = 0U; utf8_source == OMC_INVALID_ENTRY_ID && i < p->value_count; ++i)
            for (j = 0U; j < p->values[i].text.size; ++j)
                if (p->values[i].text.data[j] >= 128U) {
                    utf8_source = p->values[i].id;
                    break;
                }
        if (exact) {
            ++res.groups_unchanged;
            continue;
        }
        p->apply = 1;
        overlap = p->native_count < p->value_count ? p->native_count : p->value_count;
        operations += p->native_count - overlap + p->value_count - overlap;
        additions += p->value_count - overlap;
        for (i = 0U; i < overlap; ++i) {
            native = omc_iptc_native_at(p, (omc_u32)i);
            if (!omc_iptc_equal(s, native.id, p->values[i].text))
                ++operations;
        }
    }
    charset = OMC_INVALID_ENTRY_ID;
    add_charset = 0;
    if (utf8_source != OMC_INVALID_ENTRY_ID) {
        res.failed_source = utf8_source;
        for (i = 0U; i < s->entry_count; ++i) {
            if (!omc_iptc_native_matches(&s->entries[i], 1U, 90U))
                continue;
            bytes = omc_iptc_bytes(s, &s->entries[i]);
            if (charset != OMC_INVALID_ENTRY_ID || bytes.size != 3U ||
                memcmp(bytes.data, utf8, 3U) != 0)
                goto encoding_conflict;
            charset = (omc_entry_id)i;
        }
        if (charset == OMC_INVALID_ENTRY_ID) {
            for (i = 0U; i < s->entry_count; ++i) {
                const omc_entry *e;
                e = &s->entries[i];
                if (e->key.kind != OMC_KEY_IPTC_DATASET ||
                    (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
                    continue;
                bytes = omc_iptc_bytes(s, e);
                if (bytes.size > opts->max_total_text_bytes - total) {
                    res.status = OMC_TRANSLATION_LIMIT;
                    goto done;
                }
                total += bytes.size;
                owned = 0;
                for (k = 0U; k < count; ++k)
                    if (plans[k].eligible && !plans[k].preserved &&
                        omc_iptc_native_matches(e, 2U, mappings[k].dataset))
                        owned = 1;
                if (owned)
                    continue;
                if (e->value.kind != OMC_VAL_TEXT && e->value.kind != OMC_VAL_BYTES)
                    goto encoding_conflict;
                for (j = 0U; j < bytes.size; ++j)
                    if (bytes.data[j] >= 128U)
                        goto encoding_conflict;
            }
            add_charset = 1;
            ++additions;
            ++operations;
        }
    }
    res.failed_source = OMC_INVALID_ENTRY_ID;
    if (additions > opts->max_added_entries || operations > opts->max_operations) {
        res.status = OMC_TRANSLATION_LIMIT;
        goto done;
    }
    status = OMC_STATUS_OK;
    for (k = 0U; k < count && status == OMC_STATUS_OK; ++k) {
        p = &plans[k];
        if (!p->apply)
            continue;
        overlap = p->native_count < p->value_count ? p->native_count : p->value_count;
        order = 0U;
        for (i = 0U; i < overlap && status == OMC_STATUS_OK; ++i) {
            native = omc_iptc_native_at(p, (omc_u32)i);
            order = native.order;
            if (omc_iptc_equal(s, native.id, p->values[i].text))
                continue;
            bytes = p->values[i].text;
            status = omc_arena_append(&edit.arena, bytes.data, bytes.size, &ref);
            if (status == OMC_STATUS_OK) {
                omc_val_make_bytes(&value, ref);
                status = omc_edit_set_value(&edit, native.id, &value);
            }
            ++res.entries_updated;
        }
        if (mappings[k].repeated) {
            for (i = overlap; i < p->native_count && status == OMC_STATUS_OK; ++i) {
                status = omc_edit_tombstone(&edit, p->natives[i].id);
                ++res.entries_removed;
            }
        } else {
            for (i = 0U; i < s->entry_count && status == OMC_STATUS_OK; ++i) {
                if (!omc_iptc_native_matches(&s->entries[i], 2U, mappings[k].dataset) ||
                    (overlap != 0U && i == p->first_native.id))
                    continue;
                status = omc_edit_tombstone(&edit, (omc_entry_id)i);
                ++res.entries_removed;
            }
        }
        for (i = overlap; i < p->value_count && status == OMC_STATUS_OK; ++i) {
            status = omc_iptc_add(&edit, s, p->values[i].id, 2U, mappings[k].dataset,
                                  p->values[i].text, mappings[k].repeated, order);
            ++res.entries_added;
        }
        ++res.groups_translated;
    }
    if (status == OMC_STATUS_OK && add_charset) {
        bytes.data = utf8;
        bytes.size = sizeof(utf8);
        status = omc_iptc_add(&edit, s, utf8_source, 1U, 90U, bytes, 1, 0U);
        ++res.entries_added;
        res.utf8_charset_added = 1;
    }
    if (status == OMC_STATUS_OK)
        status = omc_edit_commit(s, &edit, 1U, out);
    if (status != OMC_STATUS_OK)
        res.status = status == OMC_STATUS_NO_MEMORY ? OMC_TRANSLATION_NO_MEMORY
                                                    : OMC_TRANSLATION_INVALID_SOURCE;
    goto done;
encoding_conflict:
    res.status = OMC_TRANSLATION_ENCODING_CONFLICT;
done:
    free(scratch.natives);
    free(scratch.values);
    omc_edit_fini(&edit);
    return res;
}

omc_translation_res
omc_translate_xmp_iptc(const omc_store *source, omc_store *out,
                       const omc_iptc_translation_opts *options)
{
    omc_iptc_translation_opts opts;
    omc_iptc_plan plans[20];
    omc_iptc_translation_opts_init(&opts);
    if (options != NULL)
        opts = *options;
    return omc_iptc_translate(source, out, &opts, omc_iptc_mappings, plans, 20U);
}

omc_translation_res
omc_translate_xmp_location(const omc_store *source, omc_store *out,
                           const omc_location_translation_opts *options)
{
    omc_location_translation_opts location;
    omc_iptc_translation_opts opts;
    omc_translation_res res;
    omc_iptc_plan plans[5];
    omc_location_translation_opts_init(&location);
    if (options != NULL)
        location = *options;
    if ((location.mappings & ~OMC_TRANSLATE_LOCATION) != 0U ||
        location.max_added_entries > 6U) {
        memset(&res, 0, sizeof(res));
        res.status = OMC_TRANSLATION_INVALID_OPTIONS;
        res.failed_source = OMC_INVALID_ENTRY_ID;
        return res;
    }
    opts.mappings = (location.mappings >> 22U) << 7U;
    opts.all_sources = location.all_sources;
    opts.conflict = location.conflict;
    opts.max_source_properties = location.max_source_properties;
    opts.max_added_entries = location.max_added_entries;
    opts.max_operations = location.max_operations;
    opts.max_total_text_bytes = location.max_total_text_bytes;
    res = omc_iptc_translate(source, out, &opts, omc_iptc_mappings + 7U, plans, 5U);
    res.failed_mapping = (res.failed_mapping >> 7U) << 22U;
    return res;
}
