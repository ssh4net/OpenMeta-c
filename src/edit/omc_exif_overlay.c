#include "omc_exif_write.h"
#include <stdlib.h>
#include <string.h>

/* Append replacement metadata directories. Retained entries keep their original
 * absolute offsets, including image strips, tiles, previews, and opaque data. */
typedef struct omc_overlay_dir {
    const omc_u8 *entries;
    omc_u32 count;
    omc_u64 next;
} omc_overlay_dir;

static omc_u64
omc_overlay_get(const omc_u8 *p, unsigned n)
{
    omc_u64 v;
    unsigned i;
    v = 0U;
    for (i = 0U; i < n; ++i)
        v |= (omc_u64)p[i] << (i * 8U);
    return v;
}

static void
omc_overlay_put(omc_u8 *p, omc_u64 v, unsigned n)
{
    unsigned i;
    for (i = 0U; i < n; ++i)
        p[i] = (omc_u8)(v >> (i * 8U));
}

static int
omc_overlay_parse(const omc_u8 *bytes, omc_size size, omc_u64 off, int big,
                  omc_overlay_dir *dir)
{
    omc_u64 count;
    omc_size cs;
    omc_size es;
    omc_size ns;
    memset(dir, 0, sizeof(*dir));
    if (off == 0U)
        return 1;
    cs = big ? 8U : 2U;
    es = big ? 20U : 12U;
    ns = big ? 8U : 4U;
    if (off > size || cs > size - (omc_size)off)
        return 0;
    count = omc_overlay_get(bytes + (omc_size)off, (unsigned)cs);
    if (count > 65535U || count * es + ns > size - (omc_size)off - cs)
        return 0;
    dir->entries = bytes + (omc_size)off + cs;
    dir->count = (omc_u32)count;
    dir->next = omc_overlay_get(dir->entries + dir->count * es, (unsigned)ns);
    return 1;
}

static int
omc_overlay_child(const omc_overlay_dir *dir, int big, omc_u16 tag, omc_u64 *offset)
{
    omc_u32 i;
    omc_size es;
    const omc_u8 *e;
    omc_u64 type;
    int found;
    es = big ? 20U : 12U;
    *offset = 0U;
    found = 0;
    for (i = 0U; i < dir->count; ++i) {
        e = dir->entries + i * es;
        if (omc_overlay_get(e, 2U) != tag)
            continue;
        type = omc_overlay_get(e + 2U, 2U);
        if (found ||
            (type != 4U && type != 13U && !(big && (type == 16U || type == 18U))) ||
            omc_overlay_get(e + 4U, big ? 8U : 4U) != 1U)
            return 0;
        found = 1;
        *offset = omc_overlay_get(e + (big ? 12U : 8U),
                                  (big && (type == 16U || type == 18U)) ? 8U : 4U);
    }
    return 1;
}

static int
omc_overlay_compare(const void *a, const void *b)
{
    omc_u64 x;
    omc_u64 y;
    x = omc_overlay_get((const omc_u8 *)a, 2U);
    y = omc_overlay_get((const omc_u8 *)b, 2U);
    return x < y ? -1 : x != y;
}

static int
omc_overlay_layout_tag(omc_u16 tag)
{
    return tag == 0x100U || tag == 0x101U || tag == 0x102U || tag == 0x103U ||
           tag == 0x106U || tag == 0x111U || tag == 0x115U || tag == 0x116U ||
           tag == 0x117U || tag == 0x11CU || tag == 0x142U || tag == 0x143U ||
           tag == 0x144U || tag == 0x145U || tag == 0x14AU || tag == 0x153U ||
           tag == 0x201U || tag == 0x202U || tag == 0xC612U || tag == 0xC613U;
}

static omc_status
omc_overlay_append(omc_arena *out, const void *bytes, omc_size size)
{
    omc_byte_ref ref;
    return omc_arena_append(out, bytes, size, &ref);
}

omc_status
omc_exif_overlay_tiff(const omc_u8 *file, omc_size size, const omc_store *source,
                      const omc_arena *canonical, omc_arena *out,
                      omc_exif_write_res *res)
{
    static const char *const names[] = {"ifd0", "exififd", "gpsifd", "interopifd"};
    static const omc_u16 ptrtags[] = {0U, 0x8769U, 0x8825U, 0xA005U};
    static const unsigned widths[] = {0U, 1U, 1U, 2U, 4U, 8U, 1U,
                                      1U, 2U, 4U, 8U, 4U, 8U};
    omc_overlay_dir old[4];
    omc_overlay_dir add[4];
    omc_u64 oldoff[4];
    omc_u64 newoff[4];
    omc_u64 written[4];
    omc_u8 *records;
    omc_u8 *claims;
    omc_u8 *e;
    const omc_u8 *in;
    omc_u8 zero[8];
    omc_u8 countbytes[8];
    omc_u64 nbytes;
    omc_u64 valueoff;
    omc_u64 count;
    omc_size es;
    omc_size cs;
    omc_size ns;
    omc_size align;
    omc_size capacity;
    omc_size used;
    omc_size i;
    omc_size j;
    omc_u16 tag;
    omc_u16 type;
    omc_const_bytes name;
    int big;
    int d;
    int child;
    omc_status status;
    records = NULL;
    claims = NULL;
    memset(zero, 0, sizeof(zero));
    memset(written, 0, sizeof(written));
    res->status = OMC_EXIF_WRITE_MALFORMED;
    if (size < 8U || file[0] != 'I' || file[1] != 'I') {
        if (size >= 2U && file[0] == 'M' && file[1] == 'M')
            res->status = OMC_EXIF_WRITE_UNSUPPORTED;
        return OMC_STATUS_OK;
    }
    big = omc_overlay_get(file + 2U, 2U) == 43U;
    if (big) {
        if (size < 16U || omc_overlay_get(file + 4U, 2U) != 8U ||
            omc_overlay_get(file + 6U, 2U) != 0U)
            return OMC_STATUS_OK;
    } else if (omc_overlay_get(file + 2U, 2U) != 42U)
        return OMC_STATUS_OK;
    cs = big ? 8U : 2U;
    es = big ? 20U : 12U;
    ns = big ? 8U : 4U;
    align = big ? 8U : 2U;
    oldoff[0] = omc_overlay_get(file + (big ? 8U : 4U), big ? 8U : 4U);
    newoff[0] = canonical->size >= 8U ? omc_overlay_get(canonical->data + 4U, 4U) : 0U;
    for (d = 0; d < 4; ++d) {
        if (d > 0) {
            child = d == 3 ? 1 : 0;
            if (!omc_overlay_child(&old[child], big, ptrtags[d], &oldoff[d]) ||
                !omc_overlay_child(&add[child], 0, ptrtags[d], &newoff[d]))
                return OMC_STATUS_OK;
        }
        if (!omc_overlay_parse(file, size, oldoff[d], big, &old[d]) ||
            !omc_overlay_parse(canonical->data, canonical->size, newoff[d], 0, &add[d]))
            return OMC_STATUS_OK;
        for (j = 0U; j < (omc_size)d; ++j) {
            if (oldoff[d] != 0U && oldoff[d] == oldoff[j])
                return OMC_STATUS_OK;
        }
    }
    status = omc_overlay_append(out, file, size);
    if (status != OMC_STATUS_OK)
        return status;
    claims = (omc_u8 *)malloc(65536U);
    if (claims == NULL)
        return OMC_STATUS_NO_MEMORY;
    for (d = 3; d >= 0; --d) {
        memset(claims, 0, 65536U);
        capacity = (omc_size)old[d].count + add[d].count + 2U;
        records = (omc_u8 *)calloc(capacity, es);
        if (records == NULL) {
            status = OMC_STATUS_NO_MEMORY;
            goto done;
        }
        if (d == 0) {
            for (i = 0U; i < old[d].count; ++i) {
                tag = (omc_u16)omc_overlay_get(old[d].entries + i * es, 2U);
                if (omc_overlay_layout_tag(tag))
                    claims[tag] = 2U;
            }
        }
        for (i = 0U; i < source->entry_count; ++i) {
            const omc_entry *entry;
            entry = &source->entries[i];
            if (entry->key.kind != OMC_KEY_EXIF_TAG ||
                !(entry->flags & OMC_ENTRY_FLAG_DELETED))
                continue;
            name = omc_arena_view(&source->arena, entry->key.u.exif_tag.ifd);
            tag = entry->key.u.exif_tag.tag;
            if (name.data != NULL && name.size == strlen(names[d]) &&
                memcmp(name.data, names[d], name.size) == 0 && claims[tag] != 2U)
                claims[tag] = 1U;
        }
        used = 0U;
        for (i = 0U; i < add[d].count; ++i) {
            in = add[d].entries + i * 12U;
            tag = (omc_u16)omc_overlay_get(in, 2U);
            if ((d == 0 && (tag == 0x8769U || tag == 0x8825U || tag == 0x14AU)) ||
                (d == 1 && tag == 0xA005U) || claims[tag] == 2U)
                continue;
            claims[tag] = 1U;
            type = (omc_u16)omc_overlay_get(in + 2U, 2U);
            count = omc_overlay_get(in + 4U, 4U);
            if (type == 0U || type > 12U) {
                status = OMC_STATUS_STATE;
                goto done;
            }
            nbytes = count * widths[type];
            in = nbytes <= 4U
                     ? in + 8U
                     : canonical->data + (omc_size)omc_overlay_get(in + 8U, 4U);
            e = records + used++ * es;
            omc_overlay_put(e, tag, 2U);
            omc_overlay_put(e + 2U, type, 2U);
            omc_overlay_put(e + 4U, count, big ? 8U : 4U);
            if (nbytes <= ns)
                memcpy(e + (big ? 12U : 8U), in, (omc_size)nbytes);
            else {
                if (out->size % align != 0U) {
                    status = omc_overlay_append(out, zero, align - out->size % align);
                    if (status != OMC_STATUS_OK)
                        goto done;
                }
                valueoff = out->size;
                status = omc_overlay_append(out, in, (omc_size)nbytes);
                if (status != OMC_STATUS_OK)
                    goto done;
                omc_overlay_put(e + (big ? 12U : 8U), valueoff, (unsigned)ns);
            }
        }
        for (child = 1; child < 4; ++child) {
            if ((child == 3 ? 1 : 0) != d)
                continue;
            claims[ptrtags[child]] = 1U;
            if (written[child] == 0U)
                continue;
            e = records + used++ * es;
            omc_overlay_put(e, ptrtags[child], 2U);
            omc_overlay_put(e + 2U, big ? 18U : 4U, 2U);
            omc_overlay_put(e + 4U, 1U, big ? 8U : 4U);
            omc_overlay_put(e + (big ? 12U : 8U), written[child], (unsigned)ns);
        }
        for (i = 0U; i < old[d].count; ++i) {
            in = old[d].entries + i * es;
            tag = (omc_u16)omc_overlay_get(in, 2U);
            if (claims[tag] != 1U)
                memcpy(records + used++ * es, in, es);
        }
        if (used > 65535U || (!big && out->size > 0xFFFFFFFFU - (used * es + 16U)) ||
            out->size - size > 64U * 1024U * 1024U) {
            res->status = OMC_EXIF_WRITE_LIMIT;
            goto done;
        }
        if (used != 0U || d == 0 || oldoff[d] != 0U) {
            qsort(records, used, es, omc_overlay_compare);
            if (out->size % align != 0U) {
                status = omc_overlay_append(out, zero, align - out->size % align);
                if (status != OMC_STATUS_OK)
                    goto done;
            }
            written[d] = out->size;
            omc_overlay_put(countbytes, used, (unsigned)cs);
            status = omc_overlay_append(out, countbytes, cs);
            if (status == OMC_STATUS_OK)
                status = omc_overlay_append(out, records, used * es);
            omc_overlay_put(countbytes, old[d].next, (unsigned)ns);
            if (status == OMC_STATUS_OK)
                status = omc_overlay_append(out, countbytes, ns);
            if (status != OMC_STATUS_OK)
                goto done;
        }
        free(records);
        records = NULL;
    }
    omc_overlay_put(out->data + (big ? 8U : 4U), written[0], (unsigned)ns);
    res->status = OMC_EXIF_WRITE_OK;
    res->inserted_exif_blocks = 1U;
    res->removed_exif_blocks = oldoff[0] != 0U ? 1U : 0U;
    res->written = out->size;
    res->needed = out->size;
done:
    free(records);
    free(claims);
    return status;
}
