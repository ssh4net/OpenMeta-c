#include "omc/omc_pay.h"

#include "omc_test_assert.h"
#include <string.h>

#include "omc_test_input.h"
static omc_pay_res
pay_both(const omc_u8* bytes, omc_size size, const omc_blk_ref* blocks,
         omc_u32 count, omc_u32 seed, omc_u8* out, omc_size cap,
         omc_u32* indices, omc_u32 index_cap, const omc_pay_opts* opts)
{
    omc_test_input host;
    omc_source_range range;
    omc_source_state state;
    omc_u8 payload[65536], stream[1];
    omc_u32 source_indices[256];
    omc_pay_source_workspace work;
    omc_pay_res memory, source;
    memory = omc_pay_ext(bytes, size, blocks, count, seed, out, cap, indices, index_cap, opts);
    assert(cap <= sizeof(payload) && index_cap <= 256U);
    host.bytes = bytes; host.size = size;
    range.source = omc_source_callback(size + 37U, &host, omc_test_input_read, 0);
    range.source_offset = 37U; range.size = size;
    omc_source_state_init(&state);
    work.stream = stream; work.stream_capacity = sizeof(stream);
    memset(payload, 0xA5, sizeof(payload));
    source = omc_pay_ext_source(&range, blocks, count, seed, out == NULL ? NULL : payload,
                cap, indices == NULL ? NULL : source_indices, index_cap, &work, &state, NULL, opts);
    assert(state.code == OMC_SOURCE_OK);
    assert(memory.status == source.status && memory.written == source.written && memory.needed == source.needed);
    if (out != NULL && source.written) assert(memcmp(out, payload, (omc_size)source.written) == 0);
    if (out != NULL) {
        omc_size i;
        for (i = (omc_size)source.written; i < sizeof(payload); ++i)
            assert(payload[i] == 0xA5U);
    }
    return memory;
}
#define omc_pay_ext(b,s,r,n,i,o,c,x,z,p) pay_both(b,s,r,n,i,o,c,x,z,p)

static omc_pay_res
measure_both(const omc_u8* bytes, omc_size size, const omc_blk_ref* blocks,
             omc_u32 count, omc_u32 seed, omc_u32* indices,
             omc_u32 index_capacity, const omc_pay_opts* opts)
{
    omc_test_input host;
    omc_source_range range;
    omc_source_state state;
    omc_u8 stream[1];
    omc_u32 source_indices[256];
    omc_pay_source_workspace work;
    omc_pay_res memory, source;
    memory = omc_pay_meas(bytes, size, blocks, count, seed, indices,
                          index_capacity, opts);
    assert(index_capacity <= 256U);
    host.bytes = bytes; host.size = size;
    range.source = omc_source_callback(size + 37U, &host, omc_test_input_read, 0);
    range.source_offset = 37U; range.size = size;
    omc_source_state_init(&state);
    work.stream = stream; work.stream_capacity = sizeof(stream);
    source = omc_pay_meas_source(&range, blocks, count, seed,
                 indices == NULL ? NULL : source_indices, index_capacity,
                 &work, &state, NULL, opts);
    assert(state.code == OMC_SOURCE_OK);
    assert(memory.status == source.status && memory.written == source.written
           && memory.needed == source.needed);
    return memory;
}
#define omc_pay_meas(b,s,r,n,i,x,z,p) measure_both(b,s,r,n,i,x,z,p)

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_bytes(omc_u8* out, omc_size* io_size, const void* src, omc_size n)
{
    memcpy(out + *io_size, src, n);
    *io_size += n;
}

static omc_u32
adler32_bytes(const omc_u8* data, omc_size size)
{
    omc_u32 s1;
    omc_u32 s2;
    omc_size i;

    s1 = 1U;
    s2 = 0U;
    for (i = 0U; i < size; ++i) {
        s1 = (s1 + data[i]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    return (s2 << 16) | s1;
}

static omc_size
make_zlib_store_stream(omc_u8* out, const omc_u8* payload, omc_size payload_size)
{
    omc_u16 len;
    omc_u16 nlen;
    omc_u32 adler;
    omc_size size;

    assert(payload_size <= 65535U);

    len = (omc_u16)payload_size;
    nlen = (omc_u16)~len;
    adler = adler32_bytes(payload, payload_size);

    size = 0U;
    append_u8(out, &size, 0x78U);
    append_u8(out, &size, 0x01U);
    append_u8(out, &size, 0x01U);
    append_u8(out, &size, (omc_u8)(len & 0xFFU));
    append_u8(out, &size, (omc_u8)((len >> 8) & 0xFFU));
    append_u8(out, &size, (omc_u8)(nlen & 0xFFU));
    append_u8(out, &size, (omc_u8)((nlen >> 8) & 0xFFU));
    append_bytes(out, &size, payload, payload_size);
    append_u8(out, &size, (omc_u8)((adler >> 24) & 0xFFU));
    append_u8(out, &size, (omc_u8)((adler >> 16) & 0xFFU));
    append_u8(out, &size, (omc_u8)((adler >> 8) & 0xFFU));
    append_u8(out, &size, (omc_u8)((adler >> 0) & 0xFFU));
    return size;
}

static void
test_pay_deflate_extract(void)
{
    static const omc_u8 payload_bytes[] = {
        'A', 'B', 'C', 'D', '1', '2', '3', '4'
    };
    omc_u8 file_bytes[64];
    omc_size file_size;
    omc_blk_ref block;
    omc_u8 out_payload[16];
    omc_pay_res res;

    memset(&block, 0, sizeof(block));
    file_size = make_zlib_store_stream(file_bytes, payload_bytes,
                                       sizeof(payload_bytes));
    block.kind = OMC_BLK_XMP;
    block.compression = OMC_BLK_COMP_DEFLATE;
    block.data_size = (omc_u64)file_size;

    res = omc_pay_ext(file_bytes, file_size, &block, 1U, 0U, out_payload,
                      sizeof(out_payload), (omc_u32*)0, 0U,
                      (const omc_pay_opts*)0);

#if OMC_HAVE_ZLIB
    assert(res.status == OMC_PAY_OK);
    assert(res.written == sizeof(payload_bytes));
    assert(res.needed == sizeof(payload_bytes));
    assert(memcmp(out_payload, payload_bytes, sizeof(payload_bytes)) == 0);
#else
    assert(res.status == OMC_PAY_UNSUPPORTED);
    assert(res.written == 0U);
    assert(res.needed == 0U);
#endif
}

static void
test_pay_deflate_measure_and_truncation(void)
{
    static const omc_u8 payload_bytes[] = {
        'A', 'B', 'C', 'D', '1', '2', '3', '4'
    };
    omc_u8 file_bytes[64];
    omc_size file_size;
    omc_blk_ref block;
    omc_u8 out_payload[4];
    omc_pay_res res;

    memset(&block, 0, sizeof(block));
    file_size = make_zlib_store_stream(file_bytes, payload_bytes,
                                       sizeof(payload_bytes));
    block.kind = OMC_BLK_ICC;
    block.compression = OMC_BLK_COMP_DEFLATE;
    block.data_size = (omc_u64)file_size;

    res = omc_pay_ext(file_bytes, file_size, &block, 1U, 0U, out_payload,
                      sizeof(out_payload), (omc_u32*)0, 0U,
                      (const omc_pay_opts*)0);

#if OMC_HAVE_ZLIB
    assert(res.status == OMC_PAY_TRUNCATED);
    assert(res.written == sizeof(out_payload));
    assert(res.needed == sizeof(payload_bytes));
    assert(memcmp(out_payload, payload_bytes, sizeof(out_payload)) == 0);

    res = omc_pay_meas(file_bytes, file_size, &block, 1U, 0U, (omc_u32*)0, 0U,
                       (const omc_pay_opts*)0);
    assert(res.status == OMC_PAY_OK);
    assert(res.written == 0U);
    assert(res.needed == sizeof(payload_bytes));
#else
    assert(res.status == OMC_PAY_UNSUPPORTED);
    assert(res.written == 0U);
    assert(res.needed == 0U);

    res = omc_pay_meas(file_bytes, file_size, &block, 1U, 0U, (omc_u32*)0, 0U,
                       (const omc_pay_opts*)0);
    assert(res.status == OMC_PAY_UNSUPPORTED);
    assert(res.written == 0U);
    assert(res.needed == 0U);
#endif
}

static void
test_pay_brotli_extract(void)
{
    static const omc_u8 payload_bytes[] = {
        'A', 'B', 'C', 'D', '1', '2', '3', '4'
    };
    static const omc_u8 file_bytes[] = {
        0x8BU, 0x03U, 0x80U, 0x41U, 0x42U, 0x43U,
        0x44U, 0x31U, 0x32U, 0x33U, 0x34U, 0x03U
    };
    omc_blk_ref block;
    omc_u8 out_payload[16];
    omc_pay_res res;

    memset(&block, 0, sizeof(block));
    block.kind = OMC_BLK_JUMBF;
    block.compression = OMC_BLK_COMP_BROTLI;
    block.data_size = (omc_u64)sizeof(file_bytes);

    res = omc_pay_ext(file_bytes, sizeof(file_bytes), &block, 1U, 0U,
                      out_payload, sizeof(out_payload), (omc_u32*)0, 0U,
                      (const omc_pay_opts*)0);

#if OMC_HAVE_BROTLI
    assert(res.status == OMC_PAY_OK);
    assert(res.written == sizeof(payload_bytes));
    assert(res.needed == sizeof(payload_bytes));
    assert(memcmp(out_payload, payload_bytes, sizeof(payload_bytes)) == 0);
#else
    assert(res.status == OMC_PAY_UNSUPPORTED);
    assert(res.written == 0U);
    assert(res.needed == 0U);
#endif
}

static void
test_pay_brotli_measure_and_truncation(void)
{
    static const omc_u8 payload_bytes[] = {
        'A', 'B', 'C', 'D', '1', '2', '3', '4'
    };
    static const omc_u8 file_bytes[] = {
        0x8BU, 0x03U, 0x80U, 0x41U, 0x42U, 0x43U,
        0x44U, 0x31U, 0x32U, 0x33U, 0x34U, 0x03U
    };
    omc_blk_ref block;
    omc_u8 out_payload[4];
    omc_pay_res res;

    memset(&block, 0, sizeof(block));
    block.kind = OMC_BLK_XMP;
    block.compression = OMC_BLK_COMP_BROTLI;
    block.data_size = (omc_u64)sizeof(file_bytes);

    res = omc_pay_ext(file_bytes, sizeof(file_bytes), &block, 1U, 0U,
                      out_payload, sizeof(out_payload), (omc_u32*)0, 0U,
                      (const omc_pay_opts*)0);

#if OMC_HAVE_BROTLI
    assert(res.status == OMC_PAY_TRUNCATED);
    assert(res.written == sizeof(out_payload));
    assert(res.needed == sizeof(payload_bytes));
    assert(memcmp(out_payload, payload_bytes, sizeof(out_payload)) == 0);

    res = omc_pay_meas(file_bytes, sizeof(file_bytes), &block, 1U, 0U,
                       (omc_u32*)0, 0U, (const omc_pay_opts*)0);
    assert(res.status == OMC_PAY_OK);
    assert(res.written == 0U);
    assert(res.needed == sizeof(payload_bytes));
#else
    assert(res.status == OMC_PAY_UNSUPPORTED);
    assert(res.written == 0U);
    assert(res.needed == 0U);

    res = omc_pay_meas(file_bytes, sizeof(file_bytes), &block, 1U, 0U,
                       (omc_u32*)0, 0U, (const omc_pay_opts*)0);
    assert(res.status == OMC_PAY_UNSUPPORTED);
    assert(res.written == 0U);
    assert(res.needed == 0U);
#endif
}

static void
test_pay_output_limits(void)
{
    static const omc_u8 payload[] = "ABCD1234";
    static const omc_u8 brotli[] = {
        0x8BU, 0x03U, 0x80U, 0x41U, 0x42U, 0x43U,
        0x44U, 0x31U, 0x32U, 0x33U, 0x34U, 0x03U
    };
    static const omc_u64 limits[] = { 8U, 7U, 0U };
    static const omc_size capacities[] = { 16U, 3U, 0U };
    omc_u8 deflate[64];
    omc_u8 out[18];
    omc_size deflate_size;
    unsigned codec, l, c;

    deflate_size = make_zlib_store_stream(deflate, payload, 8U);
    for (codec = 0U; codec < 2U; ++codec) {
        const omc_u8* bytes;
        omc_size size;
        omc_blk_ref block;
        int enabled;
        bytes = codec == 0U ? deflate : brotli;
        size = codec == 0U ? deflate_size : sizeof(brotli);
        enabled = codec == 0U ? OMC_HAVE_ZLIB : OMC_HAVE_BROTLI;
        memset(&block, 0, sizeof(block));
        block.kind = OMC_BLK_XMP;
        block.compression = codec == 0U ? OMC_BLK_COMP_DEFLATE : OMC_BLK_COMP_BROTLI;
        block.data_size = (omc_u64)size;
        for (l = 0U; l < 3U; ++l) {
            omc_pay_opts opts;
            omc_pay_res measured;
            omc_pay_opts_init(&opts);
            opts.limits.max_output_bytes = limits[l];
            for (c = 0U; c < 3U; ++c) {
                omc_pay_res res;
                omc_size i;
                memset(out, 0xA5, sizeof(out));
                res = omc_pay_ext(bytes, size, &block, 1U, 0U,
                                  out + 1U, capacities[c], NULL, 0U, &opts);
                if (!enabled) {
                    assert(res.status == OMC_PAY_UNSUPPORTED);
                    assert(res.written == 0U);
                } else {
                    omc_u64 accepted;
                    accepted = limits[l] == 7U ? 7U : 8U;
                    if (accepted > capacities[c]) accepted = capacities[c];
                    assert(res.written == accepted);
                    assert(res.needed == 8U);
                    assert(res.status == (limits[l] == 7U ? OMC_PAY_LIMIT :
                           capacities[c] < 8U ? OMC_PAY_TRUNCATED : OMC_PAY_OK));
                    assert(memcmp(out + 1U, payload, (omc_size)accepted) == 0);
                }
                assert(out[0] == 0xA5U);
                for (i = 1U + (omc_size)res.written; i < sizeof(out); ++i)
                    assert(out[i] == 0xA5U);
            }
            measured = omc_pay_meas(bytes, size, &block, 1U, 0U, NULL, 0U, &opts);
            assert(measured.written == 0U);
            assert(measured.status == (!enabled ? OMC_PAY_UNSUPPORTED :
                   limits[l] == 7U ? OMC_PAY_LIMIT : OMC_PAY_OK));
            assert(measured.needed == (enabled ? 8U : 0U));
        }
    }
}

static void
test_pay_uncompressed_unlimited(void)
{
    static const omc_u8 bytes[] = "ABCD1234";
    omc_blk_ref block;
    omc_pay_opts opts;
    omc_pay_res res;
    omc_u8 out[8];
    memset(&block, 0, sizeof(block));
    block.kind = OMC_BLK_XMP;
    block.data_size = sizeof(out);
    omc_pay_opts_init(&opts);
    opts.limits.max_output_bytes = 0U;
    res = omc_pay_ext(bytes, 8U, &block, 1U, 0U, out, sizeof(out), NULL, 0U, &opts);
    assert(res.status == OMC_PAY_OK && res.written == 8U && res.needed == 8U);
    assert(memcmp(out, bytes, sizeof(out)) == 0);
}

static void
test_pay_exact_cap_truncated_trailer(void)
{
    static const omc_u8 bytes[] = "ABCD1234";
    omc_u8 file[64], out[16];
    omc_size size;
    omc_blk_ref block;
    omc_pay_opts opts;
    omc_pay_res res;
    size = make_zlib_store_stream(file, bytes, 8U) - 1U;
    memset(&block, 0, sizeof(block));
    block.kind = OMC_BLK_XMP;
    block.compression = OMC_BLK_COMP_DEFLATE;
    block.data_size = size;
    omc_pay_opts_init(&opts);
    opts.limits.max_output_bytes = 8U;
    memset(out, 0xA5, sizeof(out));
    res = omc_pay_ext(file, size, &block, 1U, 0U, out, sizeof(out), NULL, 0U, &opts);
#if OMC_HAVE_ZLIB
    assert(res.status == OMC_PAY_MALFORMED && res.written == 8U && res.needed == 8U);
    assert(out[8] == 0xA5U && out[15] == 0xA5U);
    res = omc_pay_meas(file, size, &block, 1U, 0U, NULL, 0U, &opts);
    assert(res.status == OMC_PAY_MALFORMED && res.written == 0U && res.needed == 8U);
#else
    assert(res.status == OMC_PAY_UNSUPPORTED && res.written == 0U);
#endif
}

int
main(void)
{
    test_pay_deflate_extract();
    test_pay_deflate_measure_and_truncation();
    test_pay_brotli_extract();
    test_pay_brotli_measure_and_truncation();
    test_pay_output_limits();
    test_pay_uncompressed_unlimited();
    test_pay_exact_cap_truncated_trailer();
    return 0;
}
