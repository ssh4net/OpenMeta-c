#include "omc/omc_read_source.h"
#include "omc_test_assert.h"
#include "read_decode_makernote_fixtures.h"
#include "read_decode_vendor_fixtures.h"
#include <stdio.h>
#include <string.h>

typedef struct host {
    omc_u8 bytes[1024];
    omc_size available;
    omc_u64 base, gap;
    omc_size split;
    omc_size forbidden_begin, forbidden_end;
    int short_read;
} host;
typedef struct work {
    omc_read_source_workspace w;
    omc_u8 metadata[2048], payload[2048];
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[32];
    omc_u32 indices[32];
} work;
static omc_source_io_res
read_at(void *context, omc_u64 offset, omc_u8 *destination, omc_size size)
{
    host *h = (host *)context;
    omc_source_io_res r;
    omc_size p;
    omc_u64 relative;
    assert(offset >= h->base);
    relative = offset - h->base;
    if (h->gap && relative >= h->split) {
        assert(relative >= h->split + h->gap);
        relative -= h->gap;
    } else if (h->gap) assert(size <= h->split - relative);
    assert(relative <= h->available);
    p = (omc_size)relative;
    assert(size <= h->available - p);
    assert(!size || p >= h->forbidden_end || p + size <= h->forbidden_begin);
    r.code = OMC_SOURCE_IO_OK;
    r.bytes_read = h->short_read && size ? size - 1U : size;
    memcpy(destination, h->bytes + p, (omc_size)r.bytes_read);
    return r;
}
static void
init_work(work *w)
{
    memset(w, 0, sizeof(*w));
    w->w.metadata = w->metadata;
    w->w.metadata_capacity = sizeof(w->metadata);
    w->w.blocks = w->blocks;
    w->w.block_capacity = 32U;
    w->w.ifds = w->ifds;
    w->w.ifd_capacity = 32U;
    w->w.payload = w->payload;
    w->w.payload_capacity = sizeof(w->payload);
    w->w.payload_indices = w->indices;
    w->w.payload_index_capacity = 32U;
}
static void
put(omc_u8 *p, omc_u64 n, unsigned width, int little)
{
    unsigned i;
    for (i = 0U; i < width; ++i) {
        p[little ? i : width - 1U - i] = (omc_u8)n;
        n >>= 8U;
    }
}
static void
entry(omc_u8 *p, int big, int little, unsigned tag, unsigned type, omc_u64 count,
      omc_u64 value)
{
    put(p, tag, 2U, little);
    put(p + 2U, type, 2U, little);
    put(p + 4U, count, big ? 8U : 4U, little);
    put(p + (big ? 12U : 8U), value, big ? 8U : 4U, little);
}
static void
make_tiff(host *h, int big, int little)
{
    omc_size first = big ? 16U : 8U;
    unsigned ew = big ? 20U : 12U;
    omc_u8 *p;
    memset(h, 0, sizeof(*h));
    h->available = 192U;
    h->base = 1000U;
    h->bytes[0] = little ? 'I' : 'M';
    h->bytes[1] = h->bytes[0];
    put(h->bytes + 2U, big ? 43U : 42U, 2U, little);
    if (big) {
        put(h->bytes + 4U, 8U, 2U, little);
        put(h->bytes + 8U, first, 8U, little);
    } else
        put(h->bytes + 4U, first, 4U, little);
    put(h->bytes + first, 6U, big ? 8U : 2U, little);
    p = h->bytes + first + (big ? 8U : 2U);
    entry(p, big, little, 0x0100U, 4U, 1U,
          big && !little ? (omc_u64)640U << 32U : 640U);
    entry(p + ew, big, little, 0x0101U, 4U, 1U,
          big && !little ? (omc_u64)480U << 32U : 480U);
    entry(p + 2U * ew, big, little, 0x0111U, big ? 16U : 4U, 1U,
          big ? (omc_u64)1U << 32U : 4096U);
    entry(p + 3U * ew, big, little, 0x0117U, 4U, 1U,
          big && !little ? (omc_u64)1000U << 32U : 1000U);
    entry(p + 4U * ew, big, little, 0x010FU, 2U, 12U, 168U);
    entry(p + 5U * ew, big, little, 0xC612U, 1U, 4U,
          big ? (little ? 0x00000401U : (omc_u64)0x01040000U << 32U)
              : (little ? 0x00000401U : 0x01040000U));
    memcpy(h->bytes + 168U, "OpenMeta-C!", 12U);
}
static omc_size
make_nikon_tiff(omc_u8 *p)
{
    memset(p, 0, 136U);
    p[0] = 'I';
    p[1] = 'I';
    put(p + 2U, 42U, 2U, 1);
    put(p + 4U, 8U, 4U, 1);
    put(p + 8U, 2U, 2U, 1);
    entry(p + 10U, 0, 1, 0x010FU, 2U, 6U, 80U);
    entry(p + 22U, 0, 1, 0x8769U, 4U, 1U, 50U);
    put(p + 50U, 1U, 2U, 1);
    entry(p + 52U, 0, 1, 0x927CU, 7U, 36U, 100U);
    memcpy(p + 80U, "NIKON", 6U);
    memcpy(p + 100U, "Nikon", 6U);
    p[106U] = 2U;
    p[110U] = 'I';
    p[111U] = 'I';
    put(p + 112U, 42U, 2U, 1);
    put(p + 114U, 8U, 4U, 1);
    put(p + 118U, 1U, 2U, 1);
    entry(p + 120U, 0, 1, 2U, 3U, 2U, (omc_u64)200U << 16U);
    return 136U;
}
static void
make_jpeg(host *h)
{
    omc_size n;
    memset(h, 0, sizeof(*h));
    h->base = 1000U;
    h->bytes[0] = 0xFFU;
    h->bytes[1] = 0xD8U;
    h->bytes[2] = 0xFFU;
    h->bytes[3] = 0xDBU;
    put(h->bytes + 4U, 34U, 2U, 0);
    h->forbidden_begin = 6U;
    h->forbidden_end = 38U;
    h->bytes[38U] = 0xFFU;
    h->bytes[39U] = 0xE1U;
    memcpy(h->bytes + 42U, "Exif\0", 6U);
    n = make_nikon_tiff(h->bytes + 48U);
    put(h->bytes + 40U, n + 8U, 2U, 0);
    h->bytes[48U + n] = 0xFFU;
    h->bytes[49U + n] = 0xDAU;
    h->available = 50U + n;
}
static void
equal_ref(const omc_store *a, omc_byte_ref ar, const omc_store *b, omc_byte_ref br)
{
    assert(ar.size == br.size);
    assert(memcmp(a->arena.data + ar.offset, b->arena.data + br.offset, ar.size) == 0);
}
static void
equal_store(const omc_store *a, const omc_store *b)
{
    omc_size i;
    const omc_entry *x, *y;
    assert(a->entry_count == b->entry_count);
    for (i = 0U; i < a->entry_count; ++i) {
        x = &a->entries[i];
        y = &b->entries[i];
        assert(x->key.kind == OMC_KEY_EXIF_TAG && y->key.kind == OMC_KEY_EXIF_TAG);
        assert(x->key.u.exif_tag.tag == y->key.u.exif_tag.tag);
        equal_ref(a, x->key.u.exif_tag.ifd, b, y->key.u.exif_tag.ifd);
        assert(x->value.kind == y->value.kind &&
               x->value.elem_type == y->value.elem_type);
        assert(x->value.count == y->value.count &&
               x->value.byte_order == y->value.byte_order);
        if (x->value.kind == OMC_VAL_ARRAY || x->value.kind == OMC_VAL_TEXT ||
            x->value.kind == OMC_VAL_BYTES)
            equal_ref(a, x->value.u.ref, b, y->value.u.ref);
        else
            assert(memcmp(&x->value.u, &y->value.u, sizeof(x->value.u)) == 0);
    }
}
static void
compare(host *h, int jpeg)
{
    work w;
    omc_store expected, actual;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_opts opts;
    omc_read_source_res res;
    omc_read_res baseline;
    omc_u64 full_size = (omc_u64)5U << 30U;
    init_work(&w);
    omc_store_init(&expected);
    omc_store_init(&actual);
    omc_read_source_opts_init(&opts);
    opts.decode.exif.decode_makernote = jpeg;
    baseline =
        omc_read_simple(h->bytes, h->available, &expected, w.blocks, 32U, w.ifds, 32U,
                        w.payload, sizeof(w.payload), w.indices, 32U, &opts.decode);
    assert(baseline.scan.status == OMC_SCAN_OK && baseline.exif.status == OMC_EXIF_OK);
    range.source = omc_source_callback(h->base + full_size, h, read_at, 0);
    range.source_offset = h->base;
    range.size = full_size;
    omc_source_state_init(&state);
    res = omc_read_source(&range, &actual, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_OK && res.decoded.scan.status == OMC_SCAN_OK &&
           res.decoded.exif.status == OMC_EXIF_OK);
    equal_store(&expected, &actual);
    assert(actual.block_count == 1U && res.decoded.scan.written == 1U);
    assert(w.blocks[0].data_offset == (jpeg ? 48U : 0U));
    assert(actual.blocks[0].data_offset == w.blocks[0].data_offset);
    assert(w.blocks[0].data_size == (jpeg ? 136U : full_size));
    assert(state.bytes_requested < 512U && res.scratch_used < 256U);
    if (jpeg)
        assert(actual.entry_count > 3U);
    printf("%s: calls=%u bytes=%lu scratch=%lu source=5GiB\n", jpeg ? "JPEG" : "TIFF",
           (unsigned)state.requests_issued, (unsigned long)state.bytes_requested,
           (unsigned long)res.scratch_used);
    omc_store_fini(&actual);
    omc_store_init(&actual);
    range.source = omc_source_memory(h->bytes, h->available);
    range.source_offset = 0U;
    range.size = h->available;
    omc_source_state_init(&state);
    res = omc_read_source(&range, &actual, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_OK && state.requests_issued == 0U &&
           res.scratch_used == 0U);
    equal_store(&expected, &actual);
    omc_store_fini(&actual);
    omc_store_fini(&expected);
}
static void
sparse_tiff(host *h, int big, int little, unsigned magic)
{
    work w;
    omc_store expected, actual;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_opts opts;
    omc_read_source_res r;
    omc_exif_res e;
    omc_size first = big ? 16U : 8U;
    omc_size make_field = first + (big ? 8U : 2U) + 4U * (big ? 20U : 12U) + (big ? 12U : 8U);
    make_tiff(h, big, little);
    put(h->bytes + 2U, magic, 2U, little);
    init_work(&w);
    omc_store_init(&expected);
    omc_store_init(&actual);
    omc_read_source_opts_init(&opts);
    e = omc_exif_dec(h->bytes, h->available, &expected, OMC_INVALID_BLOCK_ID,
                      w.ifds, 32U, &opts.decode.exif);
    assert(e.status == OMC_EXIF_OK);
    h->split = first;
    h->gap = (omc_u64)(big ? 8U : 2U) << 30U;
    put(h->bytes + (big ? 8U : 4U), first + h->gap, big ? 8U : 4U, little);
    put(h->bytes + make_field, 168U + h->gap, big ? 8U : 4U, little);
    range.source = omc_source_callback(h->base + h->gap + h->available, h, read_at, 0);
    range.source_offset = h->base;
    range.size = h->gap + h->available;
    w.w.metadata_capacity = 12U;
    omc_source_state_init(&state);
    assert(omc_scan_source(&range, OMC_SCAN_FMT_UNKNOWN, w.blocks, 32U, &state, NULL).status == OMC_SCAN_OK);
    omc_source_state_init(&state);
    r = omc_read_source(&range, &actual, &w.w, &state, &opts);
    assert(r.status == OMC_READ_SOURCE_OK && r.decoded.exif.status == OMC_EXIF_OK);
    assert(r.value_scratch_needed == 0U && r.scratch_used == 12U);
    assert(w.ifds[0].offset == first + h->gap && state.bytes_requested < 256U);
    equal_store(&expected, &actual);
    omc_store_fini(&expected);
    omc_store_fini(&actual);
}

static void
failures(host *h)
{
    work w;
    omc_store store;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_opts opts;
    omc_read_source_res res;
    init_work(&w);
    omc_store_init(&store);
    omc_read_source_opts_init(&opts);
    make_tiff(h, 0, 1);
    range.source = omc_source_callback(h->base + 4096U, h, read_at, 0);
    range.source_offset = h->base;
    range.size = 4096U;
    w.w.metadata_capacity = 4U;
    omc_source_state_init(&state);
    res = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_LIMIT && store.entry_count == 0U);
    w.w.metadata_capacity = sizeof(w.metadata);
    h->short_read = 1;
    omc_source_state_init(&state);
    res = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_IO && state.code == OMC_SOURCE_SHORT_READ &&
           store.entry_count == 0U);
    h->short_read = 0;
    /* A cyclic next-IFD link is visited once. Values over the byte limit
     * retain their key and truncation flag without requesting their bytes. */
    put(h->bytes + 82U, 8U, 4U, 1);
    h->forbidden_begin = 168U;
    h->forbidden_end = 180U;
    opts.decode.exif.limits.max_value_bytes = 8U;
    omc_source_state_init(&state);
    res = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_OK && res.decoded.exif.ifds_needed == 1U);
    assert(store.entry_count == 6U && store.entries[4].value.kind == OMC_VAL_EMPTY &&
           (store.entries[4].flags & OMC_ENTRY_FLAG_TRUNCATED));
    omc_store_reset(&store);
    make_tiff(h, 0, 1);
    omc_read_source_opts_init(&opts);
    omc_source_state_init(&state);
    opts.io.max_requests = 1U;
    res = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_IO && state.code == OMC_SOURCE_REQUEST_LIMIT &&
           store.entry_count == 0U);
    omc_read_source_opts_init(&opts);
    omc_source_state_init(&state);
    h->available = make_nikon_tiff(h->bytes);
    opts.decode.exif.decode_makernote = 1;
    res = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_OK && res.nested_payloads_skipped == 0U &&
           store.entry_count > 3U);
    omc_store_reset(&store);
    opts.decode.exif.decode_makernote = 0;
    omc_source_state_init(&state);
    res = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(res.status == OMC_READ_SOURCE_OK && store.entry_count == 3U);
    omc_store_fini(&store);
    omc_store_init(&store);
    make_jpeg(h);
    range.size = h->available - 2U;
    omc_source_state_init(&state);
    res = omc_read_source(&range, &store, &w.w, &state, &opts);
    /* Match the existing metadata scanner: EOF before SOS/EOI is accepted. */
    assert(res.status == OMC_READ_SOURCE_OK && res.decoded.scan.status == OMC_SCAN_OK);
    omc_store_fini(&store);
}
static void check_xmp_sidecar(void)
{
    static const char xml[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
        "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description "
        "xmlns:v='urn:v'><v:A>A&amp;B</v:A></rdf:Description></rdf:RDF></x:xmpmeta>";
    host h;
    work w;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_opts opts;
    omc_read_source_res r;
    omc_store store;
    omc_size old_entries;
    omc_size old_arena;
    memset(&h, 0, sizeof(h));
    memcpy(h.bytes, xml, sizeof(xml) - 1U);
    h.available = sizeof(xml) - 1U;
    h.base = 37U;
    range.source = omc_source_callback(h.base + h.available, &h, read_at, 0U);
    range.source_offset = h.base;
    range.size = h.available;
    init_work(&w);
    memset(&state, 0, sizeof(state));
    omc_read_source_opts_init(&opts);
    omc_store_init(&store);
    r = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(r.status == OMC_READ_SOURCE_OK && r.decoded.xmp.status == OMC_XMP_OK);
    assert(store.entry_count == 1U && r.scratch_used == h.available);
    assert(store.entries[0].value.count == 3U &&
           store.entries[0].origin.wire_count == 3U);
    assert(store.blocks[0].data_offset == 0U &&
           store.blocks[0].data_size == h.available);
    old_entries = store.entry_count;
    old_arena = store.arena.size;
    w.w.metadata_capacity = h.available - 1U;
    memset(&state, 0, sizeof(state));
    r = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(r.status == OMC_READ_SOURCE_LIMIT && r.value_scratch_needed == h.available);
    assert(store.entry_count == old_entries && store.arena.size == old_arena);
    w.w.metadata_capacity = sizeof(w.metadata);
    opts.decode.xmp.limits.max_input_bytes = 32U;
    memset(&state, 0, sizeof(state));
    r = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(r.status == OMC_READ_SOURCE_LIMIT && store.entry_count == old_entries &&
           store.arena.size == old_arena);
    omc_read_source_opts_init(&opts);
    opts.decode.xmp.limits.max_value_bytes = 2U;
    memset(&state, 0, sizeof(state));
    r = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(r.status == OMC_READ_SOURCE_LIMIT && store.entry_count == old_entries &&
           store.arena.size == old_arena);
    omc_read_source_opts_init(&opts);
    h.short_read = 1;
    memset(&state, 0, sizeof(state));
    r = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(r.status == OMC_READ_SOURCE_IO && store.entry_count == old_entries &&
           store.arena.size == old_arena);
    omc_store_fini(&store);
}

static void sparse_phaseone(host *h)
{
    work w;
    omc_store store;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_opts opts;
    omc_read_source_res result;
    omc_size i;
    int found;
    memset(h, 0, sizeof(*h));
    h->base = 37U;
    h->available = 192U;
    h->split = 96U;
    h->gap = (omc_u64)2U << 30U;
    h->bytes[0] = 'I';
    h->bytes[1] = 'I';
    put(h->bytes + 2U, 42U, 2U, 1);
    put(h->bytes + 4U, 8U, 4U, 1);
    put(h->bytes + 8U, 1U, 2U, 1);
    entry(h->bytes + 10U, 0, 1, 0x927CU, 7U, h->gap + 128U, 64U);
    (void)omc_rd5_makernote_fixture(h->bytes + 64U, 0U);
    put(h->bytes + 72U, h->gap + 32U, 4U, 1);
    put(h->bytes + 64U + 0x54U, h->gap + 0x60U, 4U, 1);
    range.source = omc_source_callback(h->base + h->gap + h->available, h, read_at, 0);
    range.source_offset = h->base;
    range.size = h->gap + h->available;
    init_work(&w);
    w.w.metadata_capacity = 64U;
    omc_store_init(&store);
    omc_source_state_init(&state);
    omc_read_source_opts_init(&opts);
    opts.decode.exif.decode_makernote = 1;
    result = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(result.status == OMC_READ_SOURCE_OK);
    assert(result.decoded.exif.status == OMC_EXIF_OK);
    assert(result.value_scratch_needed == 0U && state.bytes_requested < 512U);
    found = 0;
    for (i = 0U; i < store.entry_count; ++i)
        if (store.entries[i].key.kind == OMC_KEY_EXIF_TAG &&
            store.entries[i].key.u.exif_tag.tag == 0x400U) {
            assert(store.entries[i].value.kind == OMC_VAL_SCALAR &&
                   store.entries[i].value.u.u64 == 7U);
            found = 1;
        }
    assert(found);
    printf("RD6 PhaseOne 2GiB gap: requests=%u bytes=%lu scratch=%lu\n",
           (unsigned)state.requests_issued, (unsigned long)state.bytes_requested,
           (unsigned long)result.scratch_used);
    omc_store_fini(&store);
}

static void check_cmt3(void)
{
    host h;
    work w;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_opts opts;
    omc_read_source_res result;
    omc_store store;
    omc_size i, saved_count;
    int found;
    memset(&h, 0, sizeof(h));
    h.available = omc_rd6_vendor_fixture(h.bytes, 4U);
    h.base = 37U;
    range.source = omc_source_callback(h.base + h.available, &h, read_at, 0U);
    range.source_offset = h.base;
    range.size = h.available;
    init_work(&w);
    omc_source_state_init(&state);
    omc_read_source_opts_init(&opts);
    opts.decode.exif.decode_makernote = 1;
    omc_store_init(&store);
    result = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(result.status == OMC_READ_SOURCE_OK);
    found = 0;
    for (i = 0U; i < store.entry_count; ++i) {
        const omc_entry *e = &store.entries[i];
        if (e->key.kind == OMC_KEY_EXIF_TAG && e->key.u.exif_tag.tag == 0x10U) {
            assert(e->value.kind == OMC_VAL_SCALAR && e->value.u.u64 == 0x80000331U);
            found = 1;
        }
    }
    assert(found);
    saved_count = store.entry_count;
    h.short_read = 1;
    omc_source_state_init(&state);
    result = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(result.status == OMC_READ_SOURCE_IO && store.entry_count == saved_count);
    h.short_read = 0;
    omc_store_reset(&store);
    opts.decode.exif.decode_makernote = 0;
    omc_source_state_init(&state);
    result = omc_read_source(&range, &store, &w.w, &state, &opts);
    assert(result.status == OMC_READ_SOURCE_OK);
    for (i = 0U; i < store.entry_count; ++i)
        assert(store.entries[i].key.kind != OMC_KEY_EXIF_TAG);
    omc_store_fini(&store);
}

int
main(void)
{
    host h;
    int big, little;
    check_cmt3();
    sparse_phaseone(&h);
    check_xmp_sidecar();
    for (big = 0; big < 2; ++big)
        for (little = 0; little < 2; ++little) {
            make_tiff(&h, big, little);
            compare(&h, 0);
            sparse_tiff(&h, big, little, big ? 43U : 42U);
            if (!big) {
                sparse_tiff(&h, 0, little, 0x55U);
                sparse_tiff(&h, 0, little, 0x4F52U);
            }
        }
    make_jpeg(&h);
    compare(&h, 1);
    failures(&h);
    return 0;
}
