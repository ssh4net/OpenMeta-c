#include "omc/omc_icc.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer_artifact.h"
#include "omc/omc_transfer_package.h"

#include "omc_test_assert.h"
#include <string.h>

static void
write_u16be_at(omc_u8* out, omc_size off, omc_u16 value)
{
    out[off + 0U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 1U] = (omc_u8)(value & 0xFFU);
}

static void
write_u16le_at(omc_u8* out, omc_size off, omc_u16 value)
{
    out[off + 0U] = (omc_u8)(value & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 8) & 0xFFU);
}

static void
write_u32be_at(omc_u8* out, omc_size off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)((value >> 24) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 3U] = (omc_u8)(value & 0xFFU);
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
write_u64be_at(omc_u8* out, omc_size off, omc_u64 value)
{
    out[off + 0U] = (omc_u8)((value >> 56) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 48) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 40) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 32) & 0xFFU);
    out[off + 4U] = (omc_u8)((value >> 24) & 0xFFU);
    out[off + 5U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 6U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 7U] = (omc_u8)(value & 0xFFU);
}

static void
build_test_icc_blob(omc_u8* out, omc_size size)
{
    omc_u32 i;

    memset(out, 0, size);
    write_u32be_at(out, 0U, (omc_u32)size);
    write_u32be_at(out, 4U, 0x6170706CU);
    write_u32be_at(out, 8U, 0x04300000U);
    write_u32be_at(out, 12U, 0x6D6E7472U);
    write_u32be_at(out, 16U, 0x52474220U);
    write_u32be_at(out, 20U, 0x58595A20U);
    write_u16be_at(out, 24U, 2026U);
    write_u16be_at(out, 26U, 1U);
    write_u16be_at(out, 28U, 28U);
    out[36U] = (omc_u8)'a';
    out[37U] = (omc_u8)'c';
    out[38U] = (omc_u8)'s';
    out[39U] = (omc_u8)'p';
    write_u32be_at(out, 40U, 0x4D534654U);
    write_u32be_at(out, 44U, 1U);
    write_u32be_at(out, 48U, 0x4150504CU);
    write_u32be_at(out, 52U, 0x4D313233U);
    write_u64be_at(out, 56U, 1U);
    write_u32be_at(out, 64U, 1U);
    write_u32be_at(out, 68U, 63189U);
    write_u32be_at(out, 72U, 65536U);
    write_u32be_at(out, 76U, 54061U);
    write_u32be_at(out, 80U, 0x6F706E6DU);
    write_u32be_at(out, 128U, 1U);
    write_u32be_at(out, 132U, 0x64657363U);
    write_u32be_at(out, 136U, 144U);
    write_u32be_at(out, 140U, 16U);
    for (i = 0U; i < 16U; ++i) {
        out[144U + i] = (omc_u8)i;
    }
}

static omc_byte_ref
append_bytes(omc_arena* arena, const void* src, omc_size size)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, src, size, &ref);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    return ref;
}

static omc_byte_ref
append_cstr(omc_arena* arena, const char* text)
{
    return append_bytes(arena, text, (omc_size)strlen(text));
}

static int
bytes_eq(omc_const_bytes view, const char* literal)
{
    omc_size literal_size;

    literal_size = (omc_size)strlen(literal);
    return view.size == literal_size
           && memcmp(view.data, literal, literal_size) == 0;
}

static int
bytes_contains(omc_const_bytes haystack, const char* needle)
{
    omc_size needle_size;
    omc_size i;

    needle_size = (omc_size)strlen(needle);
    if (needle_size == 0U) {
        return 1;
    }
    if (haystack.size < needle_size) {
        return 0;
    }
    for (i = 0U; i + needle_size <= haystack.size; ++i) {
        if (memcmp(haystack.data + i, needle, needle_size) == 0) {
            return 1;
        }
    }
    return 0;
}

static void
assert_package_materializes(const omc_transfer_package_batch* batch,
                            const omc_u8* expected, omc_size expected_size)
{
    omc_u8 bounded[512];
    omc_u8 small[8];
    omc_arena materialized;
    omc_arena serialized;
    omc_arena temp_storage;
    omc_transfer_package_io_res io_res;
    omc_transfer_package_io_res serialized_res;
    omc_status status;
    omc_size small_cap;

    OMC_TEST_REQUIRE(batch != (const omc_transfer_package_batch*)0);
    OMC_TEST_REQUIRE(expected != (const omc_u8*)0 || expected_size == 0U);
    OMC_TEST_REQUIRE(expected_size <= sizeof(bounded));

    omc_arena_init(&materialized);
    status = omc_transfer_package_batch_materialize(batch, &materialized,
                                                   &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.bytes, expected_size);
    OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, batch->chunk_count);
    OMC_TEST_REQUIRE_U64_EQ(materialized.size, expected_size);
    if (expected_size != 0U) {
        OMC_TEST_CHECK(memcmp(materialized.data, expected, expected_size) == 0);
    }
    omc_arena_fini(&materialized);

    omc_arena_init(&serialized);
    omc_arena_init(&temp_storage);
    status = omc_transfer_package_batch_serialize(batch, &serialized,
                                                  &serialized_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(serialized_res.status, OMC_TRANSFER_OK);

    memset(bounded, 0xA5, sizeof(bounded));
    status = omc_transfer_package_batch_materialize_to_buffer(
        batch, bounded, sizeof(bounded), &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.bytes, expected_size);
    OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, batch->chunk_count);
    if (expected_size != 0U) {
        OMC_TEST_CHECK(memcmp(bounded, expected, expected_size) == 0);
    }
    if (expected_size < sizeof(bounded)) {
        OMC_TEST_CHECK_U64_EQ(bounded[expected_size], 0xA5U);
    }

    if (expected_size != 0U) {
        small_cap = sizeof(small);
        if (small_cap >= expected_size) {
            small_cap = expected_size - 1U;
        }
        memset(small, 0x5AU, sizeof(small));
        status = omc_transfer_package_batch_materialize_to_buffer(
            batch, (omc_u8*)0, 0U, &io_res);
        OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
        OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_LIMIT);
        OMC_TEST_REQUIRE_U64_EQ(io_res.bytes, expected_size);
        OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, batch->chunk_count);

        status = omc_transfer_package_batch_materialize_to_buffer(
            batch, small, small_cap, &io_res);
        OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
        OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_LIMIT);
        OMC_TEST_REQUIRE_U64_EQ(io_res.bytes, expected_size);
        OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, batch->chunk_count);
        OMC_TEST_CHECK_U64_EQ(small[0], 0x5AU);
    }

    memset(bounded, 0xC3, sizeof(bounded));
    status = omc_transfer_package_bytes_materialize_to_buffer(
        serialized.data, serialized.size, &temp_storage, bounded,
        sizeof(bounded), &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.bytes, expected_size);
    OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, batch->chunk_count);
    if (expected_size != 0U) {
        OMC_TEST_CHECK(memcmp(bounded, expected, expected_size) == 0);
    }
    if (expected_size < sizeof(bounded)) {
        OMC_TEST_CHECK_U64_EQ(bounded[expected_size], 0xC3U);
    }

    if (expected_size != 0U) {
        omc_arena_reset(&temp_storage);
        status = omc_transfer_package_bytes_materialize_to_buffer(
            serialized.data, serialized.size, &temp_storage, (omc_u8*)0, 0U,
            &io_res);
        OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
        OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_LIMIT);
        OMC_TEST_REQUIRE_U64_EQ(io_res.bytes, expected_size);
        OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, batch->chunk_count);
    }

    omc_arena_fini(&temp_storage);
    omc_arena_fini(&serialized);
}

static int
chunk_has_box_type(omc_const_bytes bytes, const char* type)
{
    return bytes.size >= 8U && memcmp(bytes.data + 4U, type, 4U) == 0;
}

static int
chunk_has_png_type(omc_const_bytes bytes, const char* type)
{
    return bytes.size >= 12U && memcmp(bytes.data + 4U, type, 4U) == 0;
}

static int
chunk_has_webp_type(omc_const_bytes bytes, const char* type)
{
    return bytes.size >= 8U && memcmp(bytes.data, type, 4U) == 0;
}

static void
append_test_png_chunk(omc_u8* out, omc_size capacity, omc_size* io_size,
                      const char* type, const omc_u8* payload,
                      omc_size payload_size)
{
    omc_size off;

    OMC_TEST_REQUIRE(io_size != (omc_size*)0);
    OMC_TEST_REQUIRE(type != (const char*)0);
    OMC_TEST_REQUIRE(payload != (const omc_u8*)0 || payload_size == 0U);
    OMC_TEST_REQUIRE(*io_size <= capacity);
    OMC_TEST_REQUIRE(payload_size <= capacity - *io_size);
    OMC_TEST_REQUIRE(capacity - *io_size - payload_size >= 12U);

    off = *io_size;
    write_u32be_at(out, off, (omc_u32)payload_size);
    memcpy(out + off + 4U, type, 4U);
    if (payload_size != 0U) {
        memcpy(out + off + 8U, payload, payload_size);
    }
    memset(out + off + 8U + payload_size, 0, 4U);
    *io_size = off + 12U + payload_size;
}

static void
append_test_webp_chunk(omc_u8* out, omc_size capacity, omc_size* io_size,
                       const char* type, const omc_u8* payload,
                       omc_size payload_size)
{
    omc_size off;
    omc_size padded_size;

    OMC_TEST_REQUIRE(io_size != (omc_size*)0);
    OMC_TEST_REQUIRE(type != (const char*)0);
    OMC_TEST_REQUIRE(payload != (const omc_u8*)0 || payload_size == 0U);
    OMC_TEST_REQUIRE(*io_size <= capacity);
    padded_size = payload_size + (payload_size & 1U);
    OMC_TEST_REQUIRE(padded_size <= capacity - *io_size);
    OMC_TEST_REQUIRE(capacity - *io_size - padded_size >= 8U);

    off = *io_size;
    memcpy(out + off, type, 4U);
    write_u32le_at(out, off + 4U, (omc_u32)payload_size);
    if (payload_size != 0U) {
        memcpy(out + off + 8U, payload, payload_size);
    }
    if ((payload_size & 1U) != 0U) {
        out[off + 8U + payload_size] = 0U;
    }
    *io_size = off + 8U + padded_size;
}

static void
finish_test_webp(omc_u8* bytes, omc_size size)
{
    OMC_TEST_REQUIRE(bytes != (omc_u8*)0);
    OMC_TEST_REQUIRE(size >= 12U);
    memcpy(bytes, "RIFF", 4U);
    write_u32le_at(bytes, 4U, (omc_u32)(size - 8U));
    memcpy(bytes + 8U, "WEBP", 4U);
}

static void
append_test_bmff_box(omc_u8* out, omc_size capacity, omc_size* io_size,
                     const char* type, const omc_u8* payload,
                     omc_size payload_size)
{
    omc_size off;

    OMC_TEST_REQUIRE(io_size != (omc_size*)0);
    OMC_TEST_REQUIRE(type != (const char*)0);
    OMC_TEST_REQUIRE(payload != (const omc_u8*)0 || payload_size == 0U);
    OMC_TEST_REQUIRE(*io_size <= capacity);
    OMC_TEST_REQUIRE(payload_size <= capacity - *io_size);
    OMC_TEST_REQUIRE(capacity - *io_size - payload_size >= 8U);

    off = *io_size;
    write_u32be_at(out, off, (omc_u32)(payload_size + 8U));
    memcpy(out + off + 4U, type, 4U);
    if (payload_size != 0U) {
        memcpy(out + off + 8U, payload, payload_size);
    }
    *io_size = off + 8U + payload_size;
}

static void
add_test_xmp_entry(omc_store* store, const char* value_text)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(
        &entry.key,
        append_cstr(&store->arena, "http://ns.adobe.com/xap/1.0/"),
        append_cstr(&store->arena, "CreatorTool"));
    omc_val_make_text(&entry.value, append_cstr(&store->arena, value_text),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_test_exif_entry(omc_store* store, const char* value_text)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_cstr(&store->arena, "ifd0"),
                          0x010FU);
    omc_val_make_text(&entry.value, append_cstr(&store->arena, value_text),
                      OMC_TEXT_ASCII);
    status = omc_store_add_entry(store, &entry, NULL);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_test_iptc_entry(omc_store* store, const char* value_text)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 120U);
    omc_val_make_text(&entry.value, append_cstr(&store->arena, value_text),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_test_jumbf_cbor_text_entry(omc_store* store, const char* key_text,
                               const char* value_text)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_jumbf_cbor_key(&entry.key,
                                append_cstr(&store->arena, key_text));
    omc_val_make_text(&entry.value, append_cstr(&store->arena, value_text),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, NULL);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_test_jumbf_cbor_u64_entry(omc_store* store, const char* key_text,
                              omc_u64 value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_jumbf_cbor_key(&entry.key,
                                append_cstr(&store->arena, key_text));
    omc_val_make_u64(&entry.value, value);
    status = omc_store_add_entry(store, &entry, NULL);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_test_icc_entries(omc_store* store)
{
    omc_u8 icc[160];
    omc_icc_res res;

    build_test_icc_blob(icc, sizeof(icc));
    res = omc_icc_dec(icc, sizeof(icc), store, OMC_INVALID_BLOCK_ID,
                      (const omc_icc_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_ICC_OK);
}

typedef struct replay_state {
    omc_scan_fmt begin_target;
    omc_u32 begin_chunk_count;
    omc_scan_fmt end_target;
    omc_u32 emitted;
    omc_const_bytes routes[8];
    omc_transfer_package_chunk_kind kinds[8];
    omc_transfer_semantic_kind semantic_kinds[8];
    omc_u64 output_offsets[8];
} replay_state;

static omc_transfer_status
replay_begin(void* user, omc_scan_fmt target_format, omc_u32 chunk_count)
{
    replay_state* state;

    state = (replay_state*)user;
    state->begin_target = target_format;
    state->begin_chunk_count = chunk_count;
    return OMC_TRANSFER_OK;
}

static omc_transfer_status
replay_emit(void* user, const omc_transfer_package_view* view)
{
    replay_state* state;

    state = (replay_state*)user;
    if (state->emitted < 8U) {
        state->routes[state->emitted] = view->route;
        state->kinds[state->emitted] = view->package_kind;
        state->semantic_kinds[state->emitted] = view->semantic_kind;
        state->output_offsets[state->emitted] = view->output_offset;
    }
    state->emitted += 1U;
    return OMC_TRANSFER_OK;
}

static omc_transfer_status
replay_end(void* user, omc_scan_fmt target_format)
{
    replay_state* state;

    state = (replay_state*)user;
    state->end_target = target_format;
    return OMC_TRANSFER_OK;
}

static void
test_transfer_package_build_jpeg_roundtrip_and_replay(void)
{
    omc_store store;
    omc_arena storage;
    omc_arena serialized;
    omc_arena parsed_storage;
    omc_transfer_package_build_opts opts;
    omc_transfer_package_batch batch;
    omc_transfer_package_batch parsed;
    omc_transfer_package_io_res io_res;
    omc_transfer_package_io_res parse_res;
    omc_transfer_package_io_res view_res;
    omc_transfer_package_replay_callbacks callbacks;
    omc_transfer_package_replay_res replay_res;
    omc_transfer_package_view views[4];
    omc_transfer_artifact_info info;
    omc_transfer_artifact_io_res inspect_res;
    replay_state state;
    omc_status status;
    omc_u64 payload_bytes;

    omc_store_init(&store);
    omc_arena_init(&storage);
    omc_arena_init(&serialized);
    omc_arena_init(&parsed_storage);

    add_test_exif_entry(&store, "OpenMeta Camera");
    add_test_xmp_entry(&store, "OpenMeta-c");
    add_test_icc_entries(&store);
    add_test_iptc_entry(&store, "Package Batch");

    omc_transfer_package_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;

    status = omc_transfer_package_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, 0U);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 4U);

    OMC_TEST_CHECK(bytes_eq(batch.chunks[0].route, "jpeg:app1-exif"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].block_index, 0xFFFFFFFFU);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].jpeg_marker_code, 0xE1U);
    OMC_TEST_CHECK(batch.chunks[0].bytes.size > 10U);
    OMC_TEST_CHECK(memcmp(batch.chunks[0].bytes.data, "\xFF\xE1", 2U) == 0);
    OMC_TEST_CHECK(memcmp(batch.chunks[0].bytes.data + 4U, "Exif\0\0", 6U)
                   == 0);

    OMC_TEST_CHECK(bytes_eq(batch.chunks[1].route, "jpeg:app1-xmp"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].jpeg_marker_code, 0xE1U);
    OMC_TEST_CHECK(bytes_contains(batch.chunks[1].bytes,
                                  "http://ns.adobe.com/xap/1.0/"));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[1].bytes, "OpenMeta-c"));

    OMC_TEST_CHECK(bytes_eq(batch.chunks[2].route, "jpeg:app2-icc"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].jpeg_marker_code, 0xE2U);
    OMC_TEST_CHECK(bytes_contains(batch.chunks[2].bytes, "ICC_PROFILE\0"));

    OMC_TEST_CHECK(bytes_eq(batch.chunks[3].route, "jpeg:app13-iptc"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].jpeg_marker_code, 0xEDU);
    OMC_TEST_CHECK(bytes_contains(batch.chunks[3].bytes, "Photoshop 3.0"));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[3].bytes, "8BIM"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].output_offset,
                          (omc_u64)batch.chunks[0].bytes.size);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].output_offset,
                          (omc_u64)batch.chunks[0].bytes.size
                              + (omc_u64)batch.chunks[1].bytes.size);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].output_offset,
                          (omc_u64)batch.chunks[0].bytes.size
                              + (omc_u64)batch.chunks[1].bytes.size
                              + (omc_u64)batch.chunks[2].bytes.size);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.output_size,
                          (omc_u64)batch.chunks[0].bytes.size
                              + (omc_u64)batch.chunks[1].bytes.size
                              + (omc_u64)batch.chunks[2].bytes.size
                              + (omc_u64)batch.chunks[3].bytes.size);

    status = omc_transfer_package_batch_serialize(&batch, &serialized,
                                                  &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, 4U);
    OMC_TEST_CHECK(serialized.size > 8U);
    OMC_TEST_CHECK(memcmp(serialized.data, "OMTPKG01", 8U) == 0);

    status = omc_transfer_package_batch_deserialize(
        serialized.data, serialized.size, &parsed_storage, &parsed,
        &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(parsed.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_REQUIRE_U64_EQ(parsed.input_size, 0U);
    OMC_TEST_REQUIRE_U64_EQ(parsed.output_size, batch.output_size);
    OMC_TEST_REQUIRE_U64_EQ(parsed.chunk_count, 4U);
    OMC_TEST_CHECK(bytes_eq(parsed.chunks[0].route, "jpeg:app1-exif"));
    OMC_TEST_CHECK(bytes_eq(parsed.chunks[1].route, "jpeg:app1-xmp"));
    OMC_TEST_CHECK(bytes_eq(parsed.chunks[2].route, "jpeg:app2-icc"));
    OMC_TEST_CHECK(bytes_eq(parsed.chunks[3].route, "jpeg:app13-iptc"));
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[1].output_offset,
                          (omc_u64)parsed.chunks[0].bytes.size);

    status = omc_transfer_package_batch_collect_views(&parsed, views, 4U,
                                                      &view_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(view_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(view_res.chunk_count, 4U);
    OMC_TEST_CHECK_U64_EQ(views[0].semantic_kind, OMC_TRANSFER_SEMANTIC_EXIF);
    OMC_TEST_CHECK_U64_EQ(views[1].semantic_kind, OMC_TRANSFER_SEMANTIC_XMP);
    OMC_TEST_CHECK_U64_EQ(views[2].semantic_kind, OMC_TRANSFER_SEMANTIC_ICC);
    OMC_TEST_CHECK_U64_EQ(views[3].semantic_kind, OMC_TRANSFER_SEMANTIC_IPTC);
    OMC_TEST_CHECK_U64_EQ(views[0].package_kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT);
    OMC_TEST_CHECK(bytes_eq(views[0].route, "jpeg:app1-exif"));

    status = omc_transfer_package_batch_collect_views(
        &parsed, (omc_transfer_package_view*)0, 0U, &view_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(view_res.status, OMC_TRANSFER_LIMIT);
    OMC_TEST_CHECK_U64_EQ(view_res.chunk_count, 4U);

    memset(&state, 0, sizeof(state));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_batch = replay_begin;
    callbacks.emit_chunk = replay_emit;
    callbacks.end_batch = replay_end;
    callbacks.user = &state;
    status = omc_transfer_package_batch_replay(&parsed, &callbacks,
                                               &replay_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(replay_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(replay_res.replayed, 4U);
    OMC_TEST_CHECK_U64_EQ(state.begin_target, OMC_SCAN_FMT_JPEG);
    OMC_TEST_CHECK_U64_EQ(state.begin_chunk_count, 4U);
    OMC_TEST_CHECK_U64_EQ(state.end_target, OMC_SCAN_FMT_JPEG);
    OMC_TEST_CHECK_U64_EQ(state.emitted, 4U);
    OMC_TEST_CHECK(bytes_eq(state.routes[0], "jpeg:app1-exif"));
    OMC_TEST_CHECK(bytes_eq(state.routes[1], "jpeg:app1-xmp"));
    OMC_TEST_CHECK(bytes_eq(state.routes[2], "jpeg:app2-icc"));
    OMC_TEST_CHECK(bytes_eq(state.routes[3], "jpeg:app13-iptc"));
    OMC_TEST_CHECK_U64_EQ(state.semantic_kinds[0],
                          OMC_TRANSFER_SEMANTIC_EXIF);
    OMC_TEST_CHECK_U64_EQ(state.semantic_kinds[1],
                          OMC_TRANSFER_SEMANTIC_XMP);
    OMC_TEST_CHECK_U64_EQ(state.semantic_kinds[2],
                          OMC_TRANSFER_SEMANTIC_ICC);
    OMC_TEST_CHECK_U64_EQ(state.semantic_kinds[3],
                          OMC_TRANSFER_SEMANTIC_IPTC);
    OMC_TEST_CHECK_U64_EQ(state.output_offsets[0], 0U);

    status = omc_transfer_artifact_inspect(serialized.data, serialized.size,
                                           &info, &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(inspect_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(info.kind,
                          OMC_TRANSFER_ARTIFACT_TRANSFER_PACKAGE_BATCH);
    OMC_TEST_CHECK_U64_EQ(info.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_CHECK_U64_EQ(info.entry_count, 4U);
    payload_bytes = (omc_u64)parsed.chunks[0].bytes.size
                    + (omc_u64)parsed.chunks[1].bytes.size
                    + (omc_u64)parsed.chunks[2].bytes.size
                    + (omc_u64)parsed.chunks[3].bytes.size;
    OMC_TEST_CHECK_U64_EQ(info.payload_bytes, payload_bytes);
    OMC_TEST_CHECK_U64_EQ(info.binding_bytes,
                          (omc_u64)serialized.size - payload_bytes);

    omc_arena_fini(&parsed_storage);
    omc_arena_fini(&serialized);
    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_package_build_jxl_chunks(void)
{
    omc_store store;
    omc_arena storage;
    omc_transfer_package_build_opts opts;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&storage);

    add_test_exif_entry(&store, "JXL Camera");
    add_test_xmp_entry(&store, "OpenMeta-c");
    add_test_icc_entries(&store);
    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.label", "alpha");
    add_test_jumbf_cbor_u64_entry(&store, "box.0.1.cbor.count", 7U);

    omc_transfer_package_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JXL;
    opts.include_iptc = 0;

    status = omc_transfer_package_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_JXL);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 4U);

    OMC_TEST_CHECK(bytes_eq(batch.chunks[0].route, "jxl:box-exif"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[0].bytes, "Exif"));

    OMC_TEST_CHECK(bytes_eq(batch.chunks[1].route, "jxl:box-xml"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[1].bytes, "xml "));

    OMC_TEST_CHECK(bytes_eq(batch.chunks[2].route, "jxl:icc-profile"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK);
    OMC_TEST_CHECK(batch.chunks[2].bytes.size >= 40U);
    OMC_TEST_CHECK(memcmp(batch.chunks[2].bytes.data + 36U, "acsp", 4U)
                   == 0);

    OMC_TEST_CHECK(bytes_eq(batch.chunks[3].route, "jxl:box-jumb"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[3].bytes, "jumb"));

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_package_source_range_roundtrip(void)
{
    static const omc_u8 k_source_bytes[4] = {
        (omc_u8)'S', (omc_u8)'R', (omc_u8)'C', (omc_u8)'!'
    };
    omc_arena serialized;
    omc_arena parsed_storage;
    omc_transfer_package_chunk chunk;
    omc_transfer_package_batch batch;
    omc_transfer_package_batch parsed;
    omc_transfer_package_io_res io_res;
    omc_transfer_package_io_res parse_res;
    omc_transfer_package_io_res view_res;
    omc_transfer_package_replay_callbacks callbacks;
    omc_transfer_package_replay_res replay_res;
    omc_transfer_package_view view;
    omc_transfer_artifact_info info;
    omc_transfer_artifact_io_res inspect_res;
    replay_state state;
    omc_status status;

    omc_arena_init(&serialized);
    omc_arena_init(&parsed_storage);
    omc_transfer_package_batch_init(&batch);

    memset(&chunk, 0, sizeof(chunk));
    chunk.kind = OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE;
    chunk.output_offset = 0U;
    chunk.source_offset = 12U;
    chunk.block_index = 0xFFFFFFFFU;
    chunk.bytes.data = k_source_bytes;
    chunk.bytes.size = sizeof(k_source_bytes);

    batch.target_format = OMC_SCAN_FMT_JPEG;
    batch.input_size = 32U;
    batch.output_size = sizeof(k_source_bytes);
    batch.chunk_count = 1U;
    batch.chunks = &chunk;

    status = omc_transfer_package_batch_serialize(&batch, &serialized,
                                                  &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK(serialized.size > 8U);
    OMC_TEST_CHECK(memcmp(serialized.data, "OMTPKG01", 8U) == 0);

    status = omc_transfer_package_batch_deserialize(
        serialized.data, serialized.size, &parsed_storage, &parsed,
        &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(parsed.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_REQUIRE_U64_EQ(parsed.input_size, 32U);
    OMC_TEST_REQUIRE_U64_EQ(parsed.output_size, sizeof(k_source_bytes));
    OMC_TEST_REQUIRE_U64_EQ(parsed.chunk_count, 1U);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].source_offset, 12U);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].block_index, 0xFFFFFFFFU);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].route.size, 0U);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].jpeg_marker_code, 0U);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[0].bytes.size,
                          sizeof(k_source_bytes));
    OMC_TEST_CHECK(memcmp(parsed.chunks[0].bytes.data, k_source_bytes,
                          sizeof(k_source_bytes))
                   == 0);

    status = omc_transfer_package_batch_collect_views(&parsed, &view, 1U,
                                                      &view_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(view_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(view.semantic_kind, OMC_TRANSFER_SEMANTIC_UNKNOWN);
    OMC_TEST_CHECK_U64_EQ(view.package_kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(view.output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(view.route.size, 0U);

    memset(&state, 0, sizeof(state));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_batch = replay_begin;
    callbacks.emit_chunk = replay_emit;
    callbacks.end_batch = replay_end;
    callbacks.user = &state;
    status = omc_transfer_package_batch_replay(&parsed, &callbacks,
                                               &replay_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(replay_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(state.begin_chunk_count, 1U);
    OMC_TEST_CHECK_U64_EQ(state.emitted, 1U);
    OMC_TEST_CHECK_U64_EQ(state.kinds[0],
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(state.semantic_kinds[0],
                          OMC_TRANSFER_SEMANTIC_UNKNOWN);
    OMC_TEST_CHECK_U64_EQ(state.routes[0].size, 0U);
    OMC_TEST_CHECK_U64_EQ(state.output_offsets[0], 0U);

    status = omc_transfer_artifact_inspect(serialized.data, serialized.size,
                                           &info, &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(inspect_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(info.kind,
                          OMC_TRANSFER_ARTIFACT_TRANSFER_PACKAGE_BATCH);
    OMC_TEST_CHECK_U64_EQ(info.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_CHECK_U64_EQ(info.entry_count, 1U);
    OMC_TEST_CHECK_U64_EQ(info.payload_bytes, sizeof(k_source_bytes));

    omc_arena_fini(&parsed_storage);
    omc_arena_fini(&serialized);
}

static void
test_transfer_package_build_executed_jpeg_segments(void)
{
    static const omc_u8 xmp_payload[] =
        "http://ns.adobe.com/xap/1.0/\0Pkg";
    omc_u8 input_bytes[18];
    omc_u8 output_bytes[96];
    omc_size xmp_payload_size;
    omc_size xmp_segment_size;
    omc_size output_size;
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    memset(input_bytes, 0, sizeof(input_bytes));
    input_bytes[0] = 0xFFU;
    input_bytes[1] = 0xD8U;
    input_bytes[2] = 0xFFU;
    input_bytes[3] = 0xE0U;
    write_u16be_at(input_bytes, 4U, 4U);
    input_bytes[6] = (omc_u8)'A';
    input_bytes[7] = (omc_u8)'A';
    input_bytes[8] = 0xFFU;
    input_bytes[9] = 0xDAU;
    write_u16be_at(input_bytes, 10U, 4U);
    input_bytes[12] = 0x01U;
    input_bytes[13] = 0x02U;
    input_bytes[14] = 0x55U;
    input_bytes[15] = 0x66U;
    input_bytes[16] = 0xFFU;
    input_bytes[17] = 0xD9U;

    xmp_payload_size = sizeof(xmp_payload) - 1U;
    xmp_segment_size = 4U + xmp_payload_size;
    output_size = 8U + xmp_segment_size + 10U;
    memset(output_bytes, 0, sizeof(output_bytes));
    memcpy(output_bytes, input_bytes, 8U);
    output_bytes[8] = 0xFFU;
    output_bytes[9] = 0xE1U;
    write_u16be_at(output_bytes, 10U, (omc_u16)(xmp_payload_size + 2U));
    memcpy(output_bytes + 12U, xmp_payload, xmp_payload_size);
    memcpy(output_bytes + 8U + xmp_segment_size, input_bytes + 8U, 10U);

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_JPEG;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, sizeof(input_bytes), output_bytes, output_size, &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, sizeof(input_bytes));
    OMC_TEST_REQUIRE_U64_EQ(batch.output_size, output_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 4U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].bytes.size, 2U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].output_offset, 2U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].source_offset, 2U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].bytes.size, 6U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT);
    OMC_TEST_CHECK(bytes_eq(batch.chunks[2].route, "jpeg:app1-xmp"));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].output_offset, 8U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].block_index, 0xFFFFFFFFU);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].jpeg_marker_code, 0xE1U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].bytes.size, xmp_segment_size);
    OMC_TEST_CHECK(memcmp(batch.chunks[2].bytes.data, output_bytes + 8U,
                          xmp_segment_size)
                   == 0);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].output_offset,
                          8U + xmp_segment_size);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].source_offset, 8U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].bytes.size, 10U);

    assert_package_materializes(&batch, output_bytes, output_size);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_tiff_pointer_tail(void)
{
    omc_u8 input_bytes[32];
    omc_u8 output_bytes[64];
    omc_size tail_size;
    omc_size output_size;
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    memset(input_bytes, 0, sizeof(input_bytes));
    input_bytes[0] = (omc_u8)'I';
    input_bytes[1] = (omc_u8)'I';
    write_u16le_at(input_bytes, 2U, 42U);
    write_u32le_at(input_bytes, 4U, 8U);
    write_u16le_at(input_bytes, 8U, 1U);
    write_u16le_at(input_bytes, 10U, 0x010FU);
    write_u16le_at(input_bytes, 12U, 2U);
    write_u32le_at(input_bytes, 14U, 5U);
    write_u32le_at(input_bytes, 18U, 26U);
    write_u32le_at(input_bytes, 22U, 0U);
    memcpy(input_bytes + 26U, "MAKE", 4U);

    tail_size = 24U;
    output_size = sizeof(input_bytes) + tail_size;
    memset(output_bytes, 0, sizeof(output_bytes));
    memcpy(output_bytes, input_bytes, sizeof(input_bytes));
    write_u32le_at(output_bytes, 4U, (omc_u32)sizeof(input_bytes));
    write_u16le_at(output_bytes, sizeof(input_bytes), 1U);
    write_u16le_at(output_bytes, sizeof(input_bytes) + 2U, 0x02BCU);
    write_u16le_at(output_bytes, sizeof(input_bytes) + 4U, 1U);
    write_u32le_at(output_bytes, sizeof(input_bytes) + 6U, 4U);
    memcpy(output_bytes + sizeof(input_bytes) + 10U, "XMP", 3U);

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_TIFF;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, sizeof(input_bytes), output_bytes, output_size, &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_TIFF);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, sizeof(input_bytes));
    OMC_TEST_REQUIRE_U64_EQ(batch.output_size, output_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 4U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].bytes.size, 4U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].output_offset, 4U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].bytes.size, 4U);
    OMC_TEST_CHECK(memcmp(batch.chunks[1].bytes.data, output_bytes + 4U, 4U)
                   == 0);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].output_offset, 8U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].source_offset, 8U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].bytes.size,
                          sizeof(input_bytes) - 8U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].output_offset,
                          sizeof(input_bytes));
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].bytes.size, tail_size);
    OMC_TEST_CHECK(memcmp(batch.chunks[3].bytes.data,
                          output_bytes + sizeof(input_bytes), tail_size)
                   == 0);

    assert_package_materializes(&batch, output_bytes, output_size);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_bmff_boxes(void)
{
    static const omc_u8 ftyp_payload[] = {
        (omc_u8)'m', (omc_u8)'i', (omc_u8)'f', (omc_u8)'1',
        0U,          0U,          0U,          0U
    };
    static const omc_u8 old_meta_payload[] = {
        (omc_u8)'o', (omc_u8)'l', (omc_u8)'d'
    };
    static const omc_u8 new_meta_payload[] = {
        (omc_u8)'n', (omc_u8)'e', (omc_u8)'w', (omc_u8)'!'
    };
    static const omc_u8 free_payload[] = {
        0U, 0U, 0U, 0U
    };
    static const omc_u8 old_uuid_payload[] = {
        (omc_u8)'o', (omc_u8)'l', (omc_u8)'d', (omc_u8)'u'
    };
    static const omc_u8 new_uuid_payload[] = {
        (omc_u8)'n', (omc_u8)'e', (omc_u8)'w', (omc_u8)'u',
        (omc_u8)'u'
    };
    static const omc_u8 mdat_payload[] = {
        1U, 2U, 3U, 4U, 5U
    };
    omc_u8 input_bytes[128];
    omc_u8 output_bytes[128];
    omc_size input_size;
    omc_size output_size;
    omc_size input_ftyp_off;
    omc_size input_free_off;
    omc_size input_mdat_off;
    omc_size output_meta_off;
    omc_size output_uuid_off;
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    memset(input_bytes, 0, sizeof(input_bytes));
    input_size = 0U;
    input_ftyp_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "ftyp", ftyp_payload, sizeof(ftyp_payload));
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "meta", old_meta_payload,
                         sizeof(old_meta_payload));
    input_free_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "free", free_payload, sizeof(free_payload));
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "uuid", old_uuid_payload,
                         sizeof(old_uuid_payload));
    input_mdat_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "mdat", mdat_payload, sizeof(mdat_payload));

    memset(output_bytes, 0, sizeof(output_bytes));
    output_size = 0U;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "ftyp", ftyp_payload, sizeof(ftyp_payload));
    output_meta_off = output_size;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "meta", new_meta_payload,
                         sizeof(new_meta_payload));
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "free", free_payload, sizeof(free_payload));
    output_uuid_off = output_size;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "uuid", new_uuid_payload,
                         sizeof(new_uuid_payload));
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "mdat", mdat_payload, sizeof(mdat_payload));

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_HEIF;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, input_size, output_bytes, output_size, &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_HEIF);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, input_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.output_size, output_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 5U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, input_ftyp_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[0].bytes, "ftyp"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].output_offset, output_meta_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[1].bytes, "meta"));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[1].bytes, "new!"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].source_offset, input_free_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[2].bytes, "free"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].output_offset, output_uuid_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[3].bytes, "uuid"));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[3].bytes, "newuu"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].source_offset, input_mdat_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[4].bytes, "mdat"));

    assert_package_materializes(&batch, output_bytes, output_size);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_jxl_boxes(void)
{
    static const omc_u8 sig_payload[] = {
        0x0DU, 0x0AU, 0x87U, 0x0AU
    };
    static const omc_u8 old_exif_payload[] = {
        (omc_u8)'o', (omc_u8)'e'
    };
    static const omc_u8 new_exif_payload[] = {
        (omc_u8)'n', (omc_u8)'e', (omc_u8)'x'
    };
    static const omc_u8 free_payload[] = {
        0U, 0U
    };
    static const omc_u8 old_xml_payload[] = {
        (omc_u8)'<', (omc_u8)'o', (omc_u8)'/', (omc_u8)'>'
    };
    static const omc_u8 new_xml_payload[] = {
        (omc_u8)'<', (omc_u8)'n', (omc_u8)'/', (omc_u8)'>',
        (omc_u8)'!'
    };
    static const omc_u8 codestream_payload[] = {
        0xFFU, 0x0AU, 0x01U
    };
    omc_u8 input_bytes[128];
    omc_u8 output_bytes[128];
    omc_size input_size;
    omc_size output_size;
    omc_size input_sig_off;
    omc_size input_free_off;
    omc_size input_jxlc_off;
    omc_size output_exif_off;
    omc_size output_xml_off;
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    memset(input_bytes, 0, sizeof(input_bytes));
    input_size = 0U;
    input_sig_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "JXL ", sig_payload, sizeof(sig_payload));
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "Exif", old_exif_payload,
                         sizeof(old_exif_payload));
    input_free_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "free", free_payload, sizeof(free_payload));
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "xml ", old_xml_payload,
                         sizeof(old_xml_payload));
    input_jxlc_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "jxlc", codestream_payload,
                         sizeof(codestream_payload));

    memset(output_bytes, 0, sizeof(output_bytes));
    output_size = 0U;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "JXL ", sig_payload, sizeof(sig_payload));
    output_exif_off = output_size;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "Exif", new_exif_payload,
                         sizeof(new_exif_payload));
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "free", free_payload, sizeof(free_payload));
    output_xml_off = output_size;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "xml ", new_xml_payload,
                         sizeof(new_xml_payload));
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "jxlc", codestream_payload,
                         sizeof(codestream_payload));

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_JXL;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, input_size, output_bytes, output_size, &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_JXL);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, input_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.output_size, output_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 5U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, input_sig_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[0].bytes, "JXL "));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].output_offset, output_exif_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[1].bytes, "Exif"));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[1].bytes, "nex"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].source_offset, input_free_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[2].bytes, "free"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].output_offset, output_xml_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[3].bytes, "xml "));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[3].bytes, "<n/>!"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].source_offset, input_jxlc_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[4].bytes, "jxlc"));

    assert_package_materializes(&batch, output_bytes, output_size);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_jp2_boxes(void)
{
    static const omc_u8 sig_payload[] = {
        0x0DU, 0x0AU, 0x87U, 0x0AU
    };
    static const omc_u8 ftyp_payload[] = {
        (omc_u8)'j', (omc_u8)'p', (omc_u8)'2', (omc_u8)' ',
        0U,          0U,          0U,          0U
    };
    static const omc_u8 old_xml_payload[] = {
        (omc_u8)'<', (omc_u8)'o', (omc_u8)'/', (omc_u8)'>'
    };
    static const omc_u8 new_xml_payload[] = {
        (omc_u8)'<', (omc_u8)'n', (omc_u8)'/', (omc_u8)'>'
    };
    static const omc_u8 jp2h_payload[] = {
        1U, 2U, 3U, 4U
    };
    static const omc_u8 codestream_payload[] = {
        0xFFU, 0x4FU, 0xFFU, 0x51U
    };
    omc_u8 input_bytes[128];
    omc_u8 output_bytes[128];
    omc_size input_size;
    omc_size output_size;
    omc_size input_sig_off;
    omc_size input_ftyp_off;
    omc_size input_jp2h_off;
    omc_size input_jp2c_off;
    omc_size output_xml_off;
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    memset(input_bytes, 0, sizeof(input_bytes));
    input_size = 0U;
    input_sig_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "jP  ", sig_payload, sizeof(sig_payload));
    input_ftyp_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "ftyp", ftyp_payload, sizeof(ftyp_payload));
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "xml ", old_xml_payload,
                         sizeof(old_xml_payload));
    input_jp2h_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "jp2h", jp2h_payload, sizeof(jp2h_payload));
    input_jp2c_off = input_size;
    append_test_bmff_box(input_bytes, sizeof(input_bytes), &input_size,
                         "jp2c", codestream_payload,
                         sizeof(codestream_payload));

    memset(output_bytes, 0, sizeof(output_bytes));
    output_size = 0U;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "jP  ", sig_payload, sizeof(sig_payload));
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "ftyp", ftyp_payload, sizeof(ftyp_payload));
    output_xml_off = output_size;
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "xml ", new_xml_payload,
                         sizeof(new_xml_payload));
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "jp2h", jp2h_payload, sizeof(jp2h_payload));
    append_test_bmff_box(output_bytes, sizeof(output_bytes), &output_size,
                         "jp2c", codestream_payload,
                         sizeof(codestream_payload));

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_JP2;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, input_size, output_bytes, output_size, &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_JP2);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, input_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.output_size, output_size);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 5U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, input_sig_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[0].bytes, "jP  "));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].source_offset, input_ftyp_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[1].bytes, "ftyp"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].output_offset, output_xml_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[2].bytes, "xml "));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[2].bytes, "<n/>"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].source_offset, input_jp2h_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[3].bytes, "jp2h"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].source_offset, input_jp2c_off);
    OMC_TEST_CHECK(chunk_has_box_type(batch.chunks[4].bytes, "jp2c"));

    assert_package_materializes(&batch, output_bytes, output_size);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_png_chunks(void)
{
    static const omc_u8 png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    static const omc_u8 ihdr_payload[13] = {
        0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U, 8U, 2U, 0U, 0U, 0U
    };
    static const omc_u8 old_xmp_payload[] = {
        (omc_u8)'o', (omc_u8)'l', (omc_u8)'d'
    };
    static const omc_u8 new_xmp_payload[] = {
        (omc_u8)'n', (omc_u8)'e', (omc_u8)'w', (omc_u8)'!',
        (omc_u8)'!'
    };
    static const omc_u8 text_payload[] = {
        (omc_u8)'C', (omc_u8)'o', (omc_u8)'m', (omc_u8)'m',
        (omc_u8)'e', (omc_u8)'n', (omc_u8)'t', 0U,
        (omc_u8)'k', (omc_u8)'e', (omc_u8)'p', (omc_u8)'t'
    };
    omc_u8 input_bytes[160];
    omc_u8 output_bytes[160];
    omc_size input_size;
    omc_size output_size;
    omc_size input_ihdr_off;
    omc_size input_text_off;
    omc_size input_iend_off;
    omc_size output_xmp_off;
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    memset(input_bytes, 0, sizeof(input_bytes));
    memcpy(input_bytes, png_sig, sizeof(png_sig));
    input_size = sizeof(png_sig);
    input_ihdr_off = input_size;
    append_test_png_chunk(input_bytes, sizeof(input_bytes), &input_size,
                          "IHDR", ihdr_payload, sizeof(ihdr_payload));
    append_test_png_chunk(input_bytes, sizeof(input_bytes), &input_size,
                          "iTXt", old_xmp_payload,
                          sizeof(old_xmp_payload));
    input_text_off = input_size;
    append_test_png_chunk(input_bytes, sizeof(input_bytes), &input_size,
                          "tEXt", text_payload, sizeof(text_payload));
    input_iend_off = input_size;
    append_test_png_chunk(input_bytes, sizeof(input_bytes), &input_size,
                          "IEND", (const omc_u8*)0, 0U);

    memset(output_bytes, 0, sizeof(output_bytes));
    memcpy(output_bytes, png_sig, sizeof(png_sig));
    output_size = sizeof(png_sig);
    append_test_png_chunk(output_bytes, sizeof(output_bytes), &output_size,
                          "IHDR", ihdr_payload, sizeof(ihdr_payload));
    output_xmp_off = output_size;
    append_test_png_chunk(output_bytes, sizeof(output_bytes), &output_size,
                          "iTXt", new_xmp_payload,
                          sizeof(new_xmp_payload));
    append_test_png_chunk(output_bytes, sizeof(output_bytes), &output_size,
                          "tEXt", text_payload, sizeof(text_payload));
    append_test_png_chunk(output_bytes, sizeof(output_bytes), &output_size,
                          "IEND", (const omc_u8*)0, 0U);

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_PNG;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, input_size, output_bytes, output_size, &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_PNG);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 5U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].bytes.size, sizeof(png_sig));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].source_offset, input_ihdr_off);
    OMC_TEST_CHECK(chunk_has_png_type(batch.chunks[1].bytes, "IHDR"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].output_offset, output_xmp_off);
    OMC_TEST_CHECK(chunk_has_png_type(batch.chunks[2].bytes, "iTXt"));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[2].bytes, "new!!"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].source_offset, input_text_off);
    OMC_TEST_CHECK(chunk_has_png_type(batch.chunks[3].bytes, "tEXt"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[4].source_offset, input_iend_off);
    OMC_TEST_CHECK(chunk_has_png_type(batch.chunks[4].bytes, "IEND"));

    assert_package_materializes(&batch, output_bytes, output_size);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_webp_chunks(void)
{
    static const omc_u8 vp8x_payload[10] = {
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    static const omc_u8 old_xmp_payload[] = {
        (omc_u8)'o', (omc_u8)'l', (omc_u8)'d'
    };
    static const omc_u8 new_xmp_payload[] = {
        (omc_u8)'n', (omc_u8)'e', (omc_u8)'w', (omc_u8)'!',
        (omc_u8)'!'
    };
    static const omc_u8 vp8_payload[] = {
        0x9DU, 0x01U, 0x2AU, 0U
    };
    omc_u8 input_bytes[128];
    omc_u8 output_bytes[128];
    omc_size input_size;
    omc_size output_size;
    omc_size input_vp8x_off;
    omc_size input_vp8_off;
    omc_size output_xmp_off;
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    memset(input_bytes, 0, sizeof(input_bytes));
    input_size = 12U;
    input_vp8x_off = input_size;
    append_test_webp_chunk(input_bytes, sizeof(input_bytes), &input_size,
                           "VP8X", vp8x_payload, sizeof(vp8x_payload));
    append_test_webp_chunk(input_bytes, sizeof(input_bytes), &input_size,
                           "XMP ", old_xmp_payload,
                           sizeof(old_xmp_payload));
    input_vp8_off = input_size;
    append_test_webp_chunk(input_bytes, sizeof(input_bytes), &input_size,
                           "VP8 ", vp8_payload, sizeof(vp8_payload));
    finish_test_webp(input_bytes, input_size);

    memset(output_bytes, 0, sizeof(output_bytes));
    output_size = 12U;
    append_test_webp_chunk(output_bytes, sizeof(output_bytes), &output_size,
                           "VP8X", vp8x_payload, sizeof(vp8x_payload));
    output_xmp_off = output_size;
    append_test_webp_chunk(output_bytes, sizeof(output_bytes), &output_size,
                           "XMP ", new_xmp_payload,
                           sizeof(new_xmp_payload));
    append_test_webp_chunk(output_bytes, sizeof(output_bytes), &output_size,
                           "VP8 ", vp8_payload, sizeof(vp8_payload));
    finish_test_webp(output_bytes, output_size);

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_WEBP;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, input_size, output_bytes, output_size, &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_WEBP);
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 4U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK(memcmp(batch.chunks[0].bytes.data, "RIFF", 4U) == 0);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].source_offset, input_vp8x_off);
    OMC_TEST_CHECK(chunk_has_webp_type(batch.chunks[1].bytes, "VP8X"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].output_offset, output_xmp_off);
    OMC_TEST_CHECK(chunk_has_webp_type(batch.chunks[2].bytes, "XMP "));
    OMC_TEST_CHECK(bytes_contains(batch.chunks[2].bytes, "new!!"));

    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[3].source_offset, input_vp8_off);
    OMC_TEST_CHECK(chunk_has_webp_type(batch.chunks[3].bytes, "VP8 "));

    assert_package_materializes(&batch, output_bytes, output_size);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_output_diff_chunks(void)
{
    static const omc_u8 input_bytes[] = {
        (omc_u8)'a', (omc_u8)'b', (omc_u8)'c', (omc_u8)'O',
        (omc_u8)'L', (omc_u8)'D', (omc_u8)'x', (omc_u8)'y',
        (omc_u8)'z'
    };
    static const omc_u8 output_bytes[] = {
        (omc_u8)'a', (omc_u8)'b', (omc_u8)'c', (omc_u8)'N',
        (omc_u8)'E', (omc_u8)'W', (omc_u8)'x', (omc_u8)'y',
        (omc_u8)'z'
    };
    omc_arena storage;
    omc_arena serialized;
    omc_arena parsed_storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_batch parsed;
    omc_transfer_package_io_res io_res;
    omc_transfer_package_io_res parse_res;
    omc_transfer_artifact_info info;
    omc_transfer_artifact_io_res inspect_res;
    omc_status status;

    omc_arena_init(&storage);
    omc_arena_init(&serialized);
    omc_arena_init(&parsed_storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_JPEG;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, sizeof(input_bytes), output_bytes, sizeof(output_bytes),
        &execute, &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, sizeof(input_bytes));
    OMC_TEST_REQUIRE_U64_EQ(batch.output_size, sizeof(output_bytes));
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 3U);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].bytes.size, 3U);
    OMC_TEST_CHECK(memcmp(batch.chunks[0].bytes.data, "abc", 3U) == 0);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].output_offset, 3U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].source_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[1].bytes.size, 3U);
    OMC_TEST_CHECK(memcmp(batch.chunks[1].bytes.data, "NEW", 3U) == 0);

    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].output_offset, 6U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].source_offset, 6U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[2].bytes.size, 3U);
    OMC_TEST_CHECK(memcmp(batch.chunks[2].bytes.data, "xyz", 3U) == 0);

    assert_package_materializes(&batch, output_bytes, sizeof(output_bytes));

    status = omc_transfer_package_batch_serialize(&batch, &serialized,
                                                  &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);

    status = omc_transfer_package_batch_deserialize(
        serialized.data, serialized.size, &parsed_storage, &parsed,
        &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(parsed.chunk_count, 3U);
    OMC_TEST_CHECK_U64_EQ(parsed.chunks[1].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK(memcmp(parsed.chunks[1].bytes.data, "NEW", 3U) == 0);

    assert_package_materializes(&parsed, output_bytes, sizeof(output_bytes));

    status = omc_transfer_artifact_inspect(serialized.data, serialized.size,
                                           &info, &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(inspect_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(info.kind,
                          OMC_TRANSFER_ARTIFACT_TRANSFER_PACKAGE_BATCH);
    OMC_TEST_CHECK_U64_EQ(info.entry_count, 3U);
    OMC_TEST_CHECK_U64_EQ(info.payload_bytes, sizeof(output_bytes));

    omc_arena_fini(&parsed_storage);
    omc_arena_fini(&serialized);
    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_output_fresh_inline(void)
{
    static const omc_u8 output_bytes[] = {
        (omc_u8)'f', (omc_u8)'r', (omc_u8)'e', (omc_u8)'s',
        (omc_u8)'h'
    };
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_DNG;
    execute.edited_present = 1;

    status = omc_transfer_package_batch_build_executed_output(
        (const omc_u8*)0, 0U, output_bytes, sizeof(output_bytes), &execute,
        &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_DNG);
    OMC_TEST_REQUIRE_U64_EQ(batch.input_size, 0U);
    OMC_TEST_REQUIRE_U64_EQ(batch.output_size, sizeof(output_bytes));
    OMC_TEST_REQUIRE_U64_EQ(batch.chunk_count, 1U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].kind,
                          OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].output_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(batch.chunks[0].source_offset, 0U);
    OMC_TEST_CHECK(memcmp(batch.chunks[0].bytes.data, "fresh", 5U) == 0);

    assert_package_materializes(&batch, output_bytes, sizeof(output_bytes));

    omc_arena_fini(&storage);
}

static void
test_transfer_package_build_executed_output_rejects_missing_output(void)
{
    static const omc_u8 input_bytes[] = {
        (omc_u8)'s', (omc_u8)'a', (omc_u8)'m', (omc_u8)'e'
    };
    omc_arena storage;
    omc_transfer_res execute;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    omc_arena_init(&storage);
    memset(&execute, 0, sizeof(execute));
    execute.status = OMC_TRANSFER_OK;
    execute.format = OMC_SCAN_FMT_JPEG;
    execute.edited_present = 0;

    status = omc_transfer_package_batch_build_executed_output(
        input_bytes, sizeof(input_bytes), input_bytes, sizeof(input_bytes),
        &execute, &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(io_res.status, OMC_TRANSFER_UNSUPPORTED);

    omc_arena_fini(&storage);
}

static void
test_transfer_package_deserialize_rejects_bad_magic(void)
{
    static const omc_u8 bytes[8] = {
        (omc_u8)'N', (omc_u8)'O', (omc_u8)'T', (omc_u8)'P',
        (omc_u8)'K', (omc_u8)'G', (omc_u8)'0', (omc_u8)'1'
    };
    omc_arena storage;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_status status;

    omc_arena_init(&storage);
    status = omc_transfer_package_batch_deserialize(bytes, sizeof(bytes),
                                                    &storage, &batch,
                                                    &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(io_res.status, OMC_TRANSFER_UNSUPPORTED);
    status = omc_transfer_package_bytes_materialize_to_buffer(
        bytes, sizeof(bytes), &storage, (omc_u8*)0, 0U, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(io_res.status, OMC_TRANSFER_UNSUPPORTED);
    omc_arena_fini(&storage);
}

static void
test_transfer_package_materialize_rejects_malformed(void)
{
    static const omc_u8 chunk_bytes[] = {
        (omc_u8)'x'
    };
    omc_transfer_package_chunk chunk;
    omc_transfer_package_batch batch;
    omc_transfer_package_io_res io_res;
    omc_arena out;
    omc_byte_ref ref;
    omc_status status;

    memset(&chunk, 0, sizeof(chunk));
    chunk.kind = OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES;
    chunk.output_offset = 0U;
    chunk.bytes.data = chunk_bytes;
    chunk.bytes.size = sizeof(chunk_bytes);

    omc_transfer_package_batch_init(&batch);
    batch.target_format = OMC_SCAN_FMT_JPEG;
    batch.output_size = 2U;
    batch.chunk_count = 1U;
    batch.chunks = &chunk;

    omc_arena_init(&out);
    status = omc_arena_append(&out, "old", 3U, &ref);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);

    status = omc_transfer_package_batch_materialize(&batch, &out, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_MALFORMED);
    OMC_TEST_REQUIRE_U64_EQ(io_res.bytes, 0U);
    OMC_TEST_REQUIRE_U64_EQ(io_res.chunk_count, 0U);
    OMC_TEST_REQUIRE_U64_EQ(out.size, 0U);

    omc_arena_fini(&out);
}

int
main(void)
{
    test_transfer_package_build_jpeg_roundtrip_and_replay();
    test_transfer_package_build_jxl_chunks();
    test_transfer_package_source_range_roundtrip();
    test_transfer_package_build_executed_jpeg_segments();
    test_transfer_package_build_executed_tiff_pointer_tail();
    test_transfer_package_build_executed_bmff_boxes();
    test_transfer_package_build_executed_jxl_boxes();
    test_transfer_package_build_executed_jp2_boxes();
    test_transfer_package_build_executed_png_chunks();
    test_transfer_package_build_executed_webp_chunks();
    test_transfer_package_build_executed_output_diff_chunks();
    test_transfer_package_build_executed_output_fresh_inline();
    test_transfer_package_build_executed_output_rejects_missing_output();
    test_transfer_package_deserialize_rejects_bad_magic();
    test_transfer_package_materialize_rejects_malformed();
    return omc_test_finish();
}
