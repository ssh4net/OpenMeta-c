#include "omc/omc_pay.h"

#include <assert.h>
#include <string.h>

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

int
main(void)
{
    test_pay_deflate_extract();
    test_pay_deflate_measure_and_truncation();
    return 0;
}
