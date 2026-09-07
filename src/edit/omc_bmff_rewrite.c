#include "omc_bmff_rewrite.h"
#include <stdlib.h>
#include <string.h>

#define OMC_BMFF_MAX_META (64U * 1024U * 1024U)
#define OMC_BMFF_MAX_ITEMS 65536U
#define OMC_BMFF_MAX_EXTENTS 1048576U
#define OMC_BMFF_MAX_TABLES 4096U
#define OMC_BMFF_MAX_ASSOCIATIONS 4194304U

typedef struct omc_bmff_box {
    omc_size offset;
    omc_size size;
    omc_size header;
    omc_u32 type;
} omc_bmff_box;

typedef struct omc_bmff_item_record {
    omc_u32 id;
    omc_u32 replacement;
    unsigned family;
    int removed;
    omc_bmff_box infe;
    int located;
    omc_u16 method;
    omc_u16 reference;
    omc_u64 base;
    omc_size extent_offset;
    omc_u16 extent_count;
} omc_bmff_item_record;

typedef struct omc_bmff_graph {
    const omc_u8 *bytes;
    omc_size size;
    omc_bmff_box meta;
    omc_bmff_box iinf;
    omc_bmff_box iloc;
    omc_bmff_box idat;
    omc_bmff_box iref;
    omc_bmff_box iprp;
    omc_bmff_box dinf;
    omc_bmff_item_record *records;
    omc_u32 record_count;
    omc_u32 *slots;
    omc_size slot_count;
    omc_u32 primary;
    omc_u32 max_id;
    omc_u8 offset_size;
    omc_u8 base_size;
    omc_u8 length_size;
    omc_u8 index_size;
    omc_u8 iloc_version;
    omc_u8 iinf_version;
    omc_u8 *local_refs;
    omc_u32 local_ref_count;
    omc_transfer_status status;
    omc_status allocation_status;
} omc_bmff_graph;

static omc_u64
omc_bmff_get(const omc_u8 *bytes, unsigned width)
{
    omc_u64 value;
    unsigned i;
    value = 0U;
    for (i = 0U; i < width; ++i)
        value = (value << 8U) | bytes[i];
    return value;
}

static void
omc_bmff_put(omc_u8 *bytes, omc_u64 value, unsigned width)
{
    unsigned i;
    for (i = 0U; i < width; ++i) {
        bytes[width - 1U - i] = (omc_u8)value;
        value >>= 8U;
    }
}

static int
omc_bmff_fail(omc_bmff_graph *g, omc_transfer_status status)
{
    g->status = status;
    return 0;
}

static int
omc_bmff_box_at(omc_bmff_graph *g, omc_size offset, omc_size end, omc_bmff_box *box)
{
    omc_u64 size;
    if (offset > end || end > g->size || end - offset < 8U)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    memset(box, 0, sizeof(*box));
    box->offset = offset;
    box->header = 8U;
    box->type = (omc_u32)omc_bmff_get(g->bytes + offset + 4U, 4U);
    size = omc_bmff_get(g->bytes + offset, 4U);
    if (size == 1U) {
        if (end - offset < 16U)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        size = omc_bmff_get(g->bytes + offset + 8U, 8U);
        box->header = 16U;
    } else if (size == 0U)
        size = end - offset;
    if (size < box->header || size > end - offset)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    box->size = (omc_size)size;
    return 1;
}

static int
omc_bmff_append(omc_bmff_graph *g, omc_arena *out, const void *bytes, omc_size size)
{
    omc_byte_ref ref;
    if (size > OMC_BMFF_MAX_META || out->size > OMC_BMFF_MAX_META - size)
        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
    g->allocation_status = omc_arena_append(out, bytes, size, &ref);
    return g->allocation_status == OMC_STATUS_OK;
}

static int
omc_bmff_number(omc_bmff_graph *g, omc_arena *out, omc_u64 value, unsigned width)
{
    omc_u8 bytes[8];
    if (width > 8U || (width < 8U && value >> (width * 8U)))
        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
    omc_bmff_put(bytes, value, width);
    return omc_bmff_append(g, out, bytes, width);
}

static int
omc_bmff_emit_box(omc_bmff_graph *g, omc_arena *out, omc_u32 type,
                  const omc_arena *payload)
{
    return omc_bmff_number(g, out, payload->size + 8U, 4U) &&
           omc_bmff_number(g, out, type, 4U) &&
           omc_bmff_append(g, out, payload->data, payload->size);
}

static int
omc_bmff_copy_box(omc_bmff_graph *g, omc_arena *out, const omc_bmff_box *box)
{
    omc_size start;
    start = out->size;
    if (!omc_bmff_append(g, out, g->bytes + box->offset, box->size))
        return 0;
    if (omc_bmff_get(g->bytes + box->offset, 4U) == 0U)
        omc_bmff_put(out->data + start, box->size, 4U);
    return 1;
}

static omc_bmff_item_record *
omc_bmff_item(omc_bmff_graph *g, omc_u32 id)
{
    omc_size slot;
    if (id == 0U || g->slots == NULL)
        return NULL;
    slot = ((omc_size)id * 2654435761U) & (g->slot_count - 1U);
    while (g->slots[slot] != 0U) {
        if (g->records[g->slots[slot] - 1U].id == id)
            return &g->records[g->slots[slot] - 1U];
        slot = (slot + 1U) & (g->slot_count - 1U);
    }
    return NULL;
}

static omc_u32
omc_bmff_remap(omc_bmff_graph *g, omc_u32 id)
{
    omc_bmff_item_record *item;
    item = omc_bmff_item(g, id);
    return item != NULL && item->removed ? item->replacement : id;
}

static int
omc_bmff_cstring(omc_bmff_graph *g, omc_size *offset, omc_size end,
                 omc_const_bytes *text)
{
    omc_size start;
    start = *offset;
    while (*offset < end && g->bytes[*offset] != 0U)
        (*offset)++;
    if (*offset == end)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    text->data = g->bytes + start;
    text->size = *offset - start;
    (*offset)++;
    return 1;
}

static int
omc_bmff_text_equal(omc_const_bytes text, const char *literal)
{
    omc_size i;
    unsigned c;
    if (text.size != strlen(literal))
        return 0;
    for (i = 0U; i < text.size; ++i) {
        c = text.data[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != (unsigned char)literal[i])
            return 0;
    }
    return 1;
}

static int
omc_bmff_collect(omc_bmff_graph *g)
{
    omc_size off;
    omc_size end;
    omc_size p;
    omc_size slot;
    omc_u32 count;
    omc_u32 i;
    omc_u32 type;
    omc_u8 version;
    omc_bmff_box box;
    omc_bmff_box *selected;
    omc_bmff_item_record *item;
    omc_const_bytes text;
    int have_primary;
    have_primary = 0;
    off = 0U;
    while (off < g->size) {
        if (!omc_bmff_box_at(g, off, g->size, &box))
            return 0;
        if (box.type == OMC_FOURCC('m', 'e', 't', 'a')) {
            if (g->meta.size)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            g->meta = box;
        }
        off += box.size;
    }
    if (g->meta.size == 0U)
        return 1;
    if (g->meta.size > OMC_BMFF_MAX_META)
        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
    off = g->meta.offset + g->meta.header;
    end = g->meta.offset + g->meta.size;
    if (end - off < 4U)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    if (g->bytes[off] != 0U)
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    off += 4U;
    while (off < end) {
        if (!omc_bmff_box_at(g, off, end, &box))
            return 0;
        selected = NULL;
        switch (box.type) {
        case OMC_FOURCC('i', 'i', 'n', 'f'):
            selected = &g->iinf;
            break;
        case OMC_FOURCC('i', 'l', 'o', 'c'):
            selected = &g->iloc;
            break;
        case OMC_FOURCC('i', 'd', 'a', 't'):
            selected = &g->idat;
            break;
        case OMC_FOURCC('i', 'r', 'e', 'f'):
            selected = &g->iref;
            break;
        case OMC_FOURCC('i', 'p', 'r', 'p'):
            selected = &g->iprp;
            break;
        case OMC_FOURCC('d', 'i', 'n', 'f'):
            selected = &g->dinf;
            break;
        case OMC_FOURCC('p', 'i', 't', 'm'):
            p = box.offset + box.header;
            if (have_primary)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            if (box.size - box.header < 6U)
                return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            version = g->bytes[p];
            if (version > 1U)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            if (box.size - box.header != (version ? 8U : 6U))
                return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            g->primary = (omc_u32)omc_bmff_get(g->bytes + p + 4U, version ? 4U : 2U);
            have_primary = 1;
            break;
        default:
            break;
        }
        if (selected != NULL) {
            if (selected->size)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            *selected = box;
        }
        off += box.size;
    }
    if (!g->iinf.size)
        return 1;
    off = g->iinf.offset + g->iinf.header;
    end = g->iinf.offset + g->iinf.size;
    if (end - off < 6U)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    g->iinf_version = g->bytes[off];
    if (g->iinf_version > 1U)
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    if (g->iinf_version && end - off < 8U)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    count = (omc_u32)omc_bmff_get(g->bytes + off + 4U, g->iinf_version ? 4U : 2U);
    off += g->iinf_version ? 8U : 6U;
    if (count > OMC_BMFF_MAX_ITEMS)
        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
    g->slot_count = 8U;
    while (g->slot_count < (omc_size)count * 2U)
        g->slot_count *= 2U;
    g->slots = (omc_u32 *)calloc(g->slot_count, sizeof(*g->slots));
    g->records =
        (omc_bmff_item_record *)calloc((omc_size)count + 1U, sizeof(*g->records));
    if (g->slots == NULL || g->records == NULL) {
        g->allocation_status = OMC_STATUS_NO_MEMORY;
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (!omc_bmff_box_at(g, off, end, &box))
            return 0;
        if (box.type != OMC_FOURCC('i', 'n', 'f', 'e'))
            return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        p = box.offset + box.header;
        if (box.size - box.header < 12U)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        version = g->bytes[p];
        if (version != 2U && version != 3U)
            return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        item = &g->records[i];
        item->infe = box;
        p += 4U;
        item->id = (omc_u32)omc_bmff_get(g->bytes + p, version == 3U ? 4U : 2U);
        p += version == 3U ? 4U : 2U;
        if (box.offset + box.size - p < 6U)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        p += 2U;
        type = (omc_u32)omc_bmff_get(g->bytes + p, 4U);
        p += 4U;
        if (!omc_bmff_cstring(g, &p, box.offset + box.size, &text))
            return 0;
        if (type == OMC_FOURCC('E', 'x', 'i', 'f'))
            item->family = OMC_BMFF_FAMILY_EXIF;
        else if (type == OMC_FOURCC('x', 'm', 'l', ' '))
            item->family = OMC_BMFF_FAMILY_XMP;
        else if (type == OMC_FOURCC('j', 'u', 'm', 'b'))
            item->family = OMC_BMFF_FAMILY_JUMBF;
        else if (type == OMC_FOURCC('c', '2', 'p', 'a'))
            item->family = OMC_BMFF_FAMILY_C2PA;
        else if (type == OMC_FOURCC('m', 'i', 'm', 'e')) {
            if (!omc_bmff_cstring(g, &p, box.offset + box.size, &text))
                return 0;
            if (omc_bmff_text_equal(text, "application/rdf+xml") ||
                omc_bmff_text_equal(text, "application/xmp+xml"))
                item->family = OMC_BMFF_FAMILY_XMP;
        }
        if (item->id == 0U || omc_bmff_item(g, item->id) != NULL)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        slot = ((omc_size)item->id * 2654435761U) & (g->slot_count - 1U);
        while (g->slots[slot])
            slot = (slot + 1U) & (g->slot_count - 1U);
        g->slots[slot] = i + 1U;
        if (item->id > g->max_id)
            g->max_id = item->id;
        g->record_count++;
        off += box.size;
    }
    if (off != end)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    return 1;
}

static int
omc_bmff_drefs(omc_bmff_graph *g)
{
    omc_bmff_box dref;
    omc_bmff_box child;
    omc_size off;
    omc_size end;
    omc_size p;
    omc_u32 i;
    omc_u32 count;
    if (g->local_refs != NULL)
        return 1;
    memset(&dref, 0, sizeof(dref));
    if (!g->dinf.size)
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    off = g->dinf.offset + g->dinf.header;
    end = g->dinf.offset + g->dinf.size;
    while (off < end) {
        if (!omc_bmff_box_at(g, off, end, &child))
            return 0;
        if (child.type == OMC_FOURCC('d', 'r', 'e', 'f')) {
            if (dref.size)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            dref = child;
        }
        off += child.size;
    }
    if (!dref.size)
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    off = dref.offset + dref.header;
    end = dref.offset + dref.size;
    if (end - off < 8U)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    if (g->bytes[off] != 0U)
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    count = (omc_u32)omc_bmff_get(g->bytes + off + 4U, 4U);
    if (count > 65535U)
        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
    g->local_refs = (omc_u8 *)calloc((omc_size)count + 1U, 1U);
    if (g->local_refs == NULL) {
        g->allocation_status = OMC_STATUS_NO_MEMORY;
        return 0;
    }
    g->local_ref_count = count;
    off += 8U;
    for (i = 1U; i <= count; ++i) {
        if (!omc_bmff_box_at(g, off, end, &child))
            return 0;
        p = child.offset + child.header;
        if (child.size - child.header < 4U)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        g->local_refs[i] = (omc_u8)((child.type == OMC_FOURCC('u', 'r', 'l', ' ') ||
                                     child.type == OMC_FOURCC('u', 'r', 'n', ' ')) &&
                                    g->bytes[p] == 0U &&
                                    (omc_bmff_get(g->bytes + p + 1U, 3U) & 1U) != 0U);
        off += child.size;
    }
    if (off != end)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    return 1;
}

static int
omc_bmff_locations(omc_bmff_graph *g)
{
    omc_size p;
    omc_size end;
    omc_u32 count;
    omc_u32 i;
    omc_u32 id;
    omc_u32 total_extents;
    omc_u64 n;
    omc_u64 offset;
    omc_u64 length;
    omc_u64 available;
    unsigned base_width;
    unsigned j;
    omc_bmff_item_record *item;
    if (!g->iloc.size)
        return 1;
    p = g->iloc.offset + g->iloc.header;
    end = g->iloc.offset + g->iloc.size;
    if (end - p < 8U)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    g->iloc_version = g->bytes[p];
    if (g->iloc_version > 2U)
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    g->offset_size = (omc_u8)(g->bytes[p + 4U] >> 4U);
    g->length_size = (omc_u8)(g->bytes[p + 4U] & 15U);
    base_width = g->bytes[p + 5U] >> 4U;
    g->base_size = (omc_u8)base_width;
    g->index_size = (omc_u8)(g->bytes[p + 5U] & 15U);
    if (g->offset_size > 8U || g->length_size == 0U || g->length_size > 8U ||
        base_width > 8U || g->index_size > 8U || (!g->iloc_version && g->index_size))
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    p += 6U;
    if (end - p < (g->iloc_version == 2U ? 4U : 2U))
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    count = (omc_u32)omc_bmff_get(g->bytes + p, g->iloc_version == 2U ? 4U : 2U);
    p += g->iloc_version == 2U ? 4U : 2U;
    if (count > OMC_BMFF_MAX_ITEMS)
        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
    total_extents = 0U;
    for (i = 0U; i < count; ++i) {
        unsigned idwidth;
        idwidth = g->iloc_version == 2U ? 4U : 2U;
        if (end - p < idwidth + (g->iloc_version ? 2U : 0U) + 4U + base_width)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        id = (omc_u32)omc_bmff_get(g->bytes + p, idwidth);
        p += idwidth;
        item = omc_bmff_item(g, id);
        if (item == NULL || item->located)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        item->located = 1;
        item->method = g->iloc_version ? (omc_u16)omc_bmff_get(g->bytes + p, 2U) : 0U;
        p += g->iloc_version ? 2U : 0U;
        if (item->method > 2U)
            return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        item->reference = (omc_u16)omc_bmff_get(g->bytes + p, 2U);
        p += 2U;
        if (item->reference != 0U &&
            (!omc_bmff_drefs(g) || item->reference > g->local_ref_count ||
             !g->local_refs[item->reference] || item->method == 2U))
            return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        item->base = omc_bmff_get(g->bytes + p, base_width);
        p += base_width;
        item->extent_count = (omc_u16)omc_bmff_get(g->bytes + p, 2U);
        p += 2U;
        item->extent_offset = p;
        if (item->extent_count > OMC_BMFF_MAX_EXTENTS - total_extents)
            return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
        total_extents += item->extent_count;
        n = (omc_u64)item->extent_count *
            (g->index_size + g->offset_size + g->length_size);
        if (n > end - p)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        for (j = 0U; j < item->extent_count; ++j) {
            p += g->index_size;
            offset = omc_bmff_get(g->bytes + p, g->offset_size);
            p += g->offset_size;
            length = omc_bmff_get(g->bytes + p, g->length_size);
            p += g->length_size;
            if (offset > ~(omc_u64)0 - item->base)
                return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
            offset += item->base;
            if (item->method == 2U)
                continue;
            available = item->method == 0U ? g->size : g->idat.size - g->idat.header;
            if ((item->method == 1U && !g->idat.size) || offset > available ||
                length > available - offset)
                return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        }
    }
    if (p != end)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    return 1;
}

static int
omc_bmff_method2(omc_bmff_graph *g)
{
    omc_u32 i;
    omc_u32 j;
    omc_size p;
    omc_size off;
    omc_size end;
    omc_size targets;
    omc_u32 target_count;
    unsigned width;
    omc_u64 index;
    omc_u64 offset;
    omc_u64 length;
    omc_u64 total;
    omc_size q;
    omc_bmff_item_record *item;
    omc_bmff_item_record *parent;
    omc_bmff_box box;
    for (i = 0U; i < g->record_count; ++i) {
        item = &g->records[i];
        if (item->removed || !item->located || item->method != 2U)
            continue;
        if (!g->iref.size)
            return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        off = g->iref.offset + g->iref.header;
        end = g->iref.offset + g->iref.size;
        if (end - off < 4U)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        if (g->bytes[off] > 1U)
            return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        width = g->bytes[off] ? 4U : 2U;
        off += 4U;
        targets = 0U;
        target_count = 0U;
        while (off < end) {
            if (!omc_bmff_box_at(g, off, end, &box))
                return 0;
            p = box.offset + box.header;
            if (box.type == OMC_FOURCC('i', 'l', 'o', 'c')) {
                if (box.size - box.header < width + 2U)
                    return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
                if (omc_bmff_get(g->bytes + p, width) == item->id) {
                    if (targets)
                        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
                    target_count = (omc_u32)omc_bmff_get(g->bytes + p + width, 2U);
                    targets = p + width + 2U;
                    if ((omc_u64)target_count * width !=
                        box.offset + box.size - targets)
                        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
                }
            }
            off += box.size;
        }
        if (!target_count)
            return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        p = item->extent_offset;
        for (j = 0U; j < item->extent_count; ++j) {
            index = g->index_size ? omc_bmff_get(g->bytes + p, g->index_size) : 1U;
            p += g->index_size;
            offset = omc_bmff_get(g->bytes + p, g->offset_size) + item->base;
            p += g->offset_size;
            length = omc_bmff_get(g->bytes + p, g->length_size);
            p += g->length_size;
            if (!index || index > target_count)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            parent = omc_bmff_item(
                g, (omc_u32)omc_bmff_get(
                       g->bytes + targets + ((omc_size)index - 1U) * width, width));
            if (parent == NULL || parent->removed || !parent->located ||
                parent->method == 2U)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            total = 0U;
            q = parent->extent_offset;
            {
                unsigned k;
                for (k = 0U; k < parent->extent_count; ++k) {
                    q += g->index_size + g->offset_size;
                    index = omc_bmff_get(g->bytes + q, g->length_size);
                    q += g->length_size;
                    if (index > ~(omc_u64)0 - total)
                        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
                    total += index;
                }
            }
            if (offset > total || length > total - offset)
                return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
        }
    }
    return 1;
}

static int
omc_bmff_build_iinf(omc_bmff_graph *g, const omc_bmff_write_item *items, omc_u32 count,
                    omc_arena *out)
{
    static const omc_u32 types[5] = {
        0U, OMC_FOURCC('E', 'x', 'i', 'f'), OMC_FOURCC('m', 'i', 'm', 'e'),
        OMC_FOURCC('j', 'u', 'm', 'b'), OMC_FOURCC('c', '2', 'p', 'a')};
    static const char *const names[5] = {"", "OpenMeta EXIF", "OpenMeta XMP",
                                         "OpenMeta JUMBF", "OpenMeta C2PA"};
    omc_arena payload;
    omc_arena infe;
    omc_u32 total;
    omc_u32 i;
    omc_u32 id;
    unsigned version;
    int ok;
    omc_arena_init(&payload);
    omc_arena_init(&infe);
    total = count;
    for (i = 0U; i < g->record_count; ++i)
        if (!g->records[i].removed)
            total++;
    version = g->iinf_version || total > 65535U ? 1U : 0U;
    ok = omc_bmff_number(g, &payload, (omc_u32)version << 24U, 4U) &&
         omc_bmff_number(g, &payload, total, version ? 4U : 2U);
    for (i = 0U; ok && i < g->record_count; ++i)
        if (!g->records[i].removed)
            ok = omc_bmff_copy_box(g, &payload, &g->records[i].infe);
    for (i = 0U; ok && i < count; ++i) {
        omc_arena_reset(&infe);
        id = g->max_id + i + 1U;
        ok = omc_bmff_number(g, &infe, id > 65535U ? 0x03000000U : 0x02000000U, 4U) &&
             omc_bmff_number(g, &infe, id, id > 65535U ? 4U : 2U) &&
             omc_bmff_number(g, &infe, 0U, 2U) &&
             omc_bmff_number(g, &infe, types[items[i].family], 4U) &&
             omc_bmff_append(g, &infe, names[items[i].family],
                             strlen(names[items[i].family]) + 1U);
        if (ok && items[i].family == OMC_BMFF_FAMILY_XMP)
            ok = omc_bmff_append(g, &infe, "application/rdf+xml", 20U) &&
                 omc_bmff_number(g, &infe, 0U, 1U);
        if (ok)
            ok = omc_bmff_emit_box(g, &payload, OMC_FOURCC('i', 'n', 'f', 'e'), &infe);
    }
    if (ok)
        ok = omc_bmff_emit_box(g, out, OMC_FOURCC('i', 'i', 'n', 'f'), &payload);
    omc_arena_fini(&infe);
    omc_arena_fini(&payload);
    return ok;
}

static int
omc_bmff_build_iloc(omc_bmff_graph *g, const omc_bmff_write_item *items, omc_u32 count,
                    omc_u64 new_payload_offset, omc_arena *out)
{
    omc_arena payload;
    omc_bmff_item_record *item;
    omc_u32 total;
    omc_u32 i;
    unsigned j;
    unsigned version;
    unsigned width;
    unsigned length_width;
    unsigned base_width;
    omc_size p;
    omc_u64 offset;
    omc_u64 length;
    omc_u64 index;
    int ok;
    width = 4U;
    length_width = 4U;
    base_width = g->base_size ? (g->base_size > 4U ? 8U : 4U) : 0U;
    total = count;
    for (i = 0U; i < g->record_count; ++i) {
        item = &g->records[i];
        if (item->removed || !item->located)
            continue;
        total++;
        if (item->base && base_width == 0U)
            base_width = 4U;
        if (item->base > 0xFFFFFFFFU)
            base_width = 8U;
        p = item->extent_offset;
        for (j = 0U; j < item->extent_count; ++j) {
            p += g->index_size;
            offset = omc_bmff_get(g->bytes + p, g->offset_size);
            p += g->offset_size;
            length = omc_bmff_get(g->bytes + p, g->length_size);
            p += g->length_size;
            if (offset > 0xFFFFFFFFU)
                width = 8U;
            if (length > 0xFFFFFFFFU)
                length_width = 8U;
        }
    }
    /* Keep measurement and final layouts identical even near the 4 GiB boundary. */
    if ((omc_u64)g->size + OMC_BMFF_MAX_META > 0xFFFFFFFFU)
        width = 8U;
    version =
        g->iloc_version == 2U || total > 65535U || g->max_id + count > 65535U ? 2U : 1U;
    omc_arena_init(&payload);
    ok = omc_bmff_number(g, &payload, (omc_u32)version << 24U, 4U) &&
         omc_bmff_number(g, &payload, (width << 4U) | length_width, 1U) &&
         omc_bmff_number(g, &payload, (base_width << 4U) | g->index_size, 1U) &&
         omc_bmff_number(g, &payload, total, version == 2U ? 4U : 2U);
    for (i = 0U; ok && i < g->record_count; ++i) {
        item = &g->records[i];
        if (item->removed || !item->located)
            continue;
        ok = omc_bmff_number(g, &payload, item->id, version == 2U ? 4U : 2U) &&
             omc_bmff_number(g, &payload, item->method, 2U) &&
             omc_bmff_number(g, &payload, item->reference, 2U) &&
             omc_bmff_number(g, &payload, item->base, base_width) &&
             omc_bmff_number(g, &payload, item->extent_count, 2U);
        p = item->extent_offset;
        for (j = 0U; ok && j < item->extent_count; ++j) {
            index = omc_bmff_get(g->bytes + p, g->index_size);
            p += g->index_size;
            offset = omc_bmff_get(g->bytes + p, g->offset_size);
            p += g->offset_size;
            length = omc_bmff_get(g->bytes + p, g->length_size);
            p += g->length_size;
            ok = omc_bmff_number(g, &payload, index, g->index_size) &&
                 omc_bmff_number(g, &payload, offset, width) &&
                 omc_bmff_number(g, &payload, length, length_width);
        }
    }
    offset = new_payload_offset;
    for (i = 0U; ok && i < count; ++i) {
        ok =
            omc_bmff_number(g, &payload, g->max_id + i + 1U, version == 2U ? 4U : 2U) &&
            omc_bmff_number(g, &payload, 0U, 2U) &&
            omc_bmff_number(g, &payload, 0U, 2U) &&
            omc_bmff_number(g, &payload, 0U, base_width) &&
            omc_bmff_number(g, &payload, 1U, 2U) &&
            omc_bmff_number(g, &payload, 0U, g->index_size) &&
            omc_bmff_number(g, &payload, offset, width) &&
            omc_bmff_number(g, &payload, items[i].payload.size, length_width);
        offset += items[i].payload.size;
    }
    if (ok)
        ok = omc_bmff_emit_box(g, out, OMC_FOURCC('i', 'l', 'o', 'c'), &payload);
    omc_arena_fini(&payload);
    return ok;
}

static int
omc_bmff_build_iref(omc_bmff_graph *g, const omc_bmff_write_item *items, omc_u32 count,
                    omc_arena *out)
{
    omc_arena payload;
    omc_arena child_out;
    omc_size off;
    omc_size end;
    omc_size p;
    omc_size kept_offset;
    omc_bmff_box child;
    omc_u32 from;
    omc_u32 to;
    omc_u32 targets;
    omc_u32 kept;
    omc_u32 i;
    omc_u32 j;
    unsigned input_width;
    unsigned output_width;
    omc_u32 flags;
    omc_u8 linked[64];
    int ok;
    (void)items;
    memset(linked, 0, sizeof(linked));
    omc_arena_init(&payload);
    omc_arena_init(&child_out);
    off = g->iref.offset + g->iref.header;
    end = g->iref.offset + g->iref.size;
    input_width = 2U;
    flags = 0U;
    output_width = g->max_id + count > 65535U ? 4U : 2U;
    ok = 1;
    if (g->iref.size) {
        if (end - off < 4U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            goto done;
        }
        if (g->bytes[off] > 1U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            goto done;
        }
        input_width = g->bytes[off] ? 4U : 2U;
        if (input_width == 4U)
            output_width = 4U;
        flags = (omc_u32)omc_bmff_get(g->bytes + off + 1U, 3U);
        off += 4U;
    }
    ok = omc_bmff_number(g, &payload, flags | (output_width == 4U ? 0x01000000U : 0U),
                         4U);
    while (ok && off < end) {
        if (!omc_bmff_box_at(g, off, end, &child)) {
            ok = 0;
            break;
        }
        p = child.offset + child.header;
        if (child.size - child.header < input_width + 2U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            break;
        }
        from = (omc_u32)omc_bmff_get(g->bytes + p, input_width);
        p += input_width;
        targets = (omc_u32)omc_bmff_get(g->bytes + p, 2U);
        p += 2U;
        if ((omc_u64)targets * input_width != child.offset + child.size - p) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            break;
        }
        from = omc_bmff_remap(g, from);
        omc_arena_reset(&child_out);
        ok = omc_bmff_number(g, &child_out, from, output_width);
        kept_offset = child_out.size;
        if (ok)
            ok = omc_bmff_number(g, &child_out, 0U, 2U);
        kept = 0U;
        for (i = 0U; ok && i < targets; ++i) {
            to = omc_bmff_remap(g, (omc_u32)omc_bmff_get(g->bytes + p, input_width));
            p += input_width;
            if (!from || !to)
                continue;
            if (child.type == OMC_FOURCC('c', 'd', 's', 'c') && to == g->primary) {
                for (j = 0U; j < count; ++j)
                    if (from == g->max_id + j + 1U)
                        linked[j] = 1U;
            }
            ok = omc_bmff_number(g, &child_out, to, output_width);
            kept++;
        }
        if (ok && kept != 0U) {
            omc_bmff_put(child_out.data + kept_offset, kept, 2U);
            ok = omc_bmff_emit_box(g, &payload, child.type, &child_out);
        }
        off += child.size;
    }
    for (i = 0U; ok && g->primary != 0U && i < count; ++i) {
        if (linked[i])
            continue;
        omc_arena_reset(&child_out);
        ok = omc_bmff_number(g, &child_out, g->max_id + i + 1U, output_width) &&
             omc_bmff_number(g, &child_out, 1U, 2U) &&
             omc_bmff_number(g, &child_out, g->primary, output_width) &&
             omc_bmff_emit_box(g, &payload, OMC_FOURCC('c', 'd', 's', 'c'), &child_out);
    }
    if (ok && payload.size > 4U)
        ok = omc_bmff_emit_box(g, out, OMC_FOURCC('i', 'r', 'e', 'f'), &payload);
done:
    omc_arena_fini(&child_out);
    omc_arena_fini(&payload);
    return ok;
}

static int
omc_bmff_build_grpl(omc_bmff_graph *g, const omc_bmff_box *grpl, omc_arena *out)
{
    omc_arena payload;
    omc_arena group;
    omc_bmff_box child;
    omc_size off;
    omc_size end;
    omc_size p;
    omc_u32 count;
    omc_u32 kept;
    omc_u32 id;
    omc_u32 i;
    int ok;
    omc_arena_init(&payload);
    omc_arena_init(&group);
    off = grpl->offset + grpl->header;
    end = grpl->offset + grpl->size;
    ok = 1;
    while (ok && off < end) {
        if (!omc_bmff_box_at(g, off, end, &child)) {
            ok = 0;
            break;
        }
        p = child.offset + child.header;
        if (child.size - child.header < 12U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            break;
        }
        if (g->bytes[p] != 0U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
            break;
        }
        count = (omc_u32)omc_bmff_get(g->bytes + p + 8U, 4U);
        if ((omc_u64)count * 4U != child.size - child.header - 12U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            break;
        }
        if (count > OMC_BMFF_MAX_EXTENTS) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
            break;
        }
        omc_arena_reset(&group);
        ok = omc_bmff_append(g, &group, g->bytes + p, 12U);
        p += 12U;
        kept = 0U;
        for (i = 0U; ok && i < count; ++i) {
            id = omc_bmff_remap(g, (omc_u32)omc_bmff_get(g->bytes + p, 4U));
            p += 4U;
            if (!id)
                continue;
            ok = omc_bmff_number(g, &group, id, 4U);
            kept++;
        }
        if (ok) {
            omc_bmff_put(group.data + 8U, kept, 4U);
            ok = omc_bmff_emit_box(g, &payload, child.type, &group);
        }
        off += child.size;
    }
    if (ok)
        ok = omc_bmff_emit_box(g, out, grpl->type, &payload);
    omc_arena_fini(&group);
    omc_arena_fini(&payload);
    return ok;
}

typedef struct omc_bmff_association {
    omc_u32 next;
    omc_u16 value;
} omc_bmff_association;

typedef struct omc_bmff_property_row {
    omc_u32 item;
    omc_u32 first;
    omc_u32 last;
    omc_u16 count;
    omc_u8 replacement;
    omc_u8 essential;
} omc_bmff_property_row;

typedef struct omc_bmff_properties {
    omc_bmff_property_row *rows;
    omc_bmff_association *associations;
    omc_u32 *slots;
    omc_size slot_count;
    omc_u32 row_count;
    omc_u32 row_capacity;
    omc_u32 association_count;
    omc_u32 association_capacity;
    omc_u32 source_rows;
    omc_u32 source_associations;
    omc_u16 *property_map;
    omc_u32 old_properties;
    omc_u32 new_property;
    omc_u32 flags;
    unsigned version;
} omc_bmff_properties;

static omc_bmff_property_row *
omc_bmff_property_item(omc_bmff_graph *g, omc_bmff_properties *p, omc_u32 id)
{
    omc_size slot;
    omc_bmff_property_row *row;
    slot = ((omc_size)id * 2654435761U) & (p->slot_count - 1U);
    while (p->slots[slot]) {
        row = &p->rows[p->slots[slot] - 1U];
        if (row->item == id)
            return row;
        slot = (slot + 1U) & (p->slot_count - 1U);
    }
    if (p->row_count >= p->row_capacity || p->row_count >= OMC_BMFF_MAX_EXTENTS) {
        omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
        return NULL;
    }
    row = &p->rows[p->row_count];
    row->item = id;
    p->slots[slot] = ++p->row_count;
    return row;
}

static int
omc_bmff_associate(omc_bmff_graph *g, omc_bmff_properties *p,
                   omc_bmff_property_row *row, omc_u16 value)
{
    omc_u32 n;
    omc_bmff_association *association;
    for (n = row->first; n != 0U; n = p->associations[n - 1U].next) {
        association = &p->associations[n - 1U];
        if ((association->value & 0x7FFFU) == (value & 0x7FFFU)) {
            association->value = (omc_u16)(association->value | value);
            return 1;
        }
    }
    if (row->count == 255U || p->association_count >= p->association_capacity ||
        p->association_count >= OMC_BMFF_MAX_ASSOCIATIONS)
        return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
    n = ++p->association_count;
    p->associations[n - 1U].value = value;
    if (row->last)
        p->associations[row->last - 1U].next = n;
    else
        row->first = n;
    row->last = n;
    row->count++;
    return 1;
}

static int
omc_bmff_ipma_pass(omc_bmff_graph *g, const omc_bmff_box *box,
                   omc_bmff_properties *properties, int populate)
{
    omc_size p;
    omc_size end;
    omc_u32 rows;
    omc_u32 i;
    omc_u32 j;
    omc_u32 id;
    omc_u32 flags;
    unsigned version;
    unsigned id_width;
    unsigned width;
    unsigned count;
    omc_u16 raw;
    omc_u16 index;
    omc_u16 mapped;
    omc_bmff_property_row *row;
    p = box->offset + box->header;
    end = box->offset + box->size;
    if (end - p < 8U)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    version = g->bytes[p];
    flags = (omc_u32)omc_bmff_get(g->bytes + p + 1U, 3U);
    if (version > 1U)
        return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
    if (version > properties->version)
        properties->version = version;
    properties->flags |= flags;
    rows = (omc_u32)omc_bmff_get(g->bytes + p + 4U, 4U);
    p += 8U;
    if (!populate) {
        if (rows > OMC_BMFF_MAX_EXTENTS - properties->source_rows)
            return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
        properties->source_rows += rows;
    }
    id_width = version ? 4U : 2U;
    width = flags & 1U ? 2U : 1U;
    for (i = 0U; i < rows; ++i) {
        if (end - p < id_width + 1U)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        id = (omc_u32)omc_bmff_get(g->bytes + p, id_width);
        p += id_width;
        count = g->bytes[p++];
        if ((omc_size)count * width > end - p)
            return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
        if (!populate) {
            if (count > OMC_BMFF_MAX_ASSOCIATIONS - properties->source_associations)
                return omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
            properties->source_associations += count;
        }
        row = NULL;
        id = omc_bmff_remap(g, id);
        if (populate && id) {
            row = omc_bmff_property_item(g, properties, id);
            if (row == NULL)
                return 0;
        }
        for (j = 0U; j < count; ++j) {
            raw = (omc_u16)omc_bmff_get(g->bytes + p, width);
            p += width;
            if (width == 1U)
                raw = (omc_u16)((raw & 0x7FU) | ((raw & 0x80U) ? 0x8000U : 0U));
            index = (omc_u16)(raw & 0x7FFFU);
            /* Validate every source association, including removed-item rows. */
            if (index > properties->old_properties)
                return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
            mapped = properties->property_map[index];
            if (row == NULL || index == 0U)
                continue;
            if (mapped == 0U) {
                row->replacement = 1U;
                if (raw & 0x8000U)
                    row->essential = 1U;
            } else if (!omc_bmff_associate(g, properties, row,
                                           (omc_u16)(mapped | (raw & 0x8000U))))
                return 0;
        }
    }
    if (p != end)
        return omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
    return 1;
}

static int
omc_bmff_build_iprp(omc_bmff_graph *g, int icc_action, omc_const_bytes profile,
                    omc_arena *out)
{
    omc_bmff_properties properties;
    omc_bmff_box ipco;
    omc_bmff_box child;
    omc_arena property_bytes;
    omc_arena ipma_bytes;
    omc_arena payload;
    omc_arena colr;
    omc_size off;
    omc_size end;
    omc_size begin;
    omc_size p;
    omc_u32 table_count;
    omc_u32 retained;
    omc_u32 i;
    omc_u32 n;
    omc_u32 type;
    omc_bmff_property_row *row;
    omc_u16 value;
    int is_icc;
    int ok;
    memset(&properties, 0, sizeof(properties));
    memset(&ipco, 0, sizeof(ipco));
    omc_arena_init(&property_bytes);
    omc_arena_init(&ipma_bytes);
    omc_arena_init(&payload);
    omc_arena_init(&colr);
    ok = 1;
    table_count = 0U;
    retained = 0U;
    properties.property_map =
        (omc_u16 *)calloc(32768U, sizeof(*properties.property_map));
    if (properties.property_map == NULL) {
        g->allocation_status = OMC_STATUS_NO_MEMORY;
        ok = 0;
        goto done;
    }
    begin = g->iprp.offset + g->iprp.header;
    end = g->iprp.offset + g->iprp.size;
    for (off = begin; off < end; off += child.size) {
        if (!omc_bmff_box_at(g, off, end, &child)) {
            ok = 0;
            goto done;
        }
        if (child.type == OMC_FOURCC('i', 'p', 'c', 'o')) {
            if (ipco.size) {
                ok = omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
                goto done;
            }
            ipco = child;
        } else if (child.type == OMC_FOURCC('i', 'p', 'm', 'a')) {
            if (++table_count > OMC_BMFF_MAX_TABLES) {
                ok = omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
                goto done;
            }
        }
    }
    off = ipco.offset + ipco.header;
    p = ipco.offset + ipco.size;
    while (off < p) {
        if (!omc_bmff_box_at(g, off, p, &child)) {
            ok = 0;
            goto done;
        }
        if (++properties.old_properties > 32767U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
            goto done;
        }
        is_icc = 0;
        if (child.type == OMC_FOURCC('c', 'o', 'l', 'r')) {
            if (child.size - child.header < 4U) {
                ok = omc_bmff_fail(g, OMC_TRANSFER_MALFORMED);
                goto done;
            }
            type = (omc_u32)omc_bmff_get(g->bytes + child.offset + child.header, 4U);
            is_icc = type == OMC_FOURCC('p', 'r', 'o', 'f') ||
                     type == OMC_FOURCC('r', 'I', 'C', 'C');
        }
        if (!(icc_action && is_icc)) {
            properties.property_map[properties.old_properties] = (omc_u16)++retained;
            if (!omc_bmff_copy_box(g, &property_bytes, &child)) {
                ok = 0;
                goto done;
            }
        }
        off += child.size;
    }
    if (icc_action == 1) {
        if (retained == 32767U) {
            ok = omc_bmff_fail(g, OMC_TRANSFER_LIMIT);
            goto done;
        }
        properties.new_property = ++retained;
        ok = omc_bmff_number(g, &colr, OMC_FOURCC('p', 'r', 'o', 'f'), 4U) &&
             omc_bmff_append(g, &colr, profile.data, profile.size) &&
             omc_bmff_emit_box(g, &property_bytes, OMC_FOURCC('c', 'o', 'l', 'r'),
                               &colr);
        if (!ok)
            goto done;
    }
    for (off = begin; off < end; off += child.size) {
        if (!omc_bmff_box_at(g, off, end, &child)) {
            ok = 0;
            goto done;
        }
        if (child.type == OMC_FOURCC('i', 'p', 'm', 'a') &&
            !omc_bmff_ipma_pass(g, &child, &properties, 0)) {
            ok = 0;
            goto done;
        }
    }
    properties.row_capacity = properties.source_rows + 1U;
    properties.association_capacity =
        properties.source_associations + properties.source_rows + 1U;
    if (properties.association_capacity > OMC_BMFF_MAX_ASSOCIATIONS)
        properties.association_capacity = OMC_BMFF_MAX_ASSOCIATIONS;
    properties.slot_count = 8U;
    while (properties.slot_count < (omc_size)properties.row_capacity * 2U)
        properties.slot_count *= 2U;
    properties.rows = (omc_bmff_property_row *)calloc(properties.row_capacity,
                                                      sizeof(*properties.rows));
    properties.associations = (omc_bmff_association *)calloc(
        properties.association_capacity, sizeof(*properties.associations));
    properties.slots =
        (omc_u32 *)calloc(properties.slot_count, sizeof(*properties.slots));
    if (properties.rows == NULL || properties.associations == NULL ||
        properties.slots == NULL) {
        g->allocation_status = OMC_STATUS_NO_MEMORY;
        ok = 0;
        goto done;
    }
    for (off = begin; off < end; off += child.size) {
        if (!omc_bmff_box_at(g, off, end, &child)) {
            ok = 0;
            goto done;
        }
        if (child.type == OMC_FOURCC('i', 'p', 'm', 'a') &&
            !omc_bmff_ipma_pass(g, &child, &properties, 1)) {
            ok = 0;
            goto done;
        }
    }
    if (properties.new_property && g->primary) {
        row = omc_bmff_property_item(g, &properties, g->primary);
        if (row == NULL) {
            ok = 0;
            goto done;
        }
        row->replacement = 1U;
    }
    for (i = 0U; i < properties.row_count; ++i) {
        row = &properties.rows[i];
        if (properties.new_property && row->replacement &&
            !omc_bmff_associate(
                g, &properties, row,
                (omc_u16)(properties.new_property | (row->essential ? 0x8000U : 0U)))) {
            ok = 0;
            goto done;
        }
        if (row->item > 65535U)
            properties.version = 1U;
        for (n = row->first; n; n = properties.associations[n - 1U].next)
            if ((properties.associations[n - 1U].value & 0x7FFFU) > 127U)
                properties.flags |= 1U;
    }
    if (!omc_bmff_number(g, &ipma_bytes, (properties.version << 24U) | properties.flags,
                         4U) ||
        !omc_bmff_number(g, &ipma_bytes, properties.row_count, 4U)) {
        ok = 0;
        goto done;
    }
    for (i = 0U; ok && i < properties.row_count; ++i) {
        row = &properties.rows[i];
        ok = omc_bmff_number(g, &ipma_bytes, row->item, properties.version ? 4U : 2U) &&
             omc_bmff_number(g, &ipma_bytes, row->count, 1U);
        for (n = row->first; ok && n; n = properties.associations[n - 1U].next) {
            value = properties.associations[n - 1U].value;
            if (!(properties.flags & 1U))
                value = (omc_u16)((value & 0x7FU) | ((value & 0x8000U) ? 0x80U : 0U));
            ok =
                omc_bmff_number(g, &ipma_bytes, value, properties.flags & 1U ? 2U : 1U);
        }
    }
    if (!ok)
        goto done;
    if (ipco.size || property_bytes.size)
        ok = omc_bmff_emit_box(g, &payload, OMC_FOURCC('i', 'p', 'c', 'o'),
                               &property_bytes);
    if (ok && (table_count || properties.row_count))
        ok =
            omc_bmff_emit_box(g, &payload, OMC_FOURCC('i', 'p', 'm', 'a'), &ipma_bytes);
    for (off = begin; ok && off < end; off += child.size) {
        if (!omc_bmff_box_at(g, off, end, &child)) {
            ok = 0;
            break;
        }
        if (child.type != OMC_FOURCC('i', 'p', 'c', 'o') &&
            child.type != OMC_FOURCC('i', 'p', 'm', 'a'))
            ok = omc_bmff_copy_box(g, &payload, &child);
    }
    if (ok && (g->iprp.size || payload.size))
        ok = omc_bmff_emit_box(g, out, OMC_FOURCC('i', 'p', 'r', 'p'), &payload);
done:
    free(properties.property_map);
    free(properties.slots);
    free(properties.rows);
    free(properties.associations);
    omc_arena_fini(&colr);
    omc_arena_fini(&payload);
    omc_arena_fini(&ipma_bytes);
    omc_arena_fini(&property_bytes);
    return ok;
}

/* Remove replaced payloads from the copied idat. Method-zero data keeps its
 * original address, while method-one data uses the copied idat coordinate space. */
static int
omc_bmff_retain_data(omc_bmff_graph *g, omc_arena *idat, omc_arena *file)
{
    omc_u32 i;
    unsigned j;
    omc_size p;
    omc_u64 offset;
    omc_u64 length;
    omc_u64 begin;
    omc_u64 end;
    omc_bmff_item_record *item;
    if (idat != NULL && g->idat.size) {
        g->allocation_status = omc_arena_reserve(idat, g->idat.size - g->idat.header);
        if (g->allocation_status != OMC_STATUS_OK)
            return 0;
        idat->size = g->idat.size - g->idat.header;
        if (idat->size)
            memcpy(idat->data, g->bytes + g->idat.offset + g->idat.header, idat->size);
        for (i = 0U; i < g->record_count; ++i) {
            item = &g->records[i];
            if (!item->removed || !item->located || item->method == 2U)
                continue;
            p = item->extent_offset;
            for (j = 0U; j < item->extent_count; ++j) {
                p += g->index_size;
                offset = item->base + omc_bmff_get(g->bytes + p, g->offset_size);
                p += g->offset_size;
                length = omc_bmff_get(g->bytes + p, g->length_size);
                p += g->length_size;
                if (item->method == 0U) {
                    begin = g->idat.offset + g->idat.header;
                    if (offset < begin)
                        continue;
                    offset -= begin;
                }
                if (offset <= idat->size && length <= idat->size - offset && length)
                    memset(idat->data + (omc_size)offset, 0, (omc_size)length);
            }
        }
    }
    if (file != NULL && g->meta.size)
        memset(file->data + g->meta.offset + g->meta.header, 0,
               g->meta.size - g->meta.header);
    for (i = 0U; i < g->record_count; ++i) {
        item = &g->records[i];
        if (item->removed || !item->located || item->method == 2U)
            continue;
        p = item->extent_offset;
        for (j = 0U; j < item->extent_count; ++j) {
            p += g->index_size;
            offset = item->base + omc_bmff_get(g->bytes + p, g->offset_size);
            p += g->offset_size;
            length = omc_bmff_get(g->bytes + p, g->length_size);
            p += g->length_size;
            if (idat != NULL && item->method == 1U && length)
                memcpy(idat->data + (omc_size)offset,
                       g->bytes + g->idat.offset + g->idat.header + (omc_size)offset,
                       (omc_size)length);
            if (file != NULL && item->method == 0U && g->meta.size && length) {
                if (offset < g->meta.offset + g->meta.header &&
                    offset + length > g->meta.offset)
                    return omc_bmff_fail(g, OMC_TRANSFER_UNSUPPORTED);
                begin = offset > g->meta.offset + g->meta.header
                            ? offset
                            : g->meta.offset + g->meta.header;
                end = offset + length < g->meta.offset + g->meta.size
                          ? offset + length
                          : g->meta.offset + g->meta.size;
                if (begin < end)
                    memcpy(file->data + (omc_size)begin, g->bytes + (omc_size)begin,
                           (omc_size)(end - begin));
            }
        }
    }
    return 1;
}

omc_status
omc_bmff_rewrite(const omc_u8 *bytes, omc_size size, const omc_bmff_write_item *items,
                 omc_u32 count, unsigned strip_mask, int icc_action,
                 omc_const_bytes profile, int require_primary, omc_arena *out,
                 omc_bmff_write_res *result)
{
    omc_bmff_graph g;
    omc_bmff_box child;
    omc_bmff_item_record *item;
    omc_arena prefix;
    omc_arena iinf;
    omc_arena iloc;
    omc_arena suffix;
    omc_arena idat;
    omc_arena meta;
    omc_arena candidate;
    omc_byte_ref ref;
    omc_size off;
    omc_size end;
    omc_size measured_iloc;
    omc_u64 payload_offset;
    omc_u32 old_count[5];
    omc_u32 new_count[5];
    omc_u32 replacement[5];
    omc_u32 i;
    unsigned family;
    int ok;

    if (result == NULL)
        return OMC_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (out == NULL || (bytes == NULL && size) || (items == NULL && count) ||
        count > 64U || (strip_mask & ~30U) || icc_action < 0 || icc_action > 2 ||
        (icc_action == 1 && (profile.data == NULL || profile.size == 0U)))
        return OMC_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    for (i = 0U; i < count; ++i)
        if (items[i].family < 1U || items[i].family > 4U ||
            (items[i].payload.data == NULL && items[i].payload.size))
            return OMC_STATUS_INVALID_ARGUMENT;
    memset(&g, 0, sizeof(g));
    memset(old_count, 0, sizeof(old_count));
    memset(new_count, 0, sizeof(new_count));
    memset(replacement, 0, sizeof(replacement));
    g.bytes = bytes;
    g.size = size;
    g.status = OMC_TRANSFER_OK;
    g.allocation_status = OMC_STATUS_OK;
    omc_arena_init(&prefix);
    omc_arena_init(&iinf);
    omc_arena_init(&iloc);
    omc_arena_init(&suffix);
    omc_arena_init(&idat);
    omc_arena_init(&meta);
    omc_arena_init(&candidate);
    ok = omc_bmff_collect(&g) && omc_bmff_locations(&g);
    if (!ok)
        goto done;
    if (require_primary && count &&
        (!g.meta.size || !g.iinf.size || !g.iloc.size || !g.primary ||
         omc_bmff_item(&g, g.primary) == NULL)) {
        omc_bmff_fail(&g, OMC_TRANSFER_UNSUPPORTED);
        goto done;
    }
    if (g.max_id > 0xFFFFFFFFU - count || g.record_count > OMC_BMFF_MAX_ITEMS - count) {
        omc_bmff_fail(&g, OMC_TRANSFER_LIMIT);
        goto done;
    }
    for (i = 0U; i < count; ++i) {
        family = items[i].family;
        new_count[family]++;
        replacement[family] = g.max_id + i + 1U;
        strip_mask |= 1U << family;
    }
    for (i = 0U; i < g.record_count; ++i) {
        item = &g.records[i];
        if (item->family && (strip_mask & (1U << item->family))) {
            item->removed = 1;
            old_count[item->family]++;
            if (item->id == g.primary) {
                omc_bmff_fail(&g, OMC_TRANSFER_UNSUPPORTED);
                goto done;
            }
        }
    }
    for (i = 0U; i < g.record_count; ++i) {
        item = &g.records[i];
        if (item->removed && old_count[item->family] == 1U &&
            new_count[item->family] == 1U)
            item->replacement = replacement[item->family];
    }
    if (!omc_bmff_method2(&g))
        goto done;

    /* Retain unmanaged children and their relative order. Managed tables follow
     * them; idat is last so all new file-relative locations are known exactly. */
    off = g.meta.size ? g.meta.offset + g.meta.header + 4U : 0U;
    end = g.meta.offset + g.meta.size;
    while (off < end) {
        if (!omc_bmff_box_at(&g, off, end, &child))
            goto done;
        switch (child.type) {
        case OMC_FOURCC('i', 'i', 'n', 'f'):
        case OMC_FOURCC('i', 'l', 'o', 'c'):
        case OMC_FOURCC('i', 'r', 'e', 'f'):
        case OMC_FOURCC('i', 'p', 'r', 'p'):
        case OMC_FOURCC('i', 'd', 'a', 't'):
            break;
        case OMC_FOURCC('g', 'r', 'p', 'l'):
            if (!omc_bmff_build_grpl(&g, &child, &prefix))
                goto done;
            break;
        default:
            if (!omc_bmff_copy_box(&g, &prefix, &child))
                goto done;
            break;
        }
        off += child.size;
    }
    if ((g.iinf.size || count) && !omc_bmff_build_iinf(&g, items, count, &iinf))
        goto done;
    if ((g.iloc.size || count) && !omc_bmff_build_iloc(&g, items, count, 0U, &iloc))
        goto done;
    if (!omc_bmff_build_iref(&g, items, count, &suffix) ||
        !omc_bmff_build_iprp(&g, icc_action, profile, &suffix))
        goto done;
    if (!omc_bmff_retain_data(&g, &idat, NULL))
        goto done;
    payload_offset = (omc_u64)size + 12U + prefix.size + iinf.size + iloc.size +
                     suffix.size + 8U + idat.size;
    if (payload_offset < size || payload_offset > ~(omc_u64)0 - OMC_BMFF_MAX_META) {
        omc_bmff_fail(&g, OMC_TRANSFER_LIMIT);
        goto done;
    }
    measured_iloc = iloc.size;
    if (g.iloc.size || count) {
        omc_arena_reset(&iloc);
        if (!omc_bmff_build_iloc(&g, items, count, payload_offset, &iloc))
            goto done;
        if (iloc.size != measured_iloc) {
            omc_bmff_fail(&g, OMC_TRANSFER_LIMIT);
            goto done;
        }
    }
    for (i = 0U; i < count; ++i)
        if (!omc_bmff_append(&g, &idat, items[i].payload.data, items[i].payload.size))
            goto done;
    if (!omc_bmff_number(&g, &meta, 0U, 4U) ||
        !omc_bmff_append(&g, &meta, prefix.data, prefix.size) ||
        !omc_bmff_append(&g, &meta, iinf.data, iinf.size) ||
        !omc_bmff_append(&g, &meta, iloc.data, iloc.size) ||
        !omc_bmff_append(&g, &meta, suffix.data, suffix.size))
        goto done;
    if ((g.idat.size || count) &&
        !omc_bmff_emit_box(&g, &meta, OMC_FOURCC('i', 'd', 'a', 't'), &idat))
        goto done;
    if (meta.size > OMC_BMFF_MAX_META - 8U || size > (omc_size)-1 - meta.size - 8U) {
        omc_bmff_fail(&g, OMC_TRANSFER_LIMIT);
        goto done;
    }
    g.allocation_status = omc_arena_append(&candidate, bytes, size, &ref);
    if (g.allocation_status != OMC_STATUS_OK)
        goto done;
    /* A zero-sized top-level box formerly extended to EOF. Give it an explicit
     * size before appending, without moving its payload or expanding its header. */
    for (off = 0U; off < size; off += child.size) {
        if (!omc_bmff_box_at(&g, off, size, &child))
            goto done;
        if (omc_bmff_get(bytes + off, 4U) == 0U) {
            if (child.size > 0xFFFFFFFFU) {
                omc_bmff_fail(&g, OMC_TRANSFER_UNSUPPORTED);
                goto done;
            }
            omc_bmff_put(candidate.data + off, child.size, 4U);
        }
    }
    if (!omc_bmff_retain_data(&g, NULL, &candidate))
        goto done;
    if (g.meta.size)
        omc_bmff_put(candidate.data + g.meta.offset + 4U,
                     OMC_FOURCC('f', 'r', 'e', 'e'), 4U);
    /* Full-file output is not subject to the separate metadata-size bound. */
    {
        omc_u8 header[8];
        omc_bmff_put(header, meta.size + 8U, 4U);
        omc_bmff_put(header + 4U, OMC_FOURCC('m', 'e', 't', 'a'), 4U);
        g.allocation_status =
            omc_arena_append(&candidate, header, sizeof(header), &ref);
        if (g.allocation_status != OMC_STATUS_OK)
            goto done;
        g.allocation_status = omc_arena_append(&candidate, meta.data, meta.size, &ref);
        if (g.allocation_status != OMC_STATUS_OK)
            goto done;
    }
    omc_arena_fini(out);
    *out = candidate;
    omc_arena_init(&candidate);
    memcpy(result->removed, old_count, sizeof(old_count));
    memcpy(result->inserted, new_count, sizeof(new_count));
done:
    result->status = g.status;
    omc_arena_fini(&candidate);
    omc_arena_fini(&meta);
    omc_arena_fini(&idat);
    omc_arena_fini(&suffix);
    omc_arena_fini(&iloc);
    omc_arena_fini(&iinf);
    omc_arena_fini(&prefix);
    free(g.records);
    free(g.slots);
    free(g.local_refs);
    return g.allocation_status;
}
