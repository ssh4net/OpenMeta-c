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

static void
put_u64be(omc_u8 *out, omc_u64 value)
{
    unsigned i;
    for (i = 0U; i < 8U; ++i)
        out[i] = (omc_u8)(value >> (56U - i * 8U));
}

static omc_size
put_box_form(omc_u8 *out, omc_u32 type, const omc_u8 *payload, omc_size size,
             int extended, int terminal)
{
    omc_size header_size = extended ? 16U : 8U;
    omc_size total = header_size + size;

    if (terminal) {
        out[0] = 0U;
        out[1] = 0U;
        out[2] = 0U;
        out[3] = 0U;
    } else if (extended) {
        out[0] = 0U;
        out[1] = 0U;
        out[2] = 0U;
        out[3] = 1U;
    } else {
        out[0] = (omc_u8)(total >> 24U);
        out[1] = (omc_u8)(total >> 16U);
        out[2] = (omc_u8)(total >> 8U);
        out[3] = (omc_u8)total;
    }
    out[4] = (omc_u8)(type >> 24U);
    out[5] = (omc_u8)(type >> 16U);
    out[6] = (omc_u8)(type >> 8U);
    out[7] = (omc_u8)type;
    if (extended)
        put_u64be(out + 8U, terminal ? 0U : (omc_u64)total);
    if (size != 0U)
        memcpy(out + header_size, payload, size);
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

static omc_size
put_uuid_form(omc_u8 *out, const omc_u8 id[16], const char *text,
              int extended, int terminal)
{
    omc_u8 payload[128];
    omc_size text_size = (omc_size)strlen(text);
    memcpy(payload, id, 16U);
    memcpy(payload + 16U, text, text_size);
    return put_box_form(out, (omc_u32)OMC_FOURCC('u','u','i','d'), payload,
                        16U + text_size, extended, terminal);
}

static void
append_box(omc_u8 *out, omc_size *io_size, omc_u32 type,
           const omc_u8 *payload, omc_size size)
{
    *io_size += put_box(out + *io_size, type, payload, size);
}

static void
append_box_form(omc_u8 *out, omc_size *io_size, omc_u32 type,
                const omc_u8 *payload, omc_size size, int extended,
                int terminal)
{
    *io_size += put_box_form(out + *io_size, type, payload, size, extended,
                             terminal);
}

static void
append_uuid(omc_u8 *out, omc_size *io_size, const omc_u8 id[16],
            const char *text, int extended, int terminal)
{
    *io_size += put_uuid_form(out + *io_size, id, text, extended, terminal);
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
test_cpp_jp2_carrier_matrix(void)
{
    static const omc_u8 exif_uuid[16] = {
        0x4aU,0x70U,0x67U,0x54U,0x69U,0x66U,0x66U,0x45U,
        0x78U,0x69U,0x66U,0x2dU,0x3eU,0x4aU,0x50U,0x32U
    };
    static const omc_u8 xmp_uuid[16] = {
        0xbeU,0x7aU,0xcfU,0xcb,0x97U,0xa9U,0x42U,0xe8U,
        0x9cU,0x71U,0x99U,0x94U,0x91U,0xe3U,0xafU,0xacU
    };
    static const omc_u8 iptc_uuid[16] = {
        0x33U,0xc7U,0xa4U,0xd2U,0xb8U,0x1dU,0x47U,0x23U,
        0xa0U,0xbaU,0xf1U,0xa3U,0xe0U,0x97U,0xadU,0x38U
    };
    static const omc_u8 geo_uuid[16] = {
        0xb1U,0x4bU,0xf8U,0xbdU,0x08U,0x3dU,0x4bU,0x43U,
        0xa5U,0xaeU,0x8cU,0xd7U,0xd5U,0xa6U,0xceU,0x03U
    };
    const char *brands[2] = { "jp2 ", "jph " };
    const omc_u8 exif[] = { 'n','e','w','-','e','x','i','f' };
    const omc_u8 xmp[] = { 'n','e','w','-','x','m','p' };
    unsigned brand_index;
    unsigned selected;

    for (brand_index = 0U; brand_index < 2U; ++brand_index) {
        for (selected = 1U; selected <= 3U; ++selected) {
            omc_u8 input[4096];
            omc_u8 expected[4096];
            omc_u8 unknown[16];
            omc_size input_size = 0U;
            omc_size expected_size = 0U;
            omc_arena out;
            omc_jp2_rewrite_opts opts;
            omc_jp2_rewrite_res result;
            int replace_exif = (selected & 1U) != 0U;
            int replace_xmp = (selected & 2U) != 0U;

            append_box(input, &input_size,
                       (omc_u32)OMC_FOURCC('j','P',' ',' '),
                       (const omc_u8 *)"\r\n\x87\n", 4U);
            append_box(expected, &expected_size,
                       (omc_u32)OMC_FOURCC('j','P',' ',' '),
                       (const omc_u8 *)"\r\n\x87\n", 4U);
            append_box(input, &input_size,
                       (omc_u32)OMC_FOURCC('f','t','y','p'),
                       (const omc_u8 *)brands[brand_index], 4U);
            append_box(expected, &expected_size,
                       (omc_u32)OMC_FOURCC('f','t','y','p'),
                       (const omc_u8 *)brands[brand_index], 4U);
            append_box(input, &input_size,
                       (omc_u32)OMC_FOURCC('E','x','i','f'),
                       (const omc_u8 *)"old-exif", 8U);
            if (!replace_exif)
                append_box(expected, &expected_size,
                           (omc_u32)OMC_FOURCC('E','x','i','f'),
                           (const omc_u8 *)"old-exif", 8U);
            append_uuid(input, &input_size, exif_uuid, "old-exif-uuid", 0, 0);
            if (!replace_exif)
                append_uuid(expected, &expected_size, exif_uuid,
                            "old-exif-uuid", 0, 0);
            append_box(input, &input_size,
                       (omc_u32)OMC_FOURCC('x','m','l',' '),
                       (const omc_u8 *)"old-xmp", 7U);
            if (!replace_xmp)
                append_box(expected, &expected_size,
                           (omc_u32)OMC_FOURCC('x','m','l',' '),
                           (const omc_u8 *)"old-xmp", 7U);
            append_uuid(input, &input_size, xmp_uuid, "old-xmp-uuid", 1, 0);
            if (!replace_xmp)
                append_uuid(expected, &expected_size, xmp_uuid,
                            "old-xmp-uuid", 1, 0);
            memcpy(unknown, xmp_uuid, sizeof(unknown));
            unknown[15] ^= 1U;
            append_uuid(input, &input_size, unknown, "unknown-uuid", 0, 0);
            append_uuid(expected, &expected_size, unknown, "unknown-uuid", 0,
                        0);
            append_uuid(input, &input_size, iptc_uuid, "iptc", 0, 0);
            append_uuid(expected, &expected_size, iptc_uuid, "iptc", 0, 0);
            append_uuid(input, &input_size, geo_uuid, "geotiff", 0, 0);
            append_uuid(expected, &expected_size, geo_uuid, "geotiff", 0, 0);
            append_box(input, &input_size,
                       (omc_u32)OMC_FOURCC('f','r','e','e'),
                       (const omc_u8 *)"keep-padding", 12U);
            append_box(expected, &expected_size,
                       (omc_u32)OMC_FOURCC('f','r','e','e'),
                       (const omc_u8 *)"keep-padding", 12U);
            append_box(input, &input_size,
                       (omc_u32)OMC_FOURCC('j','p','2','c'),
                       (const omc_u8 *)"keep-codestream", 15U);
            append_box(expected, &expected_size,
                       (omc_u32)OMC_FOURCC('j','p','2','c'),
                       (const omc_u8 *)"keep-codestream", 15U);
            if (replace_exif)
                append_box(expected, &expected_size,
                           (omc_u32)OMC_FOURCC('E','x','i','f'), exif,
                           sizeof(exif));
            if (replace_xmp)
                append_box(expected, &expected_size,
                           (omc_u32)OMC_FOURCC('x','m','l',' '), xmp,
                           sizeof(xmp));

            omc_arena_init(&out);
            omc_jp2_rewrite_opts_init(&opts);
            opts.replace_exif = replace_exif;
            opts.replace_xmp = replace_xmp;
            assert(omc_jp2_rewrite(input, input_size, exif, sizeof(exif),
                                   xmp, sizeof(xmp), &opts, &out, &result)
                   == OMC_STATUS_OK);
            assert(result.status == OMC_JP2_REWRITE_OK);
            assert(result.removed_exif == (replace_exif ? 2U : 0U));
            assert(result.removed_xmp == (replace_xmp ? 2U : 0U));
            assert(result.inserted_exif == (replace_exif ? 1U : 0U));
            assert(result.inserted_xmp == (replace_xmp ? 1U : 0U));
            assert(out.size == expected_size);
            assert(memcmp(out.data, expected, expected_size) == 0);
            omc_arena_fini(&out);
        }
    }
}

static void
test_cpp_jp2_terminal_cases(void)
{
    static const omc_u8 exif_uuid[16] = {
        0x4aU,0x70U,0x67U,0x54U,0x69U,0x66U,0x66U,0x45U,
        0x78U,0x69U,0x66U,0x2dU,0x3eU,0x4aU,0x50U,0x32U
    };
    static const omc_u8 iptc_uuid[16] = {
        0x33U,0xc7U,0xa4U,0xd2U,0xb8U,0x1dU,0x47U,0x23U,
        0xa0U,0xbaU,0xf1U,0xa3U,0xe0U,0x97U,0xadU,0x38U
    };
    const omc_u8 xmp[] = { 'n','e','w','-','x','m','p' };
    unsigned terminal_index;

    for (terminal_index = 0U; terminal_index < 3U; ++terminal_index) {
        omc_u8 input[1024];
        omc_u8 expected[1024];
        omc_u8 terminal[256];
        omc_size input_size = 0U;
        omc_size expected_size = 0U;
        omc_size terminal_size;
        omc_arena out;
        omc_jp2_rewrite_opts opts;
        omc_jp2_rewrite_res result;
        int remove_terminal = terminal_index == 2U;

        append_box(input, &input_size,
                   (omc_u32)OMC_FOURCC('j','P',' ',' '),
                   (const omc_u8 *)"\r\n\x87\n", 4U);
        if (terminal_index == 0U) {
            terminal_size = put_box_form(terminal,
                                         (omc_u32)OMC_FOURCC('j','p','2','c'),
                                         (const omc_u8 *)"codestream", 10U,
                                         0, 1);
        } else {
            terminal_size = put_uuid_form(
                terminal, terminal_index == 1U ? iptc_uuid : exif_uuid,
                terminal_index == 1U ? "iptc" : "unselected-exif", 0, 1);
        }
        memcpy(input + input_size, terminal, terminal_size);
        input_size += terminal_size;
        append_box(expected, &expected_size,
                   (omc_u32)OMC_FOURCC('j','P',' ',' '),
                   (const omc_u8 *)"\r\n\x87\n", 4U);
        if (remove_terminal)
            append_box(expected, &expected_size,
                       (omc_u32)OMC_FOURCC('E','x','i','f'),
                       (const omc_u8 *)"e", 1U);
        append_box(expected, &expected_size,
                   (omc_u32)OMC_FOURCC('x','m','l',' '), xmp, sizeof(xmp));
        if (!remove_terminal) {
            memcpy(expected + expected_size, terminal, terminal_size);
            expected_size += terminal_size;
        }

        omc_arena_init(&out);
        omc_jp2_rewrite_opts_init(&opts);
        opts.replace_exif = remove_terminal;
        opts.replace_xmp = 1;
        assert(omc_jp2_rewrite(input, input_size,
                               (const omc_u8 *)"e", 1U, xmp, sizeof(xmp),
                               &opts, &out, &result) == OMC_STATUS_OK);
        assert(result.status == OMC_JP2_REWRITE_OK);
        assert(out.size == expected_size);
        assert(memcmp(out.data, expected, expected_size) == 0);
        omc_arena_fini(&out);
    }
}

static void
test_cpp_jp2_malformed_uuid_and_sizes(void)
{
    omc_u8 input[256];
    omc_u8 tail[64];
    omc_size prefix;
    omc_size tail_size;
    omc_size size;
    omc_size i;
    const omc_u64 ext_sizes[3] = { 15U, 32U, ~(omc_u64)0 };
    omc_arena out;
    omc_jp2_rewrite_opts opts;
    omc_jp2_rewrite_res result;

    prefix = put_box(input, (omc_u32)OMC_FOURCC('j','P',' ',' '),
                     (const omc_u8 *)"\r\n\x87\n", 4U);
    tail_size = put_box(tail, (omc_u32)OMC_FOURCC('u','u','i','d'),
                        (const omc_u8 *)"123456789012345", 15U);
    memcpy(input + prefix, tail, tail_size);
    omc_arena_init(&out);
    omc_jp2_rewrite_opts_init(&opts);
    assert(omc_jp2_rewrite(input, prefix + tail_size, NULL, 0U,
                           (const omc_u8 *)"x", 1U, &opts, &out, &result)
           == OMC_STATUS_OK);
    assert(result.status == OMC_JP2_REWRITE_MALFORMED && out.size == 0U);
    omc_arena_fini(&out);

    tail_size = put_box_form(tail, (omc_u32)OMC_FOURCC('u','u','i','d'),
                             (const omc_u8 *)"123456789012345", 15U, 1, 0);
    memcpy(input + prefix, tail, tail_size);
    omc_arena_init(&out);
    assert(omc_jp2_rewrite(input, prefix + tail_size, NULL, 0U,
                           (const omc_u8 *)"x", 1U, &opts, &out, &result)
           == OMC_STATUS_OK);
    assert(result.status == OMC_JP2_REWRITE_MALFORMED && out.size == 0U);
    omc_arena_fini(&out);

    memcpy(input + prefix, "short", 5U);
    omc_arena_init(&out);
    assert(omc_jp2_rewrite(input, prefix + 5U, NULL, 0U,
                           (const omc_u8 *)"x", 1U, &opts, &out, &result)
           == OMC_STATUS_OK);
    assert(result.status == OMC_JP2_REWRITE_MALFORMED && out.size == 0U);
    omc_arena_fini(&out);

    for (i = 0U; i < 3U; ++i) {
        size = prefix;
        input[size + 0U] = 0U;
        input[size + 1U] = 0U;
        input[size + 2U] = 0U;
        input[size + 3U] = 1U;
        input[size + 4U] = 'j';
        input[size + 5U] = 'p';
        input[size + 6U] = '2';
        input[size + 7U] = 'c';
        put_u64be(input + size + 8U, ext_sizes[i]);
        size += 16U;
        omc_arena_init(&out);
        assert(omc_jp2_rewrite(input, size, NULL, 0U,
                               (const omc_u8 *)"x", 1U, &opts, &out,
                               &result) == OMC_STATUS_OK);
        assert(result.status == OMC_JP2_REWRITE_MALFORMED
               && out.size == 0U);
        omc_arena_fini(&out);
    }
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
    test_cpp_jp2_carrier_matrix();
    test_cpp_jp2_terminal_cases();
    test_cpp_jp2_malformed_uuid_and_sizes();
    test_terminal_and_malformed();
    return 0;
}
