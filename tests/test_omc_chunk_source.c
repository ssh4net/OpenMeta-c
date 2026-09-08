#include "omc/omc_read_source.h"
#include "omc_test_chunk_fixture.h"
#include <stdio.h>

typedef struct chunk_work {
    omc_read_source_workspace w;
    omc_u8 metadata[2048], payload[2048];
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[32];
    omc_u32 indices[32];
} chunk_work;

static void
init_work(chunk_work *w)
{
    memset(w, 0, sizeof(*w));
    w->w.metadata = w->metadata;
    w->w.metadata_capacity = sizeof(w->metadata);
    w->w.payload = w->payload;
    w->w.payload_capacity = sizeof(w->payload);
    w->w.blocks = w->blocks;
    w->w.block_capacity = 32U;
    w->w.ifds = w->ifds;
    w->w.ifd_capacity = 32U;
    w->w.payload_indices = w->indices;
    w->w.payload_index_capacity = 32U;
}

static void
equal_ref(const omc_store *a, omc_byte_ref ar, const omc_store *b, omc_byte_ref br)
{
    assert(ar.size == br.size);
    if (ar.size)
        assert(memcmp(a->arena.data + ar.offset, b->arena.data + br.offset,
                      ar.size) == 0);
}

static void
equal_entries(const omc_store *a, const omc_store *b)
{
    omc_size i;
    const omc_entry *x, *y;
    assert(a->entry_count == b->entry_count);
    for (i = 0U; i < a->entry_count; ++i) {
        x = &a->entries[i];
        y = &b->entries[i];
        assert(x->key.kind == y->key.kind);
        switch (x->key.kind) {
        case OMC_KEY_EXIF_TAG:
            assert(x->key.u.exif_tag.tag == y->key.u.exif_tag.tag);
            equal_ref(a, x->key.u.exif_tag.ifd, b, y->key.u.exif_tag.ifd);
            break;
        case OMC_KEY_XMP_PROPERTY:
            equal_ref(a, x->key.u.xmp_property.schema_ns, b, y->key.u.xmp_property.schema_ns);
            equal_ref(a, x->key.u.xmp_property.property_path, b, y->key.u.xmp_property.property_path);
            break;
        case OMC_KEY_PNG_TEXT:
            equal_ref(a, x->key.u.png_text.keyword, b, y->key.u.png_text.keyword);
            equal_ref(a, x->key.u.png_text.field, b, y->key.u.png_text.field);
            break;
        case OMC_KEY_JUMBF_FIELD:
            equal_ref(a, x->key.u.jumbf_field.field, b, y->key.u.jumbf_field.field);
            break;
        case OMC_KEY_JUMBF_CBOR_KEY:
            equal_ref(a, x->key.u.jumbf_cbor_key.key, b, y->key.u.jumbf_cbor_key.key);
            break;
        case OMC_KEY_ICC_HEADER_FIELD:
            assert(x->key.u.icc_header_field.offset == y->key.u.icc_header_field.offset);
            break;
        default:
            assert(0);
        }
        assert(x->flags == y->flags && x->origin.block == y->origin.block);
        assert(x->origin.order_in_block == y->origin.order_in_block);
        assert(x->origin.wire_type.family == y->origin.wire_type.family &&
               x->origin.wire_type.code == y->origin.wire_type.code);
        assert(x->origin.wire_count == y->origin.wire_count);
        equal_ref(a, x->origin.wire_type_name, b, y->origin.wire_type_name);
        assert(x->value.kind == y->value.kind && x->value.elem_type == y->value.elem_type);
        assert(x->value.count == y->value.count &&
               x->value.byte_order == y->value.byte_order &&
               x->value.text_encoding == y->value.text_encoding);
        if (x->value.kind == OMC_VAL_SCALAR)
            assert(memcmp(&x->value.u, &y->value.u, sizeof(x->value.u)) == 0);
        else if (x->value.kind != OMC_VAL_EMPTY)
            equal_ref(a, x->value.u.ref, b, y->value.u.ref);
    }
}

static void
run_fixture(int webp, omc_u32 gap)
{
    omc_test_chunk_fixture f;
    chunk_work w;
    omc_store expected, actual;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_res read;
    omc_read_res baseline;
    omc_read_source_opts opts;
    omc_size i, entry_count, block_count, arena_size;
    omc_entry *entries;
    omc_u8 *arena;
    omc_u64 shift;
    omc_blk_ref block;
    int mode;
    omc_test_chunk_make(&f, webp, 1);
    init_work(&w);
    omc_store_init(&expected);
    omc_store_init(&actual);
    omc_read_source_opts_init(&opts);
    baseline = omc_read_simple(f.bytes, f.size, &expected, w.blocks, 32U,
                               w.ifds, 32U, w.payload, sizeof(w.payload),
                               w.indices, 32U, &opts.decode);
    assert(baseline.scan.status == OMC_SCAN_OK && baseline.exif.status == OMC_EXIF_OK);
    assert(baseline.jumbf.status == OMC_JUMBF_OK && baseline.jumbf.entries_decoded > 0U);
    assert(baseline.xmp.status == OMC_XMP_OK && baseline.xmp.entries_decoded > 0U);
    assert(expected.block_count >= 6U);
    assert(w.blocks[1].kind == OMC_BLK_JUMBF && w.blocks[1].part_count == 2U);
    assert(w.blocks[3].part_count == 2U && w.blocks[3].part_index == 1U);
    assert(w.blocks[4].kind == OMC_BLK_JUMBF && w.blocks[4].part_count == 0U);
#if OMC_HAVE_ZLIB
    assert(baseline.pay.status == OMC_PAY_OK);
#else
    assert(webp || baseline.pay.status == OMC_PAY_UNSUPPORTED);
#endif
    omc_test_chunk_set_gap(&f, gap);
    range.source = omc_source_callback(f.base + f.size + f.gap, &f,
                                       omc_test_chunk_read, 0);
    range.source_offset = f.base;
    range.size = f.size + f.gap;
    omc_source_state_init(&state);
    read = omc_read_source(&range, &actual, &w.w, &state, &opts);
    assert(read.status == OMC_READ_SOURCE_OK && read.decoded.scan.status == OMC_SCAN_OK);
    assert(read.decoded.pay.status == baseline.pay.status);
    assert(read.decoded.jumbf.status == OMC_JUMBF_OK);
    equal_entries(&expected, &actual);
    assert(expected.block_count == actual.block_count);
    for (i = 0U; i < actual.block_count; ++i) {
        block = expected.blocks[i];
        shift = block.outer_offset >= f.image_begin + 64U ? f.gap : 0U;
        block.outer_offset += shift;
        block.data_offset += shift;
        assert(memcmp(&block, &actual.blocks[i], sizeof(block)) == 0);
        assert(memcmp(&block, &w.blocks[i], sizeof(block)) == 0);
    }
    assert(read.scratch_used < sizeof(w.metadata));
    assert(state.bytes_requested < f.size + 64U && state.requests_issued < 32U);
    printf("%s: gap=%lu calls=%lu bytes=%lu scratch=%lu\n",
           webp ? "WebP" : "PNG", (unsigned long)gap,
           (unsigned long)state.requests_issued, (unsigned long)state.bytes_requested,
           (unsigned long)read.scratch_used);

    /* Exact aggregate capacity is sufficient. */
    w.w.metadata_capacity = read.scratch_used;
    omc_store_fini(&actual);
    omc_store_init(&actual);
    omc_source_state_init(&state);
    read = omc_read_source(&range, &actual, &w.w, &state, &opts);
    assert(read.status == OMC_READ_SOURCE_OK);
    equal_entries(&expected, &actual);

    /* Collection failures must retain a previously populated store. */
    entry_count = actual.entry_count;
    block_count = actual.block_count;
    arena_size = actual.arena.size;
    entries = actual.entries;
    arena = actual.arena.data;
    for (mode = 0; mode < 7; ++mode) {
        omc_read_source_opts_init(&opts);
        omc_source_state_init(&state);
        w.w.metadata_capacity = mode == 0 ? read.scratch_used - 1U : sizeof(w.metadata);
        f.fail_mode = mode >= 1 && mode <= 3 ? mode : 0;
        f.failure_at = f.base + f.image_begin + 64U + f.gap;
        if (mode == 4)
            opts.io.max_requests = 3U;
        if (mode == 5)
            opts.io.max_total_bytes = 24U;
        if (mode == 6)
            opts.io.max_single_read_bytes = 20U;
        {
            omc_read_source_res failed = omc_read_source(&range, &actual, &w.w, &state, &opts);
            assert(failed.status == (mode == 0 ? OMC_READ_SOURCE_LIMIT : OMC_READ_SOURCE_IO));
        }
        if (mode == 1) assert(state.code == OMC_SOURCE_SHORT_READ);
        if (mode == 2) assert(state.code == OMC_SOURCE_CANCELLED);
        if (mode == 3) assert(state.code == OMC_SOURCE_CHANGED);
        if (mode == 4) assert(state.code == OMC_SOURCE_REQUEST_LIMIT);
        if (mode == 5) assert(state.code == OMC_SOURCE_BYTE_LIMIT);
        if (mode == 6) assert(state.code == OMC_SOURCE_REQUEST_TOO_LARGE);
        assert(actual.entry_count == entry_count && actual.block_count == block_count);
        assert(actual.arena.size == arena_size && actual.entries == entries && actual.arena.data == arena);
        equal_entries(&expected, &actual);
    }
    omc_store_fini(&actual);
    omc_store_fini(&expected);
}

static void
header_only(int webp)
{
    omc_test_chunk_fixture f;
    chunk_work w;
    omc_store store;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_res read;
    omc_size header_size;
    int mode;
    omc_test_chunk_make(&f, webp, 1);
    header_size = webp ? 12U : 8U;
    f.size = header_size;
    if (webp)
        omc_test_chunk_put32(f.bytes + 4U, 4U, 1);
    init_work(&w);
    omc_store_init(&store);
    range.source = omc_source_callback(f.base + f.size, &f, omc_test_chunk_read, 0);
    range.source_offset = f.base;
    range.size = f.size;
    for (mode = 0; mode < 3; ++mode) {
        w.w.metadata_capacity = mode == 0 ? header_size :
                                mode == 1 ? header_size - 1U : 0U;
        omc_source_state_init(&state);
        read = omc_read_source(&range, &store, &w.w, &state, NULL);
        assert(read.status == (mode == 0 ? OMC_READ_SOURCE_OK : OMC_READ_SOURCE_LIMIT));
        assert(store.entry_count == 0U && store.block_count == 0U);
        if (mode == 0) assert(read.scratch_used == header_size);
    }
    omc_store_fini(&store);
}

static void
malformed(int webp)
{
    omc_test_chunk_fixture f;
    chunk_work w;
    omc_store store;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_res read;
    omc_size cut;
    omc_test_chunk_make(&f, webp, 1);
    init_work(&w);
    omc_store_init(&store);
    range.source = omc_source_callback(f.base + f.size, &f, omc_test_chunk_read, 0);
    range.source_offset = f.base;
    range.size = f.size;
    /* First metadata chunk: one missing CRC byte (PNG) or odd pad (WebP). */
    cut = webp ? 12U + 40U + 8U + 29U : 8U + 12U + 26U - 1U;
    range.size = cut;
    omc_source_state_init(&state);
    read = omc_read_source(&range, &store, &w.w, &state, NULL);
    assert(read.status == OMC_READ_SOURCE_MALFORMED && store.entry_count == 0U);
    range.size = f.size;
    omc_test_chunk_put32(f.bytes + f.image_begin - (webp ? 4U : 8U), 0xffffffffU, webp);
    omc_source_state_init(&state);
    read = omc_read_source(&range, &store, &w.w, &state, NULL);
    assert(read.status == OMC_READ_SOURCE_MALFORMED && store.entry_count == 0U);
    f.bytes[2] = 'x';
    omc_source_state_init(&state);
    read = omc_read_source(&range, &store, &w.w, &state, NULL);
    assert(read.status == OMC_READ_SOURCE_UNSUPPORTED && store.entry_count == 0U);
    omc_store_fini(&store);
}

int
main(void)
{
    int webp;
    for (webp = 0; webp < 2; ++webp) {
        run_fixture(webp, 0U);
        run_fixture(webp, 0xc0000000U);
        malformed(webp);
        header_only(webp);
    }
    return 0;
}
