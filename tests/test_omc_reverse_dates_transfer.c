#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_translation.h"
#include "omc/omc_read.h"
#include <assert.h>
#include <string.h>

static omc_byte_ref
text(omc_store *s, const char *value)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&s->arena, value, strlen(value), &ref) == OMC_STATUS_OK);
    return ref;
}

static void
native(omc_store *s, omc_u16 dataset, const char *value)
{
    omc_entry e;
    memset(&e, 0, sizeof(e));
    omc_key_make_iptc_dataset(&e.key, 2U, dataset);
    omc_val_make_bytes(&e.value, text(s, value));
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}

static void
xmp(omc_store *s, int digital, const char *value, int deleted)
{
    omc_entry e;
    omc_byte_ref ns;
    omc_byte_ref path;
    memset(&e, 0, sizeof(e));
    ns = text(s, digital ? "http://ns.adobe.com/xap/1.0/" : "http://ns.adobe.com/photoshop/1.0/");
    path = text(s, digital ? "CreateDate" : "DateCreated");
    omc_key_make_xmp_property(&e.key, ns, path);
    omc_val_make_text(&e.value, text(s, value), OMC_TEXT_UTF8);
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.flags = OMC_ENTRY_FLAG_DIRTY | (deleted ? OMC_ENTRY_FLAG_DELETED : 0U);
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}

static void
execute(const omc_u8 *data, omc_size size, omc_scan_fmt format,
         const omc_store *source, int include_exif, omc_arena *out)
{
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;
    omc_arena sidecar;
    omc_transfer_prepare_opts_init(&opts);
    opts.format = format;
    opts.include_exif = include_exif;
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    opts.embedded.packet.include_existing_xmp = 1;
    opts.embedded.packet.include_exif = 0;
    opts.embedded.packet.include_iptc = 0;
    omc_arena_init(&sidecar);
    assert(omc_transfer_prepare(data, size, source, &opts, &bundle) == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK);
    assert(omc_transfer_execute(data, size, source, out, &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    omc_arena_fini(&sidecar);
}

static int
equals(const omc_store *s, omc_byte_ref ref, const char *expected)
{
    omc_const_bytes v;
    v = omc_arena_view(&s->arena, ref);
    return v.size == strlen(expected) && memcmp(v.data, expected, v.size) == 0;
}

static void
check_native(const omc_store *s, omc_u16 dataset, const char *expected)
{
    omc_size i;
    unsigned int count;
    count = 0U;
    for (i = 0U; i < s->entry_count; ++i) {
        const omc_entry *e;
        e = &s->entries[i];
        if (e->key.kind == OMC_KEY_IPTC_DATASET &&
            e->key.u.iptc_dataset.record == 2U && e->key.u.iptc_dataset.dataset == dataset &&
            !(e->flags & OMC_ENTRY_FLAG_DELETED)) {
            assert(expected != NULL);
            assert(equals(s, e->value.u.ref, expected));
            ++count;
        }
    }
    assert(count == (expected != NULL ? 1U : 0U));
}

static void
readback(const omc_arena *bytes, const char *created, const char *digital,
          const char *time, int caption)
{
    omc_store store;
    omc_read_res result;
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[16];
    omc_u8 payload[32768];
    omc_u32 scratch[32];
    omc_size i;
    int found_make;
    omc_store_init(&store);
    result = omc_read_simple(bytes->data, bytes->size, &store, blocks, 32U,
        ifds, 16U, payload, sizeof(payload), scratch, 32U, NULL);
    assert(result.entries_added != 0U);
    check_native(&store, 55U, created); check_native(&store, 62U, digital);
    check_native(&store, 60U, time); check_native(&store, 63U, time);
    check_native(&store, 120U, caption ? "Retained caption" : NULL);
    found_make = 0;
    for (i = 0U; i < store.entry_count; ++i) {
        const omc_entry *e;
        e = &store.entries[i];
        if (e->key.kind == OMC_KEY_EXIF_TAG && e->key.u.exif_tag.tag == 0x010FU &&
            equals(&store, e->key.u.exif_tag.ifd, "ifd0")) {
            assert(equals(&store, e->value.u.ref, "Keep camera"));
            ++found_make;
        }
    }
    assert(found_make == 1);
    omc_store_fini(&store);
}

static void
run_case(omc_scan_fmt format, unsigned int mode)
{
    static const omc_u8 jpeg[] = {0xFFU, 0xD8U, 0xFFU, 0xD9U};
    static const omc_u8 tiff[] = {'I', 'I', 42U, 0U, 8U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    static const char *const dates[][4] = {
        {"1861", "2026-10", "18610000", "20261000"},
        {"2024-02", "9999", "20240200", "99990000"},
        {"2024-02-29", "2026-10-17", "20240229", "20261017"}
    };
    omc_store stale;
    omc_store source;
    omc_store translated;
    omc_translation_opts opts;
    omc_translation_res result;
    omc_arena carrier;
    omc_arena replaced;
    omc_entry make;
    omc_u16 dataset;
    int removal;
    int retain_caption;
    omc_store_init(&stale); omc_store_init(&source); omc_store_init(&translated);
    omc_arena_init(&carrier); omc_arena_init(&replaced);
    removal = mode >= 3U; retain_caption = mode != 4U;
    for (dataset = 55U; dataset <= 63U; ++dataset) {
        const char *value;
        if (dataset != 55U && dataset != 60U && dataset != 62U && dataset != 63U) continue;
        value = dataset == 55U || dataset == 62U ? "20240102" : "030405+0900";
        native(&stale, dataset, value);
        if (removal) native(&source, dataset, value);
    }
    if (retain_caption) { native(&stale, 120U, "Retained caption"); native(&source, 120U, "Retained caption"); }
    memset(&make, 0, sizeof(make));
    omc_key_make_exif_tag(&make.key, text(&stale, "ifd0"), 0x010FU);
    omc_val_make_text(&make.value, text(&stale, "Keep camera"), OMC_TEXT_ASCII);
    make.origin.block = OMC_INVALID_BLOCK_ID; make.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(&stale, &make, NULL) == OMC_STATUS_OK);
    execute(format == OMC_SCAN_FMT_JPEG ? jpeg : tiff,
            format == OMC_SCAN_FMT_JPEG ? sizeof(jpeg) : sizeof(tiff), format, &stale, 1, &carrier);
    readback(&carrier, "20240102", "20240102", "030405+0900", retain_caption);
    xmp(&source, 0, removal ? "" : dates[mode][0], removal);
    xmp(&source, 1, removal ? "" : dates[mode][1], removal);
    omc_translation_opts_init(&opts);
    opts.mappings = OMC_TRANSLATE_DATE_CREATED | OMC_TRANSLATE_CREATE_IPTC;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    result = omc_translate_xmp(&source, &translated, &opts, NULL);
    assert(result.status == OMC_TRANSLATION_OK);
    execute(carrier.data, carrier.size, format, &translated, 0, &replaced);
    if (!retain_caption) {
        /* An empty payload is omission; last-dataset carrier removal is separate. */
        readback(&replaced, "20240102", "20240102", "030405+0900", 0);
    } else {
        readback(&replaced, removal ? NULL : dates[mode][2],
                  removal ? NULL : dates[mode][3], NULL, 1);
    }
    omc_arena_fini(&replaced); omc_arena_fini(&carrier);
    omc_store_fini(&translated); omc_store_fini(&source); omc_store_fini(&stale);
}

int main(void)
{
    unsigned int mode;
    for (mode = 0U; mode < 5U; ++mode) {
        run_case(OMC_SCAN_FMT_JPEG, mode);
        run_case(OMC_SCAN_FMT_TIFF, mode);
    }
    return 0;
}
