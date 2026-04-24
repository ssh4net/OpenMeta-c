#include "omc/omc_xmp.h"

#include "omc_test_assert.h"
#include <string.h>

static const omc_entry*
find_xmp_entry(const omc_store* store, const char* schema_ns,
               const char* property_path)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ns_view;
        omc_const_bytes path_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        ns_view = omc_arena_view(&store->arena,
                                 entry->key.u.xmp_property.schema_ns);
        path_view = omc_arena_view(&store->arena,
                                   entry->key.u.xmp_property.property_path);
        if (ns_view.size == strlen(schema_ns)
            && path_view.size == strlen(property_path)
            && memcmp(ns_view.data, schema_ns, ns_view.size) == 0
            && memcmp(path_view.data, property_path, path_view.size) == 0) {
            return entry;
        }
    }

    return (const omc_entry*)0;
}

static void
assert_text_value(const omc_store* store, const omc_entry* entry,
                  const char* expect)
{
    omc_const_bytes value;
    omc_size expect_size;

    OMC_TEST_REQUIRE(entry != (const omc_entry*)0);
    OMC_TEST_REQUIRE_U64_EQ(entry->value.kind, OMC_VAL_TEXT);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    expect_size = strlen(expect);
    OMC_TEST_CHECK_SIZE_EQ(value.size, expect_size);
    OMC_TEST_CHECK_MEM_EQ(value.data, value.size, expect, expect_size);
}

static void
test_limit_on_overflowing_depth_cap(void)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/'>"
        "<dc:title>Title</dc:title>"
        "</rdf:Description>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_xmp_opts opts;
    omc_xmp_res res;

    omc_xmp_opts_init(&opts);
    opts.limits.max_depth = (omc_u32)~(omc_u32)0;

    res = omc_xmp_meas((const omc_u8*)xmp, sizeof(xmp) - 1U, &opts);
    assert(res.status == OMC_XMP_LIMIT);
    assert(res.entries_decoded == 0U);
}

static void
test_limit_on_overflowing_path_cap(void)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/'>"
        "<dc:title>Title</dc:title>"
        "</rdf:Description>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_xmp_opts opts;
    omc_xmp_res res;

    omc_xmp_opts_init(&opts);
    opts.limits.max_path_bytes = (omc_u32)~(omc_u32)0;

    res = omc_xmp_meas((const omc_u8*)xmp, sizeof(xmp) - 1U, &opts);
    assert(res.status == OMC_XMP_LIMIT);
    assert(res.entries_decoded == 0U);
}

static void
test_decode_xmp_subset(void)
{
    static const char xmp[] =
        "<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'?>"
        "<x:xmpmeta xmlns:x='adobe:ns:meta/' x:xmptk='OpenMeta'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description "
        "xmlns:dc='http://purl.org/dc/elements/1.1/' "
        "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
        "xmp:CreatorTool='OpenMeta'>"
        "<dc:creator><rdf:Seq>"
        "<rdf:li>John</rdf:li><rdf:li>Jane</rdf:li>"
        "</rdf:Seq></dc:creator>"
        "<xmp:Rating> 5 </xmp:Rating>"
        "<xmpMM:InstanceID rdf:resource='uuid:123'/>"
        "</rdf:Description>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_store store;
    omc_xmp_res meas;
    omc_xmp_res dec;

    omc_store_init(&store);
    meas = omc_xmp_meas((const omc_u8*)xmp, sizeof(xmp) - 1U,
                        (const omc_xmp_opts*)0);
    dec = omc_xmp_dec((const omc_u8*)xmp, sizeof(xmp) - 1U, &store,
                      OMC_INVALID_BLOCK_ID, OMC_ENTRY_FLAG_NONE,
                      (const omc_xmp_opts*)0);

    assert(meas.status == OMC_XMP_OK);
    assert(dec.status == OMC_XMP_OK);
    assert(dec.entries_decoded == 6U);
    assert(dec.entries_decoded == meas.entries_decoded);

    assert_text_value(&store,
                      find_xmp_entry(&store, "adobe:ns:meta/", "XMPToolkit"),
                      "OpenMeta");
    assert_text_value(
        &store,
        find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta");
    assert_text_value(&store,
                      find_xmp_entry(&store,
                                     "http://purl.org/dc/elements/1.1/",
                                     "creator[1]"),
                      "John");
    assert_text_value(&store,
                      find_xmp_entry(&store,
                                     "http://purl.org/dc/elements/1.1/",
                                     "creator[2]"),
                      "Jane");
    assert_text_value(
        &store, find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/",
                               "Rating"),
        "5");
    assert_text_value(
        &store,
        find_xmp_entry(&store, "http://ns.adobe.com/xap/1.0/mm/",
                       "InstanceID"),
        "uuid:123");

    omc_store_fini(&store);
}

int
main(void)
{
    test_limit_on_overflowing_depth_cap();
    test_limit_on_overflowing_path_cap();
    test_decode_xmp_subset();
    return omc_test_finish();
}
