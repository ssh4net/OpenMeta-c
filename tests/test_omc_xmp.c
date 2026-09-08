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

static void
test_decode_structured_resource_paths(void)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description "
        "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/' "
        "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/' "
        "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
        "xmlns:stRef='http://ns.adobe.com/xap/1.0/sType/ResourceRef#'>"
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
        "<Iptc4xmpCore:CiEmailWork> editor@example.test "
        "</Iptc4xmpCore:CiEmailWork>"
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType='Resource'>"
        "<Iptc4xmpCore:ProvinceName><rdf:Alt>"
        "<rdf:li xml:lang='x-default'>Tokyo</rdf:li>"
        "</rdf:Alt></Iptc4xmpCore:ProvinceName>"
        "</Iptc4xmpCore:CiAdrRegion>"
        "</Iptc4xmpCore:CreatorContactInfo>"
        "<xmpMM:DerivedFrom rdf:parseType='Resource'>"
        "<stRef:documentID>xmp.did:base</stRef:documentID>"
        "<stRef:instanceID rdf:resource='xmp.iid:base'/>"
        "</xmpMM:DerivedFrom>"
        "<xmpMM:Ingredients><rdf:Seq>"
        "<rdf:li rdf:parseType='Resource'>"
        "<stRef:documentID>xmp.did:ingredient</stRef:documentID>"
        "</rdf:li>"
        "</rdf:Seq></xmpMM:Ingredients>"
        "<Iptc4xmpExt:LocationShown><rdf:Seq>"
        "<rdf:li rdf:parseType='Resource'>"
        "<Iptc4xmpExt:LocationName><rdf:Alt>"
        "<rdf:li xml:lang='x-default'>Kyoto</rdf:li>"
        "<rdf:li xml:lang='fr-FR'>Kyoto FR</rdf:li>"
        "</rdf:Alt></Iptc4xmpExt:LocationName>"
        "<Iptc4xmpExt:LocationId><rdf:Bag>"
        "<rdf:li>loc-001</rdf:li>"
        "<rdf:li>loc-002</rdf:li>"
        "</rdf:Bag></Iptc4xmpExt:LocationId>"
        "</rdf:li>"
        "</rdf:Seq></Iptc4xmpExt:LocationShown>"
        "</rdf:Description>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_store store;
    omc_xmp_res dec;
    const char* schema;
    const char* ext_schema;
    const char* xmpmm_schema;

    schema = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    ext_schema = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    xmpmm_schema = "http://ns.adobe.com/xap/1.0/mm/";
    omc_store_init(&store);
    dec = omc_xmp_dec((const omc_u8*)xmp, sizeof(xmp) - 1U, &store,
                      OMC_INVALID_BLOCK_ID, OMC_ENTRY_FLAG_NONE,
                      (const omc_xmp_opts*)0);

    assert(dec.status == OMC_XMP_OK);
    assert(dec.entries_decoded == 9U);
    assert_text_value(
        &store, find_xmp_entry(&store, schema,
                               "CreatorContactInfo/CiEmailWork"),
        "editor@example.test");
    assert_text_value(
        &store,
        find_xmp_entry(&store, schema,
                       "CreatorContactInfo/CiAdrRegion/ProvinceName"
                       "[@xml:lang=x-default]"),
        "Tokyo");
    assert_text_value(&store,
                      find_xmp_entry(&store, xmpmm_schema,
                                     "DerivedFrom/stRef:documentID"),
                      "xmp.did:base");
    assert_text_value(&store,
                      find_xmp_entry(&store, xmpmm_schema,
                                     "DerivedFrom/stRef:instanceID"),
                      "xmp.iid:base");
    assert_text_value(&store,
                      find_xmp_entry(&store, xmpmm_schema,
                                     "Ingredients[1]/stRef:documentID"),
                      "xmp.did:ingredient");
    assert_text_value(
        &store,
        find_xmp_entry(&store, ext_schema,
                       "LocationShown[1]/LocationName"
                       "[@xml:lang=x-default]"),
        "Kyoto");
    assert_text_value(
        &store,
        find_xmp_entry(&store, ext_schema,
                       "LocationShown[1]/LocationName[@xml:lang=fr-FR]"),
        "Kyoto FR");
    assert_text_value(&store,
                      find_xmp_entry(&store, ext_schema,
                                     "LocationShown[1]/LocationId[1]"),
                      "loc-001");
    assert_text_value(&store,
                      find_xmp_entry(&store, ext_schema,
                                     "LocationShown[1]/LocationId[2]"),
                      "loc-002");

    omc_store_fini(&store);
}

static void test_xml_entities_and_limits(void)
{
    static const char packet[] =
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:v='urn:v' xmlns:a='urn:a' rdf:about='about&amp;id'>"
        "<v:Text>A&amp;B<!--split--><![CDATA[<C>&]]>&#x1F600;</v:Text>"
        "<v:Nested><rdf:Description><a:Child "
        "rdf:resource='a&amp;b'/></rdf:Description></v:Nested>"
        "<v:Empty><rdf:Bag/></v:Empty><v:List><rdf:Alt>"
        "<rdf:li xml:lang=' en-US '>one</rdf:li><rdf:li xml:lang='en_US'>two</rdf:li>"
        "</rdf:Alt></v:List></rdf:Description></rdf:RDF>";
    omc_store store;
    omc_xmp_opts opts;
    omc_xmp_res r;
    omc_u32 i;
    omc_store_init(&store);
    omc_xmp_opts_init(&opts);
    r = omc_xmp_dec((const omc_u8 *)packet, sizeof(packet) - 1U, &store, 17U,
                    OMC_ENTRY_FLAG_DERIVED, &opts);
    assert(r.status == OMC_XMP_OK && r.entries_decoded == 6U);
    assert_text_value(&store, find_xmp_entry(&store, "urn:v", "Text"),
                      "A&B<C>&\360\237\230\200");
    assert_text_value(
        &store, find_xmp_entry(&store, "urn:v", "Nested/nsu_75726e3a61:Child"), "a&b");
    assert_text_value(&store, find_xmp_entry(&store, "urn:v", "Empty"), "");
    assert_text_value(&store, find_xmp_entry(&store, "urn:v", "List[@xml:lang=en-US]"),
                      "one");
    assert_text_value(&store, find_xmp_entry(&store, "urn:v", "List[2]"), "two");
    for (i = 0U; i < store.entry_count; ++i) {
        assert(store.entries[i].origin.block == 17U);
        assert(store.entries[i].origin.order_in_block == i);
        assert(store.entries[i].origin.wire_count == store.entries[i].value.count);
    }
    omc_store_fini(&store);
    omc_store_init(&store);
    opts.limits.max_input_bytes = 10U;
    r = omc_xmp_dec((const omc_u8 *)packet, sizeof(packet) - 1U, &store, 0U, 0U, &opts);
    assert(r.status == OMC_XMP_LIMIT && store.entry_count == 0U &&
           store.arena.size == 0U);
    omc_xmp_opts_init(&opts);
    opts.limits.max_namespace_bytes = 3U;
    r = omc_xmp_dec((const omc_u8 *)packet, sizeof(packet) - 1U, &store, 0U, 0U, &opts);
    assert(r.status == OMC_XMP_LIMIT && store.entry_count == 0U &&
           store.arena.size == 0U);
    omc_xmp_opts_init(&opts);
    opts.limits.max_arena_bytes = 8U;
    r = omc_xmp_dec((const omc_u8 *)packet, sizeof(packet) - 1U, &store, 0U, 0U, &opts);
    assert(r.status == OMC_XMP_LIMIT && store.entry_count == 0U &&
           store.arena.size == 0U);
    omc_xmp_opts_init(&opts);
    opts.limits.max_properties = 1U;
    r = omc_xmp_meas((const omc_u8 *)packet, sizeof(packet) - 1U, &opts);
    assert(r.status == OMC_XMP_LIMIT && r.entries_decoded == 1U);
    omc_store_fini(&store);
}

static void test_malformed_xml(void)
{
    static const char prefix[] = "<rdf:RDF "
                                 "xmlns:rdf='http://www.w3.org/1999/02/"
                                 "22-rdf-syntax-ns#'><rdf:Description xmlns:v='urn:v'>";
    static const char suffix[] = "</rdf:Description></rdf:RDF>";
    static const char *const invalid[] = {"<v:A>text</v:B>",
                                          "<v:A>&unknown;</v:A>",
                                          "<v:A>&#0;</v:A>",
                                          "<v:A>&#x110000;</v:A>",
                                          "<v:A>&#xD800;</v:A>",
                                          "<v:A>&amp</v:A>",
                                          "<v:A>\300\200</v:A>",
                                          "<v:A>\355\240\200</v:A>",
                                          "<v:A>]]></v:A>",
                                          "<v:A rdf:resource='<'/>",
                                          "<v:A v:a='1' v:a='2'/>",
                                          "<v:A v:a='1'v:b='2'/>",
                                          "<unbound:A/>",
                                          "<v:/>",
                                          "<v:A><!--bad--comment--></v:A>",
                                          "<v:A>\001</v:A>"};
    char packet[512];
    omc_size i;
    omc_store store;
    omc_xmp_opts opts;
    omc_xmp_res r;
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        strcpy(packet, prefix);
        strcat(packet, invalid[i]);
        strcat(packet, suffix);
        omc_store_init(&store);
        omc_xmp_opts_init(&opts);
        r = omc_xmp_dec((const omc_u8 *)packet, strlen(packet), &store, 0U, 0U, &opts);
        assert(r.status == OMC_XMP_MALFORMED);
        omc_store_fini(&store);
        omc_store_init(&store);
        opts.malformed_mode = OMC_XMP_MALFORMED_TRUNCATED;
        r = omc_xmp_dec((const omc_u8 *)packet, strlen(packet), &store, 0U, 0U, &opts);
        assert(r.status == OMC_XMP_TRUNCATED);
        omc_store_fini(&store);
    }
}

static void test_large_attribute_table_and_explicit_limit(void)
{
    char packet[4096];
    char attribute[32];
    omc_u32 i;
    omc_xmp_res result;
    omc_xmp_opts opts;
    omc_store store;
    strcpy(packet, "<rdf:RDF "
                   "xmlns:rdf='http://www.w3.org/1999/02/"
                   "22-rdf-syntax-ns#'><rdf:Description xmlns:p='urn:large'");
    for (i = 0U; i < 100U; ++i) {
        sprintf(attribute, " p:a%u='value'", (unsigned)i);
        strcat(packet, attribute);
    }
    strcat(packet, "/></rdf:RDF>");
    omc_store_init(&store);
    omc_xmp_opts_init(&opts);
    result = omc_xmp_dec((const omc_u8 *)packet, strlen(packet), &store, 0U,
                         OMC_ENTRY_FLAG_NONE, &opts);
    assert(result.status == OMC_XMP_OK);
    assert(result.entries_decoded == 100U);
    assert(store.entry_count == 100U);
    omc_store_fini(&store);
    opts.limits.max_attributes_per_element = 64U;
    result = omc_xmp_meas((const omc_u8 *)packet, strlen(packet), &opts);
    assert(result.status == OMC_XMP_LIMIT);
}

int
main(void)
{
    test_large_attribute_table_and_explicit_limit();
    test_xml_entities_and_limits();
    test_malformed_xml();
    test_limit_on_overflowing_depth_cap();
    test_limit_on_overflowing_path_cap();
    test_decode_xmp_subset();
    test_decode_structured_resource_paths();
    return omc_test_finish();
}
