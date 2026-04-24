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
write_u32be_at(omc_u8* out, omc_size off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)((value >> 24) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 3U] = (omc_u8)(value & 0xFFU);
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

static int
chunk_has_box_type(omc_const_bytes bytes, const char* type)
{
    return bytes.size >= 8U && memcmp(bytes.data + 4U, type, 4U) == 0;
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
    omc_u64 output_offsets[8];
    omc_u64 source_offsets[8];
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
replay_emit(void* user, const omc_transfer_package_chunk* chunk)
{
    replay_state* state;

    state = (replay_state*)user;
    if (state->emitted < 8U) {
        state->routes[state->emitted] = chunk->route;
        state->kinds[state->emitted] = chunk->kind;
        state->output_offsets[state->emitted] = chunk->output_offset;
        state->source_offsets[state->emitted] = chunk->source_offset;
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
    omc_transfer_package_replay_callbacks callbacks;
    omc_transfer_package_replay_res replay_res;
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
    OMC_TEST_CHECK_U64_EQ(state.output_offsets[0], 0U);
    OMC_TEST_CHECK_U64_EQ(state.source_offsets[0], 0U);

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
    omc_transfer_package_replay_callbacks callbacks;
    omc_transfer_package_replay_res replay_res;
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
    OMC_TEST_CHECK_U64_EQ(state.routes[0].size, 0U);
    OMC_TEST_CHECK_U64_EQ(state.output_offsets[0], 0U);
    OMC_TEST_CHECK_U64_EQ(state.source_offsets[0], 12U);

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
    omc_arena_fini(&storage);
}

int
main(void)
{
    test_transfer_package_build_jpeg_roundtrip_and_replay();
    test_transfer_package_build_jxl_chunks();
    test_transfer_package_source_range_roundtrip();
    test_transfer_package_deserialize_rejects_bad_magic();
    return omc_test_finish();
}
