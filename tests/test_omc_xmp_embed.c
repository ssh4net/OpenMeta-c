#include "omc/omc_read.h"
#include "omc/omc_xmp_embed.h"

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
append_raw(omc_u8* out, omc_size* io_size, const omc_u8* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
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

static omc_size
make_jpeg_with_app1_payload(omc_u8* out, const omc_u8* payload,
                            omc_size payload_size)
{
    omc_size size;
    omc_u16 seg_len;

    assert(payload_size <= 65533U);

    size = 0U;
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD8U);
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xE1U);
    seg_len = (omc_u16)(payload_size + 2U);
    append_u16be(out, &size, seg_len);
    append_raw(out, &size, payload, payload_size);
    append_u8(out, &size, 0xFFU);
    append_u8(out, &size, 0xD9U);
    return size;
}

static void
test_embed_payload_jpeg_roundtrip(void)
{
    omc_store src;
    omc_store dst;
    omc_entry entry;
    omc_xmp_embed_opts opts;
    omc_xmp_dump_res res;
    omc_arena payload;
    omc_status status;
    omc_u8 jpeg[2048];
    omc_size jpeg_size;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_read_res read_res;

    omc_store_init(&src);
    omc_store_init(&dst);
    omc_arena_init(&payload);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&src.arena, "http://ns.adobe.com/xap/1.0/"),
        append_bytes(&src.arena, "CreatorTool"));
    omc_val_make_text(&entry.value, append_bytes(&src.arena, "OpenMeta-c"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&src, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_embed_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;
    opts.packet.include_existing_xmp = 1;
    opts.packet.include_exif = 0;
    opts.packet.include_iptc = 0;

    status = omc_xmp_embed_payload_arena(&src, &payload, &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(payload.size == (omc_size)res.written);
    assert(contains_text(payload.data, payload.size,
                         "http://ns.adobe.com/xap/1.0/"));
    assert(contains_text(payload.data, payload.size,
                         "<xmp:CreatorTool>OpenMeta-c</xmp:CreatorTool>"));

    jpeg_size = make_jpeg_with_app1_payload(jpeg, payload.data, payload.size);
    read_res = omc_read_simple(jpeg, jpeg_size, &dst, blocks, 8U, ifds, 8U,
                               payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 1U);
    assert_text_value(&dst,
                      find_xmp_entry(&dst,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "OpenMeta-c");

    omc_arena_fini(&payload);
    omc_store_fini(&dst);
    omc_store_fini(&src);
}

static void
test_embed_payload_tiff_lossless_is_raw_packet(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_embed_opts opts;
    omc_xmp_dump_res res;
    omc_u8 out[2048];
    omc_status status;

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/xap/1.0/"),
        append_bytes(&store.arena, "CreatorTool"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "OpenMeta-c"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_embed_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_TIFF;
    opts.packet.format = OMC_XMP_SIDECAR_LOSSLESS;

    status = omc_xmp_embed_payload(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(contains_text(out, (omc_size)res.written, "<x:xmpmeta"));
    assert(contains_text(out, (omc_size)res.written, "urn:openmeta:dump:1.0"));
    assert(!contains_text(out, (omc_size)res.written,
                          "http://ns.adobe.com/xap/1.0/\0"));

    omc_store_fini(&store);
}

static void
test_embed_payload_png_itxt_shape(void)
{
    omc_store store;
    omc_entry entry;
    omc_xmp_embed_opts opts;
    omc_xmp_dump_res res;
    omc_u8 out[2048];
    omc_status status;

    omc_store_init(&store);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_bytes(&store.arena, "http://ns.adobe.com/xap/1.0/"),
        append_bytes(&store.arena, "CreatorTool"));
    omc_val_make_text(&entry.value, append_bytes(&store.arena, "OpenMeta-c"),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(&store, &entry, NULL);
    assert(status == OMC_STATUS_OK);

    omc_xmp_embed_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_PNG;
    opts.packet.include_existing_xmp = 1;
    opts.packet.include_exif = 0;
    opts.packet.include_iptc = 0;

    status = omc_xmp_embed_payload(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_DUMP_OK);
    assert(res.written > 22U);
    assert(memcmp(out, "XML:com.adobe.xmp", 17U) == 0);
    assert(out[17] == 0U);
    assert(out[18] == 0U);
    assert(out[19] == 0U);
    assert(out[20] == 0U);
    assert(out[21] == 0U);
    assert(contains_text(out, (omc_size)res.written, "<x:xmpmeta"));

    omc_store_fini(&store);
}

static void
test_embed_payload_invalid_format(void)
{
    omc_store store;
    omc_xmp_embed_opts opts;
    omc_xmp_dump_res res;
    omc_u8 out[32];
    omc_status status;

    omc_store_init(&store);

    omc_xmp_embed_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_UNKNOWN;

    status = omc_xmp_embed_payload(&store, out, sizeof(out), &opts, &res);
    assert(status == OMC_STATUS_INVALID_ARGUMENT);

    omc_store_fini(&store);
}

int
main(void)
{
    test_embed_payload_jpeg_roundtrip();
    test_embed_payload_tiff_lossless_is_raw_packet();
    test_embed_payload_png_itxt_shape();
    test_embed_payload_invalid_format();
    return 0;
}
