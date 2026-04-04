#include "omc/omc_read.h"
#include "omc/omc_transfer.h"
#include "omc/omc_transfer_persist.h"

#include <assert.h>
#include <stdio.h>
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

static int
read_file_bytes(const char* path, omc_arena* out)
{
    FILE* fp;
    long end_pos;
    omc_size size;

    fp = fopen(path, "rb");
    if (fp == (FILE*)0) {
        return 0;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    end_pos = ftell(fp);
    if (end_pos < 0L) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    size = (omc_size)end_pos;
    if (omc_arena_reserve(out, size) != OMC_STATUS_OK) {
        fclose(fp);
        return 0;
    }
    out->size = size;
    if (size != 0U
        && fread(out->data, 1U, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static void
build_temp_path(char* out, const char* ext)
{
    char temp_buf[L_tmpnam];

    assert(tmpnam(temp_buf) != (char*)0);
    strcpy(out, temp_buf);
    strcat(out, ext);
}

static void
derive_sidecar_path(const char* path, char* out)
{
    const char* dot;
    const char* slash;
    omc_size stem_len;

    slash = strrchr(path, '/');
    dot = strrchr(path, '.');
    if (dot != (const char*)0
        && (slash == (const char*)0 || dot > slash + 0)) {
        stem_len = (omc_size)(dot - path);
    } else {
        stem_len = (omc_size)strlen(path);
    }

    memcpy(out, path, stem_len);
    memcpy(out + stem_len, ".xmp", 5U);
}

static void
execute_transfer(const omc_u8* file_bytes, omc_size file_size,
                 const omc_store* store,
                 const omc_transfer_prepare_opts* opts,
                 omc_arena* edited_out, omc_arena* sidecar_out,
                 omc_transfer_exec* exec, omc_transfer_res* res)
{
    omc_transfer_bundle bundle;
    omc_status status;

    status = omc_transfer_prepare(file_bytes, file_size, store, opts, &bundle);
    assert(status == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);

    status = omc_transfer_compile(&bundle, exec);
    assert(status == OMC_STATUS_OK);
    assert(exec->status == OMC_TRANSFER_OK);

    status = omc_transfer_execute(file_bytes, file_size, store, edited_out,
                                  sidecar_out, exec, res);
    assert(status == OMC_STATUS_OK);
    assert(res->status == OMC_TRANSFER_OK);
}

static void
test_transfer_persist_writes_output_and_sidecar(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    derive_sidecar_path(output_path, sidecar_path);

    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    assert(persist_res.output_status == OMC_TRANSFER_OK);
    assert(persist_res.xmp_sidecar_status == OMC_TRANSFER_OK);

    assert(read_file_bytes(output_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_text_value(&edited_store, find_comment_entry(&edited_store),
                      "Preserve me");

    omc_arena_reset(&file_read);
    assert(read_file_bytes(sidecar_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_rejects_existing_sidecar_without_overwrite(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    char output_path[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    FILE* fp;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    derive_sidecar_path(output_path, sidecar_path);

    fp = fopen(sidecar_path, "wb");
    assert(fp != (FILE*)0);
    fputs("stale sidecar", fp);
    fclose(fp);

    omc_store_init(&source_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(persist_res.xmp_sidecar_status == OMC_TRANSFER_UNSUPPORTED);

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_uses_explicit_sidecar_base_path(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store sidecar_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 8];
    char sidecar_base[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    omc_const_bytes sidecar_path_view;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    build_temp_path(sidecar_base, ".tif");
    derive_sidecar_path(sidecar_base, sidecar_path);

    omc_store_init(&source_store);
    omc_store_init(&sidecar_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    persist_opts.xmp_sidecar_base_path = sidecar_base;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    sidecar_path_view = omc_arena_view(&meta_out, persist_res.xmp_sidecar_path);
    assert(sidecar_path_view.size == strlen(sidecar_path));
    assert(memcmp(sidecar_path_view.data, sidecar_path,
                  sidecar_path_view.size) == 0);

    assert(read_file_bytes(sidecar_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &sidecar_store);
    assert_text_value(&sidecar_store,
                      find_xmp_entry(&sidecar_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&sidecar_store);
    omc_store_fini(&source_store);
}

static void
test_transfer_persist_can_remove_stale_destination_sidecar(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store source_store;
    omc_store edited_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_arena file_read;
    char output_path[L_tmpnam + 8];
    char sidecar_path[L_tmpnam + 8];
    FILE* fp;
    omc_status status;

    file_size = make_test_jpeg_with_old_xmp_and_comment(file_bytes);
    build_temp_path(output_path, ".jpg");
    derive_sidecar_path(output_path, sidecar_path);

    fp = fopen(sidecar_path, "wb");
    assert(fp != (FILE*)0);
    fputs("stale sidecar", fp);
    fclose(fp);

    omc_store_init(&source_store);
    omc_store_init(&edited_store);
    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_arena_init(&file_read);
    build_store_with_creator_tool(&source_store, "NewTool");

    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    execute_transfer(file_bytes, file_size, &source_store, &prepare_opts,
                     &edited_out, &sidecar_out, &exec, &transfer_res);

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path;
    persist_opts.destination_sidecar_mode =
        OMC_TRANSFER_DEST_SIDECAR_STRIP_EXISTING;
    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    assert(status == OMC_STATUS_OK);
    assert(persist_res.status == OMC_TRANSFER_OK);
    assert(persist_res.xmp_sidecar_cleanup_requested);
    assert(persist_res.xmp_sidecar_cleanup_removed);

    assert(!read_file_bytes(sidecar_path, &file_read));
    assert(read_file_bytes(output_path, &file_read));
    read_store_from_bytes(file_read.data, file_read.size, &edited_store);
    assert_text_value(&edited_store,
                      find_xmp_entry(&edited_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");

    remove(output_path);
    remove(sidecar_path);
    omc_arena_fini(&file_read);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&edited_store);
    omc_store_fini(&source_store);
}

int
main(void)
{
    test_transfer_persist_writes_output_and_sidecar();
    test_transfer_persist_rejects_existing_sidecar_without_overwrite();
    test_transfer_persist_uses_explicit_sidecar_base_path();
    test_transfer_persist_can_remove_stale_destination_sidecar();
    return 0;
}
