#include "omc/omc_read.h"
#include "omc/omc_transfer.h"

#include <assert.h>
#include <string.h>

static omc_byte_ref
append_store_bytes(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, strlen(text), &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_u16be(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    out[*io_size + 0U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 1U] = (omc_u8)(value & 0xFFU);
    *io_size += 2U;
}

static void
append_raw(omc_u8* out, omc_size* io_size, const void* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
append_text(omc_u8* out, omc_size* io_size, const char* text)
{
    append_raw(out, io_size, text, strlen(text));
}

static void
append_jpeg_segment(omc_u8* out, omc_size* io_size, omc_u8 marker,
                    const omc_u8* payload, omc_size payload_size)
{
    append_u8(out, io_size, 0xFFU);
    append_u8(out, io_size, marker);
    append_u16be(out, io_size, (omc_u16)(payload_size + 2U));
    append_raw(out, io_size, payload, payload_size);
}

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

static const omc_entry*
find_comment_entry(const omc_store* store)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind == OMC_KEY_COMMENT) {
            return &store->entries[i];
        }
    }
    return (const omc_entry*)0;
}

static void
assert_text_value(const omc_store* store, const omc_entry* entry,
                  const char* expect)
{
    omc_const_bytes value;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(value.size == strlen(expect));
    assert(memcmp(value.data, expect, value.size) == 0);
}

static void
build_store_with_creator_tool(omc_store* store, const char* tool)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_store_bytes(&store->arena, "http://ns.adobe.com/xap/1.0/"),
        append_store_bytes(&store->arena, "CreatorTool"));
    omc_val_make_text(&entry.value, append_store_bytes(&store->arena, tool),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    assert(status == OMC_STATUS_OK);
}

static omc_size
make_test_jpeg_with_old_xmp_and_comment(omc_u8* out)
{
    static const char jfif[] = { 'J', 'F', 'I', 'F', 0x00U, 0x01U, 0x02U,
                                 0x00U, 0x00U, 0x01U, 0x00U, 0x01U, 0x00U,
                                 0x00U };
    static const char comment[] = "Preserve me";
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 xmp_payload[512];
    omc_size xmp_size;
    omc_size size;

    xmp_size = 0U;
    append_text(xmp_payload, &xmp_size, "http://ns.adobe.com/xap/1.0/");
    append_u8(xmp_payload, &xmp_size, 0U);
    append_text(xmp_payload, &xmp_size, xmp);

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);
    append_jpeg_segment(out, &size, 0xE0U, (const omc_u8*)jfif,
                        sizeof(jfif));
    append_jpeg_segment(out, &size, 0xE1U, xmp_payload, xmp_size);
    append_jpeg_segment(out, &size, 0xFEU, (const omc_u8*)comment,
                        sizeof(comment) - 1U);
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD9U);
    return size;
}

static void
read_store_from_bytes(const omc_u8* bytes, omc_size size, omc_store* out)
{
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload[512];
    omc_u32 payload_scratch[8];
    omc_read_res res;

    res = omc_read_simple(bytes, size, out, blocks, 8U, ifds, 8U, payload,
                          sizeof(payload), payload_scratch, 8U,
                          (const omc_read_opts*)0);
    assert(res.entries_added >= 1U);
}

static void
test_transfer_prepare_allows_sidecar_only_preserve_for_gif(void)
{
    static const omc_u8 gif[] = { 'G', 'I', 'F', '8', '9', 'a' };
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_store sidecar_store;
    omc_status status;

    omc_store_init(&store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_GIF;
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;

    status = omc_transfer_prepare(gif, sizeof(gif), &store, &opts, &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.route_count == 1U);
    assert(bundle.embedded_action == OMC_TRANSFER_EMBEDDED_NONE);
    assert(bundle.sidecar_requested);
    assert(!bundle.embedded_supported);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_OK);
    assert(exec.route_count == 1U);
    assert(exec.routes[0].kind == OMC_TRANSFER_ROUTE_SIDECAR_XMP);

    status = omc_transfer_execute((const omc_u8*)0, 0U, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(!res.edited_present);
    assert(res.sidecar_present);
    assert(edited_out.size == 0U);

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_prepare_and_execute_jpeg_embedded_and_sidecar(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.format == OMC_SCAN_FMT_JPEG);
    assert(bundle.existing_xmp_blocks == 1U);
    assert(bundle.route_count == 2U);
    assert(bundle.embedded_action == OMC_TRANSFER_EMBEDDED_REWRITE);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_OK);
    assert(exec.route_count == 2U);
    assert(exec.routes[0].kind == OMC_TRANSFER_ROUTE_EMBEDDED_XMP);
    assert(exec.routes[0].embedded_action == OMC_TRANSFER_EMBEDDED_REWRITE);
    assert(exec.routes[1].kind == OMC_TRANSFER_ROUTE_SIDECAR_XMP);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.edited_present);
    assert(res.sidecar_present);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_execute_jpeg_sidecar_only_strip(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;

    status = omc_transfer_prepare(file_bytes, file_size, &store, &opts,
                                  &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.route_count == 2U);
    assert(bundle.embedded_action == OMC_TRANSFER_EMBEDDED_STRIP);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.routes[0].kind == OMC_TRANSFER_ROUTE_EMBEDDED_XMP);
    assert(exec.routes[0].embedded_action == OMC_TRANSFER_EMBEDDED_STRIP);
    assert(exec.routes[1].kind == OMC_TRANSFER_ROUTE_SIDECAR_XMP);

    status = omc_transfer_execute(file_bytes, file_size, &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 0U);

    read_store_from_bytes(edited_out.data, edited_out.size, &edited_store);
    assert(find_xmp_entry(&edited_store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool")
           == (const omc_entry*)0);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_store_from_bytes(sidecar_out.data, sidecar_out.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

static void
test_transfer_prepare_reports_unsupported_for_embedded_only_gif(void)
{
    static const omc_u8 gif[] = { 'G', 'I', 'F', '8', '9', 'a' };
    omc_store store;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_GIF;
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;

    status = omc_transfer_prepare(gif, sizeof(gif), &store, &opts, &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_UNSUPPORTED);

    status = omc_transfer_compile(&bundle, &exec);
    assert(status == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_UNSUPPORTED);
    assert(exec.route_count == 0U);

    status = omc_transfer_execute(gif, sizeof(gif), &store, &edited_out,
                                  &sidecar_out, &exec, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(edited_out.size == 0U);
    assert(sidecar_out.size == 0U);

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&store);
}

int
main(void)
{
    test_transfer_prepare_allows_sidecar_only_preserve_for_gif();
    test_transfer_prepare_and_execute_jpeg_embedded_and_sidecar();
    test_transfer_execute_jpeg_sidecar_only_strip();
    test_transfer_prepare_reports_unsupported_for_embedded_only_gif();
    return 0;
}
