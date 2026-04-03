#include "omc/omc_read.h"
#include "omc/omc_xmp_apply.h"

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
append_u16le(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    out[*io_size + 0U] = (omc_u8)(value & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    *io_size += 2U;
}

static void
append_u32le(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    out[*io_size + 0U] = (omc_u8)(value & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)((value >> 24) & 0xFFU);
    *io_size += 4U;
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

static omc_size
count_xmp_entries(const omc_store* store, const char* schema_ns,
                  const char* property_path)
{
    omc_size i;
    omc_size count;

    count = 0U;
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
            count += 1U;
        }
    }
    return count;
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

static const omc_entry*
find_exif_entry(const omc_store* store, const char* ifd_name, omc_u16 tag)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes ifd_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        if (entry->key.u.exif_tag.tag != tag) {
            continue;
        }
        ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_view.size == strlen(ifd_name)
            && memcmp(ifd_view.data, ifd_name, ifd_view.size) == 0) {
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

static omc_size
make_test_tiff_with_old_xmp_and_make(omc_u8* out)
{
    static const char make[] = "Canon";
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_size size;
    omc_u32 make_off;
    omc_u32 xmp_off;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 2U);

    make_off = 8U + 2U + 24U + 4U;
    xmp_off = make_off + (omc_u32)sizeof(make);

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)sizeof(make));
    append_u32le(out, &size, make_off);

    append_u16le(out, &size, 700U);
    append_u16le(out, &size, 7U);
    append_u32le(out, &size, (omc_u32)(sizeof(xmp) - 1U));
    append_u32le(out, &size, xmp_off);

    append_u32le(out, &size, 0U);
    append_text(out, &size, make);
    append_u8(out, &size, 0U);
    append_text(out, &size, xmp);
    return size;
}

static void
test_xmp_apply_jpeg_sidecar_only_preserves_existing_embedded_xmp(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "OldTool");
    assert(count_xmp_entries(&edited_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_jpeg_sidecar_only_strips_existing_embedded_xmp(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 0U);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(find_xmp_entry(&edited_store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool")
           == (const omc_entry*)0);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_jpeg_embedded_and_sidecar_writes_both(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    opts.embedded.packet.include_existing_xmp = 1;
    opts.embedded.packet.include_exif = 0;
    opts.embedded.packet.include_iptc = 0;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 1U);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&edited_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_xmp_apply_tiff_sidecar_only_strips_existing_embedded_xmp(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_xmp_apply_opts opts;
    omc_xmp_apply_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    file_size = make_test_tiff_with_old_xmp_and_make(file_bytes);
    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_xmp_apply_opts_init(&opts);
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.destination_embedded_mode =
        OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;

    status = omc_xmp_apply(file_bytes, file_size, &source_store, &edited_out,
                           &sidecar_out, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.embedded.status == OMC_XMP_WRITE_OK);
    assert(res.embedded.removed_xmp_blocks == 1U);
    assert(res.embedded.inserted_xmp_blocks == 0U);
    assert(res.sidecar_requested);
    assert(res.sidecar.status == OMC_XMP_DUMP_OK);

    read_res = omc_read_simple(edited_out.data, edited_out.size, &edited_store,
                               blocks, 8U, ifds, 8U, payload_buf,
                               sizeof(payload_buf), payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(find_xmp_entry(&edited_store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool")
           == (const omc_entry*)0);
    assert_text_value(&edited_store, find_exif_entry(&edited_store, "ifd0",
                                                     0x010FU),
                      "Canon");

    read_res = omc_read_simple(sidecar_out.data, sidecar_out.size,
                               &sidecar_store, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

int
main(void)
{
    test_xmp_apply_jpeg_sidecar_only_preserves_existing_embedded_xmp();
    test_xmp_apply_jpeg_sidecar_only_strips_existing_embedded_xmp();
    test_xmp_apply_jpeg_embedded_and_sidecar_writes_both();
    test_xmp_apply_tiff_sidecar_only_strips_existing_embedded_xmp();
    return 0;
}
