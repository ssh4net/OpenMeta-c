#include "omc_iptc_fixture.h"
#include "omc/omc_xmp_dump.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DC "http://purl.org/dc/elements/1.1/"
#define PS "http://ns.adobe.com/photoshop/1.0/"
#define CORE "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"
const omc_iptc_test_field omc_iptc_test_fields[20] = {
    {DC, "title[@xml:lang=x-default]", "Caf\303\251", 5U, 64U},
    {DC, "description[@xml:lang=x-default]", "Caption", 120U, 2000U},
    {DC, "creator[1]", "Author", 80U, 32U},
    {DC, "subject[1]", "Keyword", 25U, 64U},
    {DC, "rights[@xml:lang=x-default]", "Copyright", 116U, 128U},
    {PS, "Credit", "Credit", 110U, 32U},
    {PS, "Source", "Source", 115U, 32U},
    {PS, "City", "Tokyo", 90U, 32U},
    {CORE, "Location", "Studio", 92U, 32U},
    {PS, "State", "State", 95U, 32U},
    {PS, "Country", "Japan", 101U, 64U},
    {CORE, "CountryCode", "JP", 100U, 3U},
    {PS, "Headline", "Headline", 105U, 256U},
    {PS, "Instructions", "Instructions", 40U, 256U},
    {PS, "TransmissionReference", "Reference", 103U, 32U},
    {PS, "AuthorsPosition", "Photographer", 85U, 32U},
    {PS, "CaptionWriter", "Writer", 122U, 32U},
    {PS, "Category", "AbC", 15U, 3U},
    {PS, "SupplementalCategories[1]", "Nature", 20U, 32U},
    {PS, "Urgency", "8", 10U, 1U}};

static omc_byte_ref
put(omc_store *s, const void *data, omc_size size)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&s->arena, data, size, &ref) == OMC_STATUS_OK);
    return ref;
}

static void
set_text(omc_store *s, omc_entry_id id, const void *data, omc_size size)
{
    omc_val_make_text(&s->entries[id].value, put(s, data, size), OMC_TEXT_UTF8);
}

static omc_entry_id
xmp(omc_store *s, omc_u32 field, const char *path, const char *value, omc_u32 flags)
{
    omc_entry e;
    omc_entry_id id;
    const omc_iptc_test_field *f;
    f = &omc_iptc_test_fields[field];
    if (path == NULL)
        path = f->path;
    if (value == NULL)
        value = f->value;
    memset(&e, 0, sizeof(e));
    omc_key_make_xmp_property(&e.key, put(s, f->ns, strlen(f->ns)),
                              put(s, path, strlen(path)));
    omc_val_make_text(&e.value, put(s, value, strlen(value)), OMC_TEXT_UTF8);
    e.flags = flags;
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.origin.order_in_block = 90U - (omc_u32)s->entry_count;
    e.origin.wire_type_name = put(s, path, strlen(path));
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

static void
all_fields(omc_store *s)
{
    omc_u32 i;
    /* Physical and provenance order deliberately oppose mapping/index order. */
    xmp(s, 2U, "creator[3]", "Third", OMC_ENTRY_FLAG_DIRTY);
    xmp(s, 3U, "subject[2]", "Keyword", OMC_ENTRY_FLAG_DIRTY);
    xmp(s, 18U, "SupplementalCategories[2]", "Nature", OMC_ENTRY_FLAG_DIRTY);
    for (i = 20U; i != 0U; --i)
        xmp(s, i - 1U, NULL, NULL, OMC_ENTRY_FLAG_DIRTY);
}

omc_translation_status
omc_iptc_fixture(omc_u32 index, omc_store *s, omc_iptc_translation_opts *o)
{
    omc_u32 i;
    omc_u32 field;
    omc_entry_id id;
    omc_size n;
    char value[2001];
    omc_iptc_translation_opts_init(o);
    if (index < 60U) {
        field = index / 3U;
        o->mappings = 1U << field;
        id = xmp(s, field, NULL, NULL, OMC_ENTRY_FLAG_DIRTY);
        /* A bad unselected source must not affect any selected group. */
        xmp(s, (field + 1U) % 20U, NULL, "", OMC_ENTRY_FLAG_DIRTY);
        if (index % 3U != 0U) {
            n = omc_iptc_test_fields[field].max_bytes + (index % 3U == 2U);
            memset(value, field == 19U ? '8' : 'A', n);
            set_text(s, id, value, n);
        }
        return index % 3U == 2U ? OMC_TRANSLATION_VALUE_TOO_LONG : OMC_TRANSLATION_OK;
    }
    if (index == 60U || (index >= 104U && index <= 111U)) {
        all_fields(s);
        if (index == 104U)
            o->max_source_properties = 22U;
        if (index == 105U)
            o->max_added_entries = 23U;
        if (index == 106U)
            o->max_operations = 23U;
        if (index == 107U)
            o->max_total_text_bytes = 10U;
        if (index >= 108U) {
            for (i = 0U; i < (omc_u32)s->entry_count; ++i)
                s->entries[i].flags |= OMC_ENTRY_FLAG_DELETED;
            for (i = 0U; i < 20U; ++i)
                iptc(s, 2U, omc_iptc_test_fields[i].dataset, "Old", i);
            iptc(s, 2U, 130U, "Unowned", 0U);
            o->conflict = index == 109U   ? OMC_TRANSLATION_PRESERVE
                          : index == 110U ? OMC_TRANSLATION_FAIL
                                          : OMC_TRANSLATION_REPLACE;
            if (index == 111U)
                o->mappings = OMC_IPTC_TRANSLATE_EDITORIAL;
        }
        if (index >= 104U && index <= 107U)
            return OMC_TRANSLATION_LIMIT;
        if (index == 110U)
            return OMC_TRANSLATION_NATIVE_CONFLICT;
        return OMC_TRANSLATION_OK;
    }
    o->mappings = OMC_IPTC_TRANSLATE_SUPPLEMENTAL_CATEGORIES;
    xmp(s, 18U, "SupplementalCategories[9]", "Third", OMC_ENTRY_FLAG_DIRTY);
    xmp(s, 18U, "SupplementalCategories[1]", "First", OMC_ENTRY_FLAG_DIRTY);
    xmp(s, 18U, "SupplementalCategories[4]", "Second", OMC_ENTRY_FLAG_DIRTY);
    switch (index) {
    case 61U:
        break;
    case 62U:
    case 63U:
    case 64U:
        iptc(s, 2U, 20U, "First", 0xFFFFFFFFU);
        if (index == 63U)
            iptc(s, 2U, 20U, "Second", 0xFFFFFFFFU);
        if (index == 64U) {
            iptc(s, 2U, 20U, "Second", 1U);
            iptc(s, 2U, 20U, "Third", 2U);
            iptc(s, 2U, 20U, "Excess", 0xFFFFFFFFU);
        }
        o->conflict = OMC_TRANSLATION_REPLACE;
        break;
    case 65U:
        s->entries[0].flags = 0U;
        s->entries[1].flags = 0U;
        break;
    case 66U:
        s->entries[1].flags |= OMC_ENTRY_FLAG_DELETED;
        break;
    case 67U:
        xmp(s, 18U, "SupplementalCategories[04]", "Duplicate", OMC_ENTRY_FLAG_DIRTY);
        return OMC_TRANSLATION_AMBIGUOUS_SOURCE;
    case 68U:
        xmp(s, 18U, "SupplementalCategories[02]", "Leading zero", OMC_ENTRY_FLAG_DIRTY);
        break;
    case 69U:
        xmp(s, 18U, "SupplementalCategories", "", OMC_ENTRY_FLAG_DIRTY);
        xmp(s, 18U, "SupplementalCategories[0]", "", OMC_ENTRY_FLAG_DIRTY);
        xmp(s, 18U, "SupplementalCategories[4294967296]", "", OMC_ENTRY_FLAG_DIRTY);
        xmp(s, 18U, "SupplementalCategories[2]/?foo", "", OMC_ENTRY_FLAG_DIRTY);
        break;
    case 70U:
        xmp(s, 18U, "SupplementalCategories[4]", "",
            OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
        break;
    case 71U:
        xmp(s, 18U, "SupplementalCategories[4]", "Duplicate", OMC_ENTRY_FLAG_DIRTY);
        set_text(s, 0U, "", 0U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 72U:
        set_text(s, 1U, "A\0B", 3U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 73U:
        set_text(s, 1U, "\300\257", 2U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 74U:
        set_text(s, 1U, "A\001B", 3U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 75U:
        set_text(s, 1U, "\357\277\277", 3U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 76U:
        omc_val_make_u32(&s->entries[1].value, 8U);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 77U:
        s->entries[1].value.kind = OMC_VAL_BYTES;
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 78U:
        set_text(s, 1U, "A\tB\nC\rD", 7U);
        break;
    case 79U:
    case 80U:
    case 81U:
    case 82U:
    case 83U:
    case 84U:
    case 85U:
    case 86U:
    case 87U:
    case 88U:
    case 89U:
        o->mappings = OMC_IPTC_TRANSLATE_URGENCY;
        id = xmp(s, 19U, NULL, NULL, OMC_ENTRY_FLAG_DIRTY);
        if (index == 79U)
            omc_val_make_i64(&s->entries[id].value, 1);
        if (index == 80U)
            omc_val_make_u64(&s->entries[id].value, 8U);
        if (index == 81U)
            omc_val_make_i64(&s->entries[id].value, -1);
        if (index == 82U)
            omc_val_make_u64(&s->entries[id].value, 9U);
        if (index == 83U)
            omc_val_make_u64(&s->entries[id].value, (omc_u64)0xFFFFFFFFU + 1U);
        if (index == 84U)
            omc_val_make_f64_bits(&s->entries[id].value, (omc_u64)0x3FF00000U << 32U);
        if (index == 85U)
            omc_val_make_array(&s->entries[id].value, OMC_ELEM_U8, 1U,
                               put(s, "\001", 1U), OMC_BYTE_ORDER_NATIVE);
        if (index == 86U)
            set_text(s, id, "0", 1U);
        if (index == 87U)
            set_text(s, id, "9", 1U);
        if (index == 88U)
            set_text(s, id, "1", 1U);
        if (index == 89U)
            set_text(s, id, "01", 2U);
        if (index == 89U)
            return OMC_TRANSLATION_VALUE_TOO_LONG;
        return index >= 81U && index <= 87U ? OMC_TRANSLATION_INVALID_SOURCE
                                            : OMC_TRANSLATION_OK;
    case 90U:
        o->mappings = OMC_IPTC_TRANSLATE_CATEGORY;
        xmp(s, 17U, NULL, "A1", OMC_ENTRY_FLAG_DIRTY);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 91U:
    case 92U:
    case 93U:
    case 94U:
    case 95U:
    case 96U:
    case 97U:
        set_text(s, 1U, "Caf\303\251", 5U);
        if (index == 91U)
            iptc(s, 2U, 130U, "ASCII", 0U);
        if (index == 92U)
            iptc(s, 2U, 130U, "\351", 0U);
        if (index == 93U) {
            id = iptc(s, 2U, 130U, "", 0U);
            omc_val_make_u32(&s->entries[id].value, 2U);
        }
        if (index == 94U || index == 96U)
            iptc(s, 1U, 90U, "\033%G", 0U);
        if (index == 95U)
            iptc(s, 1U, 90U, "BAD", 0U);
        if (index == 96U)
            iptc(s, 1U, 90U, "\033%G", 1U);
        if (index == 97U) {
            iptc(s, 2U, 20U, "\351", 0U);
            o->conflict = OMC_TRANSLATION_REPLACE;
        }
        return index == 92U || index == 93U || index == 95U || index == 96U
                   ? OMC_TRANSLATION_ENCODING_CONFLICT
                   : OMC_TRANSLATION_OK;
    case 98U:
        o->max_source_properties = 2U;
        return OMC_TRANSLATION_LIMIT;
    case 99U:
        o->max_source_properties = 2U;
        for (i = 0U; i < 3U; ++i)
            s->entries[i].flags = 0U;
        return OMC_TRANSLATION_LIMIT;
    case 100U:
        o->max_operations = 2U;
        for (i = 0U; i < 3U; ++i)
            iptc(s, 2U, 20U, "Old", i);
        o->conflict = OMC_TRANSLATION_PRESERVE;
        return OMC_TRANSLATION_LIMIT;
    case 101U:
    case 102U:
        for (i = 0U; i < 3U; ++i)
            s->entries[i].flags = 0U;
        if (index == 102U)
            o->all_sources = 1;
        break;
    case 103U:
        iptc(s, 2U, 20U, "Conflict", 0U);
        o->mappings |= OMC_IPTC_TRANSLATE_URGENCY;
        xmp(s, 19U, NULL, "0", OMC_ENTRY_FLAG_DIRTY);
        return OMC_TRANSLATION_INVALID_SOURCE;
    case 112U:
        o->mappings = 0U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 113U:
        o->mappings |= 0x80000000U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 114U:
        o->max_source_properties = 1025U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 115U:
        o->max_added_entries = 1026U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 116U:
        o->max_operations = 4097U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 117U:
        o->max_total_text_bytes = 0U;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 118U:
        o->all_sources = 2;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    case 119U:
        o->conflict = (omc_translation_conflict)3;
        return OMC_TRANSLATION_INVALID_OPTIONS;
    default:
        assert(0);
        break;
    }
    return OMC_TRANSLATION_OK;
}

void
omc_iptc_check_success(omc_u32 index, const omc_store *out,
                       const omc_translation_res *res)
{
    omc_size i;
    omc_u32 count;
    const omc_entry *e;
    count = 0U;
    for (i = 0U; i < out->entry_count; ++i) {
        e = &out->entries[i];
        if (e->key.kind != OMC_KEY_IPTC_DATASET || (e->flags & OMC_ENTRY_FLAG_DELETED))
            continue;
        ++count;
        if ((index == 60U || index == 61U) && e->key.u.iptc_dataset.record == 2U &&
            (e->key.u.iptc_dataset.dataset == 20U ||
             e->key.u.iptc_dataset.dataset == 25U ||
             e->key.u.iptc_dataset.dataset == 80U))
            assert(e->origin.order_in_block == 0U);
        if ((index == 62U || index == 63U) && e->key.u.iptc_dataset.dataset == 20U)
            assert(e->origin.order_in_block == 0xFFFFFFFFU);
    }
    if (index < 60U)
        assert(res->groups_translated == 1U);
    if (index == 60U)
        assert(count == 24U && res->groups_translated == 20U &&
               res->utf8_charset_added);
    if (index == 64U)
        assert(count == 3U && res->entries_removed == 1U && res->entries_updated == 3U);
    if (index == 101U)
        assert(count == 0U);
    if (index == 108U)
        assert(count == 1U && res->entries_removed == 20U);
    if (index == 109U)
        assert(count == 21U && res->groups_preserved == 20U);
    if (index == 111U)
        assert(count == 18U && res->entries_removed == 3U);
}

#ifndef OMC_IPTC_PARITY_FIXTURES
static void
check_projection(const omc_store *out)
{
    omc_xmp_sidecar_opts opts;
    omc_xmp_dump_res res;
    omc_u8 xml[8192];
    static const char *const expected[] = {
        "<photoshop:Urgency>8</photoshop:Urgency>",
        "<photoshop:Instructions>Instructions</photoshop:Instructions>",
        "<photoshop:TransmissionReference>Reference</photoshop:TransmissionReference>",
        "<photoshop:AuthorsPosition>Photographer</photoshop:AuthorsPosition>",
        "<photoshop:Country>Japan</photoshop:Country>",
        "<Iptc4xmpCore:CountryCode>JP</Iptc4xmpCore:CountryCode>"};
    omc_size i;
    omc_xmp_sidecar_opts_init(&opts);
    opts.include_existing_xmp = 0;
    opts.include_exif = 0;
    opts.include_iptc = 1;
    assert(omc_xmp_dump_sidecar(out, xml, sizeof(xml) - 1U, &opts, &res) ==
           OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK && res.written < sizeof(xml));
    xml[(omc_size)res.written] = 0U;
    for (i = 0U; i < sizeof(expected) / sizeof(expected[0]); ++i)
        assert(strstr((const char *)xml, expected[i]) != NULL);
}

static void
check_legacy_order(void)
{
    omc_u32 field;
    omc_u32 growth;
    omc_store s;
    omc_store out;
    omc_store again;
    omc_translation_opts opts;
    omc_translation_res res;
    omc_size i;
    omc_u32 count;
    omc_const_bytes b;
    static const char *const expected[] = {"First", "Second", "Third"};
    for (field = 2U; field <= 3U; ++field) {
        for (growth = 0U; growth < 3U; ++growth) {
            omc_store_init(&s);
            omc_store_init(&out);
            omc_store_init(&again);
            xmp(&s, field, field == 2U ? "creator[9]" : "subject[9]", "Third",
                OMC_ENTRY_FLAG_DIRTY);
            xmp(&s, field, field == 2U ? "creator[1]" : "subject[1]", "First",
                OMC_ENTRY_FLAG_DIRTY);
            xmp(&s, field, field == 2U ? "creator[4]" : "subject[4]", "Second",
                OMC_ENTRY_FLAG_DIRTY);
            if (growth != 0U)
                iptc(&s, 2U, omc_iptc_test_fields[field].dataset, "First", 0xFFFFFFFFU);
            if (growth == 2U)
                iptc(&s, 2U, omc_iptc_test_fields[field].dataset, "Earlier", 2U);
            omc_translation_opts_init(&opts);
            opts.mappings =
                field == 2U ? OMC_TRANSLATE_CREATORS : OMC_TRANSLATE_KEYWORDS;
            opts.conflict = OMC_TRANSLATION_REPLACE;
            res = omc_translate_xmp(&s, &out, &opts, NULL);
            assert(res.status == OMC_TRANSLATION_OK);
            count = 0U;
            for (i = 0U; i < out.entry_count; ++i) {
                if (out.entries[i].key.kind != OMC_KEY_IPTC_DATASET)
                    continue;
                b = omc_arena_view(&out.arena, out.entries[i].value.u.ref);
                /* Physical order differs from rank order in the final variant. */
                assert(
                    b.size ==
                    strlen(expected[growth == 2U && count < 2U ? 1U - count : count]));
                assert(memcmp(b.data,
                              expected[growth == 2U && count < 2U ? 1U - count : count],
                              b.size) == 0);
                ++count;
            }
            assert(count == 3U);
            res = omc_translate_xmp(&out, &again, &opts, NULL);
            assert(res.status == OMC_TRANSLATION_OK && res.entries_added == 0U &&
                   res.entries_updated == 0U && res.entries_removed == 0U);
            omc_store_fini(&again);
            omc_store_fini(&out);
            omc_store_fini(&s);
        }
    }
}

int
main(void)
{
    omc_u32 i;
    omc_store s, out, before, source_before, again;
    omc_entry entries_before[64];
    omc_u8 bytes_before[8192];
    omc_iptc_translation_opts opts;
    omc_translation_status expected;
    omc_translation_res res;
    for (i = 0U; i < OMC_IPTC_CASE_COUNT; ++i) {
        printf("IPTC case %u\n", i);
        fflush(stdout);
        omc_store_init(&s);
        omc_store_init(&out);
        omc_store_init(&again);
        iptc(&out, 2U, 130U, "Sentinel", 0U);
        expected = omc_iptc_fixture(i, &s, &opts);
        before = out;
        source_before = s;
        assert(s.entry_count <= 64U && s.arena.size <= sizeof(bytes_before));
        memcpy(entries_before, s.entries, s.entry_count * sizeof(*s.entries));
        memcpy(bytes_before, s.arena.data, s.arena.size);
        res = omc_translate_xmp_iptc(&s, &out, &opts);
        assert(res.status == expected);
        assert(memcmp(&source_before, &s, sizeof(s)) == 0);
        assert(memcmp(entries_before, s.entries, s.entry_count * sizeof(*s.entries)) ==
               0);
        assert(memcmp(bytes_before, s.arena.data, s.arena.size) == 0);
        if (res.status != OMC_TRANSLATION_OK) {
            assert(memcmp(&before, &out, sizeof(out)) == 0);
            assert(memcmp(out.arena.data + out.entries[0].value.u.ref.offset,
                          "Sentinel", 8U) == 0);
        } else {
            omc_iptc_check_success(i, &out, &res);
            res = omc_translate_xmp_iptc(&out, &again, &opts);
            assert(res.status == OMC_TRANSLATION_OK && res.entries_added == 0U &&
                   res.entries_updated == 0U && res.entries_removed == 0U);
            if (i == 60U) {
                check_projection(&out);
                res = omc_translate_xmp_iptc(&s, &again, NULL);
                assert(res.status == OMC_TRANSLATION_OK &&
                       res.groups_translated == 20U);
            }
        }
        assert(omc_translate_xmp_iptc(&s, &s, &opts).status ==
               OMC_TRANSLATION_INVALID_OPTIONS);
        assert(omc_translate_xmp_iptc(&s, NULL, &opts).status ==
               OMC_TRANSLATION_INVALID_OPTIONS);
        assert(omc_translate_xmp_iptc(NULL, &out, &opts).status ==
               OMC_TRANSLATION_INVALID_OPTIONS);
        omc_store_fini(&again);
        omc_store_fini(&out);
        omc_store_fini(&s);
    }
    check_legacy_order();
    return 0;
}
#endif
