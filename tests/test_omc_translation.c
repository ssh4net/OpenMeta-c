#include "omc/omc_translation.h"
#include "omc/omc_exif_tiff_serialize.h"
#include "omc/omc_exif.h"
#include "omc_test_assert.h"
#include <string.h>

static const char ns_xmp[] = "http://ns.adobe.com/xap/1.0/";
static const char ns_exif[] = "http://ns.adobe.com/exif/1.0/";
static const char ns_tiff[] = "http://ns.adobe.com/tiff/1.0/";
static const char ns_dc[] = "http://purl.org/dc/elements/1.1/";

static omc_byte_ref
text(omc_store *s, const char *value)
{
    omc_byte_ref r;
    assert(omc_arena_append(&s->arena, value, strlen(value), &r) == OMC_STATUS_OK);
    return r;
}

static omc_entry_id
add_xmp(omc_store *s, const char *ns, const char *path, const char *value, int dirty)
{
    omc_entry e;
    omc_entry_id id;
    memset(&e, 0, sizeof(e));
    omc_key_make_xmp_property(&e.key, text(s, ns), text(s, path));
    omc_val_make_text(&e.value, text(s, value), OMC_TEXT_UTF8);
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.flags = dirty ? OMC_ENTRY_FLAG_DIRTY : 0U;
    assert(omc_store_add_entry(s, &e, &id) == OMC_STATUS_OK);
    return id;
}

static const omc_entry *
exif(const omc_store *s, const char *ifd, omc_u16 tag)
{
    omc_size i;
    omc_const_bytes r;
    for (i = 0U; i < s->entry_count; ++i) {
        if (s->entries[i].key.kind != OMC_KEY_EXIF_TAG ||
            s->entries[i].key.u.exif_tag.tag != tag ||
            (s->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        r = omc_arena_view(&s->arena, s->entries[i].key.u.exif_tag.ifd);
        if (r.size == strlen(ifd) && memcmp(r.data, ifd, r.size) == 0)
            return &s->entries[i];
    }
    return NULL;
}

static void
expect_text(const omc_store *s, const omc_entry *e, const char *value)
{
    omc_const_bytes r;
    assert(e != NULL);
    r = omc_arena_view(&s->arena, e->value.u.ref);
    assert(r.size == strlen(value) && memcmp(r.data, value, r.size) == 0);
}

static void
test_all_groups(void)
{
    omc_store s;
    omc_store out;
    omc_store before;
    omc_store decoded;
    omc_translation_opts opts;
    omc_translation_res res;
    omc_transfer_target_image_spec target;
    const omc_entry *e;
    omc_entry native;
    omc_entry_id bad;
    omc_size i;
    omc_u32 creators;
    omc_u32 charset;
    omc_u8 bytes[4096];
    omc_mut_bytes output;
    omc_exif_tiff_res serial;
    omc_exif_res read;
    omc_store_init(&s);
    omc_store_init(&out);
    omc_store_init(&decoded);
    memset(&target, 0, sizeof(target));
    target.has_dimensions = 1;
    target.width = 640U;
    target.height = 480U;
    target.has_orientation = 1;
    target.orientation = 6U;
    add_xmp(&s, ns_xmp, "CreateDate", "2024-02-29T23:59:60-00:00", 1);
    add_xmp(&s, ns_xmp, "ModifyDate", "2026-09-07T12:34:56.123456789+09:00", 1);
    add_xmp(&s, ns_exif, "DateTimeOriginal", "2020-01-02T03:04:05Z", 1);
    add_xmp(&s, ns_xmp, "CreatorTool", "C writer", 1);
    add_xmp(&s, ns_tiff, "Make", "Camera", 1);
    add_xmp(&s, ns_tiff, "Model", "Model", 1);
    add_xmp(&s, ns_exif, "ExposureTime", "8e-3", 1);
    add_xmp(&s, ns_exif, "FNumber", "2.8", 1);
    add_xmp(&s, ns_exif, "ISO", "200", 1);
    add_xmp(&s, ns_exif, "FocalLength", "24 mm", 1);
    add_xmp(&s, ns_exif, "ExposureBiasValue", "-1/3", 1);
    add_xmp(&s, ns_dc, "title[@xml:lang=x-default]", "caf\303\251", 1);
    add_xmp(&s, ns_dc, "creator[10]", "Ten", 0);
    add_xmp(&s, ns_dc, "creator[2]", "Two", 1);
    add_xmp(&s, ns_dc, "subject[1]", "Nature", 1);
    add_xmp(&s, ns_tiff, "Orientation", "6", 1);
    add_xmp(&s, ns_tiff, "ImageWidth", "640", 1);
    add_xmp(&s, ns_exif, "ExifImageHeight", "480", 1);
    omc_translation_opts_init(&opts);
    res = omc_translate_xmp(&s, &out, &opts, &target);
    assert(res.status == OMC_TRANSLATION_OK && res.utf8_charset_added);
    expect_text(&out, exif(&out, "ifd0", 0x0131U), "C writer");
    expect_text(&out, exif(&out, "exififd", 0x9012U), "-00:00");
    expect_text(&out, exif(&out, "exififd", 0x9290U), "123456789");
    e = exif(&out, "exififd", 0x829AU);
    assert(e != NULL && e->value.elem_type == OMC_ELEM_URATIONAL &&
           e->value.u.ur.numer == 1U && e->value.u.ur.denom == 125U);
    e = exif(&out, "exififd", 0x829DU);
    assert(e != NULL && e->value.u.ur.numer == 14U && e->value.u.ur.denom == 5U);
    e = exif(&out, "exififd", 0x9204U);
    assert(e != NULL && e->value.u.sr.numer == -1 && e->value.u.sr.denom == 3);
    assert(exif(&out, "ifd0", 0x0100U)->value.u.u64 == 640U);
    assert(exif(&out, "exififd", 0xA003U)->value.u.u64 == 480U);
    creators = 0U;
    charset = 0U;
    for (i = 0U; i < out.entry_count; ++i) {
        e = &out.entries[i];
        if (e->key.kind != OMC_KEY_IPTC_DATASET)
            continue;
        if (e->key.u.iptc_dataset.record == 1U && e->key.u.iptc_dataset.dataset == 90U)
            charset++;
        if (e->key.u.iptc_dataset.record == 2U &&
            e->key.u.iptc_dataset.dataset == 80U) {
            expect_text(&out, e, creators == 0U ? "Two" : "Ten");
            creators++;
        }
    }
    assert(creators == 2U && charset == 1U);
    output.data = bytes;
    output.size = sizeof(bytes);
    serial = omc_serialize_exif_tiff(&out, output, NULL);
    assert(serial.status == OMC_EXIF_TIFF_OK);
    read = omc_exif_dec(bytes, (omc_size)serial.written, &decoded, OMC_INVALID_BLOCK_ID,
                        NULL, 0U, NULL);
    assert(read.status == OMC_EXIF_OK || read.status == OMC_EXIF_TRUNCATED);
    expect_text(&decoded, exif(&decoded, "ifd0", 0x0131U), "C writer");
    assert(exif(&decoded, "exififd", 0x9204U)->value.u.sr.numer == -1);
    before = out;
    bad = add_xmp(&s, ns_exif, "ISOSpeedRatings", "200", 1);
    res = omc_translate_xmp(&s, &out, &opts, &target);
    assert(res.status == OMC_TRANSLATION_AMBIGUOUS_SOURCE);
    assert(memcmp(&before, &out, sizeof(out)) == 0);
    s.entries[bad].flags = OMC_ENTRY_FLAG_DELETED;
    target.width = 641U;
    res = omc_translate_xmp(&s, &out, &opts, &target);
    assert(res.status == OMC_TRANSLATION_TARGET_MISMATCH);
    assert(memcmp(&before, &out, sizeof(out)) == 0);
    target.width = 640U;
    opts.max_operations = 1U;
    res = omc_translate_xmp(&s, &out, &opts, &target);
    assert(res.status == OMC_TRANSLATION_LIMIT);
    assert(memcmp(&before, &out, sizeof(out)) == 0);
    opts.max_operations = 4096U;
    memset(&native, 0, sizeof(native));
    omc_key_make_exif_tag(&native.key, text(&s, "ifd0"), 0x010FU);
    omc_val_make_text(&native.value, text(&s, "Old"), OMC_TEXT_ASCII);
    native.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(&s, &native, NULL) == OMC_STATUS_OK);
    res = omc_translate_xmp(&s, &out, &opts, &target);
    assert(res.status == OMC_TRANSLATION_NATIVE_CONFLICT);
    assert(memcmp(&before, &out, sizeof(out)) == 0);
    opts.conflict = OMC_TRANSLATION_REPLACE;
    res = omc_translate_xmp(&s, &out, &opts, &target);
    assert(res.status == OMC_TRANSLATION_OK && res.entries_updated == 1U);
    expect_text(&out, exif(&out, "ifd0", 0x010FU), "Camera");
    omc_store_fini(&decoded);
    omc_store_fini(&out);
    omc_store_fini(&s);
}

static void
test_fraction_and_tombstone(void)
{
    omc_store s;
    omc_store out;
    omc_store before;
    omc_translation_opts opts;
    omc_translation_res res;
    omc_entry_id id;
    omc_store_init(&s);
    omc_store_init(&out);
    id = add_xmp(&s, ns_xmp, "CreateDate", "2026-09-07T12:34:56.1Z", 1);
    omc_translation_opts_init(&opts);
    before = out;
    res = omc_translate_xmp(&s, &out, &opts, NULL);
    assert(res.status == OMC_TRANSLATION_UNSUPPORTED_PRECISION);
    assert(memcmp(&before, &out, sizeof(out)) == 0);
    opts.mappings = OMC_TRANSLATE_CREATE_EXIF;
    res = omc_translate_xmp(&s, &out, &opts, NULL);
    assert(res.status == OMC_TRANSLATION_OK);
    expect_text(&out, exif(&out, "exififd", 0x9292U), "1");
    omc_store_fini(&s);
    s = out;
    omc_store_init(&out);
    s.entries[id].flags |= OMC_ENTRY_FLAG_DELETED;
    opts.conflict = OMC_TRANSLATION_REPLACE;
    res = omc_translate_xmp(&s, &out, &opts, NULL);
    assert(res.status == OMC_TRANSLATION_OK && res.entries_removed == 3U);
    assert(exif(&out, "exififd", 0x9004U) == NULL);
    assert(exif(&out, "exififd", 0x9012U) == NULL);
    assert(exif(&out, "exififd", 0x9292U) == NULL);
    omc_store_fini(&out);
    omc_store_fini(&s);
}

int
main(void)
{
    test_all_groups();
    test_fraction_and_tombstone();
    return 0;
}
