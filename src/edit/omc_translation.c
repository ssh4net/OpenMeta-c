#include "omc/omc_translation.h"
#include "omc/omc_store_validate.h"
#include "../core/omc_value_internal.h"
#include "../core/omc_datetime_internal.h"

#include <stdlib.h>
#include <string.h>

static const char omc_ns_xmp[] = "http://ns.adobe.com/xap/1.0/";
static const char omc_ns_exif[] = "http://ns.adobe.com/exif/1.0/";
static const char omc_ns_tiff[] = "http://ns.adobe.com/tiff/1.0/";
static const char omc_ns_ps[] = "http://ns.adobe.com/photoshop/1.0/";
static const char omc_ns_dc[] = "http://purl.org/dc/elements/1.1/";

typedef struct omc_translation_field {
    const char *ifd;
    omc_u16 tag;
    omc_u16 record;
    omc_entry_id source;
    omc_val value;
    int present;
} omc_translation_field;

typedef struct omc_translation_source {
    omc_entry_id id;
    omc_u32 index;
} omc_translation_source;

typedef struct omc_translation {
    const omc_store *source;
    omc_translation_opts opts;
    omc_translation_res res;
    omc_edit edit;
    omc_translation_field *fields;
    omc_translation_source *sources;
    omc_u32 source_count;
    omc_u32 field_count;
    omc_u64 text_bytes;
    omc_entry_id utf8_source;
    omc_u8 *owned_iptc;
} omc_translation;

void
omc_translation_opts_init(omc_translation_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->mappings = OMC_TRANSLATE_ALL;
    opts->conflict = OMC_TRANSLATION_FAIL;
    opts->max_source_properties = 1024U;
    opts->max_added_entries = 1024U;
    opts->max_operations = 4096U;
    opts->max_text_bytes_per_property = 4096U;
    opts->max_total_text_bytes = 1024U * 1024U;
}

static int
omc_translation_status_ok(omc_translation *t, omc_status status)
{
    if (status == OMC_STATUS_OK)
        return 1;
    t->res.status = status == OMC_STATUS_NO_MEMORY ? OMC_TRANSLATION_NO_MEMORY
                                                   : OMC_TRANSLATION_INVALID_SOURCE;
    return 0;
}

static int
omc_translation_ref_equal(const omc_arena *a, omc_byte_ref r, const char *s)
{
    omc_size n;
    n = strlen(s);
    return omc_ref_valid(a, r) && r.size == n &&
           (n == 0U || memcmp(a->data + r.offset, s, n) == 0);
}

static int
omc_translation_key_matches(const omc_translation *t, const omc_entry *e,
                            const omc_translation_field *f)
{
    if (f->ifd != NULL) {
        return e->key.kind == OMC_KEY_EXIF_TAG && e->key.u.exif_tag.tag == f->tag &&
               omc_translation_ref_equal(&t->source->arena, e->key.u.exif_tag.ifd,
                                         f->ifd);
    }
    return e->key.kind == OMC_KEY_IPTC_DATASET &&
           e->key.u.iptc_dataset.record == f->record &&
           e->key.u.iptc_dataset.dataset == f->tag;
}

static int
omc_translation_field_same(const omc_translation_field *a,
                           const omc_translation_field *b)
{
    return a->tag == b->tag && a->record == b->record &&
           ((a->ifd == NULL && b->ifd == NULL) ||
            (a->ifd != NULL && b->ifd != NULL && strcmp(a->ifd, b->ifd) == 0));
}

static int
omc_translation_value_equal(const omc_translation *t, const omc_val *a,
                            const omc_val *b)
{
    omc_const_bytes av;
    omc_const_bytes bv;
    if ((a->kind == OMC_VAL_TEXT || a->kind == OMC_VAL_BYTES) &&
        (b->kind == OMC_VAL_TEXT || b->kind == OMC_VAL_BYTES)) {
        av = omc_arena_view(&t->source->arena, a->u.ref);
        bv = omc_arena_view(&t->edit.arena, b->u.ref);
        return av.size == bv.size &&
               (av.size == 0U || (av.data != NULL && bv.data != NULL &&
                                  memcmp(av.data, bv.data, av.size) == 0));
    }
    if (a->kind != b->kind || a->count != b->count || a->elem_type != b->elem_type)
        return 0;
    if (a->kind == OMC_VAL_SCALAR) {
        switch (a->elem_type) {
        case OMC_ELEM_URATIONAL:
            return a->u.ur.numer == b->u.ur.numer && a->u.ur.denom == b->u.ur.denom;
        case OMC_ELEM_SRATIONAL:
            return a->u.sr.numer == b->u.sr.numer && a->u.sr.denom == b->u.sr.denom;
        case OMC_ELEM_I8:
        case OMC_ELEM_I16:
        case OMC_ELEM_I32:
        case OMC_ELEM_I64:
            return a->u.i64 == b->u.i64;
        default:
            return a->u.u64 == b->u.u64;
        }
    }
    return a->kind == OMC_VAL_EMPTY;
}

static omc_translation_field *
omc_translation_nth_field(omc_translation *t, omc_u32 key, omc_u32 ordinal)
{
    omc_u32 i;
    for (i = 0U; i < t->field_count; ++i) {
        if (t->fields[i].present &&
            omc_translation_field_same(&t->fields[key], &t->fields[i])) {
            if (ordinal == 0U)
                return &t->fields[i];
            ordinal--;
        }
    }
    return NULL;
}

static int
omc_translation_reconcile(omc_translation *t)
{
    omc_u32 k;
    omc_u32 j;
    omc_u32 ordinal;
    omc_size i;
    int any;
    int exact;
    int duplicate_key;
    omc_translation_field *f;
    omc_entry added;
    omc_const_bytes bytes;
    omc_status status;
    any = 0;
    exact = 1;
    for (k = 0U; k < t->field_count; ++k) {
        duplicate_key = 0;
        for (j = 0U; j < k; ++j)
            if (omc_translation_field_same(&t->fields[j], &t->fields[k]))
                duplicate_key = 1;
        if (duplicate_key)
            continue;
        ordinal = 0U;
        for (i = 0U; i < t->source->entry_count; ++i) {
            if ((t->source->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
                !omc_translation_key_matches(t, &t->source->entries[i], &t->fields[k]))
                continue;
            any = 1;
            f = omc_translation_nth_field(t, k, ordinal++);
            if (f == NULL || !omc_translation_value_equal(
                                 t, &t->source->entries[i].value, &f->value))
                exact = 0;
        }
        if (omc_translation_nth_field(t, k, ordinal) != NULL)
            exact = 0;
    }
    if (any && t->opts.conflict == OMC_TRANSLATION_PRESERVE) {
        t->res.groups_preserved++;
        return 1;
    }
    if (any && !exact && t->opts.conflict == OMC_TRANSLATION_FAIL) {
        t->res.status = OMC_TRANSLATION_NATIVE_CONFLICT;
        return 0;
    }
    for (i = 0U; i < t->source->entry_count; ++i) {
        if (t->source->entries[i].key.kind != OMC_KEY_IPTC_DATASET)
            continue;
        for (k = 0U; k < t->field_count; ++k) {
            if (omc_translation_key_matches(t, &t->source->entries[i], &t->fields[k])) {
                t->owned_iptc[i] = 1U;
                break;
            }
        }
    }
    if (exact) {
        t->res.groups_unchanged++;
        return 1;
    }
    for (k = 0U; k < t->field_count; ++k) {
        duplicate_key = 0;
        for (j = 0U; j < k; ++j)
            if (omc_translation_field_same(&t->fields[j], &t->fields[k]))
                duplicate_key = 1;
        if (duplicate_key)
            continue;
        ordinal = 0U;
        for (i = 0U; i < t->source->entry_count; ++i) {
            if ((t->source->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
                !omc_translation_key_matches(t, &t->source->entries[i], &t->fields[k]))
                continue;
            f = omc_translation_nth_field(t, k, ordinal++);
            if (f == NULL) {
                status = omc_edit_tombstone(&t->edit, (omc_entry_id)i);
                t->res.entries_removed++;
            } else if (!omc_translation_value_equal(t, &t->source->entries[i].value,
                                                    &f->value)) {
                status = omc_edit_set_value(&t->edit, (omc_entry_id)i, &f->value);
                t->res.entries_updated++;
            } else
                status = OMC_STATUS_OK;
            if (!omc_translation_status_ok(t, status))
                return 0;
        }
        while ((f = omc_translation_nth_field(t, k, ordinal++)) != NULL) {
            memset(&added, 0, sizeof(added));
            if (f->ifd != NULL) {
                if (!omc_translation_status_ok(
                        t, omc_arena_append(&t->edit.arena, f->ifd, strlen(f->ifd),
                                            &added.key.u.exif_tag.ifd)))
                    return 0;
                added.key.kind = OMC_KEY_EXIF_TAG;
                added.key.u.exif_tag.tag = f->tag;
            } else
                omc_key_make_iptc_dataset(&added.key, f->record, f->tag);
            added.value = f->value;
            added.origin = t->source->entries[f->source].origin;
            bytes = omc_arena_view(&t->source->arena, added.origin.wire_type_name);
            if (added.origin.wire_type_name.size != 0U &&
                !omc_translation_status_ok(
                    t, omc_arena_append(&t->edit.arena, bytes.data, bytes.size,
                                        &added.origin.wire_type_name)))
                return 0;
            j = (omc_u32)(f - t->fields) + 1U;
            if (added.origin.order_in_block <= 0xFFFFFFFFU - j)
                added.origin.order_in_block += j;
            else
                added.origin.order_in_block = 0xFFFFFFFFU;
            added.flags = OMC_ENTRY_FLAG_DIRTY;
            if (!omc_translation_status_ok(t, omc_edit_add_entry(&t->edit, &added)))
                return 0;
            t->res.entries_added++;
        }
    }
    if (t->edit.op_count > t->opts.max_operations ||
        t->res.entries_added > t->opts.max_added_entries) {
        t->res.status = OMC_TRANSLATION_LIMIT;
        return 0;
    }
    t->res.groups_translated++;
    return 1;
}

static int
omc_translation_source_compare(const void *a, const void *b)
{
    const omc_translation_source *x;
    const omc_translation_source *y;
    x = (const omc_translation_source *)a;
    y = (const omc_translation_source *)b;
    return x->index < y->index ? -1 : x->index != y->index;
}

static int
omc_translation_find(omc_translation *t, const char *ns, const char *path, int indexed,
                     int append)
{
    omc_size i;
    omc_size j;
    omc_size length;
    omc_u64 index;
    omc_const_bytes p;
    const omc_entry *e;
    int dirty;
    if (!append)
        t->source_count = 0U;
    length = strlen(path);
    dirty = t->opts.all_sources;
    for (i = 0U; i < t->source->entry_count; ++i) {
        e = &t->source->entries[i];
        if (e->key.kind != OMC_KEY_XMP_PROPERTY ||
            !omc_translation_ref_equal(&t->source->arena,
                                       e->key.u.xmp_property.schema_ns, ns))
            continue;
        p = omc_arena_view(&t->source->arena, e->key.u.xmp_property.property_path);
        index = 0U;
        if (indexed) {
            if (p.size < length + 3U || memcmp(p.data, path, length) != 0 ||
                p.data[length] != '[' || p.data[p.size - 1U] != ']' ||
                p.data[length + 1U] == '0')
                continue;
            for (j = length + 1U; j + 1U < p.size; ++j) {
                if (p.data[j] < '0' || p.data[j] > '9')
                    break;
                index = index * 10U + (omc_u32)(p.data[j] - '0');
                if (index > 0xFFFFFFFFU)
                    break;
            }
            if (j + 1U != p.size || index == 0U || index > 0xFFFFFFFFU)
                continue;
        } else if (p.size != length || memcmp(p.data, path, length) != 0)
            continue;
        if ((e->flags & OMC_ENTRY_FLAG_DELETED) != 0U &&
            (e->flags & OMC_ENTRY_FLAG_DIRTY) == 0U)
            continue;
        if (!indexed && !t->opts.all_sources && (e->flags & OMC_ENTRY_FLAG_DIRTY) == 0U)
            continue;
        if (t->source_count >= t->opts.max_source_properties) {
            t->res.status = OMC_TRANSLATION_LIMIT;
            return 0;
        }
        t->sources[t->source_count].id = (omc_entry_id)i;
        t->sources[t->source_count++].index = (omc_u32)index;
        if ((e->flags & OMC_ENTRY_FLAG_DIRTY) != 0U)
            dirty = 1;
    }
    if (indexed && !dirty)
        t->source_count = 0U;
    qsort(t->sources, t->source_count, sizeof(*t->sources),
          omc_translation_source_compare);
    for (i = 1U; i < t->source_count; ++i) {
        if (append != 2 &&
            (!indexed || t->sources[i - 1U].index == t->sources[i].index)) {
            t->res.status = OMC_TRANSLATION_AMBIGUOUS_SOURCE;
            t->res.failed_source = t->sources[i].id;
            return 0;
        }
    }
    if (t->source_count != 0U)
        t->res.failed_source = t->sources[0].id;
    return 1;
}

static int
omc_translation_text(omc_translation *t, omc_entry_id id, omc_const_bytes *bytes)
{
    const omc_val *v;
    omc_metadata_validate_opts opts;
    omc_metadata_validate_res checked;
    v = &t->source->entries[id].value;
    if (v->kind != OMC_VAL_TEXT ||
        (v->text_encoding != OMC_TEXT_ASCII && v->text_encoding != OMC_TEXT_UTF8) ||
        !omc_value_shape_valid(v, &t->source->arena)) {
        t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
        return 0;
    }
    *bytes = omc_arena_view(&t->source->arena, v->u.ref);
    if (bytes->size > t->opts.max_text_bytes_per_property ||
        t->text_bytes > t->opts.max_total_text_bytes - bytes->size) {
        t->res.status = OMC_TRANSLATION_LIMIT;
        return 0;
    }
    t->text_bytes += bytes->size;
    omc_metadata_validate_opts_init(&opts);
    opts.validate_schema = 0;
    opts.validate_wire_hints = 0;
    checked = omc_validate_entry(t->source, id, NULL, 0U, &opts);
    if (checked.status != OMC_STATUS_OK) {
        t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
        return 0;
    }
    return 1;
}

static omc_translation_field *
omc_translation_field_add(omc_translation *t, const char *ifd, omc_u16 tag,
                          omc_u16 record, omc_entry_id id)
{
    omc_translation_field *f;
    f = &t->fields[t->field_count++];
    memset(f, 0, sizeof(*f));
    f->ifd = ifd;
    f->tag = tag;
    f->record = record;
    f->source = id;
    return f;
}

static int
omc_translation_field_text(omc_translation *t, omc_translation_field *f,
                           const void *bytes, omc_size size)
{
    omc_byte_ref ref;
    if (!omc_translation_status_ok(t,
                                   omc_arena_append(&t->edit.arena, bytes, size, &ref)))
        return 0;
    if (f->ifd == NULL)
        omc_val_make_bytes(&f->value, ref);
    else
        omc_val_make_text(&f->value, ref, OMC_TEXT_ASCII);
    f->present = 1;
    return 1;
}

static int
omc_translation_date(omc_translation *t, const char *ns, const char *name, int exif,
                     int which)
{
    omc_datetime d;
    omc_const_bytes bytes;
    omc_entry_id id;
    omc_translation_field *f;
    omc_u16 tag;
    char text[32];
    omc_size n;
    int deleted;
    if (!omc_translation_find(t, ns, name, 0, 0) || t->source_count == 0U)
        return t->res.status == OMC_TRANSLATION_OK;
    id = t->sources[0].id;
    deleted = (t->source->entries[id].flags & OMC_ENTRY_FLAG_DELETED) != 0U;
    memset(&d, 0, sizeof(d));
    if (!deleted) {
        if (!omc_translation_text(t, id, &bytes))
            return 0;
        if (!omc_datetime_parse(bytes.data, bytes.size, &d)) {
            t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
            return 0;
        }
        if ((exif && d.time[0] == 0) || (!exif && d.fraction[0] != 0)) {
            t->res.status = OMC_TRANSLATION_UNSUPPORTED_PRECISION;
            return 0;
        }
    }
    t->field_count = 0U;
    if (exif) {
        tag = which == 0 ? 0x0132U : which == 1 ? 0x9003U : 0x9004U;
        f = omc_translation_field_add(t, which == 0 ? "ifd0" : "exififd", tag, 0U, id);
        if (!deleted) {
            memcpy(text, d.date, 10U);
            text[4] = ':';
            text[7] = ':';
            text[10] = ' ';
            memcpy(text + 11U, d.time, 8U);
            if (!omc_translation_field_text(t, f, text, 19U))
                return 0;
        }
        f = omc_translation_field_add(t, "exififd", (omc_u16)(0x9010U + (omc_u16)which),
                                      0U, id);
        if (!deleted && d.offset[0] != 0 &&
            !omc_translation_field_text(t, f, d.offset, 6U))
            return 0;
        f = omc_translation_field_add(t, "exififd", (omc_u16)(0x9290U + (omc_u16)which),
                                      0U, id);
        if (!deleted && d.fraction[0] != 0 &&
            !omc_translation_field_text(t, f, d.fraction, strlen(d.fraction)))
            return 0;
    } else {
        f = omc_translation_field_add(t, NULL, (omc_u16)(which ? 62U : 55U), 2U, id);
        if (!deleted) {
            memcpy(text, d.date, 4U);
            memcpy(text + 4U, d.date + 5U, 2U);
            memcpy(text + 6U, d.date + 8U, 2U);
            if (!omc_translation_field_text(t, f, text, 8U))
                return 0;
        }
        f = omc_translation_field_add(t, NULL, (omc_u16)(which ? 63U : 60U), 2U, id);
        if (!deleted && d.time[0] != 0) {
            memcpy(text, d.time, 2U);
            memcpy(text + 2U, d.time + 3U, 2U);
            memcpy(text + 4U, d.time + 6U, 2U);
            n = 6U;
            if (d.offset[0] != 0) {
                memcpy(text + 6U, d.offset, 3U);
                memcpy(text + 9U, d.offset + 4U, 2U);
                n = 11U;
            }
            if (!omc_translation_field_text(t, f, text, n))
                return 0;
        }
    }
    return omc_translation_reconcile(t);
}

static omc_u64
omc_translation_gcd(omc_u64 a, omc_u64 b)
{
    omc_u64 r;
    while (b != 0U) {
        r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static int
omc_translation_digits(const omc_u8 *p, omc_size n, omc_u64 *value)
{
    omc_size i;
    omc_u64 v;
    if (n == 0U)
        return 0;
    v = 0U;
    for (i = 0U; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9' || v > (~(omc_u64)0 - (omc_u32)(p[i] - '0')) / 10U)
            return 0;
        v = v * 10U + (omc_u32)(p[i] - '0');
    }
    *value = v;
    return 1;
}

static int
omc_translation_number(omc_translation *t, omc_entry_id id, int focal, omc_u64 *numer,
                       omc_u64 *denom, int *negative)
{
    const omc_val *v;
    omc_const_bytes text;
    const omc_u8 *p;
    omc_size n;
    omc_size i;
    omc_size slash;
    omc_u64 g;
    omc_u64 digit;
    int fraction;
    int exponent;
    int exponent_sign;
    int have_digit;
    v = &t->source->entries[id].value;
    *numer = 0U;
    *denom = 1U;
    *negative = 0;
    if (!omc_value_shape_valid(v, &t->source->arena))
        goto invalid;
    if (v->kind == OMC_VAL_SCALAR) {
        if ((v->elem_type == OMC_ELEM_U8 && v->u.u64 > 255U) ||
            (v->elem_type == OMC_ELEM_U16 && v->u.u64 > 65535U) ||
            (v->elem_type == OMC_ELEM_U32 && v->u.u64 > 0xFFFFFFFFU) ||
            (v->elem_type == OMC_ELEM_I8 && (v->u.i64 < -128 || v->u.i64 > 127)) ||
            (v->elem_type == OMC_ELEM_I16 && (v->u.i64 < -32768 || v->u.i64 > 32767)) ||
            (v->elem_type == OMC_ELEM_I32 &&
             (v->u.i64 < (-2147483647 - 1) || v->u.i64 > 2147483647)))
            goto invalid;
        switch (v->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_U16:
        case OMC_ELEM_U32:
        case OMC_ELEM_U64:
            *numer = v->u.u64;
            break;
        case OMC_ELEM_I8:
        case OMC_ELEM_I16:
        case OMC_ELEM_I32:
        case OMC_ELEM_I64:
            *negative = v->u.i64 < 0;
            *numer = *negative ? (omc_u64)(-(v->u.i64 + 1)) + 1U : (omc_u64)v->u.i64;
            break;
        case OMC_ELEM_URATIONAL:
            *numer = v->u.ur.numer;
            *denom = v->u.ur.denom;
            break;
        case OMC_ELEM_SRATIONAL:
            *negative = (v->u.sr.numer < 0) != (v->u.sr.denom < 0);
            *numer = v->u.sr.numer < 0 ? (omc_u64)(-(omc_s64)v->u.sr.numer)
                                       : (omc_u64)v->u.sr.numer;
            *denom = v->u.sr.denom < 0 ? (omc_u64)(-(omc_s64)v->u.sr.denom)
                                       : (omc_u64)v->u.sr.denom;
            break;
        default:
            goto invalid;
        }
    } else {
        if (!omc_translation_text(t, id, &text))
            return 0;
        p = text.data;
        n = text.size;
        while (n != 0U && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            p++;
            n--;
        }
        while (n != 0U && (p[n - 1U] == ' ' || p[n - 1U] == '\t' || p[n - 1U] == '\r' ||
                           p[n - 1U] == '\n'))
            n--;
        if (focal && n >= 3U && memcmp(p + n - 3U, " mm", 3U) == 0)
            n -= 3U;
        if (n != 0U && (*p == '-' || *p == '+')) {
            *negative = *p == '-';
            p++;
            n--;
        }
        slash = n;
        for (i = 0U; i < n; ++i)
            if (p[i] == '/') {
                slash = i;
                break;
            }
        if (slash < n) {
            if (!omc_translation_digits(p, slash, numer) ||
                !omc_translation_digits(p + slash + 1U, n - slash - 1U, denom))
                goto invalid;
        } else {
            fraction = -1;
            exponent = 0;
            have_digit = 0;
            i = 0U;
            while (i < n) {
                if (p[i] == '.' && fraction == -1) {
                    fraction = 0;
                    i++;
                    continue;
                }
                if (p[i] < '0' || p[i] > '9')
                    break;
                digit = (omc_u64)(p[i++] - '0');
                if (*numer > (~(omc_u64)0 - digit) / 10U)
                    goto invalid;
                *numer = *numer * 10U + digit;
                have_digit = 1;
                if (fraction >= 0)
                    fraction++;
            }
            if (!have_digit)
                goto invalid;
            if (i < n && (p[i] == 'e' || p[i] == 'E')) {
                i++;
                exponent_sign = 1;
                if (i < n && (p[i] == '+' || p[i] == '-')) {
                    exponent_sign = p[i] == '-' ? -1 : 1;
                    i++;
                }
                if (i == n)
                    goto invalid;
                for (; i < n; ++i) {
                    if (p[i] < '0' || p[i] > '9' || exponent > 100)
                        goto invalid;
                    exponent = exponent * 10 + (p[i] - '0');
                }
                exponent *= exponent_sign;
            }
            if (i != n)
                goto invalid;
            if (fraction > 0)
                exponent -= fraction;
            if (exponent < -19 || exponent > 19)
                goto invalid;
            while (exponent < 0) {
                *denom *= 10U;
                exponent++;
            }
            g = omc_translation_gcd(*numer, *denom);
            *numer /= g;
            *denom /= g;
            while (exponent > 0) {
                if (*numer > ~(omc_u64)0 / 10U)
                    goto invalid;
                *numer *= 10U;
                exponent--;
            }
        }
    }
    if (*denom == 0U)
        goto invalid;
    g = omc_translation_gcd(*numer, *denom);
    *numer /= g;
    *denom /= g;
    if (*numer == 0U)
        *negative = 0;
    return 1;
invalid:
    t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
    return 0;
}

static int
omc_translation_capture(omc_translation *t, const char *path, const char *alias,
                        omc_u16 tag, int kind)
{
    omc_translation_field *f;
    omc_entry_id id;
    omc_u64 n;
    omc_u64 d;
    int negative;
    omc_urational ur;
    omc_srational sr;
    if (!omc_translation_find(t, omc_ns_exif, path, 0, 0))
        return 0;
    if (alias != NULL && !omc_translation_find(t, omc_ns_exif, alias, 0, 1))
        return 0;
    if (kind == 1 && !omc_translation_find(t, omc_ns_exif, "ISOSpeedRatings[1]", 0, 1))
        return 0;
    if (t->source_count == 0U)
        return 1;
    id = t->sources[0].id;
    t->field_count = 0U;
    f = omc_translation_field_add(t, "exififd", tag, 0U, id);
    if ((t->source->entries[id].flags & OMC_ENTRY_FLAG_DELETED) == 0U) {
        if (!omc_translation_number(t, id, kind == 2, &n, &d, &negative))
            return 0;
        if (kind == 1) {
            if (negative || n == 0U || n > 65535U || d != 1U)
                goto invalid;
            omc_val_make_u16(&f->value, (omc_u16)n);
        } else if (kind == 3) {
            if (n > (negative ? (omc_u64)2147483648U : 2147483647U) || d > 2147483647U)
                goto invalid;
            sr.numer = negative ? (omc_s32)(-(omc_s64)n) : (omc_s32)n;
            sr.denom = (omc_s32)d;
            omc_val_make_srational(&f->value, sr);
        } else {
            if (negative || n == 0U || n > 0xFFFFFFFFU || d > 0xFFFFFFFFU)
                goto invalid;
            ur.numer = (omc_u32)n;
            ur.denom = (omc_u32)d;
            omc_val_make_urational(&f->value, ur);
        }
        f->present = 1;
    }
    return omc_translation_reconcile(t);
invalid:
    t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
    return 0;
}

static int
omc_translation_text_group(omc_translation *t, const char *ns, const char *path,
                           const char *ifd, omc_u16 tag, int indexed, omc_u32 limit)
{
    omc_entry_id id;
    omc_translation_field *f;
    omc_const_bytes bytes;
    omc_u32 i;
    omc_size j;
    int utf8;
    omc_u32 preserved;
    if (!omc_translation_find(t, ns, path, indexed, 0) || t->source_count == 0U)
        return t->res.status == OMC_TRANSLATION_OK;
    t->field_count = 0U;
    utf8 = 0;
    id = t->sources[0].id;
    for (i = 0U; i < t->source_count; ++i) {
        f = omc_translation_field_add(t, ifd, tag, ifd != NULL ? 0U : 2U,
                                      t->sources[i].id);
        if ((t->source->entries[f->source].flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        if (!omc_translation_text(t, f->source, &bytes))
            return 0;
        if (bytes.size == 0U || bytes.size > limit) {
            t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
            return 0;
        }
        for (j = 0U; j < bytes.size; ++j) {
            if (bytes.data[j] >= 128U) {
                if (ifd != NULL) {
                    t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
                    return 0;
                }
                utf8 = 1;
                id = f->source;
            }
        }
        if (!omc_translation_field_text(t, f, bytes.data, bytes.size))
            return 0;
    }
    preserved = t->res.groups_preserved;
    if (!omc_translation_reconcile(t))
        return 0;
    if (utf8 && preserved == t->res.groups_preserved)
        t->utf8_source = id;
    return 1;
}

static int
omc_translation_charset(omc_translation *t)
{
    static const omc_u8 utf8[] = {0x1BU, 0x25U, 0x47U};
    omc_size i;
    omc_size j;
    omc_size k;
    omc_size found;
    omc_const_bytes bytes;
    omc_translation_field *f;
    int owned;
    if (t->utf8_source == OMC_INVALID_ENTRY_ID)
        return 1;
    found = 0U;
    for (i = 0U; i < t->source->entry_count; ++i) {
        const omc_entry *e;
        e = &t->source->entries[i];
        if (e->key.kind != OMC_KEY_IPTC_DATASET ||
            (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            e->key.u.iptc_dataset.record != 1U || e->key.u.iptc_dataset.dataset != 90U)
            continue;
        bytes = omc_arena_view(&t->source->arena, e->value.u.ref);
        if (++found > 1U ||
            (e->value.kind != OMC_VAL_TEXT && e->value.kind != OMC_VAL_BYTES) ||
            bytes.size != 3U || memcmp(bytes.data, utf8, 3U) != 0)
            goto conflict;
    }
    if (found != 0U)
        return 1;
    for (i = 0U; i < t->source->entry_count; ++i) {
        const omc_entry *e;
        e = &t->source->entries[i];
        if (e->key.kind != OMC_KEY_IPTC_DATASET ||
            (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            (e->value.kind != OMC_VAL_TEXT && e->value.kind != OMC_VAL_BYTES))
            continue;
        owned = t->owned_iptc[i] != 0U;
        for (k = 0U; k < t->edit.op_count; ++k) {
            if (t->edit.ops[k].kind != OMC_EDIT_OP_ADD_ENTRY &&
                t->edit.ops[k].target == i) {
                owned = 1;
                break;
            }
        }
        if (owned)
            continue;
        bytes = omc_arena_view(&t->source->arena, e->value.u.ref);
        for (j = 0U; j < bytes.size; ++j)
            if (bytes.data[j] >= 128U)
                goto conflict;
    }
    t->field_count = 0U;
    f = omc_translation_field_add(t, NULL, 90U, 1U, t->utf8_source);
    if (!omc_translation_field_text(t, f, utf8, 3U) || !omc_translation_reconcile(t))
        return 0;
    t->res.utf8_charset_added = 1;
    return 1;
conflict:
    t->res.status = OMC_TRANSLATION_ENCODING_CONFLICT;
    return 0;
}

static int
omc_translation_geometry(omc_translation *t,
                         const omc_transfer_target_image_spec *target, int dimensions)
{
    static const char *const widths[] = {"ImageWidth", "ExifImageWidth",
                                         "PixelXDimension"};
    static const char *const heights[] = {"ImageLength", "ImageHeight",
                                          "ExifImageHeight", "PixelYDimension"};
    omc_u32 values[2];
    omc_entry_id ids[2];
    omc_u32 side;
    omc_u32 i;
    omc_u32 j;
    omc_u64 n;
    omc_u64 d;
    int negative;
    int was_all;
    int dirty;
    int deleted[2];
    omc_size scan;
    const omc_entry *source_entry;
    omc_translation_field *f;
    was_all = t->opts.all_sources;
    if (!was_all) {
        dirty = 0;
        for (scan = 0U; scan < t->source->entry_count; ++scan) {
            source_entry = &t->source->entries[scan];
            if (source_entry->key.kind != OMC_KEY_XMP_PROPERTY ||
                (source_entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U)
                continue;
            if (!dimensions) {
                if (omc_translation_ref_equal(
                        &t->source->arena, source_entry->key.u.xmp_property.schema_ns,
                        omc_ns_tiff) &&
                    omc_translation_ref_equal(
                        &t->source->arena,
                        source_entry->key.u.xmp_property.property_path, "Orientation"))
                    dirty = 1;
            } else {
                for (j = 0U; j < 3U; ++j) {
                    if (omc_translation_ref_equal(
                            &t->source->arena,
                            source_entry->key.u.xmp_property.schema_ns,
                            j == 0U ? omc_ns_tiff : omc_ns_exif) &&
                        omc_translation_ref_equal(
                            &t->source->arena,
                            source_entry->key.u.xmp_property.property_path, widths[j]))
                        dirty = 1;
                }
                for (j = 0U; j < 4U; ++j) {
                    if (omc_translation_ref_equal(
                            &t->source->arena,
                            source_entry->key.u.xmp_property.schema_ns,
                            j < 2U ? omc_ns_tiff : omc_ns_exif) &&
                        omc_translation_ref_equal(
                            &t->source->arena,
                            source_entry->key.u.xmp_property.property_path, heights[j]))
                        dirty = 1;
                }
            }
        }
        if (!dirty)
            return 1;
    }
    t->opts.all_sources = 1;
    dirty = was_all;
    values[0] = 0U;
    values[1] = 0U;
    ids[0] = OMC_INVALID_ENTRY_ID;
    ids[1] = OMC_INVALID_ENTRY_ID;
    deleted[0] = 0;
    deleted[1] = 0;
    for (side = 0U; side < (dimensions ? 2U : 1U); ++side) {
        t->source_count = 0U;
        if (!dimensions) {
            if (!omc_translation_find(t, omc_ns_tiff, "Orientation", 0, 2))
                goto fail;
        } else {
            for (j = 0U; j < (side == 0U ? 3U : 4U); ++j) {
                if (!omc_translation_find(
                        t, (side == 0U ? j < 1U : j < 2U) ? omc_ns_tiff : omc_ns_exif,
                        side == 0U ? widths[j] : heights[j], 0, 2))
                    goto fail;
            }
        }
        for (i = 0U; i < t->source_count; ++i) {
            const omc_entry *e;
            e = &t->source->entries[t->sources[i].id];
            if ((e->flags & OMC_ENTRY_FLAG_DIRTY) != 0U)
                dirty = 1;
            if ((e->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
                deleted[side] = 1;
            else {
                if (!omc_translation_number(t, t->sources[i].id, 0, &n, &d, &negative))
                    goto fail;
                if (negative || d != 1U || n == 0U ||
                    n > (dimensions ? 0xFFFFFFFFU : 8U))
                    goto invalid;
                if (values[side] != 0U && values[side] != n)
                    goto invalid;
                values[side] = (omc_u32)n;
            }
            if (ids[side] == OMC_INVALID_ENTRY_ID)
                ids[side] = t->sources[i].id;
        }
    }
    t->opts.all_sources = was_all;
    if (!dirty || (ids[0] == OMC_INVALID_ENTRY_ID && ids[1] == OMC_INVALID_ENTRY_ID))
        return 1;
    if (deleted[0] || deleted[1]) {
        if (values[0] != 0U || values[1] != 0U ||
            (dimensions && (!deleted[0] || !deleted[1]))) {
            t->res.status = OMC_TRANSLATION_AMBIGUOUS_SOURCE;
            return 0;
        }
        if (target != NULL &&
            (dimensions ? target->has_dimensions : target->has_orientation)) {
            t->res.status = OMC_TRANSLATION_TARGET_MISMATCH;
            return 0;
        }
    } else if (target == NULL ||
               (dimensions ? !target->has_dimensions : !target->has_orientation)) {
        t->res.status = OMC_TRANSLATION_TARGET_REQUIRED;
        return 0;
    } else if ((dimensions &&
                (ids[0] == OMC_INVALID_ENTRY_ID || ids[1] == OMC_INVALID_ENTRY_ID)) ||
               (dimensions ? values[0] != target->width || values[1] != target->height
                           : values[0] != target->orientation)) {
        t->res.status = OMC_TRANSLATION_TARGET_MISMATCH;
        return 0;
    }
    t->field_count = 0U;
    if (!dimensions) {
        f = omc_translation_field_add(t, "ifd0", 0x0112U, 0U, ids[0]);
        omc_val_make_u16(&f->value, (omc_u16)values[0]);
        f->present = !deleted[0];
    } else {
        for (j = 0U; j < 4U; ++j) {
            side = j % 2U;
            f = omc_translation_field_add(
                t, j < 2U ? "ifd0" : "exififd",
                (omc_u16)((j < 2U ? 0x0100U : 0xA002U) + side), 0U, ids[0]);
            omc_val_make_u32(&f->value, values[side]);
            f->present = !deleted[side];
        }
    }
    return omc_translation_reconcile(t);
invalid:
    t->res.status = OMC_TRANSLATION_INVALID_SOURCE;
fail:
    t->opts.all_sources = was_all;
    return 0;
}

omc_translation_res
omc_translate_xmp(const omc_store *source, omc_store *out,
                  const omc_translation_opts *options,
                  const omc_transfer_target_image_spec *target)
{
    omc_translation t;
    omc_u32 bit;
    int ok;
    omc_status status;
    memset(&t, 0, sizeof(t));
    t.source = source;
    t.utf8_source = OMC_INVALID_ENTRY_ID;
    t.res.failed_source = OMC_INVALID_ENTRY_ID;
    omc_translation_opts_init(&t.opts);
    if (options != NULL)
        t.opts = *options;
    if (!omc_store_shape_valid(source) || !omc_store_shape_valid(out) ||
        source == out || (t.opts.mappings & ~OMC_TRANSLATE_ALL) != 0U ||
        t.opts.conflict < OMC_TRANSLATION_PRESERVE ||
        t.opts.conflict > OMC_TRANSLATION_REPLACE ||
        t.opts.max_source_properties == 0U || t.opts.max_source_properties > 65536U ||
        t.opts.max_operations == 0U || t.opts.max_added_entries == 0U ||
        t.opts.max_text_bytes_per_property == 0U ||
        t.opts.max_total_text_bytes < t.opts.max_text_bytes_per_property ||
        source->entry_count > 200000U) {
        t.res.status = OMC_TRANSLATION_INVALID_OPTIONS;
        return t.res;
    }
    t.fields = (omc_translation_field *)calloc(
        (omc_size)t.opts.max_source_properties + 4U, sizeof(*t.fields));
    t.sources = (omc_translation_source *)calloc(t.opts.max_source_properties,
                                                 sizeof(*t.sources));
    t.owned_iptc = (omc_u8 *)calloc(source->entry_count + 1U, 1U);
    omc_edit_init(&t.edit);
    if (t.fields == NULL || t.sources == NULL || t.owned_iptc == NULL) {
        t.res.status = OMC_TRANSLATION_NO_MEMORY;
        goto done;
    }
    for (bit = 1U; bit <= OMC_TRANSLATE_DIMENSIONS; bit <<= 1U) {
        if ((t.opts.mappings & bit) == 0U)
            continue;
        t.res.failed_mapping = bit;
        t.res.failed_source = OMC_INVALID_ENTRY_ID;
        ok = 1;
        switch (bit) {
        case OMC_TRANSLATE_CREATE_EXIF:
            ok = omc_translation_date(&t, omc_ns_xmp, "CreateDate", 1, 2);
            break;
        case OMC_TRANSLATE_CREATE_IPTC:
            ok = omc_translation_date(&t, omc_ns_xmp, "CreateDate", 0, 1);
            break;
        case OMC_TRANSLATE_DATE_CREATED:
            ok = omc_translation_date(&t, omc_ns_ps, "DateCreated", 0, 0);
            break;
        case OMC_TRANSLATE_DATE_ORIGINAL:
            ok = omc_translation_date(&t, omc_ns_exif, "DateTimeOriginal", 1, 1);
            break;
        case OMC_TRANSLATE_MODIFY_DATE:
            ok = omc_translation_date(&t, omc_ns_xmp, "ModifyDate", 1, 0);
            break;
        case OMC_TRANSLATE_MAKE:
            ok = omc_translation_text_group(&t, omc_ns_tiff, "Make", "ifd0", 0x010FU, 0,
                                            4096U);
            break;
        case OMC_TRANSLATE_MODEL:
            ok = omc_translation_text_group(&t, omc_ns_tiff, "Model", "ifd0", 0x0110U,
                                            0, 4096U);
            break;
        case OMC_TRANSLATE_SOFTWARE:
            ok = omc_translation_text_group(&t, omc_ns_xmp, "CreatorTool", "ifd0",
                                            0x0131U, 0, 4096U);
            break;
        case OMC_TRANSLATE_EXPOSURE:
            ok = omc_translation_capture(&t, "ExposureTime", NULL, 0x829AU, 0);
            break;
        case OMC_TRANSLATE_FNUMBER:
            ok = omc_translation_capture(&t, "FNumber", NULL, 0x829DU, 0);
            break;
        case OMC_TRANSLATE_ISO:
            ok = omc_translation_capture(&t, "ISO", "ISOSpeedRatings", 0x8827U, 1);
            break;
        case OMC_TRANSLATE_FOCAL_LENGTH:
            ok = omc_translation_capture(&t, "FocalLength", NULL, 0x920AU, 2);
            break;
        case OMC_TRANSLATE_EXPOSURE_BIAS:
            ok = omc_translation_capture(&t, "ExposureCompensation",
                                         "ExposureBiasValue", 0x9204U, 3);
            break;
        case OMC_TRANSLATE_TITLE:
            ok = omc_translation_text_group(&t, omc_ns_dc, "title[@xml:lang=x-default]",
                                            NULL, 5U, 0, 64U);
            break;
        case OMC_TRANSLATE_DESCRIPTION:
            ok = omc_translation_text_group(&t, omc_ns_dc,
                                            "description[@xml:lang=x-default]", NULL,
                                            120U, 0, 2000U);
            break;
        case OMC_TRANSLATE_CREATORS:
            ok =
                omc_translation_text_group(&t, omc_ns_dc, "creator", NULL, 80U, 1, 32U);
            break;
        case OMC_TRANSLATE_KEYWORDS:
            ok =
                omc_translation_text_group(&t, omc_ns_dc, "subject", NULL, 25U, 1, 64U);
            break;
        case OMC_TRANSLATE_RIGHTS:
            ok = omc_translation_text_group(
                &t, omc_ns_dc, "rights[@xml:lang=x-default]", NULL, 116U, 0, 128U);
            break;
        case OMC_TRANSLATE_CREDIT:
            ok =
                omc_translation_text_group(&t, omc_ns_ps, "Credit", NULL, 110U, 0, 32U);
            break;
        case OMC_TRANSLATE_SOURCE:
            ok =
                omc_translation_text_group(&t, omc_ns_ps, "Source", NULL, 115U, 0, 32U);
            break;
        case OMC_TRANSLATE_ORIENTATION:
            ok = omc_translation_geometry(&t, target, 0);
            break;
        case OMC_TRANSLATE_DIMENSIONS:
            ok = omc_translation_geometry(&t, target, 1);
            break;
        default:
            break;
        }
        if (!ok)
            goto done;
    }
    if (!omc_translation_charset(&t))
        goto done;
    status = omc_edit_commit(source, &t.edit, 1U, out);
    if (!omc_translation_status_ok(&t, status))
        goto done;
    t.res.failed_mapping = 0U;
    t.res.failed_source = OMC_INVALID_ENTRY_ID;
done:
    omc_edit_fini(&t.edit);
    free(t.sources);
    free(t.fields);
    free(t.owned_iptc);
    return t.res;
}
