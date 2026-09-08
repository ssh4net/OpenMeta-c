#ifndef OMC_TEST_CHUNK_FIXTURE_H
#define OMC_TEST_CHUNK_FIXTURE_H

#include "omc/omc_source.h"
#include "omc_test_assert.h"
#include <string.h>

/* Independently constructed metadata and a replaceable image-data gap. */
typedef struct omc_test_chunk_fixture {
    omc_u8 bytes[2048];
    omc_size size;
    omc_size image_begin;
    omc_u64 gap;
    omc_u64 base;
    omc_u64 failure_at;
    int webp;
    int fail_mode;
} omc_test_chunk_fixture;

static const omc_u8 omc_test_chunk_jumbf[] = {
    0U, 0U, 0U, 33U, 'j', 'u', 'm', 'b',
    0U, 0U, 0U, 13U, 'j', 'u', 'm', 'd', 'c', '2', 'p', 'a', 0U,
    0U, 0U, 0U, 12U, 'c', 'b', 'o', 'r', 0xa1U, 0x61U, 0x61U, 1U
};

static void
omc_test_chunk_put32(omc_u8 *out, omc_u32 n, int little)
{
    unsigned i;
    for (i = 0U; i < 4U; ++i) {
        out[little ? i : 3U - i] = (omc_u8)(n & 255U);
        n >>= 8U;
    }
}

static void
omc_test_chunk_append(omc_test_chunk_fixture *f, const char *type,
                       const omc_u8 *payload, omc_size size)
{
    omc_size framing = f->webp ? 8U + (size & 1U) : 12U;
    assert(size + framing <= sizeof(f->bytes) - f->size);
    omc_test_chunk_put32(f->bytes + f->size + (f->webp ? 4U : 0U),
                         (omc_u32)size, f->webp);
    memcpy(f->bytes + f->size + (f->webp ? 0U : 4U), type, 4U);
    if (size != 0U)
        memcpy(f->bytes + f->size + 8U, payload, size);
    f->size += size + framing;
}

static void
omc_test_chunk_make(omc_test_chunk_fixture *f, int webp, int include_compressed)
{
    static const omc_u8 png[8] = {0x89U, 'P', 'N', 'G', 13U, 10U, 26U, 10U};
    static const omc_u8 tiff[26] = {
        'I', 'I', 42U, 0U, 8U, 0U, 0U, 0U, 1U, 0U,
        0x12U, 1U, 3U, 0U, 1U, 0U, 0U, 0U, 6U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    static const char xmp[] =
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
        "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
        "<rdf:Description xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "dc:format=\"image/test\"/></rdf:RDF></x:xmpmeta>";
    static const omc_u8 text[] = {'T', 'i', 't', 'l', 'e', 0U, 'h', 'e', 'l', 'l', 'o'};
    static const omc_u8 compressed[] = {
        'N', 'o', 't', 'e', 0U, 0U,
        0x78U, 0x9cU, 0xcbU, 0x48U, 0xcdU, 0xc9U, 0xc9U, 0x07U,
        0x00U, 0x06U, 0x2cU, 0x02U, 0x15U
    };
    omc_u8 payload[512], pixels[64];
    const char *jumb_type = webp ? "C2PA" : "caBX";
    memset(f, 0, sizeof(*f));
    memset(payload, 0, sizeof(payload));
    memset(pixels, 0xa5, sizeof(pixels));
    f->webp = webp;
    f->base = 37U;
    f->size = webp ? 12U : 8U;
    if (webp) {
        memcpy(f->bytes, "RIFF", 4U);
        memcpy(f->bytes + 8U, "WEBP", 4U);
        memcpy(payload, "Exif\0", 6U);
        memcpy(payload + 6U, tiff, sizeof(tiff));
        omc_test_chunk_append(f, "EXIF", payload, 6U + sizeof(tiff));
    } else {
        memcpy(f->bytes, png, sizeof(png));
        omc_test_chunk_append(f, "eXIf", tiff, sizeof(tiff));
    }
    /* Two fragments separated by XMP and image data, then an independent box. */
    omc_test_chunk_append(f, jumb_type, omc_test_chunk_jumbf, 29U);
    if (webp) {
        omc_test_chunk_append(f, "XMP ", (const omc_u8 *)xmp, sizeof(xmp) - 1U);
    } else {
        memset(payload, 0, 22U);
        memcpy(payload, "XML:com.adobe.xmp", 17U);
        memcpy(payload + 22U, xmp, sizeof(xmp) - 1U);
        omc_test_chunk_append(f, "iTXt", payload, 22U + sizeof(xmp) - 1U);
    }
    f->image_begin = f->size + 8U;
    omc_test_chunk_append(f, webp ? "VP8 " : "IDAT", pixels, sizeof(pixels));
    omc_test_chunk_append(f, jumb_type, omc_test_chunk_jumbf + 29U, 4U);
    omc_test_chunk_append(f, jumb_type, omc_test_chunk_jumbf,
                           sizeof(omc_test_chunk_jumbf));
    if (webp) {
        memset(payload, 0, 132U);
        omc_test_chunk_put32(payload, 132U, 0);
        memcpy(payload + 12U, "mntrRGB XYZ ", 12U);
        memcpy(payload + 36U, "acsp", 4U);
        omc_test_chunk_append(f, "ICCP", payload, 132U);
        omc_test_chunk_put32(f->bytes + 4U, (omc_u32)f->size - 8U, 1);
    } else {
        omc_test_chunk_append(f, "tEXt", text, sizeof(text));
        if (include_compressed)
            omc_test_chunk_append(f, "zTXt", compressed, sizeof(compressed));
        omc_test_chunk_append(f, "IEND", (const omc_u8 *)0, 0U);
    }
}

static void
omc_test_chunk_set_gap(omc_test_chunk_fixture *f, omc_u32 gap)
{
    f->gap = gap;
    omc_test_chunk_put32(f->bytes + f->image_begin - (f->webp ? 4U : 8U),
                         64U + gap, f->webp);
    if (f->webp)
        omc_test_chunk_put32(f->bytes + 4U, (omc_u32)f->size + gap - 8U, 1);
}

static omc_source_io_res
omc_test_chunk_read(void *context, omc_u64 offset, omc_u8 *out, omc_size size)
{
    omc_test_chunk_fixture *f = (omc_test_chunk_fixture *)context;
    omc_source_io_res r;
    omc_u64 p, end;
    assert(offset >= f->base);
    p = offset - f->base;
    assert(p <= f->size + f->gap && size <= f->size + f->gap - p);
    end = f->image_begin + 64U + f->gap;
    assert(size == 0U || p >= end || p + size <= f->image_begin);
    if (p >= end)
        p -= f->gap;
    assert(p <= f->size && size <= f->size - p);
    r.code = OMC_SOURCE_IO_OK;
    r.bytes_read = size;
    if (f->fail_mode && offset >= f->failure_at) {
        if (f->fail_mode == 1)
            r.bytes_read = size ? size - 1U : 0U;
        else {
            r.bytes_read = 0U;
            r.code = f->fail_mode == 2 ? OMC_SOURCE_IO_CANCELLED
                                      : OMC_SOURCE_IO_CHANGED;
        }
    }
    if (r.bytes_read)
        memcpy(out, f->bytes + (omc_size)p, (omc_size)r.bytes_read);
    return r;
}
#endif
