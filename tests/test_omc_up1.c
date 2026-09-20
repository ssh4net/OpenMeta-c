#include "omc/omc_exif.h"
#include "omc/omc_xmp.h"
#include "omc_test_assert.h"

#include <string.h>

typedef struct up1_callback {
    const omc_u8 *bytes;
    omc_size size;
} up1_callback;

static omc_source_io_res
read_at(void *context, omc_u64 offset, omc_u8 *destination, omc_size size)
{
    up1_callback *callback;
    omc_source_io_res result;
    callback = (up1_callback *)context;
    result.code = OMC_SOURCE_IO_OK;
    result.bytes_read = 0U;
    if (offset > (omc_u64)callback->size ||
        size > callback->size - (omc_size)offset)
        return result;
    if (size != 0U)
        memcpy(destination, callback->bytes + (omc_size)offset, size);
    result.bytes_read = size;
    return result;
}

static void
put_u32le(omc_u8 *out, omc_u32 value)
{
    out[0] = (omc_u8)(value & 0xFFU);
    out[1] = (omc_u8)((value >> 8) & 0xFFU);
    out[2] = (omc_u8)((value >> 16) & 0xFFU);
    out[3] = (omc_u8)((value >> 24) & 0xFFU);
}

static void
put_u64le(omc_u8 *out, omc_u64 value)
{
    omc_u32 i;
    for (i = 0U; i < 8U; ++i) {
        out[i] = (omc_u8)(value & 0xFFU);
        value >>= 8;
    }
}

static void
test_root_ifd_bounds(void)
{
    static const omc_u64 offsets[] = {
        (omc_u64)0U,
        (omc_u64)16U,
        (omc_u64)0xFFFFFFF0U
    };
    omc_u8 tiff[24];
    omc_u32 i;

    for (i = 0U; i < (omc_u32)(sizeof(offsets) / sizeof(offsets[0])); ++i) {
        omc_store store;
        omc_exif_opts opts;
        omc_exif_res result;

        memset(tiff, 0, sizeof(tiff));
        tiff[0] = (omc_u8)'I';
        tiff[1] = (omc_u8)'I';
        put_u32le(tiff + 4U, (omc_u32)offsets[i]);
        tiff[2] = 42U;
        tiff[3] = 0U;
        omc_store_init(&store);
        omc_exif_opts_init(&opts);
        result = omc_exif_dec(tiff, 16U, &store, OMC_INVALID_BLOCK_ID,
                              (omc_exif_ifd_ref *)0, 0U, &opts);
        OMC_TEST_REQUIRE(result.status ==
                         (i == 0U ? OMC_EXIF_OK : OMC_EXIF_MALFORMED));
        OMC_TEST_REQUIRE(result.entries_decoded == 0U);
        OMC_TEST_REQUIRE(store.entry_count == 0U);
        omc_store_fini(&store);
    }

    for (i = 0U; i < (omc_u32)(sizeof(offsets) / sizeof(offsets[0])); ++i) {
        omc_store store;
        omc_exif_opts opts;
        omc_exif_res result;

        memset(tiff, 0, sizeof(tiff));
        tiff[0] = (omc_u8)'I';
        tiff[1] = (omc_u8)'I';
        tiff[2] = 43U;
        tiff[3] = 0U;
        tiff[4] = 8U;
        tiff[5] = 0U;
        tiff[6] = 0U;
        tiff[7] = 0U;
        put_u64le(tiff + 8U, offsets[i]);
        omc_store_init(&store);
        omc_exif_opts_init(&opts);
        result = omc_exif_dec(tiff, 24U, &store, OMC_INVALID_BLOCK_ID,
                              (omc_exif_ifd_ref *)0, 0U, &opts);
        OMC_TEST_REQUIRE(result.status ==
                         (i == 0U ? OMC_EXIF_OK : OMC_EXIF_MALFORMED));
        OMC_TEST_REQUIRE(result.entries_decoded == 0U);
        OMC_TEST_REQUIRE(store.entry_count == 0U);
        omc_store_fini(&store);
    }
}

static void
test_callback_root_ifd_bounds(void)
{
    static const omc_u64 offsets[] = {
        (omc_u64)0U,
        (omc_u64)16U,
        (omc_u64)0xFFFFFFF0U
    };
    omc_u8 tiff[24];
    up1_callback callback;
    omc_u32 i;

    for (i = 0U; i < (omc_u32)(sizeof(offsets) / sizeof(offsets[0])); ++i) {
        omc_store store;
        omc_exif_opts opts;
        omc_exif_source_res result;
        omc_exif_source_workspace workspace;
        omc_u8 value[32];
        omc_source_range range;
        omc_source_state state;

        memset(tiff, 0, sizeof(tiff));
        tiff[0] = (omc_u8)'I';
        tiff[1] = (omc_u8)'I';
        tiff[2] = 42U;
        tiff[3] = 0U;
        put_u32le(tiff + 4U, (omc_u32)offsets[i]);
        callback.bytes = tiff;
        callback.size = 16U;
        range.source = omc_source_callback(16U, &callback, read_at, 0);
        range.source_offset = 0U;
        range.size = 16U;
        omc_source_state_init(&state);
        workspace.value = value;
        workspace.value_capacity = sizeof(value);
        omc_store_init(&store);
        omc_exif_opts_init(&opts);
        result = omc_exif_dec_source(&range, &store, OMC_INVALID_BLOCK_ID,
                                     (omc_exif_ifd_ref *)0, 0U, &workspace, &state,
                                     NULL, &opts);
        OMC_TEST_REQUIRE(result.decoded.status ==
                         (i == 0U ? OMC_EXIF_OK : OMC_EXIF_MALFORMED));
        OMC_TEST_REQUIRE(result.decoded.entries_decoded == 0U);
        OMC_TEST_REQUIRE(store.entry_count == 0U);
        omc_store_fini(&store);
    }

    for (i = 0U; i < (omc_u32)(sizeof(offsets) / sizeof(offsets[0])); ++i) {
        omc_store store;
        omc_exif_opts opts;
        omc_exif_source_res result;
        omc_exif_source_workspace workspace;
        omc_u8 value[32];
        omc_source_range range;
        omc_source_state state;

        memset(tiff, 0, sizeof(tiff));
        tiff[0] = (omc_u8)'I';
        tiff[1] = (omc_u8)'I';
        tiff[2] = 43U;
        tiff[3] = 0U;
        tiff[4] = 8U;
        put_u64le(tiff + 8U, offsets[i]);
        callback.bytes = tiff;
        callback.size = 24U;
        range.source = omc_source_callback(24U, &callback, read_at, 0);
        range.source_offset = 0U;
        range.size = 24U;
        omc_source_state_init(&state);
        workspace.value = value;
        workspace.value_capacity = sizeof(value);
        omc_store_init(&store);
        omc_exif_opts_init(&opts);
        result = omc_exif_dec_source(&range, &store, OMC_INVALID_BLOCK_ID,
                                     (omc_exif_ifd_ref *)0, 0U, &workspace, &state,
                                     NULL, &opts);
        OMC_TEST_REQUIRE(result.decoded.status ==
                         (i == 0U ? OMC_EXIF_OK : OMC_EXIF_MALFORMED));
        OMC_TEST_REQUIRE(result.decoded.entries_decoded == 0U);
        OMC_TEST_REQUIRE(store.entry_count == 0U);
        omc_store_fini(&store);
    }
}

static omc_size
append_text(omc_u8 *out, omc_size offset, const char *text)
{
    omc_size size;
    size = strlen(text);
    memcpy(out + offset, text, size);
    return offset + size;
}

static omc_size
make_camera_xmp(omc_u8 *out, const char *schema, const char *name,
                omc_u32 form)
{
    omc_size size;
    size = 0U;
    size = append_text(out, size,
                       "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:e='");
    size = append_text(out, size, schema);
    size = append_text(out, size, "'");
    if (form == 0U) {
        size = append_text(out, size, " e:");
        size = append_text(out, size, name);
        size = append_text(out, size,
                           "=' 001 &amp; value '><e:Unrelated> trimmed </e:Unrelated>");
    } else if (form == 1U) {
        size = append_text(out, size, "><e:");
        size = append_text(out, size, name);
        size = append_text(out, size,
                           " rdf:resource=' 001 &amp; value '/><e:Unrelated> trimmed </e:Unrelated>");
    } else {
        size = append_text(out, size, "><e:");
        size = append_text(out, size, name);
        size = append_text(out, size, "> 001 &amp; value </e:");
        size = append_text(out, size, name);
        size = append_text(out, size,
                           "><e:Unrelated> trimmed </e:Unrelated>");
    }
    size = append_text(out, size, "</rdf:Description></rdf:RDF>");
    return size;
}

static const omc_entry *
find_xmp(const omc_store *store, const char *schema, const char *path)
{
    omc_size i;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry;
        omc_const_bytes schema_view;
        omc_const_bytes path_view;
        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_XMP_PROPERTY)
            continue;
        schema_view = omc_arena_view(&store->arena,
                                     entry->key.u.xmp_property.schema_ns);
        path_view = omc_arena_view(&store->arena,
                                   entry->key.u.xmp_property.property_path);
        if (schema_view.size == strlen(schema) && path_view.size == strlen(path) &&
            memcmp(schema_view.data, schema, schema_view.size) == 0 &&
            memcmp(path_view.data, path, path_view.size) == 0)
            return entry;
    }
    return (const omc_entry *)0;
}

static void
test_scoped_camera_whitespace(void)
{
    static const char *const names[] = {
        "SpectralSensitivity", "CameraOwnerName", "BodySerialNumber",
        "LensMake", "LensModel", "LensSerialNumber"
    };
    static const char *const exif_schema = "http://ns.adobe.com/exif/1.0/";
    static const char *const cipa_schema = "http://cipa.jp/exif/1.0/";
    omc_u8 packet[768];
    omc_u32 i;
    omc_u32 form;

    for (i = 0U; i < 6U; ++i) {
        for (form = 0U; form < 3U; ++form) {
            const char *schema;
            omc_size packet_size;
            omc_store store;
            omc_xmp_res result;
            const omc_entry *entry;
            omc_const_bytes value;
            schema = i == 0U || form == 0U ? exif_schema : cipa_schema;
            packet_size = make_camera_xmp(packet, schema, names[i], form);
            omc_store_init(&store);
            result = omc_xmp_dec(packet, packet_size, &store,
                                 OMC_INVALID_BLOCK_ID, OMC_ENTRY_FLAG_NONE,
                                 (const omc_xmp_opts *)0);
            OMC_TEST_REQUIRE(result.status == OMC_XMP_OK);
            entry = find_xmp(&store, schema, names[i]);
            OMC_TEST_REQUIRE(entry != (const omc_entry *)0 &&
                             entry->value.kind == OMC_VAL_TEXT);
            value = omc_arena_view(&store.arena, entry->value.u.ref);
            OMC_TEST_REQUIRE(value.size == strlen(" 001 & value ") &&
                             memcmp(value.data, " 001 & value ", value.size) == 0);
            entry = find_xmp(&store, schema, "Unrelated");
            OMC_TEST_REQUIRE(entry != (const omc_entry *)0);
            value = omc_arena_view(&store.arena, entry->value.u.ref);
            OMC_TEST_REQUIRE(value.size == strlen("trimmed") &&
                             memcmp(value.data, "trimmed", value.size) == 0);
            omc_store_fini(&store);
        }
    }
}

int
main(void)
{
    test_root_ifd_bounds();
    test_callback_root_ifd_bounds();
    test_scoped_camera_whitespace();
    return 0;
}
