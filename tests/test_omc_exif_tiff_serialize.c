#include "omc/omc_exif_tiff_serialize.h"
#include "omc/omc_exif.h"
#include "omc_test_assert.h"
#include <string.h>

static void
add(omc_store *s, const char *ifd, omc_u16 tag, omc_val value)
{
    omc_entry e;
    omc_byte_ref r;
    memset(&e, 0, sizeof(e));
    assert(omc_arena_append(&s->arena, ifd, strlen(ifd), &r) == OMC_STATUS_OK);
    omc_key_make_exif_tag(&e.key, r, tag);
    e.value = value;
    e.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}

static void
add_wire_text(omc_store *s, omc_u16 tag, const omc_u8 *bytes, omc_size size,
              omc_u16 wire_type)
{
    omc_entry e;
    omc_byte_ref r;
    memset(&e, 0, sizeof(e));
    assert(omc_arena_append(&s->arena, "ifd0", 4U, &r) == OMC_STATUS_OK);
    omc_key_make_exif_tag(&e.key, r, tag);
    assert(omc_arena_append(&s->arena, bytes, size, &r) == OMC_STATUS_OK);
    omc_val_make_text(&e.value, r, wire_type == 129U ? OMC_TEXT_UTF8
                                                     : OMC_TEXT_ASCII);
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.origin.wire_type.family = OMC_WIRE_TIFF;
    e.origin.wire_type.code = wire_type;
    e.origin.wire_count = (omc_u32)size;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}

static omc_u32
get_le32(const omc_u8 *p)
{
    return (omc_u32)p[0] | ((omc_u32)p[1] << 8U) |
           ((omc_u32)p[2] << 16U) | ((omc_u32)p[3] << 24U);
}

static void
test_type129_text_and_retained_terminators(void)
{
    omc_store s;
    omc_exif_tiff_opts opts;
    omc_exif_tiff_res written;
    omc_mut_bytes out;
    omc_u8 bytes[512];
    omc_u8 *entry;
    omc_u32 root;
    omc_u32 count;
    omc_u32 i;
    int found_utf8_terminated;
    int found_utf8_unterminated;
    int found_ascii_terminated;
    static const omc_u8 utf8_terminated[] = {
        'c', 'a', 'f', 0xC3U, 0xA9U, 0U
    };
    static const omc_u8 utf8_unterminated[] = {0xE2U, 0x82U, 0xACU};
    static const omc_u8 ascii_terminated[] = {'a', 'b', 'c', 0U};
    omc_store_init(&s);
    add_wire_text(&s, 0xF100U, utf8_terminated,
                  sizeof(utf8_terminated), 129U);
    add_wire_text(&s, 0xF101U, utf8_unterminated,
                  sizeof(utf8_unterminated), 129U);
    add_wire_text(&s, 0xF102U, ascii_terminated,
                  sizeof(ascii_terminated), 2U);
    omc_exif_tiff_opts_init(&opts);
    opts.validate = 0;
    out.data = bytes;
    out.size = sizeof(bytes);
    written = omc_serialize_exif_tiff(&s, out, &opts);
    assert(written.status == OMC_EXIF_TIFF_OK);
    root = get_le32(bytes + 4U);
    count = (omc_u32)bytes[root] | ((omc_u32)bytes[root + 1U] << 8U);
    found_utf8_terminated = 0;
    found_utf8_unterminated = 0;
    found_ascii_terminated = 0;
    for (i = 0U; i < count; ++i) {
        entry = bytes + root + 2U + i * 12U;
        if (entry[0] == 0x00U && entry[1] == 0xF1U) {
            if (entry[2] == 129U && entry[3] == 0U &&
                get_le32(entry + 4U) == sizeof(utf8_terminated)) {
                omc_u32 offset;
                offset = get_le32(entry + 8U);
                assert(offset <= written.written &&
                       sizeof(utf8_terminated) <= written.written - offset);
                assert(memcmp(bytes + offset, utf8_terminated,
                              sizeof(utf8_terminated)) == 0);
                found_utf8_terminated = 1;
            }
        } else if (entry[0] == 0x01U && entry[1] == 0xF1U) {
            assert(entry[2] == 129U && entry[3] == 0U &&
                   get_le32(entry + 4U) == 4U);
            assert(entry[8] == 0xE2U && entry[9] == 0x82U &&
                   entry[10] == 0xACU && entry[11] == 0U);
            found_utf8_unterminated = 1;
        } else if (entry[0] == 0x02U && entry[1] == 0xF1U) {
            assert(entry[2] == 2U && entry[3] == 0U &&
                   get_le32(entry + 4U) == sizeof(ascii_terminated));
            assert(memcmp(entry + 8U, ascii_terminated,
                          sizeof(ascii_terminated)) == 0);
            found_ascii_terminated = 1;
        }
    }
    assert(found_utf8_terminated && found_utf8_unterminated &&
           found_ascii_terminated);
    omc_store_fini(&s);
}

static void
assert_invalid_type129(const omc_u8 *value, omc_size size, int as_bytes)
{
    omc_store s;
    omc_entry e;
    omc_byte_ref r;
    omc_exif_tiff_opts opts;
    omc_exif_tiff_res result;
    omc_mut_bytes out;
    omc_u8 output[128];
    omc_size i;
    omc_store_init(&s);
    memset(&e, 0, sizeof(e));
    assert(omc_arena_append(&s.arena, "ifd0", 4U, &r) == OMC_STATUS_OK);
    omc_key_make_exif_tag(&e.key, r, 0xF103U);
    assert(omc_arena_append(&s.arena, value, size, &r) == OMC_STATUS_OK);
    if (as_bytes)
        omc_val_make_bytes(&e.value, r);
    else
        omc_val_make_text(&e.value, r, OMC_TEXT_UTF8);
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.origin.wire_type.family = OMC_WIRE_TIFF;
    e.origin.wire_type.code = 129U;
    e.origin.wire_count = (omc_u32)size;
    assert(omc_store_add_entry(&s, &e, NULL) == OMC_STATUS_OK);
    omc_exif_tiff_opts_init(&opts);
    opts.validate = 0;
    memset(output, 0xA5, sizeof(output));
    out.data = output;
    out.size = sizeof(output);
    result = omc_serialize_exif_tiff(&s, out, &opts);
    assert(result.status == OMC_EXIF_TIFF_INVALID_METADATA);
    for (i = 0U; i < sizeof(output); ++i)
        assert(output[i] == 0xA5U);
    omc_store_fini(&s);
}

static void
test_type129_rejects_invalid_text_without_validation(void)
{
    static const omc_u8 invalid_utf8[] = {0xC0U, 0xAFU};
    static const omc_u8 embedded_nul[] = {'a', 0U, 'b'};
    static const omc_u8 double_terminal[] = {'a', 0U, 0U};
    static const omc_u8 bytes_value[] = {'a'};
    assert_invalid_type129(invalid_utf8, sizeof(invalid_utf8), 0);
    assert_invalid_type129(embedded_nul, sizeof(embedded_nul), 0);
    assert_invalid_type129(double_terminal, sizeof(double_terminal), 0);
    assert_invalid_type129(bytes_value, sizeof(bytes_value), 1);
}

int
main(void)
{
    omc_store s;
    omc_store decoded;
    omc_val v;
    omc_srational rational;
    omc_byte_ref r;
    omc_exif_tiff_opts opts;
    omc_exif_tiff_res measured;
    omc_exif_tiff_res written;
    omc_exif_res read;
    omc_mut_bytes out;
    omc_u8 bytes[512];
    omc_u8 prefix[13];
    omc_size i;
    int found;
    static const omc_u8 array_be[] = {0, 1, 0x12, 0x34};
    omc_store_init(&s);
    omc_store_init(&decoded);
    assert(omc_arena_append(&s.arena, "Writer", 6U, &r) == OMC_STATUS_OK);
    omc_val_make_text(&v, r, OMC_TEXT_ASCII);
    add(&s, "ifd0", 0x0131U, v);
    rational.numer = -2;
    rational.denom = 3;
    omc_val_make_srational(&v, rational);
    add(&s, "exififd", 0x9204U, v);
    assert(omc_arena_append(&s.arena, array_be, sizeof(array_be), &r) == OMC_STATUS_OK);
    omc_val_make_array(&v, OMC_ELEM_U16, 2U, r, OMC_BYTE_ORDER_BIG);
    add(&s, "ifd0", 0xF000U, v);
    omc_val_make_u32(&v, 12U);
    add(&s, "ifd2", 0x0100U, v);
    add(&s, "subifd3", 0x0101U, v);
    omc_exif_tiff_opts_init(&opts);
    opts.include_subifds = 1;
    out.data = NULL;
    out.size = 0U;
    measured = omc_serialize_exif_tiff(&s, out, &opts);
    assert(measured.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED &&
           measured.needed < sizeof(bytes));
    out.data = bytes;
    out.size = sizeof(bytes);
    written = omc_serialize_exif_tiff(&s, out, &opts);
    assert(written.status == OMC_EXIF_TIFF_OK && written.written == measured.needed);
    assert(written.entries_serialized == 5U);
    out.data = prefix;
    out.size = sizeof(prefix);
    written = omc_serialize_exif_tiff(&s, out, &opts);
    assert(written.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED);
    assert(memcmp(bytes, prefix, sizeof(prefix)) == 0);
    read = omc_exif_dec(bytes, (omc_size)measured.needed, &decoded,
                        OMC_INVALID_BLOCK_ID, NULL, 0U, NULL);
    assert(read.status == OMC_EXIF_OK || read.status == OMC_EXIF_TRUNCATED);
    found = 0;
    for (i = 0U; i < decoded.entry_count; ++i) {
        if (decoded.entries[i].key.u.exif_tag.tag == 0xF000U) {
            omc_const_bytes a;
            a = omc_arena_view(&decoded.arena, decoded.entries[i].value.u.ref);
            assert(a.size == 4U && a.data[0] == 1U && a.data[1] == 0U &&
                   a.data[2] == 0x34U && a.data[3] == 0x12U);
            found = 1;
        }
    }
    assert(found);
    memset(prefix, 0xA5, sizeof(prefix));
    opts.max_output_bytes = 8U;
    written = omc_serialize_exif_tiff(&s, out, &opts);
    assert(written.status == OMC_EXIF_TIFF_LIMIT);
    for (i = 0U; i < sizeof(prefix); ++i)
        assert(prefix[i] == 0xA5U);
    omc_exif_tiff_opts_init(&opts);
    s.entries[1].value.u.sr.denom = 0;
    written = omc_serialize_exif_tiff(&s, out, &opts);
    assert(written.status == OMC_EXIF_TIFF_INVALID_METADATA);
    for (i = 0U; i < sizeof(prefix); ++i)
        assert(prefix[i] == 0xA5U);
    test_type129_text_and_retained_terminators();
    test_type129_rejects_invalid_text_without_validation();
    omc_store_fini(&decoded);
    omc_store_fini(&s);
    return 0;
}
