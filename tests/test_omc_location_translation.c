#include "omc_location_fixture.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static const char *const namespaces[5] = {
    "http://ns.adobe.com/photoshop/1.0/", "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
    "http://ns.adobe.com/photoshop/1.0/", "http://ns.adobe.com/photoshop/1.0/",
    "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"};
static const char *const paths[5] = {"City", "Location", "State", "Country",
                                     "CountryCode"};

static omc_byte_ref
put(omc_store *s, const void *data, omc_size n)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&s->arena, data, n, &ref) == OMC_STATUS_OK);
    return ref;
}

static void
set_text(omc_store *s, omc_entry_id id, const void *data, omc_size n)
{
    omc_val_make_text(&s->entries[id].value, put(s, data, n), OMC_TEXT_UTF8);
}

static omc_entry_id
xmp(omc_store *s, omc_u32 field, const char *path, const char *value, omc_u32 flags)
{
    omc_entry e;
    omc_entry_id id;
    memset(&e, 0, sizeof(e));
    omc_key_make_xmp_property(&e.key,
                              put(s, namespaces[field], strlen(namespaces[field])),
                              put(s, path, strlen(path)));
    omc_val_make_text(&e.value, put(s, value, strlen(value)), OMC_TEXT_UTF8);
    e.flags = flags;
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.origin.order_in_block = 42U + field;
    e.origin.wire_type_name = put(s, "source", 6U);
    assert(omc_store_add_entry(s, &e, &id) == OMC_STATUS_OK);
    return id;
}

static omc_entry_id
iptc(omc_store *s, omc_u16 record, omc_u16 dataset, const char *value, omc_u32 order)
{
    omc_entry e;
    omc_entry_id id;
    memset(&e, 0, sizeof(e));
    omc_key_make_iptc_dataset(&e.key, record, dataset);
    omc_val_make_bytes(&e.value, put(s, value, strlen(value)));
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.origin.order_in_block = order;
    assert(omc_store_add_entry(s, &e, &id) == OMC_STATUS_OK);
    return id;
}

omc_translation_status
omc_location_fixture(omc_u32 index, omc_store *s, omc_location_translation_opts *o)
{
    omc_entry_id id;
    omc_u32 i;
    omc_u32 field;
    omc_size n;
    char value[65];
    omc_location_translation_opts_init(o);
    o->mappings = OMC_TRANSLATE_CITY;
    xmp(s, 0U, "City", "Tokyo", OMC_ENTRY_FLAG_DIRTY);
    if (index >= 60U) {
        field = (index - 60U) / 2U;
        s->entries[0].flags = OMC_ENTRY_FLAG_DELETED;
        o->mappings = OMC_TRANSLATE_CITY << field;
        n = field == 3U ? 64U : field == 4U ? 3U : 32U;
        n += index % 2U;
        memset(value, 'A', n);
        id = xmp(s, field, paths[field], "A", OMC_ENTRY_FLAG_DIRTY);
        set_text(s, id, value, n);
        return index % 2U ? OMC_TRANSLATION_VALUE_TOO_LONG : OMC_TRANSLATION_OK;
    }
    switch (index) {
    case 0U:
        o->mappings = OMC_TRANSLATE_LOCATION;
        set_text(s, 0U, "Montr\303\251al", 9U);
        for (i = 1U; i < 5U; ++i)
            xmp(s, i, paths[i], i == 4U ? "CA" : "Place", OMC_ENTRY_FLAG_DIRTY);
        iptc(s, 2U, 120U, "Keep caption", 0U);
        break;
    case 1U:
        s->entries[0].flags = 0U;
        break;
    case 2U:
        s->entries[0].flags = 0U;
        o->all_sources = 1;
        break;
    case 3U:
        o->mappings = 0U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 4U:
        s->entries[0].flags = OMC_ENTRY_FLAG_DELETED;
        xmp(s, 0U, "City[1]", "Other", OMC_ENTRY_FLAG_DIRTY);
        xmp(s, 0U, "City[@xml:lang=x-default]", "Other", OMC_ENTRY_FLAG_DIRTY);
        xmp(s, 1U, "City", "Other", OMC_ENTRY_FLAG_DIRTY);
        break;
    case 5U:
        xmp(s, 0U, "City", "Clean", 0U);
        return OMC_TRANSLATION_AMBIGUOUS_SOURCE;
    case 6U:
        xmp(s, 0U, "City", "", OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
        break;
    case 7U:
        s->entries[0].flags |= OMC_ENTRY_FLAG_DELETED;
        xmp(s, 0U, "City", "", OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
        break;
    case 8U:
        s->entries[0].flags = OMC_ENTRY_FLAG_DELETED;
        break;
    case 9U:
        iptc(s, 2U, 90U, "Old", 0U);
        return OMC_TRANSLATION_NATIVE_CONFLICT;
    case 10U:
        iptc(s, 2U, 90U, "Old", 0U);
        o->conflict = OMC_TRANSLATION_PRESERVE;
        break;
    case 11U:
        iptc(s, 2U, 90U, "Tokyo", 0U);
        break;
    case 12U:
        iptc(s, 2U, 90U, "Old", 100U);
        iptc(s, 2U, 90U, "Earlier", 2U);
        o->conflict = OMC_TRANSLATION_REPLACE;
        break;
    case 13U:
        set_text(s, 0U, "", 0U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 14U:
        set_text(s, 0U, "A\0B", 3U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 15U:
        set_text(s, 0U, "\300\257", 2U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 16U:
        set_text(s, 0U, "\357\277\276", 3U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 17U:
        set_text(s, 0U, "A\001B", 3U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 18U:
        omc_val_make_u32(&s->entries[0].value, 7U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 19U:
        s->entries[0].value.kind = OMC_VAL_BYTES;
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 20U:
    case 21U:
    case 22U:
    case 23U:
    case 24U:
        o->mappings = OMC_TRANSLATE_COUNTRY_CODE;
        xmp(s, 4U, "CountryCode",
            index == 20U   ? "us"
            : index == 21U ? "U"
            : index == 22U ? "U1"
            : index == 23U ? "USA"
                           : "ZZ",
            OMC_ENTRY_FLAG_DIRTY);
        return index < 23U ? OMC_TRANSLATION_INVALID_SOURCE : OMC_TRANSLATION_OK;
    case 25U:
        set_text(s, 0U, "A\tB\nC\rD", 7U);
        break;
    case 26U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 2U, 120U, "ASCII", 0U);
        break;
    case 27U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 2U, 120U, "\351", 0U);
        return OMC_TRANSLATION_ENCODING_CONFLICT;
    case 28U:
        set_text(s, 0U, "caf\303\251", 5U);
        id = iptc(s, 2U, 120U, "", 0U);
        omc_val_make_u32(&s->entries[id].value, 1U);
        return OMC_TRANSLATION_ENCODING_CONFLICT;
    case 29U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 1U, 90U, "\033%G", 0U);
        break;
    case 30U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 1U, 90U, "BAD", 0U);
        return OMC_TRANSLATION_ENCODING_CONFLICT;
    case 31U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 1U, 90U, "\033%G", 0U);
        iptc(s, 1U, 90U, "\033%G", 1U);
        return OMC_TRANSLATION_ENCODING_CONFLICT;
    case 32U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 2U, 90U, "\351", 0U);
        o->conflict = OMC_TRANSLATION_REPLACE;
        break;
    case 33U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 2U, 90U, "\351", 0U);
        o->conflict = OMC_TRANSLATION_PRESERVE;
        break;
    case 34U:
    case 35U:
    case 36U:
        s->entries[0].flags |= OMC_ENTRY_FLAG_DELETED;
        iptc(s, 2U, 90U, "Old", 0U);
        iptc(s, 2U, 90U, "Old2", 1U);
        o->conflict = index == 34U   ? OMC_TRANSLATION_REPLACE
                      : index == 35U ? OMC_TRANSLATION_PRESERVE
                                     : OMC_TRANSLATION_FAIL;
        return index == 36U ? OMC_TRANSLATION_NATIVE_CONFLICT : OMC_TRANSLATION_OK;
    case 37U:
        o->max_source_properties = 0U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 38U:
        o->max_source_properties = 1025U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 39U:
        o->max_added_entries = 0U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 40U:
        o->max_added_entries = 7U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 41U:
        o->max_operations = 0U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 42U:
        o->max_operations = 4097U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 43U:
        o->max_total_text_bytes = 0U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 44U:
        o->max_total_text_bytes = 8388609U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 45U:
        o->max_total_text_bytes = 4U;
        return OMC_TRANSLATION_LIMIT;
    case 46U:
        o->max_total_text_bytes = 5U;
        break;
    case 47U:
        o->max_source_properties = 1U;
        o->mappings |= OMC_TRANSLATE_STATE;
        xmp(s, 2U, "State", "State", OMC_ENTRY_FLAG_DIRTY);
        return OMC_TRANSLATION_LIMIT;
    case 48U:
        o->max_source_properties = 1U;
        s->entries[0].flags = 0U;
        xmp(s, 0U, "City", "Clean", 0U);
        return OMC_TRANSLATION_LIMIT;
    case 49U:
        o->max_operations = 1U;
        o->conflict = OMC_TRANSLATION_PRESERVE;
        iptc(s, 2U, 90U, "Old", 0U);
        iptc(s, 2U, 90U, "Old2", 1U);
        return OMC_TRANSLATION_LIMIT;
    case 50U:
        set_text(s, 0U, "caf\303\251", 5U);
        o->max_added_entries = 1U;
        return OMC_TRANSLATION_LIMIT;
    case 51U:
        set_text(s, 0U, "caf\303\251", 5U);
        o->max_operations = 1U;
        return OMC_TRANSLATION_LIMIT;
    case 52U:
    case 53U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 2U, 90U, "Old", 0U);
        o->conflict = OMC_TRANSLATION_REPLACE;
        o->max_total_text_bytes = index == 52U ? 7U : 8U;
        return index == 52U ? OMC_TRANSLATION_LIMIT : OMC_TRANSLATION_OK;
    case 54U:
        o->all_sources = 2;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 55U:
        o->conflict = (omc_translation_conflict)3;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 56U:
        o->mappings |= OMC_TRANSLATE_TITLE;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 57U:
        o->mappings |= OMC_TRANSLATE_STATE;
        iptc(s, 2U, 90U, "Old", 0U);
        xmp(s, 2U, "State", "", OMC_ENTRY_FLAG_DIRTY);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 58U:
        xmp(s, 0U, "City", "", OMC_ENTRY_FLAG_DIRTY);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 59U:
        set_text(s, 0U, "caf\303\251", 5U);
        iptc(s, 2U, 90U, "caf\303\251", 0U);
        break;
    default:
        assert(0);
        break;
    }
    return OMC_TRANSLATION_OK;
}

void
omc_location_check_success(omc_u32 index, const omc_store *out,
                           const omc_translation_res *res)
{
    omc_size i;
    omc_u32 count;
    const omc_entry *e;
    omc_const_bytes bytes;
    count = 0U;
    for (i = 0U; i < out->entry_count; ++i) {
        e = &out->entries[i];
        if (e->key.kind != OMC_KEY_IPTC_DATASET ||
            (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        ++count;
        bytes = omc_arena_view(&out->arena, e->value.u.ref);
        if (index == 12U) {
            assert(e->origin.order_in_block == 2U);
            assert(bytes.size == 5U && memcmp(bytes.data, "Tokyo", 5U) == 0);
        }
        if (index == 0U && e->key.u.iptc_dataset.record == 1U)
            assert(e->origin.order_in_block == 0U);
    }
    if (index == 0U)
        assert(count == 7U && res->groups_translated == 5U && res->entries_added == 6U);
    if (index == 1U || index == 4U || index == 7U || index == 8U || index == 34U)
        assert(count == 0U);
    if (index == 12U)
        assert(count == 1U && res->entries_removed == 1U && res->entries_updated == 1U);
    if (index == 59U)
        assert(count == 2U && res->groups_unchanged == 1U && res->utf8_charset_added);
}

#ifndef OMC_LOCATION_PARITY_FIXTURES
int
main(void)
{
    omc_u32 i;
    omc_store s;
    omc_store out;
    omc_store before;
    omc_store source_before;
    omc_entry entries_before[16];
    omc_u8 bytes_before[2048];
    omc_location_translation_opts opts;
    omc_translation_status expected;
    omc_translation_res res;
    for (i = 0U; i < OMC_LOCATION_CASE_COUNT; ++i) {
        omc_store_init(&s);
        omc_store_init(&out);
        iptc(&out, 2U, 120U, "Sentinel", 0U);
        expected = omc_location_fixture(i, &s, &opts);
        before = out;
        source_before = s;
        assert(s.entry_count <= 16U && s.arena.size <= sizeof(bytes_before));
        memcpy(entries_before, s.entries, s.entry_count * sizeof(*s.entries));
        memcpy(bytes_before, s.arena.data, s.arena.size);
        res = omc_translate_xmp_location(&s, &out, &opts);
        assert(res.status == expected);
        assert(memcmp(&source_before, &s, sizeof(s)) == 0);
        assert(memcmp(entries_before, s.entries, s.entry_count * sizeof(*s.entries)) ==
               0);
        assert(memcmp(bytes_before, s.arena.data, s.arena.size) == 0);
        if (res.status != OMC_TRANSLATION_OK) {
            assert(memcmp(&before, &out, sizeof(out)) == 0);
            assert(memcmp(out.arena.data + out.entries[0].value.u.ref.offset,
                          "Sentinel", 8U) == 0);
        } else
            omc_location_check_success(i, &out, &res);
        assert(omc_translate_xmp_location(&s, &s, &opts).status ==
               OMC_TRANSLATION_INVALID_OPTIONS);
        assert(omc_translate_xmp_location(&s, NULL, &opts).status ==
               OMC_TRANSLATION_INVALID_OPTIONS);
        assert(omc_translate_xmp_location(NULL, &out, &opts).status ==
               OMC_TRANSLATION_INVALID_OPTIONS);
        if (i == 0U) {
            assert(omc_translate_xmp(&s, &out, NULL, NULL).status ==
                   OMC_TRANSLATION_OK);
            assert(out.entry_count == s.entry_count);
            res = omc_translate_xmp_location(&s, &out, NULL);
            assert(res.status == OMC_TRANSLATION_OK);
            omc_location_check_success(i, &out, &res);
        }
        omc_store_fini(&out);
        omc_store_fini(&s);
    }
    return 0;
}
#endif
