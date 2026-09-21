#include "omc/omc_jp2_rewrite.h"
#include "omc/omc_scan.h"
#include "omc_test_assert.h"

#include <string.h>

static omc_size
put_box(omc_u8 *out, omc_u32 type, const omc_u8 *payload, omc_size size)
{
    omc_size total = size + 8U;
    out[0] = (omc_u8)(total >> 24U);
    out[1] = (omc_u8)(total >> 16U);
    out[2] = (omc_u8)(total >> 8U);
    out[3] = (omc_u8)total;
    out[4] = (omc_u8)(type >> 24U);
    out[5] = (omc_u8)(type >> 16U);
    out[6] = (omc_u8)(type >> 8U);
    out[7] = (omc_u8)type;
    if (size != 0U)
        memcpy(out + 8U, payload, size);
    return total;
}

static omc_size
put_uuid(omc_u8 *out, const omc_u8 id[16], const char *text)
{
    omc_size text_size = (omc_size)strlen(text);
    memcpy(out + 8U, id, 16U);
    memcpy(out + 24U, text, text_size);
    return put_box(out, (omc_u32)OMC_FOURCC('u','u','i','d'), out + 8U,
                   16U + text_size);
}

static int
contains_bytes(const omc_u8 *data, omc_size size, const omc_u8 *needle,
               omc_size needle_size)
{
    omc_size i;
    if (needle_size == 0U)
        return 1;
    if (needle_size > size)
        return 0;
    for (i = 0U; i + needle_size <= size; ++i) {
        if (memcmp(data + i, needle, needle_size) == 0)
            return 1;
    }
    return 0;
}

static void
test_selected_carriers(void)
{
    static const omc_u8 exif_uuid[16] = {
        0x4aU,0x70U,0x67U,0x54U,0x69U,0x66U,0x66U,0x45U,
        0x78U,0x69U,0x66U,0x2dU,0x3eU,0x4aU,0x50U,0x32U
    };
    static const omc_u8 xmp_uuid[16] = {
        0xbeU,0x7aU,0xcfU,0xcbU,0x97U,0xa9U,0x42U,0xe8U,
        0x9cU,0x71U,0x99U,0x94U,0x91U,0xe3U,0xafU,0xacU
    };
    omc_u8 input[512];
    omc_u8 box[128];
    omc_size size;
    omc_size box_size;
    omc_arena out;
    omc_jp2_rewrite_opts opts;
    omc_jp2_rewrite_res result;
    const omc_u8 exif[] = { 'n','e','w','-','e','x','i','f' };
    const omc_u8 xmp[] = { 'n','e','w','-','x','m','p' };

    size = 0U;
    box_size = put_box(input + size, (omc_u32)OMC_FOURCC('j','P',' ',' '),
                       (const omc_u8 *)"\r\n\x87\n", 4U);
    size += box_size;
    box_size = put_box(input + size, (omc_u32)OMC_FOURCC('f','t','y','p'),
                       (const omc_u8 *)"jph \0\0\0\0jph ", 12U);
    size += box_size;
    box_size = put_box(input + size, (omc_u32)OMC_FOURCC('E','x','i','f'),
                       (const omc_u8 *)"old-exif", 8U);
    size += box_size;
    box_size = put_uuid(box, exif_uuid, "old-exif-uuid");
    memcpy(input + size, box, box_size); size += box_size;
    box_size = put_box(input + size, (omc_u32)OMC_FOURCC('x','m','l',' '),
                       (const omc_u8 *)"old-xmp", 7U);
    size += box_size;
    box_size = put_uuid(box, xmp_uuid, "old-xmp-uuid");
    memcpy(input + size, box, box_size); size += box_size;
    box_size = put_box(input + size, (omc_u32)OMC_FOURCC('f','r','e','e'),
                       (const omc_u8 *)"keep", 4U);
    size += box_size;

    omc_arena_init(&out);
    omc_jp2_rewrite_opts_init(&opts);
    opts.replace_exif = 1;
    opts.replace_xmp = 1;
    assert(omc_jp2_rewrite(input, size, exif, sizeof(exif), xmp, sizeof(xmp),
                           &opts, &out, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_JP2_REWRITE_OK);
    assert(result.removed_exif == 2U && result.removed_xmp == 2U);
    assert(result.inserted_exif == 1U && result.inserted_xmp == 1U);
    assert(out.size != 0U);
    assert(contains_bytes(out.data, out.size, exif, sizeof(exif)));
    assert(contains_bytes(out.data, out.size, xmp, sizeof(xmp)));
    omc_arena_fini(&out);
}

static void
test_terminal_and_malformed(void)
{
    omc_u8 input[256];
    omc_u8 malformed[64];
    omc_size size;
    omc_arena out;
    omc_jp2_rewrite_opts opts;
    omc_jp2_rewrite_res result;
    const omc_u8 xmp[] = { 'x' };
    size = put_box(input, (omc_u32)OMC_FOURCC('j','P',' ',' '),
                   (const omc_u8 *)"\r\n\x87\n", 4U);
    size += put_box(input + size, (omc_u32)OMC_FOURCC('j','p','2','c'),
                    (const omc_u8 *)"pixels", 6U);
    input[size] = 0U; input[size + 1U] = 0U; input[size + 2U] = 0U; input[size + 3U] = 0U;
    input[size + 4U] = 'j'; input[size + 5U] = 'p'; input[size + 6U] = '2'; input[size + 7U] = 'c';
    memcpy(input + size + 8U, "terminal", 8U);
    size += 16U;
    omc_arena_init(&out);
    omc_jp2_rewrite_opts_init(&opts);
    assert(omc_jp2_rewrite(input, size, NULL, 0U, xmp, sizeof(xmp), &opts,
                           &out, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_JP2_REWRITE_OK);
    assert(out.size > size);
    assert(out.data[out.size - 16U] == 0U && out.data[out.size - 12U] == 'j');
    omc_arena_fini(&out);

    memset(malformed, 0, sizeof(malformed));
    malformed[3] = 8U;
    malformed[7] = 'j';
    malformed[8] = 'u';
    malformed[9] = 'n';
    malformed[10] = 'k';
    omc_arena_init(&out);
    assert(omc_jp2_rewrite(malformed, 11U, NULL, 0U, xmp, sizeof(xmp),
                           &opts, &out, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_JP2_REWRITE_MALFORMED && out.size == 0U);
    omc_arena_fini(&out);
}

int
main(void)
{
    test_selected_carriers();
    test_terminal_and_malformed();
    return 0;
}
