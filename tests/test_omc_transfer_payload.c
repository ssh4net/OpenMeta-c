#include "omc/omc_jumbf.h"
#include "omc/omc_icc.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer_artifact.h"
#include "omc/omc_transfer_payload.h"

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

static omc_u16
read_u16le(const omc_u8* data)
{
    return (omc_u16)(((omc_u16)data[1] << 8) | (omc_u16)data[0]);
}

static omc_u32
read_u32le(const omc_u8* data)
{
    return ((omc_u32)data[3] << 24) | ((omc_u32)data[2] << 16)
           | ((omc_u32)data[1] << 8) | (omc_u32)data[0];
}

static int
find_tiff_tag_entry_le(omc_const_bytes exif_payload, omc_u32 ifd_off,
                       omc_u16 tag, omc_u16* out_type,
                       omc_u32* out_count, omc_u32* out_value)
{
    omc_size tiff_off;
    omc_size count_off;
    omc_size entry_off;
    omc_u16 count;
    omc_u16 i;

    if (exif_payload.data == (const omc_u8*)0 || exif_payload.size < 14U
        || memcmp(exif_payload.data, "Exif\0\0", 6U) != 0) {
        return 0;
    }
    tiff_off = 6U;
    if (exif_payload.data[tiff_off + 0U] != (omc_u8)'I'
        || exif_payload.data[tiff_off + 1U] != (omc_u8)'I'
        || read_u16le(exif_payload.data + tiff_off + 2U) != 42U) {
        return 0;
    }
    if ((omc_u64)tiff_off + (omc_u64)ifd_off + 2U
        > (omc_u64)exif_payload.size) {
        return 0;
    }
    count_off = tiff_off + ifd_off;
    count = read_u16le(exif_payload.data + count_off);
    entry_off = count_off + 2U;
    if ((omc_u64)entry_off + (omc_u64)count * 12U + 4U
        > (omc_u64)exif_payload.size) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        omc_size off;

        off = entry_off + (omc_size)i * 12U;
        if (read_u16le(exif_payload.data + off) != tag) {
            continue;
        }
        if (out_type != (omc_u16*)0) {
            *out_type = read_u16le(exif_payload.data + off + 2U);
        }
        if (out_count != (omc_u32*)0) {
            *out_count = read_u32le(exif_payload.data + off + 4U);
        }
        if (out_value != (omc_u32*)0) {
            *out_value = read_u32le(exif_payload.data + off + 8U);
        }
        return 1;
    }
    return 0;
}

static int
find_ifd0_tag_entry_le(omc_const_bytes exif_payload, omc_u16 tag,
                       omc_u16* out_type, omc_u32* out_count,
                       omc_u32* out_value)
{
    omc_u32 ifd0_off;

    if (exif_payload.size < 14U) {
        return 0;
    }
    ifd0_off = read_u32le(exif_payload.data + 10U);
    return find_tiff_tag_entry_le(exif_payload, ifd0_off, tag, out_type,
                                  out_count, out_value);
}

static int
find_exif_ifd_tag_entry_le(omc_const_bytes exif_payload, omc_u16 tag,
                           omc_u16* out_type, omc_u32* out_count,
                           omc_u32* out_value)
{
    omc_u16 type;
    omc_u32 count;
    omc_u32 exif_ifd_off;

    if (!find_ifd0_tag_entry_le(exif_payload, 0x8769U, &type, &count,
                                &exif_ifd_off)) {
        return 0;
    }
    if (type != 4U || count != 1U) {
        return 0;
    }
    return find_tiff_tag_entry_le(exif_payload, exif_ifd_off, tag, out_type,
                                  out_count, out_value);
}

static int
read_u16_array_tag_le(omc_const_bytes exif_payload, omc_u16 tag,
                      omc_u16* out_values, omc_u32 value_cap,
                      omc_u32* out_count)
{
    omc_u16 type;
    omc_u32 count;
    omc_u32 value_or_offset;
    omc_size tiff_off;
    omc_size data_off;
    omc_u32 i;

    if (out_values == (omc_u16*)0 || out_count == (omc_u32*)0
        || !find_ifd0_tag_entry_le(exif_payload, tag, &type, &count,
                                   &value_or_offset)
        || type != 3U || count > value_cap) {
        return 0;
    }
    *out_count = count;
    if (count <= 2U) {
        for (i = 0U; i < count; ++i) {
            out_values[i] = (omc_u16)((value_or_offset >> (i * 16U))
                                      & 0xFFFFU);
        }
        return 1;
    }
    tiff_off = 6U;
    data_off = tiff_off + (omc_size)value_or_offset;
    if ((omc_u64)data_off + (omc_u64)count * 2U
        > (omc_u64)exif_payload.size) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        out_values[i] = read_u16le(exif_payload.data + data_off + i * 2U);
    }
    return 1;
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
add_exif_u32_entry(omc_store* store, const char* ifd, omc_u16 tag,
                   omc_u32 value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_cstr(&store->arena, ifd), tag);
    omc_val_make_u32(&entry.value, value);
    status = omc_store_add_entry(store, &entry, NULL);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_xmp_u32_entry(omc_store* store, const char* ns, const char* path,
                  omc_u32 value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_cstr(&store->arena, ns),
                              append_cstr(&store->arena, path));
    omc_val_make_u32(&entry.value, value);
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

static const omc_entry*
find_jumbf_cbor_key(const omc_store* store, const char* key_text)
{
    omc_size i;
    omc_size key_size;

    key_size = (omc_size)strlen(key_text);
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key_view = omc_arena_view(&store->arena, entry->key.u.jumbf_cbor_key.key);
        if (key_view.size == key_size
            && memcmp(key_view.data, key_text, key_size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static void
write_u32be(omc_u8* out, omc_u32 value)
{
    out[0] = (omc_u8)((value >> 24) & 0xFFU);
    out[1] = (omc_u8)((value >> 16) & 0xFFU);
    out[2] = (omc_u8)((value >> 8) & 0xFFU);
    out[3] = (omc_u8)(value & 0xFFU);
}

static void
assert_projected_jumbf_roundtrip(const omc_u8* logical_payload,
                                 omc_size logical_size)
{
    omc_store decoded;
    omc_jumbf_res res;
    const omc_entry* entry;
    omc_const_bytes text;

    omc_store_init(&decoded);
    res = omc_jumbf_dec(logical_payload, logical_size, &decoded,
                        OMC_INVALID_BLOCK_ID, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_JUMBF_OK);

    entry = find_jumbf_cbor_key(&decoded, "box.0.1.cbor.label");
    OMC_TEST_REQUIRE(entry != (const omc_entry*)0);
    OMC_TEST_REQUIRE_U64_EQ(entry->value.kind, OMC_VAL_TEXT);
    text = omc_arena_view(&decoded.arena, entry->value.u.ref);
    OMC_TEST_CHECK(bytes_eq(text, "alpha"));

    entry = find_jumbf_cbor_key(&decoded, "box.0.1.cbor.count");
    OMC_TEST_REQUIRE(entry != (const omc_entry*)0);
    OMC_TEST_REQUIRE_U64_EQ(entry->value.kind, OMC_VAL_SCALAR);
    OMC_TEST_CHECK_U64_EQ(entry->value.u.u64, 7U);

    entry = find_jumbf_cbor_key(&decoded, "box.0.1.cbor.items[0]");
    OMC_TEST_REQUIRE(entry != (const omc_entry*)0);
    OMC_TEST_REQUIRE_U64_EQ(entry->value.kind, OMC_VAL_TEXT);
    text = omc_arena_view(&decoded.arena, entry->value.u.ref);
    OMC_TEST_CHECK(bytes_eq(text, "left"));

    entry = find_jumbf_cbor_key(&decoded, "box.0.1.cbor.items[1]");
    OMC_TEST_REQUIRE(entry != (const omc_entry*)0);
    OMC_TEST_REQUIRE_U64_EQ(entry->value.kind, OMC_VAL_TEXT);
    text = omc_arena_view(&decoded.arena, entry->value.u.ref);
    OMC_TEST_CHECK(bytes_eq(text, "right"));

    omc_store_fini(&decoded);
}

typedef struct replay_state {
    omc_scan_fmt begin_target;
    omc_u32 begin_payload_count;
    omc_scan_fmt end_target;
    omc_u32 emitted;
    omc_const_bytes routes[8];
    omc_transfer_semantic_kind semantics[8];
} replay_state;

static omc_transfer_status
replay_begin(void* user, omc_scan_fmt target_format, omc_u32 payload_count)
{
    replay_state* state;

    state = (replay_state*)user;
    state->begin_target = target_format;
    state->begin_payload_count = payload_count;
    return OMC_TRANSFER_OK;
}

static omc_transfer_status
replay_emit(void* user, const omc_transfer_payload* payload)
{
    replay_state* state;

    state = (replay_state*)user;
    if (state->emitted < 8U) {
        state->routes[state->emitted] = payload->route;
        state->semantics[state->emitted] = payload->semantic_kind;
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
test_transfer_payload_build_jpeg_roundtrip_and_replay(void)
{
    omc_store store;
    omc_arena storage;
    omc_arena serialized;
    omc_arena parsed_storage;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_batch parsed;
    omc_transfer_payload_io_res io_res;
    omc_transfer_payload_io_res parse_res;
    omc_transfer_payload_replay_callbacks callbacks;
    omc_transfer_payload_replay_res replay_res;
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
    add_test_iptc_entry(&store, "Payload Batch");

    omc_transfer_payload_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;

    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_REQUIRE_U64_EQ(batch.payload_count, 4U);

    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_EXIF);
    OMC_TEST_CHECK(bytes_eq(batch.payloads[0].route, "jpeg:app1-exif"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.jpeg_marker_code, 0xE1U);
    OMC_TEST_CHECK(batch.payloads[0].payload.size > 6U);
    OMC_TEST_CHECK(memcmp(batch.payloads[0].payload.data, "Exif\0\0", 6U)
                   == 0);

    OMC_TEST_CHECK_U64_EQ(batch.payloads[1].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_XMP);
    OMC_TEST_CHECK(bytes_eq(batch.payloads[1].route, "jpeg:app1-xmp"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[1].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[1].op.jpeg_marker_code, 0xE1U);
    OMC_TEST_CHECK(bytes_contains(batch.payloads[1].payload,
                                  "http://ns.adobe.com/xap/1.0/"));
    OMC_TEST_CHECK(bytes_contains(batch.payloads[1].payload,
                                  "OpenMeta-c"));

    OMC_TEST_CHECK_U64_EQ(batch.payloads[2].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_ICC);
    OMC_TEST_CHECK(bytes_eq(batch.payloads[2].route, "jpeg:app2-icc"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[2].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[2].op.jpeg_marker_code, 0xE2U);
    OMC_TEST_CHECK(batch.payloads[2].payload.size > 14U);
    OMC_TEST_CHECK(memcmp(batch.payloads[2].payload.data, "ICC_PROFILE\0",
                          12U)
                   == 0);

    OMC_TEST_CHECK_U64_EQ(batch.payloads[3].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_IPTC);
    OMC_TEST_CHECK(bytes_eq(batch.payloads[3].route, "jpeg:app13-iptc"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[3].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[3].op.jpeg_marker_code, 0xEDU);
    OMC_TEST_CHECK(batch.payloads[3].payload.size > 8U);
    OMC_TEST_CHECK(memcmp(batch.payloads[3].payload.data, "8BIM", 4U) == 0);

    status = omc_transfer_payload_batch_serialize(&batch, &serialized,
                                                  &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.payload_count, 4U);
    OMC_TEST_CHECK(serialized.size > 8U);
    OMC_TEST_CHECK(memcmp(serialized.data, "OMTPLD01", 8U) == 0);

    status = omc_transfer_payload_batch_deserialize(
        serialized.data, serialized.size, &parsed_storage, &parsed,
        &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(parsed.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_REQUIRE_U64_EQ(parsed.payload_count, 4U);
    OMC_TEST_CHECK(bytes_eq(parsed.payloads[0].route, "jpeg:app1-exif"));
    OMC_TEST_CHECK(bytes_eq(parsed.payloads[1].route, "jpeg:app1-xmp"));
    OMC_TEST_CHECK(bytes_eq(parsed.payloads[2].route, "jpeg:app2-icc"));
    OMC_TEST_CHECK(bytes_eq(parsed.payloads[3].route, "jpeg:app13-iptc"));
    OMC_TEST_CHECK_U64_EQ(parsed.payloads[0].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_EXIF);
    OMC_TEST_CHECK_U64_EQ(parsed.payloads[1].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_XMP);
    OMC_TEST_CHECK_U64_EQ(parsed.payloads[2].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_ICC);
    OMC_TEST_CHECK_U64_EQ(parsed.payloads[3].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_IPTC);

    memset(&state, 0, sizeof(state));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_batch = replay_begin;
    callbacks.emit_payload = replay_emit;
    callbacks.end_batch = replay_end;
    callbacks.user = &state;
    status = omc_transfer_payload_batch_replay(&parsed, &callbacks,
                                               &replay_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(replay_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(replay_res.replayed, 4U);
    OMC_TEST_CHECK_U64_EQ(state.begin_target, OMC_SCAN_FMT_JPEG);
    OMC_TEST_CHECK_U64_EQ(state.begin_payload_count, 4U);
    OMC_TEST_CHECK_U64_EQ(state.end_target, OMC_SCAN_FMT_JPEG);
    OMC_TEST_CHECK_U64_EQ(state.emitted, 4U);
    OMC_TEST_CHECK(bytes_eq(state.routes[0], "jpeg:app1-exif"));
    OMC_TEST_CHECK(bytes_eq(state.routes[1], "jpeg:app1-xmp"));
    OMC_TEST_CHECK(bytes_eq(state.routes[2], "jpeg:app2-icc"));
    OMC_TEST_CHECK(bytes_eq(state.routes[3], "jpeg:app13-iptc"));

    status = omc_transfer_artifact_inspect(serialized.data, serialized.size,
                                           &info, &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(inspect_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(info.kind,
                          OMC_TRANSFER_ARTIFACT_TRANSFER_PAYLOAD_BATCH);
    OMC_TEST_CHECK_U64_EQ(info.target_format, OMC_SCAN_FMT_JPEG);
    OMC_TEST_CHECK_U64_EQ(info.entry_count, 4U);
    payload_bytes = (omc_u64)parsed.payloads[0].payload.size
                    + (omc_u64)parsed.payloads[1].payload.size
                    + (omc_u64)parsed.payloads[2].payload.size
                    + (omc_u64)parsed.payloads[3].payload.size;
    OMC_TEST_CHECK_U64_EQ(info.payload_bytes, payload_bytes);
    OMC_TEST_CHECK_U64_EQ(info.binding_bytes,
                          (omc_u64)serialized.size - payload_bytes);

    omc_arena_fini(&parsed_storage);
    omc_arena_fini(&serialized);
    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_target_image_spec_filters_stale_layout(void)
{
    static const char k_ns_tiff[] = "http://ns.adobe.com/tiff/1.0/";
    static const char k_ns_exif[] = "http://ns.adobe.com/exif/1.0/";
    omc_store store;
    omc_arena storage;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_const_bytes exif_payload;
    omc_const_bytes xmp_payload;
    omc_u16 type;
    omc_u32 count;
    omc_u32 value;
    omc_u16 values[3];
    omc_u32 values_count;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&storage);

    add_test_exif_entry(&store, "Target Camera");
    add_exif_u32_entry(&store, "ifd0", 0x0100U, 999U);
    add_exif_u32_entry(&store, "ifd0", 0x0101U, 999U);
    add_exif_u32_entry(&store, "exififd", 0xA002U, 999U);
    add_xmp_u32_entry(&store, k_ns_tiff, "ImageWidth", 999U);
    add_xmp_u32_entry(&store, k_ns_exif, "ExifImageWidth", 999U);

    omc_transfer_payload_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;
    opts.include_icc = 0;
    opts.include_iptc = 0;
    opts.include_jumbf = 0;
    opts.target_image_spec.has_dimensions = 1;
    opts.target_image_spec.width = 640U;
    opts.target_image_spec.height = 480U;
    opts.target_image_spec.has_orientation = 1;
    opts.target_image_spec.orientation = 1U;
    opts.target_image_spec.has_samples_per_pixel = 1;
    opts.target_image_spec.samples_per_pixel = 3U;
    opts.target_image_spec.bits_per_sample_count = 1U;
    opts.target_image_spec.bits_per_sample[0] = 8U;
    opts.target_image_spec.sample_format_count = 1U;
    opts.target_image_spec.sample_format[0] = 1U;
    opts.target_image_spec.has_photometric_interpretation = 1;
    opts.target_image_spec.photometric_interpretation = 2U;
    opts.target_image_spec.has_planar_configuration = 1;
    opts.target_image_spec.planar_configuration = 1U;
    opts.target_image_spec.has_exif_color_space = 1;
    opts.target_image_spec.exif_color_space = 1U;

    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.payload_count, 2U);
    OMC_TEST_REQUIRE_U64_EQ(batch.payloads[0].semantic_kind,
                            OMC_TRANSFER_SEMANTIC_EXIF);
    OMC_TEST_REQUIRE_U64_EQ(batch.payloads[1].semantic_kind,
                            OMC_TRANSFER_SEMANTIC_XMP);

    exif_payload = batch.payloads[0].payload;
    xmp_payload = batch.payloads[1].payload;

    OMC_TEST_REQUIRE(find_ifd0_tag_entry_le(exif_payload, 0x0100U, &type,
                                            &count, &value));
    OMC_TEST_CHECK_U64_EQ(type, 4U);
    OMC_TEST_CHECK_U64_EQ(count, 1U);
    OMC_TEST_CHECK_U64_EQ(value, 640U);
    OMC_TEST_REQUIRE(find_ifd0_tag_entry_le(exif_payload, 0x0101U, &type,
                                            &count, &value));
    OMC_TEST_CHECK_U64_EQ(value, 480U);
    OMC_TEST_REQUIRE(find_ifd0_tag_entry_le(exif_payload, 0x0112U, &type,
                                            &count, &value));
    OMC_TEST_CHECK_U64_EQ((omc_u16)(value & 0xFFFFU), 1U);
    OMC_TEST_REQUIRE(find_ifd0_tag_entry_le(exif_payload, 0x0115U, &type,
                                            &count, &value));
    OMC_TEST_CHECK_U64_EQ((omc_u16)(value & 0xFFFFU), 3U);
    OMC_TEST_REQUIRE(read_u16_array_tag_le(exif_payload, 0x0102U, values, 3U,
                                           &values_count));
    OMC_TEST_CHECK_U64_EQ(values_count, 3U);
    OMC_TEST_CHECK_U64_EQ(values[0], 8U);
    OMC_TEST_CHECK_U64_EQ(values[1], 8U);
    OMC_TEST_CHECK_U64_EQ(values[2], 8U);
    OMC_TEST_REQUIRE(read_u16_array_tag_le(exif_payload, 0x0153U, values, 3U,
                                           &values_count));
    OMC_TEST_CHECK_U64_EQ(values_count, 3U);
    OMC_TEST_CHECK_U64_EQ(values[0], 1U);
    OMC_TEST_CHECK_U64_EQ(values[1], 1U);
    OMC_TEST_CHECK_U64_EQ(values[2], 1U);
    OMC_TEST_REQUIRE(find_ifd0_tag_entry_le(exif_payload, 0x0106U, &type,
                                            &count, &value));
    OMC_TEST_CHECK_U64_EQ((omc_u16)(value & 0xFFFFU), 2U);
    OMC_TEST_REQUIRE(find_ifd0_tag_entry_le(exif_payload, 0x011CU, &type,
                                            &count, &value));
    OMC_TEST_CHECK_U64_EQ((omc_u16)(value & 0xFFFFU), 1U);
    OMC_TEST_REQUIRE(find_exif_ifd_tag_entry_le(exif_payload, 0xA002U, &type,
                                                &count, &value));
    OMC_TEST_CHECK_U64_EQ(value, 640U);
    OMC_TEST_REQUIRE(find_exif_ifd_tag_entry_le(exif_payload, 0xA003U, &type,
                                                &count, &value));
    OMC_TEST_CHECK_U64_EQ(value, 480U);
    OMC_TEST_REQUIRE(find_exif_ifd_tag_entry_le(exif_payload, 0xA001U, &type,
                                                &count, &value));
    OMC_TEST_CHECK_U64_EQ((omc_u16)(value & 0xFFFFU), 1U);

    OMC_TEST_CHECK(bytes_contains(xmp_payload,
                                  "<tiff:ImageWidth>640</tiff:ImageWidth>"));
    OMC_TEST_CHECK(bytes_contains(xmp_payload,
                                  "<tiff:ImageHeight>480</tiff:ImageHeight>"));
    OMC_TEST_CHECK(bytes_contains(xmp_payload,
                                  "<exif:ExifImageWidth>640</exif:ExifImageWidth>"));
    OMC_TEST_CHECK(bytes_contains(xmp_payload,
                                  "<exif:ExifImageHeight>480</exif:ExifImageHeight>"));
    OMC_TEST_CHECK(bytes_contains(xmp_payload,
                                  "<exif:ColorSpace>sRGB</exif:ColorSpace>"));
    OMC_TEST_CHECK(!bytes_contains(xmp_payload, ">999<"));

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_target_image_spec_rejects_invalid(void)
{
    omc_store store;
    omc_arena storage;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&storage);

    omc_transfer_payload_build_opts_init(&opts);
    opts.target_image_spec.has_dimensions = 1;
    opts.target_image_spec.width = 0U;
    opts.target_image_spec.height = 480U;
    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_CHECK_U64_EQ(status, OMC_STATUS_INVALID_ARGUMENT);
    OMC_TEST_CHECK_SIZE_EQ(storage.size, 0U);

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_build_bmff_xmp_and_icc(void)
{
    omc_store store;
    omc_arena storage;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&storage);

    add_test_exif_entry(&store, "BMFF Camera");
    add_test_xmp_entry(&store, "OpenMeta-c");
    add_test_icc_entries(&store);

    omc_transfer_payload_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_HEIF;
    opts.include_iptc = 0;

    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.target_format, OMC_SCAN_FMT_HEIF);
    OMC_TEST_REQUIRE_U64_EQ(batch.payload_count, 3U);

    OMC_TEST_CHECK(bytes_eq(batch.payloads[0].route, "bmff:item-exif"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_EXIF);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.bmff_item_type,
                          OMC_FOURCC('E', 'x', 'i', 'f'));
    OMC_TEST_CHECK(batch.payloads[0].payload.size > 10U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[0], 0U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[1], 0U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[2], 0U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[3], 6U);
    OMC_TEST_CHECK(memcmp(batch.payloads[0].payload.data + 4U, "Exif\0\0", 6U)
                   == 0);

    OMC_TEST_CHECK(bytes_eq(batch.payloads[1].route, "bmff:item-xmp"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[1].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_XMP);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[1].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[1].op.bmff_item_type,
                          OMC_FOURCC('m', 'i', 'm', 'e'));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[1].op.bmff_mime_xmp, 1U);

    OMC_TEST_CHECK(bytes_eq(batch.payloads[2].route,
                            "bmff:property-colr-icc"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[2].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_ICC);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[2].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_BMFF_PROPERTY);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[2].op.bmff_property_type,
                          OMC_FOURCC('c', 'o', 'l', 'r'));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[2].op.bmff_property_subtype,
                          OMC_FOURCC('p', 'r', 'o', 'f'));
    OMC_TEST_CHECK(batch.payloads[2].payload.size > 4U);
    OMC_TEST_CHECK(memcmp(batch.payloads[2].payload.data, "prof", 4U) == 0);

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_build_jpeg_projected_jumbf(void)
{
    omc_store store;
    omc_arena storage;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&storage);

    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.label", "alpha");
    add_test_jumbf_cbor_u64_entry(&store, "box.0.1.cbor.count", 7U);
    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.items[0]", "left");
    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.items[1]", "right");

    omc_transfer_payload_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;
    opts.include_exif = 0;
    opts.include_xmp = 0;
    opts.include_icc = 0;
    opts.include_iptc = 0;

    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.payload_count, 1U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_JUMBF);
    OMC_TEST_CHECK(bytes_eq(batch.payloads[0].route, "jpeg:app11-jumbf"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.jpeg_marker_code, 0xEBU);
    OMC_TEST_REQUIRE(batch.payloads[0].payload.size > 16U);
    OMC_TEST_CHECK(memcmp(batch.payloads[0].payload.data, "JP\0\0", 4U) == 0);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[4], 0U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[5], 0U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[6], 0U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].payload.data[7], 1U);

    assert_projected_jumbf_roundtrip(batch.payloads[0].payload.data + 8U,
                                     batch.payloads[0].payload.size - 8U);

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_build_jxl_projected_jumbf(void)
{
    omc_store store;
    omc_arena storage;
    omc_u8 logical_box[1024];
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_status status;
    omc_size logical_size;

    omc_store_init(&store);
    omc_arena_init(&storage);

    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.label", "alpha");
    add_test_jumbf_cbor_u64_entry(&store, "box.0.1.cbor.count", 7U);
    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.items[0]", "left");
    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.items[1]", "right");

    omc_transfer_payload_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JXL;
    opts.include_exif = 0;
    opts.include_xmp = 0;
    opts.include_icc = 0;
    opts.include_iptc = 0;

    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.payload_count, 1U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_JUMBF);
    OMC_TEST_CHECK(bytes_eq(batch.payloads[0].route, "jxl:box-jumb"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_JXL_BOX);
    OMC_TEST_CHECK(memcmp(batch.payloads[0].op.box_type, "jumb", 4U) == 0);
    OMC_TEST_REQUIRE(batch.payloads[0].payload.size > 8U);

    logical_size = 0U;
    write_u32be(logical_box, (omc_u32)(batch.payloads[0].payload.size + 8U));
    memcpy(logical_box + 4U, "jumb", 4U);
    memcpy(logical_box + 8U, batch.payloads[0].payload.data,
           batch.payloads[0].payload.size);
    logical_size = batch.payloads[0].payload.size + 8U;
    assert_projected_jumbf_roundtrip(logical_box, logical_size);

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_build_bmff_projected_jumbf(void)
{
    omc_store store;
    omc_arena storage;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&storage);

    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.label", "alpha");
    add_test_jumbf_cbor_u64_entry(&store, "box.0.1.cbor.count", 7U);
    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.items[0]", "left");
    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.items[1]", "right");

    omc_transfer_payload_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_HEIF;
    opts.include_exif = 0;
    opts.include_xmp = 0;
    opts.include_icc = 0;
    opts.include_iptc = 0;

    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);
    OMC_TEST_REQUIRE_U64_EQ(batch.payload_count, 1U);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].semantic_kind,
                          OMC_TRANSFER_SEMANTIC_JUMBF);
    OMC_TEST_CHECK(bytes_eq(batch.payloads[0].route, "bmff:item-jumb"));
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.kind,
                          OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM);
    OMC_TEST_CHECK_U64_EQ(batch.payloads[0].op.bmff_item_type,
                          OMC_FOURCC('j', 'u', 'm', 'b'));
    OMC_TEST_REQUIRE(batch.payloads[0].payload.size > 16U);
    OMC_TEST_CHECK(memcmp(batch.payloads[0].payload.data + 4U, "jumb", 4U)
                   == 0);

    assert_projected_jumbf_roundtrip(batch.payloads[0].payload.data,
                                     batch.payloads[0].payload.size);

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_build_rejects_ambiguous_projected_jumbf(void)
{
    omc_store store;
    omc_arena storage;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&storage);

    add_test_jumbf_cbor_text_entry(&store, "box.0.1.cbor.map.1", "bad");

    omc_transfer_payload_build_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_HEIF;
    opts.include_exif = 0;
    opts.include_xmp = 0;
    opts.include_icc = 0;
    opts.include_iptc = 0;

    status = omc_transfer_payload_batch_build(&store, &opts, &storage, &batch,
                                              &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(io_res.status, OMC_TRANSFER_UNSUPPORTED);
    OMC_TEST_CHECK_U64_EQ(batch.payload_count, 0U);

    omc_arena_fini(&storage);
    omc_store_fini(&store);
}

static void
test_transfer_payload_deserialize_rejects_bad_magic(void)
{
    omc_u8 bytes[16];
    omc_arena storage;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res io_res;
    omc_status status;

    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "BADPLD01", 8U);
    omc_arena_init(&storage);

    status = omc_transfer_payload_batch_deserialize(
        bytes, sizeof(bytes), &storage, &batch, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(io_res.status, OMC_TRANSFER_UNSUPPORTED);

    omc_arena_fini(&storage);
}

int
main(void)
{
    test_transfer_payload_build_jpeg_roundtrip_and_replay();
    test_transfer_payload_target_image_spec_filters_stale_layout();
    test_transfer_payload_target_image_spec_rejects_invalid();
    test_transfer_payload_build_bmff_xmp_and_icc();
    test_transfer_payload_build_jpeg_projected_jumbf();
    test_transfer_payload_build_jxl_projected_jumbf();
    test_transfer_payload_build_bmff_projected_jumbf();
    test_transfer_payload_build_rejects_ambiguous_projected_jumbf();
    test_transfer_payload_deserialize_rejects_bad_magic();
    return omc_test_finish();
}
