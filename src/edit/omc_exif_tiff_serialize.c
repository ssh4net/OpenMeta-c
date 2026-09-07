#include "omc/omc_exif_tiff_serialize.h"
#include "omc/omc_store_validate.h"
#include "../core/omc_value_internal.h"

#include <stdlib.h>
#include <string.h>

/* Sparse directory keys avoid allocating in proportion to untrusted IFD indices. */
typedef struct omc_tiff_entry {
    omc_u64 group;
    omc_entry_id source;
    omc_u32 order;
    omc_u16 tag;
    omc_u16 type;
    omc_u32 count;
    omc_u32 size;
    omc_u32 offset;
    omc_u32 pointer;
} omc_tiff_entry;

typedef struct omc_tiff_dir {
    omc_u64 group;
    omc_size first;
    omc_u32 count;
    omc_u32 offset;
} omc_tiff_dir;

void
omc_exif_tiff_opts_init(omc_exif_tiff_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->validate = 1;
    opts->honor_wire_type_hints = 1;
    opts->max_output_bytes = 64U * 1024U * 1024U;
}

static omc_u64
omc_subifd_base(void)
{
    return (omc_u64)0xFFFFFFFFU + 5U;
}

static int
omc_tiff_group(omc_const_bytes name, omc_u64 *group)
{
    static const char *const names[] = {"ifd0", "exififd", "gpsifd", "interopifd"};
    omc_size i;
    omc_u64 n;
    int sub;
    for (i = 0U; i < 4U; ++i) {
        if (name.size == strlen(names[i]) &&
            memcmp(name.data, names[i], name.size) == 0) {
            *group = i;
            return 1;
        }
    }
    sub = name.size > 6U && memcmp(name.data, "subifd", 6U) == 0;
    if (sub)
        i = 6U;
    else if (name.size > 3U && memcmp(name.data, "ifd", 3U) == 0)
        i = 3U;
    else
        return 0;
    n = 0U;
    for (; i < name.size; ++i) {
        if (name.data[i] < '0' || name.data[i] > '9')
            return 0;
        n = n * 10U + (omc_u32)(name.data[i] - '0');
        if (n > 0xFFFFFFFFU)
            return 0;
    }
    if (!sub && n == 0U)
        return 0;
    *group = sub ? omc_subifd_base() + n : 4U + n;
    return 1;
}

static int
omc_tiff_compare(const void *a, const void *b)
{
    const omc_tiff_entry *x;
    const omc_tiff_entry *y;
    x = (const omc_tiff_entry *)a;
    y = (const omc_tiff_entry *)b;
    if (x->group != y->group)
        return x->group < y->group ? -1 : 1;
    if (x->tag != y->tag)
        return x->tag < y->tag ? -1 : 1;
    if (x->order != y->order)
        return x->order < y->order ? -1 : 1;
    return x->source < y->source ? -1 : x->source != y->source;
}

static void
omc_tiff_put(omc_u8 *p, omc_u64 v, omc_u32 n)
{
    omc_u32 i;
    for (i = 0U; i < n; ++i)
        p[i] = (omc_u8)(v >> (i * 8U));
}

static void
omc_tiff_scalar(omc_u8 *p, const omc_val *v)
{
    switch (v->elem_type) {
    case OMC_ELEM_URATIONAL:
        omc_tiff_put(p, v->u.ur.numer, 4U);
        omc_tiff_put(p + 4U, v->u.ur.denom, 4U);
        break;
    case OMC_ELEM_SRATIONAL:
        omc_tiff_put(p, (omc_u32)v->u.sr.numer, 4U);
        omc_tiff_put(p + 4U, (omc_u32)v->u.sr.denom, 4U);
        break;
    case OMC_ELEM_F32_BITS:
        omc_tiff_put(p, v->u.f32_bits, 4U);
        break;
    case OMC_ELEM_F64_BITS:
        omc_tiff_put(p, v->u.f64_bits, 8U);
        break;
    case OMC_ELEM_I8:
    case OMC_ELEM_I16:
    case OMC_ELEM_I32:
    case OMC_ELEM_I64:
        omc_tiff_put(p, (omc_u64)v->u.i64, omc_elem_size(v->elem_type));
        break;
    default:
        omc_tiff_put(p, v->u.u64, omc_elem_size(v->elem_type));
        break;
    }
}

static void
omc_tiff_synthetic(omc_tiff_entry *e, omc_u64 group, omc_u16 tag, omc_u32 count)
{
    memset(e, 0, sizeof(*e));
    e->group = group;
    e->source = OMC_INVALID_ENTRY_ID;
    e->order = 0xFFFFFFFFU;
    e->tag = tag;
    e->type = 4U;
    e->count = count;
    e->size = count * 4U;
}

omc_exif_tiff_res
omc_serialize_exif_tiff(const omc_store *store, omc_mut_bytes output,
                        const omc_exif_tiff_opts *options)
{
    omc_exif_tiff_opts opts;
    omc_exif_tiff_res res;
    omc_metadata_validate_res validated;
    omc_tiff_entry *entries;
    omc_tiff_entry *e;
    omc_tiff_dir *dirs;
    const omc_entry *source;
    const omc_val *value;
    omc_val scalar;
    omc_const_bytes name;
    omc_size count;
    omc_size dir_count;
    omc_size i;
    omc_size j;
    omc_size k;
    omc_u32 width;
    omc_u32 sub_count;
    omc_u32 next;
    omc_u64 cursor;
    omc_u64 group;
    omc_u64 size;
    omc_u8 *bytes;
    omc_u8 *p;
    omc_u8 *dst;
    int present[4];
    int dng;
    memset(&res, 0, sizeof(res));
    omc_exif_tiff_opts_init(&opts);
    if (options != NULL)
        opts = *options;
    if (!omc_store_shape_valid(store) || (output.size != 0U && output.data == NULL) ||
        opts.max_output_bytes == 0U || opts.preserve_opaque_makernote < 0 ||
        opts.preserve_opaque_makernote > 1) {
        res.status = OMC_EXIF_TIFF_INVALID_OPTIONS;
        return res;
    }
    if (opts.validate) {
        validated = omc_validate_store(store, NULL, 0U, NULL);
        if (validated.status != OMC_STATUS_OK) {
            res.status = OMC_EXIF_TIFF_INVALID_METADATA;
            return res;
        }
    }
    if (store->entry_count > ((omc_size)-1) / sizeof(*entries) - 5U ||
        store->entry_count > ((omc_size)-1) / sizeof(*dirs) - 5U ||
        store->entry_count > 0xFFFFFFFAU) {
        res.status = OMC_EXIF_TIFF_LIMIT;
        return res;
    }
    entries = (omc_tiff_entry *)calloc(store->entry_count + 5U, sizeof(*entries));
    dirs = (omc_tiff_dir *)calloc(store->entry_count + 5U, sizeof(*dirs));
    bytes = NULL;
    if (entries == NULL || dirs == NULL) {
        res.status = OMC_EXIF_TIFF_NO_MEMORY;
        goto done;
    }
    memset(present, 0, sizeof(present));
    count = 0U;
    sub_count = 0U;
    dng = 0;
    for (i = 0U; i < store->entry_count; ++i) {
        source = &store->entries[i];
        if (source->key.kind != OMC_KEY_EXIF_TAG ||
            (source->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        if (!omc_ref_valid(&store->arena, source->key.u.exif_tag.ifd) ||
            !omc_value_shape_valid(&source->value, &store->arena)) {
            res.status = OMC_EXIF_TIFF_INVALID_METADATA;
            goto done;
        }
        name = omc_arena_view(&store->arena, source->key.u.exif_tag.ifd);
        if (!omc_tiff_group(name, &group) ||
            (!opts.include_subifds && group >= omc_subifd_base())) {
            res.entries_skipped++;
            continue;
        }
        if (group == 0U && source->key.u.exif_tag.tag == 0xC612U)
            dng = 1;
        if ((group == 0U && (source->key.u.exif_tag.tag == 0x8769U ||
                             source->key.u.exif_tag.tag == 0x8825U ||
                             source->key.u.exif_tag.tag == 0x014AU)) ||
            (group == 1U && source->key.u.exif_tag.tag == 0xA005U) ||
            (group < 4U && source->key.u.exif_tag.tag == 0x927CU &&
             !opts.preserve_opaque_makernote))
            continue;
        if (group == 0U && (source->key.u.exif_tag.tag == 0x0190U ||
                            source->key.u.exif_tag.tag == 0xC6F5U)) {
            res.entries_skipped++;
            continue;
        }
        e = &entries[count];
        value = &source->value;
        e->type = omc_value_tiff_type(value);
        if (opts.honor_wire_type_hints && value->kind == OMC_VAL_BYTES &&
            source->origin.wire_type.family == OMC_WIRE_TIFF &&
            (source->origin.wire_type.code == 1U ||
             source->origin.wire_type.code == 6U ||
             source->origin.wire_type.code == 7U))
            e->type = source->origin.wire_type.code;
        if (e->type == 0U || value->kind == OMC_VAL_EMPTY) {
            res.entries_skipped++;
            continue;
        }
        e->group = group;
        e->source = (omc_entry_id)i;
        e->order = source->origin.order_in_block;
        e->tag = source->key.u.exif_tag.tag;
        e->count = value->count;
        if (value->kind == OMC_VAL_TEXT) {
            if (value->count == 0xFFFFFFFFU) {
                res.status = OMC_EXIF_TIFF_LIMIT;
                goto done;
            }
            e->count++;
        }
        size = value->kind >= OMC_VAL_BYTES
                   ? e->count
                   : (omc_u64)e->count * omc_elem_size(value->elem_type);
        if (size > 0xFFFFFFFFU || size > opts.max_output_bytes) {
            res.status = OMC_EXIF_TIFF_LIMIT;
            goto done;
        }
        e->size = (omc_u32)size;
        if (group < 4U)
            present[(omc_size)group] = 1;
        count++;
        res.entries_serialized++;
    }
    if (opts.inject_minimal_dng_version && !dng) {
        omc_tiff_synthetic(&entries[count], 0U, 0xC612U, 1U);
        entries[count].type = 1U;
        entries[count].count = 4U;
        entries[count++].pointer = 0x00000401U;
        present[0] = 1;
    }
    if (count == 0U) {
        res.status = OMC_EXIF_TIFF_NO_EXIF_DATA;
        goto done;
    }
    present[0] = 1;
    if (present[3])
        present[1] = 1;
    if (present[1])
        omc_tiff_synthetic(&entries[count++], 0U, 0x8769U, 1U);
    if (present[2])
        omc_tiff_synthetic(&entries[count++], 0U, 0x8825U, 1U);
    if (present[3])
        omc_tiff_synthetic(&entries[count++], 1U, 0xA005U, 1U);
    qsort(entries, count, sizeof(*entries), omc_tiff_compare);
    group = 0U;
    for (i = 0U; i < count; ++i) {
        if (entries[i].group >= omc_subifd_base() && entries[i].group != group)
            sub_count++;
        group = entries[i].group;
    }
    if (sub_count != 0U) {
        if (sub_count > 0x3FFFFFFFU) {
            res.status = OMC_EXIF_TIFF_LIMIT;
            goto done;
        }
        omc_tiff_synthetic(&entries[count++], 0U, 0x014AU, sub_count);
        qsort(entries, count, sizeof(*entries), omc_tiff_compare);
    }
    dir_count = 1U;
    dirs[0].group = 0U;
    for (i = 0U; i < count; ++i) {
        if (entries[i].group != dirs[dir_count - 1U].group) {
            dirs[dir_count].group = entries[i].group;
            dirs[dir_count].first = i;
            dir_count++;
        }
        dirs[dir_count - 1U].count++;
    }
    cursor = 8U;
    for (i = 0U; i < dir_count; ++i) {
        if (dirs[i].count > 65535U || cursor > 0xFFFFFFFFU) {
            res.status = OMC_EXIF_TIFF_LIMIT;
            goto done;
        }
        dirs[i].offset = (omc_u32)cursor;
        cursor += 6U + (omc_u64)12U * dirs[i].count;
    }
    for (i = 0U; i < count; ++i) {
        e = &entries[i];
        if (e->size <= 4U)
            continue;
        cursor = (cursor + 1U) & ~(omc_u64)1U;
        if (cursor > 0xFFFFFFFFU) {
            res.status = OMC_EXIF_TIFF_LIMIT;
            goto done;
        }
        e->offset = (omc_u32)cursor;
        cursor += e->size;
    }
    if (cursor > opts.max_output_bytes || cursor > 0xFFFFFFFFU ||
        cursor > (omc_size)-1) {
        res.status = OMC_EXIF_TIFF_LIMIT;
        goto done;
    }
    res.needed = cursor;
    if (output.size == 0U) {
        res.status = OMC_EXIF_TIFF_OUTPUT_TRUNCATED;
        goto done;
    }
    bytes = (omc_u8 *)calloc((omc_size)cursor, 1U);
    if (bytes == NULL) {
        res.status = OMC_EXIF_TIFF_NO_MEMORY;
        goto done;
    }
    bytes[0] = 'I';
    bytes[1] = 'I';
    bytes[2] = 42U;
    omc_tiff_put(bytes + 4U, 8U, 4U);
    for (i = 0U; i < dir_count; ++i) {
        p = bytes + dirs[i].offset;
        omc_tiff_put(p, dirs[i].count, 2U);
        p += 2U;
        for (j = 0U; j < dirs[i].count; ++j, p += 12U) {
            e = &entries[dirs[i].first + j];
            omc_tiff_put(p, e->tag, 2U);
            omc_tiff_put(p + 2U, e->type, 2U);
            omc_tiff_put(p + 4U, e->count, 4U);
            if (e->size <= 4U)
                dst = p + 8U;
            else {
                omc_tiff_put(p + 8U, e->offset, 4U);
                dst = bytes + e->offset;
            }
            if (e->source == OMC_INVALID_ENTRY_ID) {
                if (e->tag == 0xC612U)
                    omc_tiff_put(dst, e->pointer, 4U);
                else {
                    next = 0U;
                    for (k = 0U; k < dir_count; ++k) {
                        if ((e->tag == 0x8769U && dirs[k].group == 1U) ||
                            (e->tag == 0x8825U && dirs[k].group == 2U) ||
                            (e->tag == 0xA005U && dirs[k].group == 3U) ||
                            (e->tag == 0x014AU && dirs[k].group >= omc_subifd_base())) {
                            omc_tiff_put(dst + next, dirs[k].offset, 4U);
                            next += 4U;
                        }
                    }
                }
            } else {
                value = &store->entries[e->source].value;
                if (value->kind == OMC_VAL_TEXT || value->kind == OMC_VAL_BYTES) {
                    if (value->u.ref.size != 0U)
                        memcpy(dst, store->arena.data + value->u.ref.offset,
                               value->u.ref.size);
                } else if (value->kind == OMC_VAL_SCALAR)
                    omc_tiff_scalar(dst, value);
                else {
                    width = omc_elem_size(value->elem_type);
                    for (k = 0U; k < value->count; ++k) {
                        omc_value_array_scalar(value, &store->arena, (omc_u32)k,
                                               &scalar);
                        omc_tiff_scalar(dst + k * width, &scalar);
                    }
                }
            }
        }
        next = 0U;
        if (dirs[i].group == 0U ||
            (dirs[i].group > 3U && dirs[i].group < omc_subifd_base())) {
            for (j = i + 1U; j < dir_count; ++j) {
                if (dirs[j].group > 3U && dirs[j].group < omc_subifd_base()) {
                    next = dirs[j].offset;
                    break;
                }
            }
        }
        omc_tiff_put(p, next, 4U);
    }
    res.needed = cursor;
    res.written = output.size < cursor ? output.size : cursor;
    if (res.written != 0U)
        memcpy(output.data, bytes, (omc_size)res.written);
    res.status =
        res.written < res.needed ? OMC_EXIF_TIFF_OUTPUT_TRUNCATED : OMC_EXIF_TIFF_OK;
done:
    free(bytes);
    free(dirs);
    free(entries);
    return res;
}
