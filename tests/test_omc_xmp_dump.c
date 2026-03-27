#include "omc/omc_edit.h"
#include "omc/omc_xmp_dump.h"

#include <assert.h>
#include <string.h>

static omc_byte_ref
append_bytes(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, strlen(text), &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static int
contains_text(const omc_u8* bytes, omc_size size, const char* text)
{
    omc_size text_size;
    omc_size i;

    text_size = strlen(text);
    if (text_size > size) {
        return 0;
    }

    for (i = 0U; i + text_size <= size; ++i) {
        if (memcmp(bytes + i, text, text_size) == 0) {
            return 1;
        }
    }

    return 0;
}

static void
test_sidecar_truncation_and_success(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref name_ref;
    omc_byte_ref value_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 small[16];
    omc_u8 full[512];

    omc_store_init(&store);
    schema_ref = append_bytes(&store.arena, "http://ns.adobe.com/xap/1.0/");
    name_ref = append_bytes(&store.arena, "CreatorTool");
    value_ref = append_bytes(&store.arena, "OpenMeta-c");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, name_ref);
    omc_val_make_text(&entry.value, value_ref, OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);

    status = omc_xmp_dump_sidecar(&store, small, sizeof(small), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_TRUNCATED);
    assert(res.needed > sizeof(small));
    assert(res.entries == 1U);

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(res.entries == 1U);
    assert(contains_text(full, (omc_size)res.written, "<x:xmpmeta"));
    assert(contains_text(full, (omc_size)res.written,
                         "<xmp:CreatorTool>OpenMeta-c</xmp:CreatorTool>"));

    omc_store_fini(&store);
}

static void
test_sidecar_uses_edit_commit_output(void)
{
    omc_store base;
    omc_store merged;
    omc_edit edit;
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref name_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[512];

    omc_store_init(&base);
    omc_store_init(&merged);
    omc_edit_init(&edit);

    schema_ref = append_bytes(&edit.arena, "urn:test:writeback/");
    name_ref = append_bytes(&edit.arena, "Thing");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, name_ref);
    omc_val_make_u32(&entry.value, 42U);
    status = omc_edit_add_entry(&edit, &entry);
    assert(status == OMC_STATUS_OK);

    status = omc_edit_commit(&base, &edit, 1U, &merged);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    status = omc_xmp_dump_sidecar(&merged, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(full, (omc_size)res.written,
                         "xmlns:ns1=\"urn:test:writeback/\""));
    assert(contains_text(full, (omc_size)res.written,
                         "<ns1:Thing>42</ns1:Thing>"));

    omc_edit_fini(&edit);
    omc_store_fini(&merged);
    omc_store_fini(&base);
}

static void
test_sidecar_limits_and_skips_unsupported_properties(void)
{
    omc_store store;
    omc_entry entry;
    omc_byte_ref schema_ref;
    omc_byte_ref first_name_ref;
    omc_byte_ref second_name_ref;
    omc_byte_ref invalid_name_ref;
    omc_byte_ref text_ref;
    omc_xmp_dump_res res;
    omc_xmp_sidecar_opts opts;
    omc_status status;
    omc_u8 full[512];

    omc_store_init(&store);
    schema_ref = append_bytes(&store.arena, "http://purl.org/dc/elements/1.1/");
    first_name_ref = append_bytes(&store.arena, "title");
    second_name_ref = append_bytes(&store.arena, "creator");
    invalid_name_ref = append_bytes(&store.arena, "bad[1]");
    text_ref = append_bytes(&store.arena, "hello");

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, invalid_name_ref);
    omc_val_make_text(&entry.value, text_ref, OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, first_name_ref);
    omc_val_make_text(&entry.value, text_ref, OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, schema_ref, second_name_ref);
    omc_val_make_u16(&entry.value, 5U);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_sidecar_opts_init(&opts);
    opts.limits.max_entries = 1U;

    status = omc_xmp_dump_sidecar(&store, full, sizeof(full), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_LIMIT);
    assert(res.entries == 1U);
    assert(contains_text(full, (omc_size)res.written, "<dc:title>hello</dc:title>"));
    assert(!contains_text(full, (omc_size)res.written, "bad[1]"));
    assert(!contains_text(full, (omc_size)res.written, "<dc:creator>5</dc:creator>"));

    omc_store_fini(&store);
}

int
main(void)
{
    test_sidecar_truncation_and_success();
    test_sidecar_uses_edit_commit_output();
    test_sidecar_limits_and_skips_unsupported_properties();
    return 0;
}
