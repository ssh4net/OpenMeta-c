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
    omc_store_fini(&decoded);
    omc_store_fini(&s);
    return 0;
}
