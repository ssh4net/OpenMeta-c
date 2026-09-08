#include "omc/omc_read_source.h"
#include "omc_test_assert.h"
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    omc_u8 bytes[2048];
    omc_size size, split, forbidden_begin, forbidden_end, fail_at;
    omc_u64 gap;
    int fail;
} fixture;
static void put(omc_u8* p, omc_u64 value, unsigned width, int little)
{
    unsigned i;
    for (i = 0U; i < width; ++i) { p[little ? i : width - i - 1U] = (omc_u8)value; value >>= 8U; }
}
static omc_source_io_res read_at(void* user, omc_u64 offset, omc_u8* out, omc_size size)
{
    fixture* f = (fixture*)user;
    omc_source_io_res result;
    assert(offset >= 37U); offset -= 37U;
    if (offset >= f->split + f->gap) offset -= f->gap;
    else assert(size <= f->split - offset);
    assert(offset <= f->size && size <= f->size - offset);
    assert(!size || offset >= f->forbidden_end || offset + size <= f->forbidden_begin);
    result.code = OMC_SOURCE_IO_OK;
    result.bytes_read = size;
    if (f->fail && offset >= f->fail_at && size) --result.bytes_read;
    if (result.bytes_read) memcpy(out, f->bytes + (omc_size)offset, (omc_size)result.bytes_read);
    return result;
}
static omc_size tiff(omc_u8* p)
{
    memset(p, 0, 26U); memcpy(p, "II", 2U); put(p + 2U, 42U, 2U, 1);
    put(p + 4U, 8U, 4U, 1); put(p + 8U, 1U, 2U, 1);
    put(p + 10U, 0x112U, 2U, 1); put(p + 12U, 3U, 2U, 1);
    put(p + 14U, 1U, 4U, 1); put(p + 18U, 6U, 2U, 1); return 26U;
}
static omc_size jpeg(omc_u8* p)
{
    put(p, 0xffd8U, 2U, 0); put(p + 2U, 0xffe1U, 2U, 0);
    put(p + 4U, 34U, 2U, 0); memcpy(p + 6U, "Exif\0\0", 6U);
    (void)tiff(p + 12U); put(p + 38U, 0xffdaU, 2U, 0);
    memset(p + 40U, 0xa5, 64U); return 104U;
}
static void make(fixture* f, unsigned format, omc_u64 gap)
{
    omc_u8* p;
    omc_size n, i;
    memset(f, 0, sizeof(*f)); f->gap = gap;
    if (format == 0U) {
        /* One string attribute followed by an unread image region. */
        put(f->bytes, 20000630U, 4U, 1); put(f->bytes + 4U, 2U, 4U, 1);
        memcpy(f->bytes + 8U, "label\0string\0", 13U);
        put(f->bytes + 21U, 20U, 4U, 1); memset(f->bytes + 25U, 'x', 20U);
        f->bytes[45U] = 0U; f->split = 46U; f->size = 110U;
        f->forbidden_begin = 46U; f->forbidden_end = 110U; f->fail_at = 25U;
    } else if (format == 1U) {
        /* Root values precede a sparse gap; directory and footer follow it. */
        memcpy(f->bytes, "II", 2U); put(f->bytes + 2U, 14U, 4U, 1);
        memcpy(f->bytes + 6U, "HEAPCCDR", 8U);
        memset(f->bytes + 14U, 'c', 20U); f->bytes[33U] = 0U;
        f->split = 54U; f->size = 70U;
        put(f->bytes + 54U, 1U, 2U, 1); put(f->bytes + 56U, 0x0805U, 2U, 1);
        put(f->bytes + 58U, 20U, 4U, 1); put(f->bytes + 62U, 0U, 4U, 1);
        put(f->bytes + 66U, 40U + gap, 4U, 1); f->fail_at = 54U;
    } else if (format == 2U) {
        memcpy(f->bytes, "FUJIFILMCCD-RAW ", 16U); memcpy(f->bytes + 0x3cU, "0200", 4U);
        f->split = 164U;
        put(f->bytes + 0x5cU, 164U + gap, 4U, 0); put(f->bytes + 0x60U, 28U, 4U, 0);
        put(f->bytes + 164U, 1U, 4U, 0); put(f->bytes + 168U, 0xc000U, 2U, 0);
        put(f->bytes + 170U, 20U, 2U, 0); memset(f->bytes + 172U, 0x42, 20U);
        put(f->bytes + 0x64U, 192U + gap, 4U, 0); put(f->bytes + 0x68U, tiff(f->bytes + 192U), 4U, 0);
        put(f->bytes + 0x54U, 218U + gap, 4U, 0); put(f->bytes + 0x58U, jpeg(f->bytes + 218U), 4U, 0);
        f->size = 322U; f->forbidden_begin = 258U; f->forbidden_end = 322U; f->fail_at = 164U;
    } else if (format == 3U) {
        memcpy(f->bytes, "FOVb", 4U); put(f->bytes + 4U, 0x20003U, 4U, 1);
        put(f->bytes + 28U, 2640U, 4U, 1); put(f->bytes + 32U, 1760U, 4U, 1);
        f->split = 264U; p = f->bytes + 264U;
        memcpy(p, "SECp", 4U); put(p + 8U, 1U, 4U, 1); put(p + 28U, 9U, 4U, 1);
        for (i = 0U; i < 8U; ++i) p[32U + i * 2U] = (omc_u8)"CAMMODEL"[i];
        for (i = 0U; i < 9U; ++i) p[50U + i * 2U] = (omc_u8)"SIGMA DP2"[i];
        n = 334U; memcpy(f->bytes + n, "SECi", 4U); (void)jpeg(f->bytes + n + 28U);
        f->forbidden_begin = n + 68U; f->forbidden_end = n + 132U;
        n += 132U; p = f->bytes + n; memcpy(p, "SECd", 4U); put(p + 8U, 2U, 4U, 1);
        put(p + 12U, 264U + gap, 4U, 1); put(p + 16U, 70U, 4U, 1); memcpy(p + 20U, "PROP", 4U);
        put(p + 24U, 334U + gap, 4U, 1); put(p + 28U, 132U, 4U, 1); memcpy(p + 32U, "IMA2", 4U);
        put(p + 36U, n + gap, 4U, 1); f->size = n + 40U; f->fail_at = n;
    } else {
        memcpy(f->bytes, "GIF89a", 6U); f->bytes[6U] = 1U; f->bytes[8U] = 1U;
        f->bytes[13U] = 0x2cU; f->bytes[23U] = 2U; f->bytes[24U] = 200U;
        memset(f->bytes + 25U, 0xab, 200U); f->bytes[225U] = 0U;
        memcpy(f->bytes + 226U, "\x21\xfe\x04test\0\x3b", 9U);
        f->size = 235U; f->split = f->size; f->gap = 0U;
        f->forbidden_begin = 25U; f->forbidden_end = 225U; f->fail_at = 226U;
    }
}
static void check(unsigned format, omc_u64 gap)
{
    fixture f;
    omc_read_source_workspace w;
    omc_read_source_opts opts;
    omc_source_range range;
    omc_source_state state;
    omc_read_source_res result;
    omc_store store;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[16];
    omc_u32 indices[16];
    omc_u8 metadata[64], payload[256];
    omc_entry* entries;
    omc_u8* arena;
    omc_size entry_count, arena_size;
    unsigned mode;
    make(&f, format, gap);
    range.source = omc_source_callback(f.size + f.gap + 37U, &f, read_at, 0);
    range.source_offset = 37U; range.size = f.size + f.gap;
    memset(&w, 0, sizeof(w)); w.metadata = metadata; w.metadata_capacity = sizeof(metadata);
    w.payload = payload; w.payload_capacity = sizeof(payload); w.blocks = blocks; w.block_capacity = 16U;
    w.ifds = ifds; w.ifd_capacity = 16U; w.payload_indices = indices; w.payload_index_capacity = 16U;
    omc_read_source_opts_init(&opts); omc_source_state_init(&state); omc_store_init(&store);
    result = omc_read_source(&range, &store, &w, &state, &opts);
    assert(result.status == OMC_READ_SOURCE_OK && store.entry_count > 0U);
    assert(state.code == OMC_SOURCE_OK && result.nested_payloads_skipped == 0U);
    assert(result.undeclared_searches_skipped == (format == 2U || format == 3U ? 1U : 0U));
    assert(result.scratch_used <= sizeof(metadata));
    if (format < 3U) assert(result.scratch_used == 20U);
    printf("RD4 format=%u gap=%llu calls=%llu bytes=%llu scratch=%lu entries=%lu\n", format,
        (unsigned long long)f.gap, (unsigned long long)state.requests_issued,
        (unsigned long long)state.bytes_requested, (unsigned long)result.scratch_used, (unsigned long)store.entry_count);
    if (format == 2U) {
        omc_size i;
        unsigned derived = 0U;
        for (i = 0U; i < store.entry_count; ++i) {
            if (store.entries[i].flags & OMC_ENTRY_FLAG_DERIVED) {
                omc_const_bytes name = omc_arena_view(&store.arena, store.entries[i].key.u.exif_tag.ifd);
                assert(name.size == 17U && memcmp(name.data, "mk_fuji_rafdata_0", 17U) == 0);
                ++derived;
            }
        }
        assert(derived == 4U);
    }
    entries = store.entries; arena = store.arena.data; entry_count = store.entry_count; arena_size = store.arena.size;
    for (mode = 0U; mode < 3U; ++mode) {
        if (mode == 0U && format >= 3U) continue; /* Fixed-size X3F fields and GIF comment use no value buffer. */
        omc_read_source_opts_init(&opts); omc_source_state_init(&state);
        w.metadata_capacity = mode == 0U ? 19U : sizeof(metadata);
        f.fail = mode == 1U;
        if (mode == 2U) opts.io.max_requests = 1U;
        result = omc_read_source(&range, &store, &w, &state, &opts);
        assert(result.status == (mode == 0U ? OMC_READ_SOURCE_LIMIT : OMC_READ_SOURCE_IO));
        if (mode == 0U) assert(result.value_scratch_needed == 20U);
        assert(store.entries == entries && store.arena.data == arena);
        assert(store.entry_count == entry_count && store.arena.size == arena_size);
    }
    f.fail = 0; w.metadata_capacity = sizeof(metadata);
    if (format == 2U || format == 3U) {
        omc_read_source_opts_init(&opts); opts.decode.exif.limits.max_total_entries = 1U;
        omc_source_state_init(&state);
        result = omc_read_source(&range, &store, &w, &state, &opts);
        assert(result.status == OMC_READ_SOURCE_LIMIT && result.decoded.exif.status == OMC_EXIF_LIMIT);
        assert(store.entries == entries && store.entry_count == entry_count && store.arena.size == arena_size);
    }
    if (format == 1U || format == 2U || format == 3U) {
        if (format == 1U) put(f.bytes + 2U, 0xffffffffU, 4U, 1);
        else if (format == 2U) put(f.bytes + 0x54U, 0xffffffffU, 4U, 0);
        else put(f.bytes + f.size - 16U, 0xffffffffU, 4U, 1);
        omc_read_source_opts_init(&opts); omc_source_state_init(&state);
        result = omc_read_source(&range, &store, &w, &state, &opts);
        assert(result.status == OMC_READ_SOURCE_MALFORMED && state.code == OMC_SOURCE_OK);
        assert(store.entries == entries && store.entry_count == entry_count && store.arena.size == arena_size);
    }
    if (format == 0U) {
        omc_exr_source_res measured;
        f.fail = 0; f.forbidden_begin = 25U; f.forbidden_end = 45U;
        omc_source_state_init(&state);
        measured = omc_exr_dec_source(&range, NULL, NULL, 0U, &state, NULL, OMC_ENTRY_FLAG_NONE, NULL);
        assert(measured.decoded.status == OMC_EXR_OK && measured.decoded.entries_decoded == 1U);
        assert(measured.value_scratch_needed == 0U && measured.value_scratch_used == 0U);
    }
    omc_store_fini(&store);
}
int main(void)
{
    unsigned format;
    for (format = 0U; format < 5U; ++format) {
        check(format, 0U);
        if (format < 4U) check(format, (omc_u64)3U << 30U);
    }
    return 0;
}
