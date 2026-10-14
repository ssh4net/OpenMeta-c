#include "edit/omc_bmff_rewrite.h"
#include "omc/omc_transfer_package.h"
#include "omc_test_assert.h"
#include <stdlib.h>
#include <string.h>

typedef struct buffer {
    omc_u8 data[262144];
    omc_size size;
} buffer;
typedef struct fixture {
    buffer bytes;
    omc_size meta;
    omc_size media;
    omc_size second_index;
    omc_size dref_flags;
    omc_size method2_index;
} fixture;

static omc_u64
get(const omc_u8 *p, unsigned n)
{
    omc_u64 v = 0U;
    while (n--)
        v = (v << 8U) | *p++;
    return v;
}
static void
put(omc_u8 *p, omc_u64 v, unsigned n)
{
    while (n) {
        p[--n] = (omc_u8)v;
        v >>= 8U;
    }
}
static void
num(buffer *b, omc_u64 v, unsigned n)
{
    assert(b->size + n <= sizeof(b->data));
    put(b->data + b->size, v, n);
    b->size += n;
}
static void
raw(buffer *b, const void *data, omc_size n)
{
    assert(b->size + n <= sizeof(b->data));
    if (n)
        memcpy(b->data + b->size, data, n);
    b->size += n;
}
static omc_size
begin(buffer *b, const char *type)
{
    omc_size p = b->size;
    num(b, 0U, 4U);
    raw(b, type, 4U);
    return p;
}
static void
end(buffer *b, omc_size p)
{
    put(b->data + p, b->size - p, 4U);
}
static void
relation_targets(buffer *b, const char *type, unsigned from,
                 const unsigned *targets, unsigned count, unsigned id_width)
{
    omc_size p = begin(b, type);
    unsigned i;
    num(b, from, id_width);
    num(b, count, 2U);
    for (i = 0U; i < count; ++i)
        num(b, targets[i], id_width);
    end(b, p);
}
static void
relation(buffer *b, const char *type, unsigned from, unsigned to,
         unsigned id_width)
{
    relation_targets(b, type, from, &to, 1U, id_width);
}
static void
make_fixture_version(fixture *f, unsigned duplicate_family, unsigned tables,
                     unsigned iref_version)
{
    static const unsigned ids[] = {1U, 2U, 3U, 4U, 10U, 11U, 20U};
    static const unsigned repeated_xmp_targets[] = {2U, 11U, 2U};
    static const unsigned removed_source_targets[] = {3U, 3U};
    static const char *const types[] = {"Exif", "mime", "jumb", "c2pa",
                                        "hvc1", "hvc1", "hvc1"};
    buffer *b = &f->bytes;
    omc_size p, q, r, iprp, ipco, primary_offset = 0U;
    unsigned id_width;
    unsigned i;
    memset(f, 0, sizeof(*f));
    id_width = iref_version ? 4U : 2U;
    p = begin(b, "ftyp");
    raw(b, "mif1", 4U);
    num(b, 0U, 4U);
    raw(b, "mif1", 4U);
    end(b, p);
    f->meta = begin(b, "meta");
    num(b, 0U, 4U);
    p = begin(b, "pitm");
    num(b, 0U, 4U);
    num(b, 10U, 2U);
    end(b, p);
    p = begin(b, "iinf");
    num(b, 0U, 4U);
    num(b, 7U, 2U);
    for (i = 0U; i < 7U; ++i) {
        q = begin(b, "infe");
        num(b, 0x02000000U, 4U);
        num(b, ids[i], 2U);
        num(b, 0U, 2U);
        if (i == 2U && duplicate_family == OMC_BMFF_FAMILY_EXIF)
            raw(b, "Exif", 4U);
        else if (i == 2U && duplicate_family == OMC_BMFF_FAMILY_XMP)
            raw(b, "mime", 4U);
        else
            raw(b, types[i], 4U);
        num(b, 0U, 1U);
        if (i == 1U ||
            (i == 2U && duplicate_family == OMC_BMFF_FAMILY_XMP)) {
            raw(b, "application/rdf+xml", 20U);
            num(b, 0U, 1U);
        }
        end(b, q);
    }
    end(b, p);
    p = begin(b, "iloc");
    num(b, 0x01000000U, 4U);
    num(b, 0x22U, 1U);
    num(b, 0x11U, 1U);
    num(b, 7U, 2U);
    for (i = 0U; i < 7U; ++i) {
        num(b, ids[i], 2U);
        num(b, i == 4U ? 0U : i == 6U ? 2U : 1U, 2U);
        num(b, i == 4U ? 1U : 0U, 2U);
        num(b, 0U, 1U);
        num(b, 1U, 2U);
        if (i == 6U)
            f->method2_index = b->size;
        num(b, i == 6U ? 1U : 0U, 1U);
        if (i == 4U)
            primary_offset = b->size;
        num(b, i < 4U ? i * 4U : i == 5U ? 16U : 0U, 2U);
        num(b, i == 6U ? 2U : 4U, 2U);
    }
    end(b, p);
    p = begin(b, "dinf");
    q = begin(b, "dref");
    num(b, 0U, 4U);
    num(b, 1U, 4U);
    r = begin(b, "url ");
    f->dref_flags = b->size;
    num(b, 1U, 4U);
    end(b, r);
    end(b, q);
    end(b, p);
    p = begin(b, "idat");
    raw(b, "EXIFXMP!JUMBC2PAAUX!", 20U);
    end(b, p);
    p = begin(b, "iref");
    num(b, (omc_u32)iref_version << 24U, 4U);
    relation(b, "cdsc", 1U, 10U, id_width);
    relation(b, "test", 1U, 11U, id_width);
    relation(b, "test", 11U, 1U, id_width);
    relation(b, "auxl", 11U, 10U, id_width);
    relation(b, "iloc", 20U, 11U, id_width);
    relation_targets(b, "dimg", 10U, repeated_xmp_targets, 3U, id_width);
    relation_targets(b, "dimg", 1U, removed_source_targets, 2U, id_width);
    end(b, p);
    p = begin(b, "grpl");
    q = begin(b, "altr");
    num(b, 0U, 4U);
    num(b, 7U, 4U);
    num(b, 2U, 4U);
    num(b, 1U, 4U);
    num(b, 11U, 4U);
    end(b, q);
    end(b, p);
    iprp = begin(b, "iprp");
    ipco = begin(b, "ipco");
    p = begin(b, "ispe");
    num(b, 0U, 4U);
    num(b, 640U, 4U);
    num(b, 480U, 4U);
    end(b, p);
    p = begin(b, "colr");
    raw(b, "profOLDICC", 10U);
    end(b, p);
    p = begin(b, "pixi");
    num(b, 0U, 4U);
    num(b, 1U, 1U);
    num(b, 8U, 1U);
    end(b, p);
    end(b, ipco);
    for (i = 0U; i < tables; ++i) {
        p = begin(b, "ipma");
        if (i == 0U) {
            num(b, 0U, 4U);
            num(b, 3U, 4U);
            num(b, 10U, 2U);
            num(b, 2U, 1U);
            num(b, 1U, 1U);
            num(b, 0x82U, 1U);
            num(b, 1U, 2U);
            num(b, 1U, 1U);
            num(b, 2U, 1U);
            num(b, 11U, 2U);
            num(b, 1U, 1U);
            num(b, 2U, 1U);
        } else if (i == 1U) {
            num(b, 0x01000001U, 4U);
            num(b, 2U, 4U);
            num(b, 10U, 4U);
            num(b, 3U, 1U);
            f->second_index = b->size;
            num(b, 0x8001U, 2U);
            num(b, 2U, 2U);
            num(b, 3U, 2U);
            num(b, 1U, 4U);
            num(b, 1U, 1U);
            num(b, 3U, 2U);
        } else {
            num(b, 0U, 4U);
            num(b, 0U, 4U);
        }
        end(b, p);
    }
    end(b, iprp);
    end(b, f->meta);
    p = begin(b, "mdat");
    f->media = b->size;
    raw(b, "PIXELS!!", 8U);
    end(b, p);
    put(b->data + primary_offset, f->media, 2U);
}
static void
make_fixture(fixture *f, int duplicate_exif, unsigned tables)
{
    make_fixture_version(f, duplicate_exif ? OMC_BMFF_FAMILY_EXIF : 0U,
                         tables, 0U);
}
static omc_size
box(const omc_u8 *data, omc_size begin_at, omc_size end_at, const char *type)
{
    omc_size n;
    while (begin_at < end_at) {
        assert(end_at - begin_at >= 8U);
        n = (omc_size)get(data + begin_at, 4U);
        assert(n >= 8U && n <= end_at - begin_at);
        if (memcmp(data + begin_at + 4U, type, 4U) == 0)
            return begin_at;
        begin_at += n;
    }
    return 0U;
}
static omc_size
child(const omc_arena *a, const char *type)
{
    omc_size p = box(a->data, 0U, a->size, "meta");
    assert(p);
    return box(a->data, p + 12U, p + (omc_size)get(a->data + p, 4U), type);
}
static int
edge(const omc_arena *a, const char *type, unsigned from, unsigned to)
{
    omc_size p = child(a, "iref"), limit, q, n, k;
    unsigned width;
    if (!p)
        return 0;
    limit = p + (omc_size)get(a->data + p, 4U);
    width = a->data[p + 8U] ? 4U : 2U;
    for (p += 12U; p < limit; p += n) {
        n = (omc_size)get(a->data + p, 4U);
        if (memcmp(a->data + p + 4U, type, 4U) != 0 ||
            get(a->data + p + 8U, width) != from)
            continue;
        q = p + 8U + width;
        k = (omc_size)get(a->data + q, 2U);
        q += 2U;
        while (k--) {
            if (get(a->data + q, width) == to)
                return 1;
            q += width;
        }
    }
    return 0;
}
static int
relation_targets_equal(const omc_arena *a, const char *type, unsigned from,
                       const unsigned *expected, unsigned expected_count)
{
    omc_size p = child(a, "iref"), limit, q, n, target_count;
    unsigned width, matches, i;
    if (!p)
        return expected_count == 0U;
    limit = p + (omc_size)get(a->data + p, 4U);
    width = a->data[p + 8U] ? 4U : 2U;
    matches = 0U;
    for (p += 12U; p < limit; p += n) {
        n = (omc_size)get(a->data + p, 4U);
        if (n < 8U + width + 2U ||
            memcmp(a->data + p + 4U, type, 4U) != 0 ||
            get(a->data + p + 8U, width) != from)
            continue;
        ++matches;
        q = p + 8U + width;
        target_count = (omc_size)get(a->data + q, 2U);
        q += 2U;
        if (target_count != expected_count)
            return 0;
        for (i = 0U; i < expected_count; ++i) {
            if (get(a->data + q, width) != expected[i])
                return 0;
            q += width;
        }
    }
    return expected_count == 0U ? matches == 0U : matches == 1U;
}
static omc_u16
association(const omc_arena *a, unsigned id, unsigned index)
{
    omc_size p = child(a, "iprp"), limit, q;
    omc_u32 rows, item;
    unsigned idwidth, width, n, value;
    assert(p);
    limit = p + (omc_size)get(a->data + p, 4U);
    p = box(a->data, p + 8U, limit, "ipma");
    assert(p);
    idwidth = a->data[p + 8U] ? 4U : 2U;
    width = a->data[p + 11U] & 1U ? 2U : 1U;
    rows = (omc_u32)get(a->data + p + 12U, 4U);
    q = p + 16U;
    while (rows--) {
        item = (omc_u32)get(a->data + q, idwidth);
        q += idwidth;
        n = a->data[q++];
        while (n--) {
            value = (unsigned)get(a->data + q, width);
            q += width;
            if (width == 1U)
                value = (value & 127U) | ((value & 128U) ? 0x8000U : 0U);
            if (item == id && (value & 0x7FFFU) == index)
                return (omc_u16)value;
        }
    }
    return 0U;
}
static void
check_locations(const omc_arena *a, const fixture *f)
{
    omc_size p = child(a, "iloc"), q;
    omc_u32 count, id;
    unsigned ow, lw, bw, iw, method, reference, n;
    omc_u64 base, index, offset, length;
    assert(p && a->data[p + 8U] == 1U);
    ow = a->data[p + 12U] >> 4U;
    lw = a->data[p + 12U] & 15U;
    bw = a->data[p + 13U] >> 4U;
    iw = a->data[p + 13U] & 15U;
    assert(ow == 4U && lw == 4U && bw == 4U && iw == 1U);
    count = (omc_u32)get(a->data + p + 14U, 2U);
    assert(count == 7U);
    q = p + 16U;
    while (count--) {
        id = (omc_u32)get(a->data + q, 2U);
        q += 2U;
        method = (unsigned)get(a->data + q, 2U);
        q += 2U;
        reference = (unsigned)get(a->data + q, 2U);
        q += 2U;
        base = get(a->data + q, bw);
        q += bw;
        n = (unsigned)get(a->data + q, 2U);
        q += 2U;
        assert(n == 1U);
        index = get(a->data + q, iw);
        q += iw;
        offset = get(a->data + q, ow);
        q += ow;
        length = get(a->data + q, lw);
        q += lw;
        assert(base == 0U);
        if (id == 10U) {
            assert(method == 0U && reference == 1U && offset == f->media &&
                   length == 4U);
        }
        if (id == 20U) {
            assert(method == 2U && reference == 0U && index == 1U && offset == 0U &&
                   length == 2U);
        }
        if (id >= 21U) {
            assert(method == 0U && reference == 0U && offset < a->size && length == 4U);
            assert(memcmp(a->data + (omc_size)offset, "NEW!", 4U) == 0);
        }
    }
}
static void
rewrite(fixture *f, int duplicate, unsigned iref_version)
{
    static const unsigned repeated_xmp_targets[] = {22U, 11U, 22U};
    static const unsigned second_edit_xmp_targets[] = {26U, 11U, 26U};
    static const unsigned removed_source_targets[] = {23U, 23U};
    omc_arena out;
    omc_arena repeated;
    omc_bmff_write_res res;
    omc_bmff_write_res repeated_res;
    omc_bmff_write_item items[4];
    omc_const_bytes profile;
    omc_size p, q;
    unsigned i;
    make_fixture_version(f, duplicate ? OMC_BMFF_FAMILY_EXIF : 0U, 2U,
                         iref_version);
    omc_arena_init(&out);
    for (i = 0U; i < 4U; ++i) {
        items[i].family = i + 1U;
        items[i].payload.data = (const omc_u8 *)"NEW!";
        items[i].payload.size = 4U;
    }
    profile.data = (const omc_u8 *)"NEWICC";
    profile.size = 6U;
    assert(omc_bmff_rewrite(f->bytes.data, f->bytes.size, items, 4U, 0U, 1, profile, 1,
                            &out, &res) == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK && res.removed[1] == (duplicate ? 2U : 1U));
    assert(out.size > f->bytes.size);
    assert(memcmp(out.data + f->meta + 4U, "free", 4U) == 0);
    assert(memcmp(out.data + f->media, "PIXELS!!", 8U) == 0);
    assert(edge(&out, "auxl", 11U, 10U) && edge(&out, "iloc", 20U, 11U));
    assert(edge(&out, "test", 21U, 11U) == !duplicate);
    assert(edge(&out, "test", 11U, 21U) == !duplicate);
    for (i = 21U; i <= 24U; ++i)
        assert(edge(&out, "cdsc", i, 10U));
    assert(relation_targets_equal(&out, "dimg", 10U, repeated_xmp_targets,
                                  3U));
    if (duplicate)
        assert(relation_targets_equal(&out, "dimg", 21U,
                                      removed_source_targets, 0U));
    else
        assert(relation_targets_equal(&out, "dimg", 21U,
                                      removed_source_targets, 2U));
    p = child(&out, "grpl");
    assert(p);
    q = p + 8U;
    assert(get(out.data + q + 16U, 4U) == (duplicate ? 1U : 2U));
    assert(get(out.data + q + 20U, 4U) == (duplicate ? 11U : 21U));
    assert(association(&out, 10U, 1U) == 0x8001U);
    assert(association(&out, 10U, 2U) == 2U);
    assert(association(&out, 10U, 3U) == 0x8003U);
    assert(association(&out, 11U, 3U) == 3U);
    assert(association(&out, 21U, 2U) == (duplicate ? 0U : 2U));
    if (!duplicate)
        check_locations(&out, f);

    omc_arena_init(&repeated);
    assert(omc_bmff_rewrite(out.data, out.size, items, 4U, 0U, 0, profile, 1,
                            &repeated, &repeated_res) == OMC_STATUS_OK);
    assert(repeated_res.status == OMC_TRANSFER_OK);
    assert(relation_targets_equal(&repeated, "dimg", 10U,
                                  second_edit_xmp_targets, 3U));
    assert(memcmp(repeated.data + f->media, "PIXELS!!", 8U) == 0);
    omc_arena_fini(&repeated);
    omc_arena_fini(&out);
}
static void
reject_cases(fixture *f)
{
    omc_arena out;
    omc_byte_ref ref;
    omc_bmff_write_res res;
    omc_bmff_write_item item;
    omc_const_bytes profile;
    unsigned i;
    omc_transfer_status expected[4] = {OMC_TRANSFER_MALFORMED, OMC_TRANSFER_UNSUPPORTED,
                                       OMC_TRANSFER_UNSUPPORTED, OMC_TRANSFER_LIMIT};
    omc_arena_init(&out);
    assert(omc_arena_append(&out, "keep", 4U, &ref) == OMC_STATUS_OK);
    item.family = 1U;
    item.payload.data = (const omc_u8 *)"NEW!";
    item.payload.size = 4U;
    profile.data = NULL;
    profile.size = 0U;
    for (i = 0U; i < 4U; ++i) {
        make_fixture(f, 0, i == 3U ? 4097U : 2U);
        if (i == 0U)
            put(f->bytes.data + f->second_index, 4U, 2U);
        if (i == 1U)
            put(f->bytes.data + f->dref_flags, 0U, 4U);
        if (i == 2U)
            f->bytes.data[f->method2_index] = 2U;
        assert(omc_bmff_rewrite(f->bytes.data, f->bytes.size, &item, 1U, 0U, 0, profile,
                                1, &out, &res) == OMC_STATUS_OK);
        assert(res.status == expected[i]);
        assert(out.size == 4U && memcmp(out.data, "keep", 4U) == 0);
    }
    omc_arena_fini(&out);
}
static void
dimg_cases(fixture *f)
{
    static const unsigned repeated_xmp_targets[] = {2U, 11U, 2U};
    omc_arena out;
    omc_byte_ref ref;
    omc_bmff_write_res res;
    omc_const_bytes profile;
    unsigned iref_version;

    profile.data = NULL;
    profile.size = 0U;
    for (iref_version = 0U; iref_version <= 1U; ++iref_version) {
        make_fixture_version(f, 0U, 2U, iref_version);
        omc_arena_init(&out);
        assert(omc_arena_append(&out, "keep", 4U, &ref) == OMC_STATUS_OK);
        assert(omc_bmff_rewrite(f->bytes.data, f->bytes.size, NULL, 0U,
                                1U << OMC_BMFF_FAMILY_XMP, 0, profile, 0,
                                &out, &res) == OMC_STATUS_OK);
        assert(res.status == OMC_TRANSFER_UNSUPPORTED);
        assert(out.size == 4U && memcmp(out.data, "keep", 4U) == 0);
        omc_arena_fini(&out);

        make_fixture_version(f, 0U, 2U, iref_version);
        omc_arena_init(&out);
        assert(omc_bmff_rewrite(f->bytes.data, f->bytes.size, NULL, 0U,
                                1U << OMC_BMFF_FAMILY_EXIF, 0, profile, 0,
                                &out, &res) == OMC_STATUS_OK);
        assert(res.status == OMC_TRANSFER_OK);
        assert(relation_targets_equal(&out, "dimg", 1U,
                                      repeated_xmp_targets, 0U));
        assert(relation_targets_equal(&out, "dimg", 10U,
                                      repeated_xmp_targets, 3U));
        assert(memcmp(out.data + f->media, "PIXELS!!", 8U) == 0);
        omc_arena_fini(&out);
    }
}
static void
init_xmp_package(omc_transfer_package_batch *batch,
                 omc_transfer_package_chunk *chunk,
                 const omc_u8 *payload, omc_size payload_size,
                 omc_size target_size)
{
    static const omc_u8 route[] = "bmff:item-xmp";
    memset(chunk, 0, sizeof(*chunk));
    chunk->kind = OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK;
    chunk->route.data = route;
    chunk->route.size = sizeof(route) - 1U;
    chunk->bytes.data = payload;
    chunk->bytes.size = payload_size;
    omc_transfer_package_batch_init(batch);
    batch->target_format = OMC_SCAN_FMT_HEIF;
    batch->input_size = target_size;
    batch->output_size = payload_size;
    batch->chunk_count = 1U;
    batch->chunks = chunk;
}
static void
package_cases(fixture *f)
{
    static const omc_u8 payload[] = {'N', 'E', 'W', '!'};
    omc_arena direct;
    omc_arena materialized;
    omc_arena serialized;
    omc_arena temp_storage;
    omc_arena serialized_output;
    omc_arena original;
    omc_bmff_write_item direct_item;
    omc_bmff_write_res direct_res;
    omc_const_bytes profile;
    omc_transfer_package_batch batch;
    omc_transfer_package_chunk chunk;
    omc_transfer_package_io_res package_res;
    omc_size original_size;
    omc_byte_ref ref;

    make_fixture_version(f, 0U, 2U, 1U);
    original_size = f->bytes.size;
    omc_arena_init(&original);
    assert(omc_arena_append(&original, f->bytes.data, original_size, &ref) ==
           OMC_STATUS_OK);
    direct_item.family = OMC_BMFF_FAMILY_XMP;
    direct_item.payload.data = payload;
    direct_item.payload.size = sizeof(payload);
    profile.data = NULL;
    profile.size = 0U;
    omc_arena_init(&direct);
    assert(omc_bmff_rewrite(f->bytes.data, original_size, &direct_item, 1U,
                            0U, 0, profile, 1, &direct, &direct_res) ==
           OMC_STATUS_OK);
    assert(direct_res.status == OMC_TRANSFER_OK);

    init_xmp_package(&batch, &chunk, payload, sizeof(payload), original_size);
    omc_arena_init(&materialized);
    assert(omc_transfer_package_bmff_materialize(
               f->bytes.data, original_size, &batch, &materialized,
               &package_res) == OMC_STATUS_OK);
    assert(package_res.status == OMC_TRANSFER_OK);
    assert(package_res.chunk_count == 1U);
    assert(materialized.size == direct.size);
    assert(memcmp(materialized.data, direct.data, direct.size) == 0);
    assert(memcmp(f->bytes.data, original.data, original_size) == 0);

    omc_arena_init(&serialized);
    assert(omc_transfer_package_batch_serialize(&batch, &serialized,
                                               &package_res) == OMC_STATUS_OK);
    assert(package_res.status == OMC_TRANSFER_OK);
    omc_arena_init(&temp_storage);
    omc_arena_init(&serialized_output);
    assert(omc_transfer_package_bmff_bytes_materialize(
               f->bytes.data, original_size, serialized.data, serialized.size,
               &temp_storage, &serialized_output, &package_res) ==
           OMC_STATUS_OK);
    assert(package_res.status == OMC_TRANSFER_OK);
    assert(serialized_output.size == direct.size);
    assert(memcmp(serialized_output.data, direct.data, direct.size) == 0);

    omc_arena_fini(&serialized_output);
    omc_arena_fini(&temp_storage);
    omc_arena_fini(&serialized);
    omc_arena_fini(&materialized);
    omc_arena_fini(&direct);
    omc_arena_fini(&original);

    make_fixture_version(f, OMC_BMFF_FAMILY_XMP, 2U, 1U);
    original_size = f->bytes.size;
    omc_arena_init(&original);
    assert(omc_arena_append(&original, f->bytes.data, original_size, &ref) ==
           OMC_STATUS_OK);
    init_xmp_package(&batch, &chunk, payload, sizeof(payload), original_size);

    omc_arena_init(&materialized);
    assert(omc_arena_append(&materialized, "keep", 4U, &ref) == OMC_STATUS_OK);
    assert(omc_transfer_package_bmff_materialize(
               f->bytes.data, original_size, &batch, &materialized,
               &package_res) == OMC_STATUS_OK);
    assert(package_res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(package_res.bytes == 0U && package_res.chunk_count == 0U);
    assert(materialized.size == 0U);
    assert(memcmp(f->bytes.data, original.data, original_size) == 0);

    omc_arena_init(&serialized);
    assert(omc_transfer_package_batch_serialize(&batch, &serialized,
                                               &package_res) == OMC_STATUS_OK);
    assert(package_res.status == OMC_TRANSFER_OK);
    omc_arena_init(&temp_storage);
    omc_arena_init(&serialized_output);
    assert(omc_arena_append(&serialized_output, "keep", 4U, &ref) ==
           OMC_STATUS_OK);
    assert(omc_transfer_package_bmff_bytes_materialize(
               f->bytes.data, original_size, serialized.data, serialized.size,
               &temp_storage, &serialized_output, &package_res) ==
           OMC_STATUS_OK);
    assert(package_res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(package_res.bytes == 0U && package_res.chunk_count == 0U);
    assert(serialized_output.size == 0U);
    assert(memcmp(f->bytes.data, original.data, original_size) == 0);

    omc_arena_fini(&serialized_output);
    omc_arena_fini(&temp_storage);
    omc_arena_fini(&serialized);
    omc_arena_fini(&materialized);
    omc_arena_fini(&original);
}
int
main(void)
{
    fixture *f = (fixture *)malloc(sizeof(*f));
    assert(f);
    rewrite(f, 0, 0U);
    rewrite(f, 1, 0U);
    rewrite(f, 0, 1U);
    rewrite(f, 1, 1U);
    reject_cases(f);
    dimg_cases(f);
    package_cases(f);
    free(f);
    return 0;
}
