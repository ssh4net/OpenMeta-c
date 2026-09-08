#include "../core/omc_value_internal.h"
#include "omc/omc_store_validate.h"
#include "omc/omc_translation.h"

#include <string.h>

typedef struct omc_location_mapping {
    const char *ns;
    const char *path;
    omc_u32 bit;
    omc_u16 dataset;
    omc_u16 max_bytes;
} omc_location_mapping;

static const omc_location_mapping omc_locations[] = {
    {"http://ns.adobe.com/photoshop/1.0/", "City", OMC_TRANSLATE_CITY, 90U, 32U},
    {"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "Location",
     OMC_TRANSLATE_SUBLOCATION, 92U, 32U},
    {"http://ns.adobe.com/photoshop/1.0/", "State", OMC_TRANSLATE_STATE, 95U, 32U},
    {"http://ns.adobe.com/photoshop/1.0/", "Country", OMC_TRANSLATE_COUNTRY, 101U, 64U},
    {"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "CountryCode",
     OMC_TRANSLATE_COUNTRY_CODE, 100U, 3U}};

/* Each group has at most one active source. Native replacement retains the
 * first entry in wire order, so only its ID and the native count are needed. */
typedef struct omc_location_plan {
    omc_entry_id source;
    omc_entry_id native;
    omc_u32 native_count;
    omc_const_bytes text;
    int eligible;
    int preserved;
    int apply;
    int non_ascii;
} omc_location_plan;

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
omc_location_ref_equal(const omc_arena *arena, omc_byte_ref ref, const char *text)
{
    omc_size n;
    n = strlen(text);
    return omc_ref_valid(arena, ref) && ref.size == n &&
           memcmp(arena->data + ref.offset, text, n) == 0;
}

static int
omc_location_source_matches(const omc_store *s, const omc_entry *e,
                            const omc_location_mapping *m)
{
    return e->key.kind == OMC_KEY_XMP_PROPERTY &&
           omc_location_ref_equal(&s->arena, e->key.u.xmp_property.schema_ns, m->ns) &&
           omc_location_ref_equal(&s->arena, e->key.u.xmp_property.property_path,
                                  m->path) &&
           ((e->flags & OMC_ENTRY_FLAG_DELETED) == 0U ||
            (e->flags & OMC_ENTRY_FLAG_DIRTY) != 0U);
}

static int
omc_location_native_matches(const omc_entry *e, omc_u16 record, omc_u16 dataset)
{
    return e->key.kind == OMC_KEY_IPTC_DATASET &&
           (e->flags & OMC_ENTRY_FLAG_DELETED) == 0U &&
           e->key.u.iptc_dataset.record == record &&
           e->key.u.iptc_dataset.dataset == dataset;
}

static omc_const_bytes
omc_location_bytes(const omc_store *s, const omc_entry *e)
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
omc_location_equal(const omc_store *s, omc_entry_id id, omc_const_bytes text)
{
    omc_const_bytes bytes;
    if (id == OMC_INVALID_ENTRY_ID)
        return 0;
    bytes = omc_location_bytes(s, &s->entries[id]);
    return bytes.size == text.size && bytes.data != NULL &&
           memcmp(bytes.data, text.data, text.size) == 0;
}

static int
omc_location_text_valid(const omc_store *s, omc_entry_id id, omc_const_bytes bytes)
{
    omc_metadata_validate_opts opts;
    omc_size i;
    omc_metadata_validate_opts_init(&opts);
    opts.validate_schema = 0;
    opts.validate_wire_hints = 0;
    if (bytes.size == 0U ||
        omc_validate_entry(s, id, NULL, 0U, &opts).status != OMC_STATUS_OK)
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

static omc_translation_status
omc_location_collect(const omc_store *s, const omc_location_translation_opts *opts,
                     const omc_location_mapping *m, omc_location_plan *p,
                     omc_u32 *matched, omc_u64 *total, omc_entry_id *failed)
{
    omc_size i;
    omc_size j;
    omc_u32 count;
    omc_u32 active;
    omc_entry_id second;
    int dirty;
    const omc_entry *e;
    omc_const_bytes bytes;
    count = 0U;
    dirty = opts->all_sources;
    for (i = 0U; i < s->entry_count; ++i) {
        e = &s->entries[i];
        if (!omc_location_source_matches(s, e, m))
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
    active = 0U;
    second = OMC_INVALID_ENTRY_ID;
    for (i = 0U; i < s->entry_count; ++i) {
        e = &s->entries[i];
        if (!omc_location_source_matches(s, e, m) ||
            (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        *failed = (omc_entry_id)i;
        bytes = omc_location_bytes(s, e);
        if (e->value.kind != OMC_VAL_TEXT ||
            (e->value.text_encoding != OMC_TEXT_ASCII &&
             e->value.text_encoding != OMC_TEXT_UTF8) ||
            !omc_location_text_valid(s, (omc_entry_id)i, bytes))
            return OMC_TRANSLATION_INVALID_SOURCE;
        if (bytes.size > m->max_bytes)
            return OMC_TRANSLATION_VALUE_TOO_LONG;
        if (m->bit == OMC_TRANSLATE_COUNTRY_CODE) {
            if (bytes.size < 2U)
                return OMC_TRANSLATION_INVALID_SOURCE;
            for (j = 0U; j < bytes.size; ++j)
                if (bytes.data[j] < 'A' || bytes.data[j] > 'Z')
                    return OMC_TRANSLATION_INVALID_SOURCE;
        }
        if (bytes.size > opts->max_total_text_bytes ||
            *total > opts->max_total_text_bytes - bytes.size)
            return OMC_TRANSLATION_LIMIT;
        *total += bytes.size;
        if (active++ == 0U) {
            p->source = (omc_entry_id)i;
            p->text = bytes;
            for (j = 0U; j < bytes.size; ++j)
                if (bytes.data[j] >= 128U)
                    p->non_ascii = 1;
        } else if (active == 2U)
            second = (omc_entry_id)i;
    }
    if (active > 1U) {
        *failed = second;
        return OMC_TRANSLATION_AMBIGUOUS_SOURCE;
    }
    return OMC_TRANSLATION_OK;
}

static omc_status
omc_location_add(omc_edit *edit, const omc_store *source, omc_entry_id id,
                 omc_u16 record, omc_u16 dataset, omc_const_bytes bytes)
{
    omc_entry entry;
    omc_byte_ref ref;
    omc_status status;
    memset(&entry, 0, sizeof(entry));
    status = omc_arena_append(&edit->arena, bytes.data, bytes.size, &ref);
    if (status != OMC_STATUS_OK)
        return status;
    omc_key_make_iptc_dataset(&entry.key, record, dataset);
    omc_val_make_bytes(&entry.value, ref);
    entry.origin = source->entries[id].origin;
    bytes = omc_arena_view(&source->arena, entry.origin.wire_type_name);
    if (!omc_ref_valid(&source->arena, entry.origin.wire_type_name))
        return OMC_STATUS_INVALID_ARGUMENT;
    status = omc_arena_append(&edit->arena, bytes.data, bytes.size,
                              &entry.origin.wire_type_name);
    if (status != OMC_STATUS_OK)
        return status;
    if (record == 1U)
        entry.origin.order_in_block = 0U;
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    return omc_edit_add_entry(edit, &entry);
}

omc_translation_res
omc_translate_xmp_location(const omc_store *source, omc_store *out,
                           const omc_location_translation_opts *options)
{
    static const omc_u8 utf8[] = {0x1BU, 0x25U, 0x47U};
    omc_location_translation_opts opts;
    omc_translation_res res;
    omc_location_plan plans[5];
    omc_location_plan *p;
    const omc_entry *e;
    omc_size i;
    omc_size j;
    omc_size k;
    omc_u32 matched;
    omc_u32 natives;
    omc_u32 additions;
    omc_u32 operations;
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
    omc_location_translation_opts_init(&opts);
    if (options != NULL)
        opts = *options;
    if (!omc_store_shape_valid(source) || !omc_store_shape_valid(out) ||
        source == out || source->entry_count > 200000U || opts.mappings == 0U ||
        (opts.mappings & ~OMC_TRANSLATE_LOCATION) != 0U ||
        (opts.all_sources != 0 && opts.all_sources != 1) ||
        opts.conflict < OMC_TRANSLATION_PRESERVE ||
        opts.conflict > OMC_TRANSLATION_REPLACE || opts.max_source_properties == 0U ||
        opts.max_source_properties > 1024U || opts.max_added_entries == 0U ||
        opts.max_added_entries > 6U || opts.max_operations == 0U ||
        opts.max_operations > 4096U || opts.max_total_text_bytes == 0U ||
        opts.max_total_text_bytes > 8U * 1024U * 1024U) {
        res.status = OMC_TRANSLATION_INVALID_OPTIONS;
        return res;
    }
    memset(plans, 0, sizeof(plans));
    matched = 0U;
    total = 0U;
    /* Validate every selected source before considering native conflicts. */
    for (k = 0U; k < 5U; ++k) {
        p = &plans[k];
        p->source = OMC_INVALID_ENTRY_ID;
        p->native = OMC_INVALID_ENTRY_ID;
        if ((opts.mappings & omc_locations[k].bit) == 0U)
            continue;
        res.status = omc_location_collect(source, &opts, &omc_locations[k], p, &matched,
                                          &total, &res.failed_source);
        if (res.status != OMC_TRANSLATION_OK) {
            res.failed_mapping = omc_locations[k].bit;
            return res;
        }
    }
    res.failed_source = OMC_INVALID_ENTRY_ID;
    natives = 0U;
    additions = 0U;
    operations = 0U;
    utf8_source = OMC_INVALID_ENTRY_ID;
    for (k = 0U; k < 5U; ++k) {
        p = &plans[k];
        if (!p->eligible)
            continue;
        for (i = 0U; i < source->entry_count; ++i) {
            e = &source->entries[i];
            if (!omc_location_native_matches(e, 2U, omc_locations[k].dataset))
                continue;
            if (natives++ == opts.max_operations) {
                res.status = OMC_TRANSLATION_LIMIT;
                res.failed_mapping = omc_locations[k].bit;
                return res;
            }
            ++p->native_count;
            if (p->native == OMC_INVALID_ENTRY_ID ||
                e->origin.order_in_block <
                    source->entries[p->native].origin.order_in_block)
                p->native = (omc_entry_id)i;
        }
        exact = p->source == OMC_INVALID_ENTRY_ID
                    ? p->native_count == 0U
                    : p->native_count == 1U &&
                          omc_location_equal(source, p->native, p->text);
        if (p->native_count != 0U && opts.conflict == OMC_TRANSLATION_PRESERVE) {
            p->preserved = 1;
            ++res.groups_preserved;
            continue;
        }
        if (p->native_count != 0U && !exact && opts.conflict == OMC_TRANSLATION_FAIL) {
            res.status = OMC_TRANSLATION_NATIVE_CONFLICT;
            res.failed_mapping = omc_locations[k].bit;
            res.failed_source = p->source;
            return res;
        }
        if (p->non_ascii && utf8_source == OMC_INVALID_ENTRY_ID)
            utf8_source = p->source;
        if (exact) {
            ++res.groups_unchanged;
            continue;
        }
        p->apply = 1;
        operations += p->native_count;
        if (p->source != OMC_INVALID_ENTRY_ID) {
            if (p->native_count == 0U) {
                ++additions;
                ++operations;
            } else if (omc_location_equal(source, p->native, p->text))
                --operations;
        }
    }
    charset = OMC_INVALID_ENTRY_ID;
    add_charset = 0;
    if (utf8_source != OMC_INVALID_ENTRY_ID) {
        res.failed_source = utf8_source;
        for (i = 0U; i < source->entry_count; ++i) {
            if (!omc_location_native_matches(&source->entries[i], 1U, 90U))
                continue;
            bytes = omc_location_bytes(source, &source->entries[i]);
            if (charset != OMC_INVALID_ENTRY_ID || bytes.size != 3U ||
                memcmp(bytes.data, utf8, 3U) != 0)
                goto encoding_conflict;
            charset = (omc_entry_id)i;
        }
        if (charset == OMC_INVALID_ENTRY_ID) {
            for (i = 0U; i < source->entry_count; ++i) {
                e = &source->entries[i];
                if (e->key.kind != OMC_KEY_IPTC_DATASET ||
                    (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
                    continue;
                bytes = omc_location_bytes(source, e);
                if (bytes.size > opts.max_total_text_bytes - total) {
                    res.status = OMC_TRANSLATION_LIMIT;
                    return res;
                }
                total += bytes.size;
                owned = 0;
                for (k = 0U; k < 5U; ++k)
                    if (plans[k].eligible && !plans[k].preserved &&
                        omc_location_native_matches(e, 2U, omc_locations[k].dataset))
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
    if (additions > opts.max_added_entries || operations > opts.max_operations) {
        res.status = OMC_TRANSLATION_LIMIT;
        return res;
    }
    omc_edit_init(&edit);
    status = OMC_STATUS_OK;
    for (k = 0U; k < 5U && status == OMC_STATUS_OK; ++k) {
        p = &plans[k];
        if (!p->apply)
            continue;
        for (i = 0U; i < source->entry_count && status == OMC_STATUS_OK; ++i) {
            if (!omc_location_native_matches(&source->entries[i], 2U,
                                             omc_locations[k].dataset))
                continue;
            if (i != p->native || p->source == OMC_INVALID_ENTRY_ID) {
                status = omc_edit_tombstone(&edit, (omc_entry_id)i);
                ++res.entries_removed;
            } else if (!omc_location_equal(source, p->native, p->text)) {
                status =
                    omc_arena_append(&edit.arena, p->text.data, p->text.size, &ref);
                if (status == OMC_STATUS_OK) {
                    omc_val_make_bytes(&value, ref);
                    status = omc_edit_set_value(&edit, (omc_entry_id)i, &value);
                }
                ++res.entries_updated;
            }
        }
        if (status == OMC_STATUS_OK && p->source != OMC_INVALID_ENTRY_ID &&
            p->native_count == 0U) {
            status = omc_location_add(&edit, source, p->source, 2U,
                                      omc_locations[k].dataset, p->text);
            ++res.entries_added;
        }
        ++res.groups_translated;
    }
    if (status == OMC_STATUS_OK && add_charset) {
        bytes.data = utf8;
        bytes.size = sizeof(utf8);
        status = omc_location_add(&edit, source, utf8_source, 1U, 90U, bytes);
        ++res.entries_added;
        res.utf8_charset_added = 1;
    }
    if (status == OMC_STATUS_OK)
        status = omc_edit_commit(source, &edit, 1U, out);
    omc_edit_fini(&edit);
    if (status != OMC_STATUS_OK)
        res.status = status == OMC_STATUS_NO_MEMORY ? OMC_TRANSLATION_NO_MEMORY
                                                    : OMC_TRANSLATION_INVALID_SOURCE;
    return res;
encoding_conflict:
    res.status = OMC_TRANSLATION_ENCODING_CONFLICT;
    return res;
}
