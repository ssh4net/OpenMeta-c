#include "omc/omc_read.h"
#include "omc/omc_transfer.h"
#include "omc_test_assert.h"

#include <string.h>

static void
append_u16le(omc_u8 *out, omc_size *io_size, omc_u16 value)
{
    out[*io_size + 0U] = (omc_u8)value;
    out[*io_size + 1U] = (omc_u8)(value >> 8U);
    *io_size += 2U;
}

static void
append_u32le(omc_u8 *out, omc_size *io_size, omc_u32 value)
{
    out[*io_size + 0U] = (omc_u8)value;
    out[*io_size + 1U] = (omc_u8)(value >> 8U);
    out[*io_size + 2U] = (omc_u8)(value >> 16U);
    out[*io_size + 3U] = (omc_u8)(value >> 24U);
    *io_size += 4U;
}

static void
append_u64le(omc_u8 *out, omc_size *io_size, omc_u64 value)
{
    unsigned i;
    for (i = 0U; i < 8U; ++i)
        out[*io_size + i] = (omc_u8)(value >> (i * 8U));
    *io_size += 8U;
}

static void
append_raw(omc_u8 *out, omc_size *io_size, const void *data, omc_size size)
{
    if (size != 0U)
        memcpy(out + *io_size, data, size);
    *io_size += size;
}

static omc_byte_ref
append_store_text(omc_arena *arena, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(arena, text, strlen(text), &ref) == OMC_STATUS_OK);
    return ref;
}

static omc_size
make_target_tiff(omc_u8 *out, int bigtiff)
{
    static const char make[] = "Canon";
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OldTool'/></rdf:RDF></x:xmpmeta>";
    omc_size size;
    omc_u64 xmp_size = (omc_u64)(sizeof(xmp) - 1U);

    size = 0U;
    if (!bigtiff) {
        const omc_u32 exif_off = 50U;
        const omc_u32 xmp_off = 74U;
        append_raw(out, &size, "II", 2U);
        append_u16le(out, &size, 42U);
        append_u32le(out, &size, 8U);
        append_u16le(out, &size, 3U);
        append_u16le(out, &size, 0x010FU);
        append_u16le(out, &size, 2U);
        append_u32le(out, &size, 6U);
        append_u32le(out, &size, 68U);
        append_u16le(out, &size, 0x8769U);
        append_u16le(out, &size, 4U);
        append_u32le(out, &size, 1U);
        append_u32le(out, &size, exif_off);
        append_u16le(out, &size, 700U);
        append_u16le(out, &size, 7U);
        append_u32le(out, &size, (omc_u32)xmp_size);
        append_u32le(out, &size, xmp_off);
        append_u32le(out, &size, 0U);
        append_u16le(out, &size, 1U);
        append_u16le(out, &size, 0xA403U);
        append_u16le(out, &size, 3U);
        append_u32le(out, &size, 1U);
        append_u16le(out, &size, 1U);
        append_u16le(out, &size, 0U);
        append_u32le(out, &size, 0U);
        append_raw(out, &size, make, sizeof(make));
        append_raw(out, &size, xmp, (omc_size)xmp_size);
    } else {
        const omc_u64 exif_off = 92U;
        const omc_u64 xmp_off = 128U;
        append_raw(out, &size, "II", 2U);
        append_u16le(out, &size, 43U);
        append_u16le(out, &size, 8U);
        append_u16le(out, &size, 0U);
        append_u64le(out, &size, 16U);
        append_u64le(out, &size, 3U);
        append_u16le(out, &size, 0x010FU);
        append_u16le(out, &size, 2U);
        append_u64le(out, &size, 6U);
        append_raw(out, &size, make, sizeof(make));
        append_u16le(out, &size, 0U);
        append_u16le(out, &size, 0x8769U);
        append_u16le(out, &size, 18U);
        append_u64le(out, &size, 1U);
        append_u64le(out, &size, exif_off);
        append_u16le(out, &size, 700U);
        append_u16le(out, &size, 7U);
        append_u64le(out, &size, xmp_size);
        append_u64le(out, &size, xmp_off);
        append_u64le(out, &size, 0U);
        append_u64le(out, &size, 1U);
        append_u16le(out, &size, 0xA403U);
        append_u16le(out, &size, 3U);
        append_u64le(out, &size, 1U);
        append_u16le(out, &size, 1U);
        append_u16le(out, &size, 0U);
        append_u32le(out, &size, 0U);
        append_u64le(out, &size, 0U);
        append_raw(out, &size, xmp, (omc_size)xmp_size);
    }
    return size;
}

static void
add_source_make(omc_store *store)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_text(&store->arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value,
                      append_store_text(&store->arena, "Retained"),
                      OMC_TEXT_UTF8);
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static void
add_deleted_exif_value(omc_store *store)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key,
                          append_store_text(&store->arena, "exififd"),
                          0xA403U);
    omc_val_make_u16(&entry.value, 1U);
    entry.flags = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static const omc_entry *
find_exif(const omc_store *store, const char *ifd, omc_u16 tag)
{
    omc_size i;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        omc_const_bytes name;
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag)
            continue;
        name = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (name.size == strlen(ifd) && memcmp(name.data, ifd, name.size) == 0)
            return entry;
    }
    return NULL;
}

static const omc_entry *
find_xmp(const omc_store *store)
{
    omc_size i;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        omc_const_bytes ns;
        omc_const_bytes path;
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY)
            continue;
        ns = omc_arena_view(&store->arena,
                            entry->key.u.xmp_property.schema_ns);
        path = omc_arena_view(&store->arena,
                              entry->key.u.xmp_property.property_path);
        if (ns.size == sizeof("http://ns.adobe.com/xap/1.0/") - 1U
            && path.size == sizeof("CreatorTool") - 1U
            && memcmp(ns.data, "http://ns.adobe.com/xap/1.0/", ns.size) == 0
            && memcmp(path.data, "CreatorTool", path.size) == 0)
            return entry;
    }
    return NULL;
}

static void
read_store(const omc_u8 *bytes, omc_size size, omc_store *store)
{
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[16];
    omc_u8 payload[4096];
    omc_u32 scratch[32];
    omc_read_res result;
    result = omc_read_simple(bytes, size, store, blocks, 16U, ifds, 16U,
                             payload, sizeof(payload), scratch, 32U, NULL);
    assert(result.entries_added != 0U);
}

static void
test_empty_exif_ifd_removal(int bigtiff, int clear, int strip)
{
    omc_u8 input[4096];
    omc_size input_size;
    omc_store source;
    omc_store decoded;
    omc_transfer_prepare_opts prepare;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res transfer;
    omc_arena edited;
    omc_arena sidecar;

    input_size = make_target_tiff(input, bigtiff);
    omc_store_init(&source);
    omc_store_init(&decoded);
    omc_arena_init(&edited);
    omc_arena_init(&sidecar);
    add_source_make(&source);
    if (clear)
        add_deleted_exif_value(&source);
    omc_transfer_prepare_opts_init(&prepare);
    prepare.format = OMC_SCAN_FMT_TIFF;
    prepare.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    prepare.destination_embedded_mode = strip
        ? OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING
        : OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;
    assert(omc_transfer_prepare(input, input_size, &source, &prepare, &bundle)
           == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK);
    assert(exec.status == OMC_TRANSFER_OK);
    assert(omc_transfer_execute(input, input_size, &source, &edited, &sidecar,
                                &exec, &transfer) == OMC_STATUS_OK);
    assert(transfer.status == OMC_TRANSFER_OK);
    assert(transfer.edited_present);
    read_store(edited.data, edited.size, &decoded);
    assert(find_exif(&decoded, "ifd0", 0x010FU) != NULL);
    assert((find_exif(&decoded, "ifd0", 0x8769U) == NULL) == (clear != 0));
    assert((find_exif(&decoded, "exififd", 0xA403U) == NULL)
           == (clear != 0));
    assert((find_xmp(&decoded) == NULL) == (strip != 0));
    omc_arena_fini(&sidecar);
    omc_arena_fini(&edited);
    omc_store_fini(&decoded);
    omc_store_fini(&source);
}

int
main(void)
{
    int bigtiff;
    int clear;
    int strip;
    for (bigtiff = 0; bigtiff <= 1; ++bigtiff) {
        for (clear = 0; clear <= 1; ++clear) {
            for (strip = 0; strip <= 1; ++strip)
                test_empty_exif_ifd_removal(bigtiff, clear, strip);
        }
    }
    return 0;
}
