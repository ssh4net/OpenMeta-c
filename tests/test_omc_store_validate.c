#include "omc/omc_store_validate.h"
#include "omc_test_assert.h"
#include <string.h>

static omc_entry *
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
    return &s->entries[s->entry_count - 1U];
}

int
main(void)
{
    omc_store s;
    omc_val v;
    omc_urational ur;
    omc_srational sr;
    omc_metadata_validate_res res;
    omc_metadata_validate_opts opts;
    omc_metadata_issue issues[8];
    omc_entry *e;
    omc_byte_ref r;
    static const omc_u8 rational_be[] = {0, 0, 0, 1, 0, 0, 0, 2,
                                         0, 0, 0, 3, 0, 0, 0, 0};

    omc_store_init(&s);
    omc_val_make_i8(&v, -128);
    assert(v.u.i64 == -128 && v.elem_type == OMC_ELEM_I8);
    omc_val_make_i32(&v, -2147483647 - 1);
    assert(v.u.i64 == (-2147483647 - 1));
    sr.numer = -1;
    sr.denom = 3;
    omc_val_make_srational(&v, sr);
    add(&s, "exififd", 0x9204U, v);
    ur.numer = 1U;
    ur.denom = 125U;
    omc_val_make_urational(&v, ur);
    add(&s, "exififd", 0x829AU, v);
    res = omc_validate_store(&s, issues, 8U, NULL);
    assert(res.status == OMC_STATUS_OK && res.entries_checked == 2U);
    add(&s, "exififd", 0x829AU, v);
    res = omc_validate_store(&s, issues, 8U, NULL);
    assert(res.error_count == 1U && issues[0].code == OMC_METADATA_DUPLICATE_SINGLETON);
    s.entries[2].flags = OMC_ENTRY_FLAG_DELETED;
    res = omc_validate_store(&s, issues, 8U, NULL);
    assert(res.status == OMC_STATUS_OK);
    s.entries[1].value.u.ur.denom = 0U;
    res = omc_validate_entry(&s, 1U, issues, 8U, NULL);
    assert(res.error_count == 1U && issues[0].code == OMC_METADATA_ZERO_DENOMINATOR);
    s.entries[1].value.u.ur.denom = 125U;
    assert(omc_arena_append(&s.arena, rational_be, sizeof(rational_be), &r) ==
           OMC_STATUS_OK);
    omc_val_make_array(&v, OMC_ELEM_URATIONAL, 2U, r, OMC_BYTE_ORDER_BIG);
    e = add(&s, "ifd0", 0xFF00U, v);
    res = omc_validate_store(&s, issues, 8U, NULL);
    assert(res.error_count == 1U && issues[0].code == OMC_METADATA_ZERO_DENOMINATOR);
    e->value.count = 1U;
    res = omc_validate_store(&s, issues, 8U, NULL);
    assert(res.error_count == 1U && issues[0].code == OMC_METADATA_INVALID_VALUE);
    e->value.u.ref.size = 8U;
    res = omc_validate_store(&s, issues, 8U, NULL);
    assert(res.status == OMC_STATUS_OK);
    omc_metadata_validate_opts_init(&opts);
    opts.unknown_tags = OMC_UNKNOWN_TAG_WARNING;
    res = omc_validate_store(&s, NULL, 0U, &opts);
    assert(res.status == OMC_STATUS_OK && res.warning_count == 1U &&
           res.issues_needed == 1U);
    opts.warnings_as_errors = 1;
    res = omc_validate_store(&s, issues, 8U, &opts);
    assert(res.status == OMC_STATUS_STATE && res.error_count == 1U);
    opts.max_entries = 1U;
    res = omc_validate_store(&s, issues, 8U, &opts);
    assert(res.status == OMC_STATUS_OVERFLOW);
    e->key.u.exif_tag.ifd.offset = 0xFFFFFFFFU;
    res = omc_validate_entry(&s, 3U, issues, 8U, NULL);
    assert(res.error_count == 1U && issues[0].code == OMC_METADATA_INVALID_KEY);
    omc_store_reset(&s);
    omc_val_make_u32(&v, 640U);
    add(&s, "ifd0", 0x0100U, v);
    omc_metadata_validate_opts_init(&opts);
    opts.has_dimensions = 1;
    opts.width = 800U;
    opts.height = 600U;
    res = omc_validate_store(&s, issues, 8U, &opts);
    assert(res.error_count == 1U && issues[0].code == OMC_METADATA_IMAGE_MISMATCH);
    res = omc_validate_entry(&s, 0U, issues, 8U, &opts);
    assert(res.status == OMC_STATUS_OK);
    {
        static const omc_u8 dims[] = {0U, 2U, 0U, 3U};
        static const omc_u8 pattern[] = {0U, 1U, 1U, 2U};
        assert(omc_arena_append(&s.arena, dims, sizeof(dims), &r) == OMC_STATUS_OK);
        omc_val_make_array(&v, OMC_ELEM_U16, 2U, r, OMC_BYTE_ORDER_BIG);
        add(&s, "ifd0", 0x828DU, v);
        assert(omc_arena_append(&s.arena, pattern, sizeof(pattern), &r) ==
               OMC_STATUS_OK);
        omc_val_make_array(&v, OMC_ELEM_U8, 4U, r, OMC_BYTE_ORDER_NATIVE);
        add(&s, "ifd0", 0x828EU, v);
        res = omc_validate_store(&s, issues, 8U, NULL);
        assert(res.error_count == 1U && issues[0].code == OMC_METADATA_RELATED_ENTRIES);
        assert(issues[0].entry == 2U && issues[0].related_entry == 1U);
    }
    omc_store_fini(&s);
    return 0;
}
