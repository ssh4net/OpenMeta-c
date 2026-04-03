#include "omc/omc_read.h"
#include "omc/omc_xmp_write.h"

#include <assert.h>
#include <string.h>

static const omc_u8 k_png_sig[8] = {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
};

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
append_u32be(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    out[*io_size + 0U] = (omc_u8)((value >> 24) & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)(value & 0xFFU);
    *io_size += 4U;
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
append_u64be(omc_u8* out, omc_size* io_size, omc_u64 value)
{
    out[*io_size + 0U] = (omc_u8)((value >> 56) & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 48) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 40) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)((value >> 32) & 0xFFU);
    out[*io_size + 4U] = (omc_u8)((value >> 24) & 0xFFU);
    out[*io_size + 5U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 6U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 7U] = (omc_u8)(value & 0xFFU);
    *io_size += 8U;
}

static void
append_u64le(omc_u8* out, omc_size* io_size, omc_u64 value)
{
    out[*io_size + 0U] = (omc_u8)(value & 0xFFU);
    out[*io_size + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[*io_size + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[*io_size + 3U] = (omc_u8)((value >> 24) & 0xFFU);
    out[*io_size + 4U] = (omc_u8)((value >> 32) & 0xFFU);
    out[*io_size + 5U] = (omc_u8)((value >> 40) & 0xFFU);
    out[*io_size + 6U] = (omc_u8)((value >> 48) & 0xFFU);
    out[*io_size + 7U] = (omc_u8)((value >> 56) & 0xFFU);
    *io_size += 8U;
}

static void
append_raw(omc_u8* out, omc_size* io_size, const void* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
write_u32le_at(omc_u8* out, omc_size off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)(value & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 24) & 0xFFU);
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

static void
append_png_chunk(omc_u8* out, omc_size* io_size, const char* type,
                 const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)payload_size);
    append_raw(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_raw(out, io_size, payload, payload_size);
    }
    append_u32be(out, io_size, 0U);
}

static void
append_webp_chunk(omc_u8* out, omc_size* io_size, const char* type,
                  const omc_u8* payload, omc_size payload_size)
{
    append_text(out, io_size, type);
    append_u32le(out, io_size, (omc_u32)payload_size);
    if (payload_size != 0U) {
        append_raw(out, io_size, payload, payload_size);
    }
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static omc_u32
fourcc(char a, char b, char c, char d)
{
    return ((omc_u32)(omc_u8)a << 24) | ((omc_u32)(omc_u8)b << 16)
           | ((omc_u32)(omc_u8)c << 8) | (omc_u32)(omc_u8)d;
}

static void
append_bmff_box(omc_u8* out, omc_size* io_size, omc_u32 type,
                const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)(payload_size + 8U));
    append_u32be(out, io_size, type);
    if (payload_size != 0U) {
        append_raw(out, io_size, payload, payload_size);
    }
}

static void
append_fullbox_header(omc_u8* out, omc_size* io_size, omc_u8 version)
{
    append_u8(out, io_size, version);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
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
find_png_text_entry(const omc_store* store, const char* keyword,
                    const char* field)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes keyword_view;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_PNG_TEXT) {
            continue;
        }
        keyword_view = omc_arena_view(&store->arena,
                                      entry->key.u.png_text.keyword);
        field_view = omc_arena_view(&store->arena,
                                    entry->key.u.png_text.field);
        if (keyword_view.size == strlen(keyword)
            && field_view.size == strlen(field)
            && memcmp(keyword_view.data, keyword, keyword_view.size) == 0
            && memcmp(field_view.data, field, field_view.size) == 0) {
            return entry;
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

static omc_size
make_test_tiff_le_with_make_only(omc_u8* out)
{
    static const char make[] = "Canon";
    omc_size size;
    omc_u32 make_off;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);

    make_off = 8U + 2U + 12U + 4U;

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, (omc_u32)sizeof(make));
    append_u32le(out, &size, make_off);

    append_u32le(out, &size, 0U);
    append_text(out, &size, make);
    append_u8(out, &size, 0U);
    return size;
}

static omc_size
make_test_tiff_be_with_old_xmp_and_make(omc_u8* out)
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
    append_text(out, &size, "MM");
    append_u16be(out, &size, 42U);
    append_u32be(out, &size, 8U);
    append_u16be(out, &size, 2U);

    make_off = 8U + 2U + 24U + 4U;
    xmp_off = make_off + (omc_u32)sizeof(make);

    append_u16be(out, &size, 0x010FU);
    append_u16be(out, &size, 2U);
    append_u32be(out, &size, (omc_u32)sizeof(make));
    append_u32be(out, &size, make_off);

    append_u16be(out, &size, 700U);
    append_u16be(out, &size, 7U);
    append_u32be(out, &size, (omc_u32)(sizeof(xmp) - 1U));
    append_u32be(out, &size, xmp_off);

    append_u32be(out, &size, 0U);
    append_text(out, &size, make);
    append_u8(out, &size, 0U);
    append_text(out, &size, xmp);
    return size;
}

static omc_size
make_test_bigtiff_le_with_make_and_old_xmp(omc_u8* out)
{
    static const char make[] = { 'C', 'a', 'n', 'o', 'n', 0U };
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_size size;
    omc_u64 xmp_off;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 43U);
    append_u16le(out, &size, 8U);
    append_u16le(out, &size, 0U);
    append_u64le(out, &size, 16U);
    append_u64le(out, &size, 2U);

    xmp_off = 16U + 8U + 40U + 8U;

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u64le(out, &size, (omc_u64)sizeof(make));
    append_raw(out, &size, make, sizeof(make));
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);

    append_u16le(out, &size, 700U);
    append_u16le(out, &size, 7U);
    append_u64le(out, &size, (omc_u64)(sizeof(xmp) - 1U));
    append_u64le(out, &size, xmp_off);

    append_u64le(out, &size, 0U);
    append_text(out, &size, xmp);
    return size;
}

static omc_size
make_test_bigtiff_le_with_make_only(omc_u8* out)
{
    static const char make[] = { 'C', 'a', 'n', 'o', 'n', 0U };
    omc_size size;

    size = 0U;
    append_text(out, &size, "II");
    append_u16le(out, &size, 43U);
    append_u16le(out, &size, 8U);
    append_u16le(out, &size, 0U);
    append_u64le(out, &size, 16U);
    append_u64le(out, &size, 1U);

    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u64le(out, &size, (omc_u64)sizeof(make));
    append_raw(out, &size, make, sizeof(make));
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);

    append_u64le(out, &size, 0U);
    return size;
}

static omc_size
make_test_jpeg_with_old_xmp_and_comment(omc_u8* out)
{
    static const char jfif[] = { 'J', 'F', 'I', 'F', 0x00, 0x01, 0x02,
                                 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 };
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
make_test_png_with_old_xmp_and_text(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 ihdr[13];
    omc_u8 xmp_payload[512];
    omc_u8 text_payload[64];
    omc_size xmp_size;
    omc_size text_size;
    omc_size size;

    memset(ihdr, 0, sizeof(ihdr));
    ihdr[3] = 1U;
    ihdr[7] = 1U;
    ihdr[8] = 8U;
    ihdr[9] = 2U;

    xmp_size = 0U;
    append_text(xmp_payload, &xmp_size, "XML:com.adobe.xmp");
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_text(xmp_payload, &xmp_size, xmp);

    text_size = 0U;
    append_text(text_payload, &text_size, "Comment");
    append_u8(text_payload, &text_size, 0U);
    append_text(text_payload, &text_size, "Preserve me");

    size = 0U;
    append_raw(out, &size, k_png_sig, sizeof(k_png_sig));
    append_png_chunk(out, &size, "IHDR", ihdr, sizeof(ihdr));
    append_png_chunk(out, &size, "iTXt", xmp_payload, xmp_size);
    append_png_chunk(out, &size, "tEXt", text_payload, text_size);
    append_png_chunk(out, &size, "IEND", (const omc_u8*)0, 0U);
    return size;
}

static omc_size
make_test_webp_with_old_xmp_and_exif(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[512];
    omc_u8 exif[544];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size size;

    tiff_size = make_test_tiff_with_old_xmp_and_make(tiff);
    exif_size = 0U;
    append_text(exif, &exif_size, "Exif");
    append_u8(exif, &exif_size, 0U);
    append_u8(exif, &exif_size, 0U);
    append_raw(exif, &exif_size, tiff, tiff_size);

    size = 0U;
    append_text(out, &size, "RIFF");
    append_u32le(out, &size, 0U);
    append_text(out, &size, "WEBP");
    append_webp_chunk(out, &size, "EXIF", exif, exif_size);
    append_webp_chunk(out, &size, "XMP ", (const omc_u8*)xmp,
                      sizeof(xmp) - 1U);
    write_u32le_at(out, 4U, (omc_u32)(size - 8U));
    return size;
}

static omc_size
make_test_jp2_with_old_xmp_and_exif(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[512];
    omc_u8 exif_payload[544];
    omc_u8 colr_payload[8];
    omc_u8 colr_box[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size colr_size;
    omc_size colr_box_size;
    omc_size size;

    tiff_size = make_test_tiff_with_old_xmp_and_make(tiff);
    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    colr_size = 0U;
    append_u8(colr_payload, &colr_size, 1U);
    append_u8(colr_payload, &colr_size, 0U);
    append_u8(colr_payload, &colr_size, 0U);
    append_u8(colr_payload, &colr_size, 0U);
    colr_box_size = 0U;
    append_bmff_box(colr_box, &colr_box_size, fourcc('c', 'o', 'l', 'r'),
                    colr_payload, colr_size);

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('j', 'P', ' ', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('j', 'p', '2', 'h'),
                    colr_box, colr_box_size);
    append_bmff_box(out, &size, fourcc('x', 'm', 'l', ' '),
                    (const omc_u8*)xmp, sizeof(xmp) - 1U);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    return size;
}

static omc_size
make_test_bmff_with_old_xmp_and_exif(omc_u8* out, omc_u32 major_brand)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[128];
    omc_u8 exif_payload[256];
    omc_u8 idat_payload[512];
    omc_u8 infe_exif[64];
    omc_u8 infe_xmp[96];
    omc_u8 iinf_payload[256];
    omc_u8 iloc_payload[160];
    omc_u8 meta_payload[1024];
    omc_u8 moov_box[16];
    omc_u8 ftyp_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size idat_size;
    omc_size exif_off;
    omc_size xmp_off;
    omc_size infe_exif_size;
    omc_size infe_xmp_size;
    omc_size iinf_size;
    omc_size iloc_size;
    omc_size meta_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 6U);
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    idat_size = 0U;
    exif_off = idat_size;
    append_raw(idat_payload, &idat_size, exif_payload, exif_size);
    xmp_off = idat_size;
    append_text(idat_payload, &idat_size, xmp);

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif, &infe_exif_size, 2U);
    append_u16be(infe_exif, &infe_exif_size, 1U);
    append_u16be(infe_exif, &infe_exif_size, 0U);
    append_u32be(infe_exif, &infe_exif_size, fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif, &infe_exif_size, "Exif");
    append_u8(infe_exif, &infe_exif_size, 0U);

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 0U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/rdf+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 0U);
    append_u16be(iinf_payload, &iinf_size, 2U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_exif, infe_exif_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);

    iloc_size = 0U;
    append_fullbox_header(iloc_payload, &iloc_size, 1U);
    append_u8(iloc_payload, &iloc_size, 0x44U);
    append_u8(iloc_payload, &iloc_size, 0x40U);
    append_u16be(iloc_payload, &iloc_size, 2U);

    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_size);

    append_u16be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)xmp_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)(sizeof(xmp) - 1U));

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'l', 'o', 'c'),
                    iloc_payload, iloc_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'd', 'a', 't'),
                    idat_payload, idat_size);

    moov_size = 0U;
    append_bmff_box(moov_box, &moov_size, fourcc('m', 'o', 'o', 'v'),
                    (const omc_u8*)0, 0U);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_raw(out, &size, moov_box, moov_size);
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static omc_size
make_test_jxl_with_old_xmp_and_exif(omc_u8* out)
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    omc_u8 tiff[128];
    omc_u8 exif_payload[256];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('J', 'X', 'L', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    append_bmff_box(out, &size, fourcc('x', 'm', 'l', ' '),
                    (const omc_u8*)xmp, sizeof(xmp) - 1U);
    return size;
}

static omc_size
make_test_jxl_with_brob_xmp_and_exif(omc_u8* out)
{
    omc_u8 tiff[128];
    omc_u8 exif_payload[256];
    omc_u8 brob_payload[64];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size brob_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);

    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    brob_size = 0U;
    append_u32be(brob_payload, &brob_size, fourcc('x', 'm', 'l', ' '));
    append_text(brob_payload, &brob_size, "old compressed xmp");

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('J', 'X', 'L', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    append_bmff_box(out, &size, fourcc('b', 'r', 'o', 'b'),
                    brob_payload, brob_size);
    return size;
}

static omc_size
make_test_cr3_with_exif_uuid_only(omc_u8* out)
{
    static const omc_u8 canon_uuid[16] = {
        0x85U, 0xC0U, 0xB6U, 0x87U, 0x82U, 0x0FU, 0x11U, 0xE0U,
        0x81U, 0x11U, 0xF4U, 0xCEU, 0x46U, 0x2BU, 0x6AU, 0x48U
    };
    omc_u8 tiff[128];
    omc_u8 exif_payload[256];
    omc_u8 cmt_payload[320];
    omc_u8 uuid_payload[384];
    omc_u8 moov_payload[448];
    omc_u8 ftyp_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size cmt_size;
    omc_size uuid_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    tiff_size = make_test_tiff_le_with_make_only(tiff);

    exif_size = 0U;
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_raw(exif_payload, &exif_size, tiff, tiff_size);

    cmt_size = 0U;
    append_bmff_box(cmt_payload, &cmt_size, fourcc('C', 'M', 'T', '1'),
                    exif_payload, exif_size);

    uuid_size = 0U;
    append_raw(uuid_payload, &uuid_size, canon_uuid, sizeof(canon_uuid));
    append_raw(uuid_payload, &uuid_size, cmt_payload, cmt_size);

    moov_size = 0U;
    append_bmff_box(moov_payload, &moov_size, fourcc('u', 'u', 'i', 'd'),
                    uuid_payload, uuid_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, fourcc('c', 'r', 'x', ' '));
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('C', 'R', '3', ' '));

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'o', 'o', 'v'),
                    moov_payload, moov_size);
    return size;
}

static void
test_write_embedded_jpeg_replaces_xmp_and_keeps_comment(void)
{
    omc_u8 jpeg[1024];
    omc_size jpeg_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    jpeg_size = make_test_jpeg_with_old_xmp_and_comment(jpeg);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(jpeg, jpeg_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_JPEG);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 8U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_comment_entry(&read_store),
                      "Preserve me");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_png_replaces_xmp_and_keeps_text(void)
{
    omc_u8 png[1024];
    omc_size png_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[512];
    omc_u32 payload_scratch[8];
    omc_status status;

    png_size = make_test_png_with_old_xmp_and_text(png);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(png, png_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_PNG);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 8U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store,
                      find_png_text_entry(&read_store, "Comment", "text"),
                      "Preserve me");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_tiff_replaces_xmp_and_keeps_make(void)
{
    omc_u8 tiff[1024];
    omc_size tiff_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[768];
    omc_u32 payload_scratch[8];
    omc_status status;

    tiff_size = make_test_tiff_with_old_xmp_and_make(tiff);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(tiff, tiff_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_TIFF);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 8U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_tiff_be_replaces_xmp_and_keeps_make(void)
{
    omc_u8 tiff[1024];
    omc_size tiff_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[768];
    omc_u32 payload_scratch[8];
    omc_status status;

    tiff_size = make_test_tiff_be_with_old_xmp_and_make(tiff);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(tiff, tiff_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_TIFF);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 8U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_bigtiff_inserts_xmp_and_keeps_make(void)
{
    omc_u8 tiff[1024];
    omc_size tiff_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[768];
    omc_u32 payload_scratch[8];
    omc_status status;

    tiff_size = make_test_bigtiff_le_with_make_only(tiff);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(tiff, tiff_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_TIFF);
    assert(res.removed_xmp_blocks == 0U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 8U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_bigtiff_replaces_xmp_and_keeps_make(void)
{
    omc_u8 tiff[1536];
    omc_size tiff_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[8];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[1024];
    omc_u32 payload_scratch[8];
    omc_status status;

    tiff_size = make_test_bigtiff_le_with_make_and_old_xmp(tiff);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(tiff, tiff_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_TIFF);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 8U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 8U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_webp_replaces_xmp_and_keeps_exif(void)
{
    omc_u8 webp[2048];
    omc_size webp_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[1024];
    omc_u32 payload_scratch[16];
    omc_status status;

    webp_size = make_test_webp_with_old_xmp_and_exif(webp);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(webp, webp_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_WEBP);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 16U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 16U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_jp2_replaces_xmp_and_keeps_exif(void)
{
    omc_u8 jp2[2048];
    omc_size jp2_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[1024];
    omc_u32 payload_scratch[16];
    omc_status status;

    jp2_size = make_test_jp2_with_old_xmp_and_exif(jp2);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(jp2, jp2_size, &edit_store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_JP2);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 16U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 16U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_heif_replaces_xmp_and_keeps_exif(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[1536];
    omc_u32 payload_scratch[16];
    omc_status status;

    file_size = make_test_bmff_with_old_xmp_and_exif(
        file_bytes, fourcc('h', 'e', 'i', 'c'));
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(file_bytes, file_size, &edit_store, &out,
                                    &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_HEIF);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 16U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 16U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_avif_replaces_xmp_and_keeps_exif(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[1536];
    omc_u32 payload_scratch[16];
    omc_status status;

    file_size = make_test_bmff_with_old_xmp_and_exif(
        file_bytes, fourcc('a', 'v', 'i', 'f'));
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(file_bytes, file_size, &edit_store, &out,
                                    &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_AVIF);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 16U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 16U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_jxl_replaces_xml_box_and_keeps_exif(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[1536];
    omc_u32 payload_scratch[16];
    omc_status status;

    file_size = make_test_jxl_with_old_xmp_and_exif(file_bytes);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(file_bytes, file_size, &edit_store, &out,
                                    &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_JXL);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 16U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 16U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_jxl_replaces_brob_xmp_and_keeps_exif(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store edit_store;
    omc_store read_store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_read_res read_res;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[8];
    omc_u8 payload_buf[1536];
    omc_u32 payload_scratch[16];
    omc_status status;

    file_size = make_test_jxl_with_brob_xmp_and_exif(file_bytes);
    omc_store_init(&edit_store);
    omc_store_init(&read_store);
    omc_arena_init(&out);

    build_store_with_creator_tool(&edit_store, "NewTool");
    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(file_bytes, file_size, &edit_store, &out,
                                    &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_OK);
    assert(res.format == OMC_SCAN_FMT_JXL);
    assert(res.removed_xmp_blocks == 1U);
    assert(res.inserted_xmp_blocks == 1U);

    read_res = omc_read_simple(out.data, out.size, &read_store, blocks, 16U,
                               ifds, 8U, payload_buf, sizeof(payload_buf),
                               payload_scratch, 16U,
                               (const omc_read_opts*)0);
    assert(read_res.entries_added >= 2U);
    assert_text_value(&read_store,
                      find_xmp_entry(&read_store,
                                     "http://ns.adobe.com/xap/1.0/",
                                     "CreatorTool"),
                      "NewTool");
    assert(count_xmp_entries(&read_store, "http://ns.adobe.com/xap/1.0/",
                             "CreatorTool")
           == 1U);
    assert_text_value(&read_store, find_exif_entry(&read_store, "ifd0", 0x010FU),
                      "Canon");

    omc_arena_fini(&out);
    omc_store_fini(&read_store);
    omc_store_fini(&edit_store);
}

static void
test_write_embedded_reports_unsupported_for_cr3_uuid_layout(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_store store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_status status;

    file_size = make_test_cr3_with_exif_uuid_only(file_bytes);
    omc_store_init(&store);
    omc_arena_init(&out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(file_bytes, file_size, &store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.format == OMC_SCAN_FMT_CR3);
    assert(res.status == OMC_XMP_WRITE_UNSUPPORTED);
    assert(out.size == 0U);

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_write_embedded_reports_unsupported_for_cr3_meta_layout(void)
{
    omc_u8 file_bytes[2048];
    omc_size file_size;
    omc_store store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_status status;

    file_size = make_test_bmff_with_old_xmp_and_exif(
        file_bytes, fourcc('c', 'r', 'x', ' '));
    omc_store_init(&store);
    omc_arena_init(&out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(file_bytes, file_size, &store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.format == OMC_SCAN_FMT_CR3);
    assert(res.status == OMC_XMP_WRITE_UNSUPPORTED);
    assert(out.size == 0U);

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_write_embedded_reports_unsupported_for_other_formats(void)
{
    static const omc_u8 gif[] = { 'G', 'I', 'F', '8', '9', 'a' };
    omc_store store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(gif, sizeof(gif), &store, &out, &opts,
                                    &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_UNSUPPORTED);

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

static void
test_write_embedded_reports_malformed_input(void)
{
    static const omc_u8 bad_jpeg[] = { 0xFFU, 0xD8U, 0x12U, 0x34U };
    omc_store store;
    omc_arena out;
    omc_xmp_write_opts opts;
    omc_xmp_write_res res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&out);
    build_store_with_creator_tool(&store, "NewTool");

    omc_xmp_write_opts_init(&opts);
    opts.embed.packet.include_existing_xmp = 1;
    opts.embed.packet.include_exif = 0;
    opts.embed.packet.include_iptc = 0;

    status = omc_xmp_write_embedded(bad_jpeg, sizeof(bad_jpeg), &store, &out,
                                    &opts, &res);
    assert(status == OMC_STATUS_OK);
    assert(res.status == OMC_XMP_WRITE_MALFORMED);

    omc_arena_fini(&out);
    omc_store_fini(&store);
}

int
main(void)
{
    test_write_embedded_jpeg_replaces_xmp_and_keeps_comment();
    test_write_embedded_png_replaces_xmp_and_keeps_text();
    test_write_embedded_tiff_replaces_xmp_and_keeps_make();
    test_write_embedded_tiff_be_replaces_xmp_and_keeps_make();
    test_write_embedded_bigtiff_inserts_xmp_and_keeps_make();
    test_write_embedded_bigtiff_replaces_xmp_and_keeps_make();
    test_write_embedded_webp_replaces_xmp_and_keeps_exif();
    test_write_embedded_jp2_replaces_xmp_and_keeps_exif();
    test_write_embedded_heif_replaces_xmp_and_keeps_exif();
    test_write_embedded_avif_replaces_xmp_and_keeps_exif();
    test_write_embedded_jxl_replaces_xml_box_and_keeps_exif();
    test_write_embedded_jxl_replaces_brob_xmp_and_keeps_exif();
    test_write_embedded_reports_unsupported_for_cr3_uuid_layout();
    test_write_embedded_reports_unsupported_for_cr3_meta_layout();
    test_write_embedded_reports_unsupported_for_other_formats();
    test_write_embedded_reports_malformed_input();
    return 0;
}
