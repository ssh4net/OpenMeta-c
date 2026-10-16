#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_translation.h"
#include "omc/omc_read.h"
#include "native_edit_fixture.h"
#include <assert.h>

static const omc_u16 date_tags[2][3] = {
    {0x9003U, 0x9011U, 0x9291U}, {0x9004U, 0x9012U, 0x9292U}
};

static omc_byte_ref
text(omc_store *store, const char *value)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, value, strlen(value), &ref) == OMC_STATUS_OK);
    return ref;
}

static void
native(omc_store *store, unsigned int group, unsigned int field,
        const char *value, omc_entry_flags flags)
{
    omc_entry entry;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, text(store, "exififd"), date_tags[group][field]);
    omc_val_make_text(&entry.value, text(store, value), OMC_TEXT_ASCII);
    entry.flags = flags;
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static void
xmp(omc_store *store, unsigned int group, const char *value, int deleted)
{
    omc_entry entry;
    omc_byte_ref ns;
    omc_byte_ref path;
    memset(&entry, 0, sizeof(entry));
    ns = text(store, group ? "http://ns.adobe.com/xap/1.0/" : "http://ns.adobe.com/exif/1.0/");
    path = text(store, group ? "CreateDate" : "DateTimeOriginal");
    omc_key_make_xmp_property(&entry.key, ns, path);
    omc_val_make_text(&entry.value, text(store, value), OMC_TEXT_UTF8);
    entry.flags = OMC_ENTRY_FLAG_DIRTY | (deleted ? OMC_ENTRY_FLAG_DELETED : 0U);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, NULL) == OMC_STATUS_OK);
}

static void
execute(const omc_u8 *data, omc_size size, omc_scan_fmt format,
         const omc_store *source, omc_arena *out)
{
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec executor;
    omc_transfer_res result;
    omc_arena sidecar;
    omc_transfer_prepare_opts_init(&opts);
    opts.format = format;
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    opts.embedded.packet.include_existing_xmp = 0;
    opts.embedded.packet.include_exif = 0;
    opts.embedded.packet.include_iptc = 0;
    omc_arena_init(&sidecar);
    assert(omc_transfer_prepare(data, size, source, &opts, &bundle) == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    if (format == OMC_SCAN_FMT_TIFF) assert(bundle.native_edits.merge_exif);
    assert(omc_transfer_compile(&bundle, &executor) == OMC_STATUS_OK);
    assert(omc_transfer_execute(data, size, source, out, &sidecar, &executor, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    omc_arena_fini(&sidecar);
}

static void
check_text(const omc_store *store, const char *ifd, omc_u16 tag,
            const char *expected)
{
    omc_size i;
    unsigned int count;
    count = 0U;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry;
        omc_const_bytes name;
        omc_const_bytes value;
        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG || entry->key.u.exif_tag.tag != tag ||
            (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U) continue;
        name = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (name.size != strlen(ifd) || memcmp(name.data, ifd, name.size) != 0) continue;
        assert(expected != NULL);
        assert(entry->value.kind == OMC_VAL_TEXT || entry->value.kind == OMC_VAL_BYTES);
        value = omc_arena_view(&store->arena, entry->value.u.ref);
        while (value.size != 0U && value.data[value.size - 1U] == 0U) --value.size;
        assert(value.size == strlen(expected) && memcmp(value.data, expected, value.size) == 0);
        ++count;
    }
    assert(count == (expected != NULL ? 1U : 0U));
}

static void
readback(const omc_arena *file, unsigned int group, unsigned int mode,
          int before, int tiff)
{
    omc_store store;
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[16];
    omc_u8 payload[32768];
    omc_u32 scratch[32];
    omc_read_res result;
    unsigned int g;
    omc_store_init(&store);
    result = omc_read_simple(file->data, file->size, &store, blocks, 32U,
        ifds, 16U, payload, sizeof(payload), scratch, 32U, NULL);
    assert(result.entries_added != 0U);
    for (g = 0U; g < 2U; ++g) {
        int unchanged;
        unchanged = before || g != group;
        check_text(&store, "exififd", date_tags[g][0], unchanged ? "2000:01:02 03:04:05" :
            mode == 2U ? NULL : "2026:10:17 12:34:56");
        check_text(&store, "exififd", date_tags[g][1], unchanged ? "+01:00" :
            mode == 0U || mode == 4U ? "+09:00" : NULL);
        check_text(&store, "exififd", date_tags[g][2], unchanged ? "999" : mode == 0U ? "125" : NULL);
    }
    if (tiff) {
        check_text(&store, "ifd0", 0x010FU, "Camera!");
        check_text(&store, "ifd0", 0x0132U, "2020:01:02 03:04:05");
        assert(file->size >= 984U);
        assert(memcmp(file->data + 944U, "NOTE!!!!", 8U) == 0);
        assert(memcmp(file->data + 960U, "OPAQUE!!", 8U) == 0);
        assert(memcmp(file->data + 976U, "PIXELS!!", 8U) == 0);
    }
    omc_store_fini(&store);
}

static void
run_case(unsigned int carrier, unsigned int group, unsigned int mode)
{
    static const omc_u8 jpeg[] = {0xFFU, 0xD8U, 0xFFU, 0xD9U};
    omc_u8 original[1024];
    omc_store stale;
    omc_store source;
    omc_store translated;
    omc_arena target;
    omc_arena output;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_scan_fmt format;
    unsigned int g;
    int big;
    int little;
    const omc_u8 *entry;
    omc_store_init(&stale); omc_store_init(&source); omc_store_init(&translated);
    omc_arena_init(&target); omc_arena_init(&output);
    big = carrier >= 3U; little = carrier == 1U || carrier == 3U;
    format = carrier == 0U ? OMC_SCAN_FMT_JPEG : OMC_SCAN_FMT_TIFF;
    native_fixture(original, big, little);
    entry = native_find(original, sizeof(original), 16U, 0x010FU, big, little);
    assert(entry != NULL);
    for (g = 0U; g < 2U; ++g) {
        native(&stale, g, 0U, "2000:01:02 03:04:05", OMC_ENTRY_FLAG_DIRTY);
        native(&stale, g, 1U, "+01:00", OMC_ENTRY_FLAG_DIRTY);
        native(&stale, g, 2U, "999", OMC_ENTRY_FLAG_DIRTY);
    }
    execute(carrier == 0U ? jpeg : original, carrier == 0U ? sizeof(jpeg) : sizeof(original), format, &stale, &target);
    readback(&target, group, mode, 1, carrier != 0U);
    /* JPEG replaces the complete EXIF packet; retain unselected fields in source.
     * TIFF selected edits retain these fields directly from the destination. */
    if (carrier == 0U) {
        native(&source, 1U - group, 0U, "2000:01:02 03:04:05", 0U);
        native(&source, 1U - group, 1U, "+01:00", 0U);
        native(&source, 1U - group, 2U, "999", 0U);
    }
    xmp(&source, group, mode == 0U ? "2026-10-17T12:34:56.125+09:00" :
        mode == 4U ? "2026-10-17T12:34:56+09:00" : "2026-10-17T12:34:56", mode == 2U);
    if (mode != 2U) native(&source, group, 0U, "2026:10:17 12:34:56", 0U);
    if (mode == 0U || mode == 4U) native(&source, group, 1U, "+09:00", 0U);
    if (mode == 0U) native(&source, group, 2U, "125", 0U);
    if (mode == 3U) native(&source, group, 1U, "old tombstone", OMC_ENTRY_FLAG_DELETED);
    omc_translation_opts_init(&opts);
    opts.mappings = group ? OMC_TRANSLATE_CREATE_EXIF : OMC_TRANSLATE_DATE_ORIGINAL;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp(&source, &translated, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK && result.groups_translated == 1U);
    execute(target.data, target.size, format, &translated, &output);
    readback(&output, group, mode, 0, carrier != 0U);
    omc_arena_fini(&output); omc_arena_fini(&target);
    omc_store_fini(&translated); omc_store_fini(&source); omc_store_fini(&stale);
}

int main(void)
{
    unsigned int carrier;
    unsigned int group;
    unsigned int mode;
    for (carrier = 0U; carrier < 5U; ++carrier)
        for (group = 0U; group < 2U; ++group)
            for (mode = 0U; mode < 5U; ++mode) run_case(carrier, group, mode);
    return 0;
}
