#include "omc_exif_write.h"
#include "omc_native_edit_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct omc_overlay_dir {
    const omc_u8 *entries;
    omc_u32 count;
    omc_u64 next;
} omc_overlay_dir;

typedef struct omc_overlay_record {
    omc_u16 tag;
    omc_u8 bytes[20];
} omc_overlay_record;

static omc_u64
omc_overlay_get(const omc_u8 *p, unsigned n, int little)
{
    omc_u64 v;
    unsigned i;
    v = 0U;
    for (i = 0U; i < n; ++i) {
        if (little)
            v |= (omc_u64)p[i] << (i * 8U);
        else
            v = (v << 8U) | p[i];
    }
    return v;
}

static void
omc_overlay_put(omc_u8 *p, omc_u64 v, unsigned n, int little)
{
    unsigned i;
    for (i = 0U; i < n; ++i) {
        p[little ? i : n - 1U - i] = (omc_u8)(v >> (i * 8U));
    }
}

static unsigned
omc_overlay_width(omc_u16 type)
{
    switch (type) {
    case 1U:
    case 2U:
    case 6U:
    case 7U:
    case 129U:
        return 1U;
    case 3U:
    case 8U:
        return 2U;
    case 4U:
    case 9U:
    case 11U:
    case 13U:
        return 4U;
    case 5U:
    case 10U:
    case 12U:
        return 8U;
    case 16U:
    case 17U:
    case 18U:
        return 8U;
    default:
        return 0U;
    }
}

static void
omc_overlay_convert_payload(omc_u8 *bytes, omc_size size, omc_u16 type)
{
    omc_size i;
    unsigned width;
    omc_u8 temp;
    if (type == 3U || type == 8U)
        width = 2U;
    else if (type == 4U || type == 9U || type == 11U || type == 13U)
        width = 4U;
    else if (type == 5U || type == 10U)
        width = 4U;
    else if (type == 12U || type == 16U || type == 17U || type == 18U)
        width = 8U;
    else
        return;
    for (i = 0U; i + width <= size; i += width) {
        unsigned j;
        for (j = 0U; j < width / 2U; ++j) {
            temp = bytes[i + j];
            bytes[i + j] = bytes[i + width - 1U - j];
            bytes[i + width - 1U - j] = temp;
        }
    }
}

static int
omc_overlay_parse(const omc_u8 *bytes, omc_size size, omc_u64 off, int big,
                  int little, omc_overlay_dir *dir)
{
    omc_u64 count;
    omc_size cs;
    omc_size es;
    omc_size ns;
    memset(dir, 0, sizeof(*dir));
    if (off == 0U)
        return 1;
    if (bytes == NULL || off > (omc_u64)size)
        return 0;
    cs = big ? 8U : 2U;
    es = big ? 20U : 12U;
    ns = big ? 8U : 4U;
    if (cs > size - (omc_size)off)
        return 0;
    count = omc_overlay_get(bytes + (omc_size)off, (unsigned)cs, little);
    if (count > 65535U || count * es + ns > size - (omc_size)off - cs)
        return 0;
    dir->entries = bytes + (omc_size)off + cs;
    dir->count = (omc_u32)count;
    dir->next = omc_overlay_get(dir->entries + dir->count * es,
                                (unsigned)ns, little);
    return 1;
}

static int
omc_overlay_child(const omc_overlay_dir *dir, int big, int little,
                  omc_u16 tag, omc_u64 *offset)
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
        if (omc_overlay_get(e, 2U, little) != tag)
            continue;
        type = omc_overlay_get(e + 2U, 2U, little);
        if (found ||
            (type != 4U && type != 13U &&
             !(big && (type == 16U || type == 18U))) ||
            omc_overlay_get(e + 4U, big ? 8U : 4U, little) != 1U)
            return 0;
        found = 1;
        *offset = omc_overlay_get(
            e + (big ? 12U : 8U),
            (big && (type == 16U || type == 18U)) ? 8U : 4U, little);
    }
    return 1;
}

static int
omc_overlay_record_compare(const void *a, const void *b)
{
    const omc_overlay_record *x;
    const omc_overlay_record *y;
    x = (const omc_overlay_record *)a;
    y = (const omc_overlay_record *)b;
    return x->tag < y->tag ? -1 : x->tag != y->tag;
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

static omc_size
omc_overlay_source_count(const omc_store *source, const char *ifd,
                         omc_u16 tag, int dirty_only)
{
    omc_size i;
    omc_size found;
    const omc_entry *entry;
    omc_const_bytes name;
    found = 0U;
    for (i = 0U; i < source->entry_count; ++i) {
        entry = &source->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG ||
            entry->key.u.exif_tag.tag != tag ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            (dirty_only && (entry->flags & OMC_ENTRY_FLAG_DIRTY) == 0U))
            continue;
        name = omc_arena_view(&source->arena, entry->key.u.exif_tag.ifd);
        if (name.data != NULL && name.size == strlen(ifd) &&
            memcmp(name.data, ifd, name.size) == 0)
            ++found;
    }
    return found;
}

static int
omc_overlay_entry_payload(const omc_u8 *bytes, omc_size size,
                          const omc_u8 *entry, int big, int little,
                          const omc_u8 **payload, omc_u64 *count_out,
                          omc_u16 *type_out, omc_u64 *size_out)
{
    omc_u16 type;
    omc_u64 count;
    omc_u64 nbytes;
    omc_u64 offset;
    unsigned width;
    unsigned slot;
    type = (omc_u16)omc_overlay_get(entry + 2U, 2U, little);
    count = omc_overlay_get(entry + 4U, big ? 8U : 4U, little);
    width = omc_overlay_width(type);
    if (width == 0U || count > (~(omc_u64)0) / width)
        return 0;
    nbytes = count * width;
    slot = big ? 8U : 4U;
    offset = omc_overlay_get(entry + (big ? 12U : 8U), slot, little);
    if (nbytes <= slot) {
        *payload = entry + (big ? 12U : 8U);
    } else {
        if (offset > (omc_u64)size || nbytes > (omc_u64)size - offset)
            return 0;
        *payload = bytes + (omc_size)offset;
    }
    *count_out = count;
    *type_out = type;
    *size_out = nbytes;
    return 1;
}

static void
omc_overlay_set_removals(omc_u8 *claims, unsigned directory,
                         const omc_native_edit_intent *intent)
{
    omc_u32 i;
    if (directory == 0U) {
        for (i = 0U; i < intent->ifd0_removal_count; ++i) {
            if (claims[intent->ifd0_removals[i]] != 2U)
                claims[intent->ifd0_removals[i]] = 1U;
        }
    } else if (directory == 1U) {
        for (i = 0U; i < intent->exif_removal_count; ++i) {
            if (claims[intent->exif_removals[i]] != 2U)
                claims[intent->exif_removals[i]] = 1U;
        }
    } else if (directory == 2U) {
        for (i = 0U; i < intent->gps_removal_count; ++i) {
            if (claims[intent->gps_removals[i]] != 2U)
                claims[intent->gps_removals[i]] = 1U;
        }
    }
}

static int
omc_overlay_intent_removes(const omc_native_edit_intent *intent,
                           unsigned directory, omc_u16 tag)
{
    const omc_u16 *tags;
    omc_u32 count;
    omc_u32 i;
    tags = NULL;
    count = 0U;
    if (directory == 0U) {
        tags = intent->ifd0_removals;
        count = intent->ifd0_removal_count;
    } else if (directory == 1U) {
        tags = intent->exif_removals;
        count = intent->exif_removal_count;
    } else if (directory == 2U) {
        tags = intent->gps_removals;
        count = intent->gps_removal_count;
    }
    for (i = 0U; i < count; ++i) {
        if (tags[i] == tag)
            return 1;
    }
    return 0;
}

static int
omc_overlay_directory_selected(const omc_native_edit_intent *intent,
                               unsigned directory)
{
    if (!omc_native_edit_intent_present(intent))
        return 1;
    if (directory == 1U)
        return intent->merge_exif || intent->exif_removal_count != 0U;
    if (directory == 2U)
        return intent->merge_gps || intent->gps_removal_count != 0U;
    return directory == 0U;
}

static int
omc_overlay_append_fits(const omc_arena *out, omc_size original_size,
                        omc_size append_size)
{
    omc_size extra;
    if (out->size < original_size)
        return 0;
    extra = out->size - original_size;
    return extra <= 64U * 1024U * 1024U &&
           append_size <= 64U * 1024U * 1024U - extra;
}

static omc_status
omc_overlay_build(const omc_u8 *file, omc_size size, const omc_store *source,
                  const omc_arena *canonical,
                  const omc_native_edit_intent *intent, omc_arena *out,
                  omc_exif_write_res *res)
{
    static const omc_u16 ptrtags[] = {0U, 0x8769U, 0x8825U, 0xA005U};
    omc_overlay_dir old[4];
    omc_overlay_dir add[4];
    omc_u64 oldoff[4];
    omc_u64 newoff[4];
    omc_u64 written[4];
    omc_overlay_record *records;
    omc_u8 *claims;
    omc_u8 *seen_gps;
    omc_u8 *e;
    const omc_u8 *in;
    const omc_u8 *target_version;
    const omc_u8 *source_version;
    omc_u8 zero[8];
    omc_u8 countbytes[8];
    omc_u8 dirty_gps[32];
    omc_u64 nbytes;
    omc_u64 valueoff;
    omc_u64 count;
    omc_u64 target_count;
    omc_u64 target_payload_size;
    omc_u64 source_count;
    omc_u64 source_payload_size;
    omc_size es;
    omc_size cs;
    omc_size ns;
    omc_size align;
    omc_size capacity;
    omc_size used;
    omc_size table_bytes;
    omc_size padding;
    omc_size source_version_count;
    omc_size i;
    omc_size j;
    omc_u16 tag;
    omc_u16 type;
    omc_u16 source_type;
    omc_const_bytes name;
    omc_status status;
    int little;
    int big;
    int child;
    int d;
    int clear_exif;
    int clear_gps;
    int merge_gps;
    int dirty_dng_version;
    int live_gps_updates;
    int supplied_version_count;
    int target_version_count;
    int has_nonversion_gps;
    int directory_changed;
    int selected_intent;
    memset(old, 0, sizeof(old));
    memset(add, 0, sizeof(add));
    memset(oldoff, 0, sizeof(oldoff));
    memset(newoff, 0, sizeof(newoff));
    memset(written, 0, sizeof(written));
    memset(zero, 0, sizeof(zero));
    memset(dirty_gps, 0, sizeof(dirty_gps));
    records = NULL;
    claims = NULL;
    seen_gps = NULL;
    if (res == NULL || intent == NULL)
        return OMC_STATUS_INVALID_ARGUMENT;
    res->status = OMC_EXIF_WRITE_MALFORMED;
    clear_exif = intent->clear_exif != 0;
    clear_gps = intent->clear_gps != 0;
    merge_gps = intent->merge_gps != 0 || intent->gps_removal_count != 0U;
    selected_intent = omc_native_edit_intent_present(intent);
    if (clear_exif && (intent->merge_exif || intent->exif_removal_count != 0U))
        return OMC_STATUS_OK;
    if (file == NULL || canonical == NULL || source == NULL || out == NULL ||
        (canonical->size != 0U && canonical->data == NULL) ||
        (source->entry_count != 0U && source->entries == NULL))
        return OMC_STATUS_INVALID_ARGUMENT;
    if (size < 8U)
        return OMC_STATUS_OK;
    little = file[0] == 'I' && file[1] == 'I';
    if (!little && !(file[0] == 'M' && file[1] == 'M'))
        return OMC_STATUS_OK;
    little = file[0] == 'I';
    if (omc_overlay_get(file + 2U, 2U, little) == 43U) {
        big = 1;
        if (size < 16U || omc_overlay_get(file + 4U, 2U, little) != 8U ||
            omc_overlay_get(file + 6U, 2U, little) != 0U)
            return OMC_STATUS_OK;
    } else if (omc_overlay_get(file + 2U, 2U, little) == 42U) {
        big = 0;
    } else
        return OMC_STATUS_OK;
    cs = big ? 8U : 2U;
    es = big ? 20U : 12U;
    ns = big ? 8U : 4U;
    align = big ? 8U : 2U;
    oldoff[0] = omc_overlay_get(file + (big ? 8U : 4U),
                                big ? 8U : 4U, little);
    newoff[0] = canonical->size >= 8U
                    ? omc_overlay_get(canonical->data + 4U, 4U, 1)
                    : 0U;
    if (!omc_overlay_parse(file, size, oldoff[0], big, little, &old[0]) ||
        !omc_overlay_parse(canonical->data, canonical->size, newoff[0], 0, 1,
                           &add[0]))
        return OMC_STATUS_OK;
    for (d = 1; d < 4; ++d) {
        child = d == 3 ? 1 : 0;
        if (!omc_overlay_child(&add[child], 0, 1, ptrtags[d], &newoff[d]))
            return OMC_STATUS_OK;
        if ((clear_exif && (d == 1 || d == 3)) || (clear_gps && d == 2)) {
            oldoff[d] = 0U;
            memset(&old[d], 0, sizeof(old[d]));
        } else {
            if (!omc_overlay_child(&old[child], big, little, ptrtags[d],
                                   &oldoff[d]))
                return OMC_STATUS_OK;
            for (j = 0U; j < (omc_size)d; ++j) {
                if (oldoff[d] != 0U && oldoff[d] == oldoff[j])
                    return OMC_STATUS_OK;
            }
            if (!omc_overlay_parse(file, size, oldoff[d], big, little,
                                   &old[d]))
                return OMC_STATUS_OK;
        }
        if (!omc_overlay_parse(canonical->data, canonical->size, newoff[d], 0,
                               1, &add[d]))
            return OMC_STATUS_OK;
    }

    if ((clear_exif && (add[1].count != 0U || add[3].count != 0U)) ||
        (clear_gps && add[2].count != 0U)) {
        res->status = OMC_EXIF_WRITE_MALFORMED;
        return OMC_STATUS_OK;
    }
    for (d = 0; d < 3; ++d) {
        for (i = 0U; i < add[d].count; ++i) {
            in = add[d].entries + i * 12U;
            tag = (omc_u16)omc_overlay_get(in, 2U, 1);
            if (omc_overlay_intent_removes(intent, (unsigned)d, tag)) {
                res->status = OMC_EXIF_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
        }
    }

    if (canonical->size == 0U && !selected_intent) {
        status = omc_overlay_append(out, file, size);
        if (status == OMC_STATUS_OK) {
            res->status = OMC_EXIF_WRITE_OK;
            res->written = out->size;
            res->needed = out->size;
        }
        return status;
    }

    live_gps_updates = 0;
    if (merge_gps) {
        source_version_count =
            omc_overlay_source_count(source, "gpsifd", 0U, 0);
        if (source_version_count > 1U) {
            res->status = OMC_EXIF_WRITE_MALFORMED;
            return OMC_STATUS_OK;
        }
        for (i = 0U; i < source->entry_count; ++i) {
            const omc_entry *entry;
            entry = &source->entries[i];
            if (entry->key.kind != OMC_KEY_EXIF_TAG ||
                (entry->flags & (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED)) !=
                    OMC_ENTRY_FLAG_DIRTY)
                continue;
            name = omc_arena_view(&source->arena,
                                  entry->key.u.exif_tag.ifd);
            if (name.data == NULL || name.size != 6U ||
                memcmp(name.data, "gpsifd", 6U) != 0)
                continue;
            tag = entry->key.u.exif_tag.tag;
            if (tag == 0U)
                continue;
            if (tag > 31U) {
                res->status = OMC_EXIF_WRITE_UNSUPPORTED;
                return OMC_STATUS_OK;
            }
            if (dirty_gps[tag] != 0U) {
                res->status = OMC_EXIF_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            dirty_gps[tag] = 1U;
            ++live_gps_updates;
        }
        if (live_gps_updates != 0) {
            supplied_version_count = 0;
            source_version = NULL;
            source_payload_size = 0U;
            source_count = 0U;
            source_type = 0U;
            for (i = 0U; i < add[2].count; ++i) {
                in = add[2].entries + i * 12U;
                if (omc_overlay_get(in, 2U, 1) != 0U)
                    continue;
                ++supplied_version_count;
                if (!omc_overlay_entry_payload(canonical->data,
                                               canonical->size, in, 0, 1,
                                               &source_version, &source_count,
                                               &source_type,
                                               &source_payload_size)) {
                    res->status = OMC_EXIF_WRITE_MALFORMED;
                    return OMC_STATUS_OK;
                }
            }
            if (supplied_version_count != 1 || source_type != 1U ||
                source_count != 4U || source_payload_size != 4U) {
                res->status = OMC_EXIF_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            target_version_count = 0;
            target_version = NULL;
            target_count = 0U;
            target_payload_size = 0U;
            type = 0U;
            for (i = 0U; i < old[2].count; ++i) {
                in = old[2].entries + i * es;
                if (omc_overlay_get(in, 2U, little) != 0U)
                    continue;
                ++target_version_count;
                if (!omc_overlay_entry_payload(file, size, in, big, little,
                                               &target_version, &target_count,
                                               &type,
                                               &target_payload_size)) {
                    res->status = OMC_EXIF_WRITE_MALFORMED;
                    return OMC_STATUS_OK;
                }
            }
            if (target_version_count > 1) {
                res->status = OMC_EXIF_WRITE_MALFORMED;
                return OMC_STATUS_OK;
            }
            if (target_version_count == 1) {
                if (type != 1U || target_count != 4U ||
                    target_payload_size != 4U ||
                    memcmp(target_version, source_version, 4U) != 0) {
                    res->status = OMC_EXIF_WRITE_MALFORMED;
                    return OMC_STATUS_OK;
                }
            }
            for (i = 0U; i < 32U; ++i) {
                if (dirty_gps[i] != 0U &&
                    omc_overlay_source_count(source, "gpsifd", (omc_u16)i,
                                             1) != 1U) {
                    res->status = OMC_EXIF_WRITE_MALFORMED;
                    return OMC_STATUS_OK;
                }
            }
        }
    }

    status = omc_overlay_append(out, file, size);
    if (status != OMC_STATUS_OK)
        return status;
    claims = (omc_u8 *)malloc(65536U);
    seen_gps = (omc_u8 *)malloc(65536U);
    if (claims == NULL || seen_gps == NULL) {
        status = OMC_STATUS_NO_MEMORY;
        goto done;
    }
    dirty_dng_version =
        omc_overlay_source_count(source, "ifd0", 0xC612U, 1) != 0U;
    for (d = 3; d >= 0; --d) {
        if (d != 0 &&
            ((clear_exif && (d == 1 || d == 3)) ||
             (clear_gps && d == 2)))
            continue;
        if (d != 0 &&
            !omc_overlay_directory_selected(intent, (unsigned)d)) {
            written[d] = oldoff[d];
            continue;
        }
        memset(claims, 0, 65536U);
        memset(seen_gps, 0, 65536U);
        capacity = (omc_size)old[d].count + add[d].count + 2U;
        if (capacity > ((omc_size)-1) / sizeof(*records) ||
            capacity > ((omc_size)-1 - cs - ns - (align - 1U)) / es) {
            res->status = OMC_EXIF_WRITE_LIMIT;
            status = OMC_STATUS_OVERFLOW;
            goto done;
        }
        table_bytes = cs + capacity * es + ns + align - 1U;
        if (!omc_overlay_append_fits(out, size, table_bytes)) {
            res->status = OMC_EXIF_WRITE_LIMIT;
            status = OMC_STATUS_OK;
            goto done;
        }
        records = (omc_overlay_record *)calloc(capacity, sizeof(*records));
        if (records == NULL) {
            status = OMC_STATUS_NO_MEMORY;
            goto done;
        }
        if (d == 0) {
            for (i = 0U; i < old[d].count; ++i) {
                tag = (omc_u16)omc_overlay_get(old[d].entries + i * es, 2U,
                                               little);
                if (omc_overlay_layout_tag(tag)) {
                    if (tag == 0xC612U && dirty_dng_version &&
                        omc_overlay_source_count(source, "ifd0", tag, 0) == 1U)
                        claims[tag] = 0U;
                    else
                        claims[tag] = 2U;
                }
            }
        }
        omc_overlay_set_removals(claims, (unsigned)d, intent);
        if ((d == 0 && clear_exif) || (d == 0 && clear_gps)) {
            if (clear_exif)
                claims[0x8769U] = 1U;
            if (clear_gps)
                claims[0x8825U] = 1U;
        }
        if (d == 0 && oldoff[1] != 0U && written[1] == 0U)
            claims[0x8769U] = 1U;
        if (d == 0 && oldoff[2] != 0U && written[2] == 0U)
            claims[0x8825U] = 1U;
        if (d == 1 && clear_exif)
            claims[0xA005U] = 1U;
        used = 0U;
        directory_changed = 0;
        for (i = 0U; i < add[d].count; ++i) {
            in = add[d].entries + i * 12U;
            tag = (omc_u16)omc_overlay_get(in, 2U, 1);
            if ((d == 0 && (tag == 0x8769U || tag == 0x8825U || tag == 0x14AU)) ||
                (d == 1 && tag == 0xA005U))
                continue;
            if ((clear_exif && (d == 1 || d == 3)) ||
                (clear_gps && d == 2))
                continue;
            if (selected_intent && d == 3)
                continue;
            if (d == 2 && merge_gps) {
                if (tag == 0U) {
                    if (live_gps_updates == 0)
                        continue;
                } else if (tag > 31U || dirty_gps[tag] == 0U) {
                    continue;
                }
                if (seen_gps[tag] != 0U) {
                    res->status = OMC_EXIF_WRITE_MALFORMED;
                    status = OMC_STATUS_OK;
                    goto done;
                }
                seen_gps[tag] = 1U;
            }
            if (claims[tag] == 2U)
                continue;
            claims[tag] = 1U;
            directory_changed = 1;
            type = (omc_u16)omc_overlay_get(in + 2U, 2U, 1);
            count = omc_overlay_get(in + 4U, 4U, 1);
            if (type == 0U || omc_overlay_width(type) == 0U ||
                count > (~(omc_u64)0) / omc_overlay_width(type)) {
                res->status = OMC_EXIF_WRITE_UNSUPPORTED;
                status = OMC_STATUS_OK;
                goto done;
            }
            nbytes = count * omc_overlay_width(type);
            if (nbytes <= 4U)
                in = in + 8U;
            else {
                valueoff = omc_overlay_get(in + 8U, 4U, 1);
                if (valueoff > (omc_u64)canonical->size ||
                    nbytes > (omc_u64)canonical->size - valueoff) {
                    res->status = OMC_EXIF_WRITE_MALFORMED;
                    status = OMC_STATUS_OK;
                    goto done;
                }
                in = canonical->data + (omc_size)valueoff;
            }
            e = records[used].bytes;
            records[used].tag = tag;
            memset(e, 0, es);
            omc_overlay_put(e, tag, 2U, little);
            omc_overlay_put(e + 2U, type, 2U, little);
            omc_overlay_put(e + 4U, count, big ? 8U : 4U, little);
            if (nbytes <= ns) {
                memcpy(e + (big ? 12U : 8U), in, (omc_size)nbytes);
                if (!little)
                    omc_overlay_convert_payload(e + (big ? 12U : 8U),
                                                 (omc_size)nbytes, type);
            } else {
                padding = (align - out->size % align) % align;
                if (padding != 0U) {
                    if (!omc_overlay_append_fits(out, size, padding)) {
                        res->status = OMC_EXIF_WRITE_LIMIT;
                        status = OMC_STATUS_OK;
                        goto done;
                    }
                    status = omc_overlay_append(out, zero, padding);
                    if (status != OMC_STATUS_OK)
                        goto done;
                }
                valueoff = out->size;
                if (!big && valueoff > 0xFFFFFFFFU) {
                    res->status = OMC_EXIF_WRITE_LIMIT;
                    status = OMC_STATUS_OK;
                    goto done;
                }
                if (!omc_overlay_append_fits(out, size, (omc_size)nbytes)) {
                    res->status = OMC_EXIF_WRITE_LIMIT;
                    status = OMC_STATUS_OK;
                    goto done;
                }
                status = omc_overlay_append(out, in, (omc_size)nbytes);
                if (status != OMC_STATUS_OK)
                    goto done;
                if (!little)
                    omc_overlay_convert_payload(out->data + (omc_size)valueoff,
                                                 (omc_size)nbytes, type);
                omc_overlay_put(e + (big ? 12U : 8U), valueoff,
                                (unsigned)ns, little);
            }
            ++used;
        }
        if (d == 2 && merge_gps && live_gps_updates != 0) {
            if (seen_gps[0] == 0U) {
                res->status = OMC_EXIF_WRITE_MALFORMED;
                status = OMC_STATUS_OK;
                goto done;
            }
            for (i = 1U; i <= 31U; ++i) {
                if (dirty_gps[i] != 0U && seen_gps[i] == 0U) {
                    res->status = OMC_EXIF_WRITE_MALFORMED;
                    status = OMC_STATUS_OK;
                    goto done;
                }
            }
        }
        for (child = 1; child < 4; ++child) {
            int parent;
            parent = child == 3 ? 1 : 0;
            if (parent != d)
                continue;
            if (child == 1 && clear_exif)
                continue;
            if (child == 2 && clear_gps)
                continue;
            if (written[child] == 0U)
                continue;
            if (written[child] == oldoff[child])
                continue;
            claims[ptrtags[child]] = 1U;
            directory_changed = 1;
            e = records[used].bytes;
            records[used].tag = ptrtags[child];
            memset(e, 0, es);
            omc_overlay_put(e, ptrtags[child], 2U, little);
            omc_overlay_put(e + 2U, big ? 18U : 4U, 2U, little);
            omc_overlay_put(e + 4U, 1U, big ? 8U : 4U, little);
            omc_overlay_put(e + (big ? 12U : 8U), written[child],
                            (unsigned)ns, little);
            ++used;
        }
        for (i = 0U; i < old[d].count; ++i) {
            in = old[d].entries + i * es;
            tag = (omc_u16)omc_overlay_get(in, 2U, little);
            if (claims[tag] != 1U) {
                records[used].tag = tag;
                memcpy(records[used].bytes, in, es);
                ++used;
            } else {
                directory_changed = 1;
            }
        }
        if (d == 2 && merge_gps) {
            has_nonversion_gps = 0;
            for (i = 0U; i < used; ++i) {
                if (records[i].tag != 0U) {
                    has_nonversion_gps = 1;
                    break;
                }
            }
            if (!has_nonversion_gps) {
                j = 0U;
                for (i = 0U; i < used; ++i) {
                    if (records[i].tag != 0U)
                        records[j++] = records[i];
                }
                if (j != used)
                    directory_changed = 1;
                used = j;
            }
        }
        if (d != 0 && !directory_changed) {
            written[d] = oldoff[d];
            free(records);
            records = NULL;
            continue;
        }
        if (used > 65535U ||
            (!big && out->size > 0xFFFFFFFFU - (used * es + cs + ns))) {
            res->status = OMC_EXIF_WRITE_LIMIT;
            status = OMC_STATUS_OK;
            goto done;
        }
        if (used != 0U || d == 0) {
            qsort(records, used, sizeof(*records), omc_overlay_record_compare);
            padding = (align - out->size % align) % align;
            table_bytes = padding + cs + used * es + ns;
            if (!omc_overlay_append_fits(out, size, table_bytes)) {
                res->status = OMC_EXIF_WRITE_LIMIT;
                status = OMC_STATUS_OK;
                goto done;
            }
            if (padding != 0U) {
                status = omc_overlay_append(out, zero, padding);
                if (status != OMC_STATUS_OK)
                    goto done;
            }
            written[d] = out->size;
            omc_overlay_put(countbytes, used, (unsigned)cs, little);
            status = omc_overlay_append(out, countbytes, cs);
            for (i = 0U; status == OMC_STATUS_OK && i < used; ++i)
                status = omc_overlay_append(out, records[i].bytes, es);
            omc_overlay_put(countbytes, old[d].next, (unsigned)ns, little);
            if (status == OMC_STATUS_OK)
                status = omc_overlay_append(out, countbytes, ns);
            if (status != OMC_STATUS_OK)
                goto done;
        }
        free(records);
        records = NULL;
    }
    omc_overlay_put(out->data + (big ? 8U : 4U), written[0],
                    (unsigned)ns, little);
    res->status = OMC_EXIF_WRITE_OK;
    res->inserted_exif_blocks = 1U;
    res->removed_exif_blocks = oldoff[0] != 0U ? 1U : 0U;
    res->written = out->size;
    res->needed = out->size;
    status = OMC_STATUS_OK;
done:
    free(records);
    free(claims);
    free(seen_gps);
    return status;
}

omc_status
omc_exif_overlay_tiff_edit(const omc_u8 *file, omc_size size,
                           const omc_store *source,
                           const omc_arena *canonical,
                           const omc_native_edit_intent *intent,
                           omc_arena *out, omc_exif_write_res *res)
{
    omc_arena candidate;
    omc_arena previous;
    omc_status status;
    if (res == NULL || out == NULL || source == NULL || canonical == NULL ||
        intent == NULL)
        return OMC_STATUS_INVALID_ARGUMENT;
    res->status = OMC_EXIF_WRITE_MALFORMED;
    res->needed = 0U;
    res->written = 0U;
    res->removed_exif_blocks = 0U;
    res->inserted_exif_blocks = 0U;
    status = omc_native_edit_intent_validate(intent);
    if (status != OMC_STATUS_OK)
        return status;
    if (intent->clear_exif &&
        (intent->merge_exif || intent->exif_removal_count != 0U))
        return OMC_STATUS_INVALID_ARGUMENT;
    if (intent->clear_gps &&
        (intent->merge_gps || intent->gps_removal_count != 0U))
        return OMC_STATUS_INVALID_ARGUMENT;
    omc_arena_init(&candidate);
    status = omc_overlay_build(file, size, source, canonical, intent,
                               &candidate, res);
    if (status == OMC_STATUS_OK && res->status == OMC_EXIF_WRITE_OK) {
        previous = *out;
        *out = candidate;
        candidate = previous;
        res->written = out->size;
        res->needed = out->size;
    }
    omc_arena_fini(&candidate);
    return status;
}

omc_status
omc_exif_overlay_tiff(const omc_u8 *file, omc_size size,
                      const omc_store *source, const omc_arena *canonical,
                      omc_arena *out, omc_exif_write_res *res)
{
    omc_native_edit_intent intent;
    omc_status status;
    if (source == NULL || res == NULL)
        return OMC_STATUS_INVALID_ARGUMENT;
    status = omc_native_edit_intent_collect(source, &intent);
    if (status != OMC_STATUS_OK)
        return status;
    return omc_exif_overlay_tiff_edit(file, size, source, canonical, &intent,
                                      out, res);
}
