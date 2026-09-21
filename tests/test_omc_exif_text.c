#include "omc/omc_exif.h"
#include "omc/omc_exif_tiff_serialize.h"
#include "omc/omc_translation.h"
#include "omc_test_assert.h"

#include <string.h>

static const char k_exif_ns[] = "http://ns.adobe.com/exif/1.0/";
static const char k_exif_ex_ns[] = "http://cipa.jp/exif/1.0/";

static omc_byte_ref
append_text(omc_store *store, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, text, strlen(text), &ref)
           == OMC_STATUS_OK);
    return ref;
}

static omc_byte_ref
append_bytes(omc_store *store, const void *data, omc_size size)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&store->arena, data, size, &ref) == OMC_STATUS_OK);
    return ref;
}

static omc_entry_id
add_xmp(omc_store *store, const char *schema, const char *path,
        const char *value, int dirty)
{
    omc_entry entry;
    omc_entry_id id;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(store, schema),
                              append_text(store, path));
    omc_val_make_text(&entry.value, append_text(store, value), OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.flags = dirty ? OMC_ENTRY_FLAG_DIRTY : OMC_ENTRY_FLAG_NONE;
    assert(omc_store_add_entry(store, &entry, &id) == OMC_STATUS_OK);
    return id;
}

static const omc_entry *
find_tag(const omc_store *store, omc_u16 tag)
{
    omc_size i;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        omc_const_bytes ifd;
        if (entry->key.kind != OMC_KEY_EXIF_TAG
            || entry->key.u.exif_tag.tag != tag
            || (entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        ifd = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd.size == 7U && memcmp(ifd.data, "exififd", 7U) == 0) {
            return entry;
        }
    }
    return (const omc_entry *)0;
}

static omc_const_bytes
value_bytes(const omc_store *store, const omc_entry *entry)
{
    return omc_arena_view(&store->arena, entry->value.u.ref);
}

static void
test_core_family(void)
{
    omc_store source;
    omc_store output;
    omc_store reread;
    omc_exif_text_translation_res result;
    omc_exif_tiff_res serial;
    omc_exif_res decoded;
    omc_mut_bytes bytes;
    omc_u8 buffer[65536];
    omc_const_bytes raw;
    const omc_entry *entry;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&reread);
    add_xmp(&source, k_exif_ns, "ExifVersion", "0300", 1);
    add_xmp(&source, k_exif_ns, "FlashpixVersion", "0100", 1);
    add_xmp(&source, k_exif_ns, "UserComment[@xml:lang=x-default]",
            "Hello & world", 1);
    add_xmp(&source, k_exif_ex_ns, "ImageTitle", "Title", 1);
    result = omc_translate_xmp_exif_text(&source, &output, (const omc_exif_text_translation_opts *)0);
    assert(result.status == OMC_EXIF_TEXT_TRANSLATION_OK);
    assert(result.entries_added == 4U && result.groups_translated == 4U);
    entry = find_tag(&output, 0x9000U);
    assert(entry != (const omc_entry *)0);
    raw = value_bytes(&output, entry);
    assert(raw.size == 4U && memcmp(raw.data, "0300", 4U) == 0);
    entry = find_tag(&output, 0xA000U);
    assert(entry != (const omc_entry *)0);
    raw = value_bytes(&output, entry);
    assert(raw.size == 4U && memcmp(raw.data, "0100", 4U) == 0);
    entry = find_tag(&output, 0x9286U);
    assert(entry != (const omc_entry *)0);
    raw = value_bytes(&output, entry);
    assert(raw.size == 21U && memcmp(raw.data, "ASCII\0\0\0Hello & world", 21U) == 0);
    entry = find_tag(&output, 0xA436U);
    assert(entry != (const omc_entry *)0 && entry->origin.wire_type.code == 2U);
    raw = value_bytes(&output, entry);
    assert(raw.size == 5U && memcmp(raw.data, "Title", 5U) == 0);

    bytes.data = buffer;
    bytes.size = sizeof(buffer);
    serial = omc_serialize_exif_tiff(&output, bytes, (const omc_exif_tiff_opts *)0);
    assert(serial.status == OMC_EXIF_TIFF_OK);
    decoded = omc_exif_dec(buffer, (omc_size)serial.written, &reread,
                           OMC_INVALID_BLOCK_ID, (omc_exif_ifd_ref *)0,
                           0U, (const omc_exif_opts *)0);
    assert(decoded.status == OMC_EXIF_OK || decoded.status == OMC_EXIF_TRUNCATED);
    assert(find_tag(&reread, 0x9000U) != (const omc_entry *)0);
    assert(find_tag(&reread, 0x9286U) != (const omc_entry *)0);

    omc_store_reset(&reread);
    result = omc_translate_xmp_exif_text(&output, &reread,
                                         (const omc_exif_text_translation_opts *)0);
    assert(result.status == OMC_EXIF_TEXT_TRANSLATION_OK);
    assert(result.groups_unchanged == 4U && result.groups_translated == 0U);
    omc_store_fini(&reread);
    omc_store_fini(&output);
    omc_store_fini(&source);
}

static void
test_non_ascii_and_conflict(void)
{
    omc_store source;
    omc_store output;
    omc_store legacy;
    omc_store legacy_output;
    omc_exif_text_translation_opts opts;
    omc_exif_text_translation_res result;
    omc_entry native;

    omc_store_init(&source);
    omc_store_init(&output);
    omc_store_init(&legacy);
    omc_store_init(&legacy_output);
    add_xmp(&source, k_exif_ns, "ExifVersion", "0300", 1);
    add_xmp(&source, k_exif_ns, "UserComment", "日本", 1);
    result = omc_translate_xmp_exif_text(&source, &output,
                                         (const omc_exif_text_translation_opts *)0);
    assert(result.status == OMC_EXIF_TEXT_TRANSLATION_OK);
    assert(value_bytes(&output, find_tag(&output, 0x9286U)).size == 14U);
    omc_store_fini(&output);
    omc_store_init(&output);

    add_xmp(&legacy, k_exif_ns, "ExifVersion", "0220", 1);
    add_xmp(&legacy, k_exif_ns, "UserComment", "日本", 1);
    result = omc_translate_xmp_exif_text(&legacy, &legacy_output,
                                         (const omc_exif_text_translation_opts *)0);
    assert(result.status == OMC_EXIF_TEXT_TRANSLATION_OK);
    {
        omc_const_bytes legacy_comment =
            value_bytes(&legacy_output, find_tag(&legacy_output, 0x9286U));
        assert(legacy_comment.size == 14U && legacy_comment.data[8] == 0xFFU
               && legacy_comment.data[9] == 0xFEU);
    }
    omc_store_reset(&output);
    result = omc_translate_xmp_exif_text(&legacy_output, &output,
                                         (const omc_exif_text_translation_opts *)0);
    assert(result.status == OMC_EXIF_TEXT_TRANSLATION_OK
           && result.groups_unchanged == 2U);

    memset(&native, 0, sizeof(native));
    omc_key_make_exif_tag(&native.key, append_text(&source, "exififd"),
                          0x9286U);
    omc_val_make_bytes(&native.value,
                       append_bytes(&source, "ASCII\0\0\0old", 11U));
    assert(omc_store_add_entry(&source, &native, (omc_entry_id *)0)
           == OMC_STATUS_OK);
    omc_exif_text_translation_opts_init(&opts);
    result = omc_translate_xmp_exif_text(&source, &output, &opts);
    assert(result.status == OMC_EXIF_TEXT_TRANSLATION_NATIVE_CONFLICT);
    omc_store_fini(&output);
    omc_store_fini(&legacy_output);
    omc_store_fini(&legacy);
    omc_store_fini(&source);
}

int
main(void)
{
    test_core_family();
    test_non_ascii_and_conflict();
    return omc_test_finish();
}
