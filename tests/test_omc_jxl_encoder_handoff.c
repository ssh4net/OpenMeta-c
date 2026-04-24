#include "omc/omc_icc.h"
#include "omc/omc_jxl_encoder_handoff.h"
#include "omc/omc_store.h"

#include "omc_test_assert.h"
#include <string.h>

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
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

static void
build_store_with_test_icc(omc_store* store)
{
    omc_u8 icc[160];
    omc_icc_res res;

    build_test_icc_blob(icc, sizeof(icc));
    res = omc_icc_dec(icc, sizeof(icc), store, OMC_INVALID_BLOCK_ID,
                      (const omc_icc_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_ICC_OK);
}

static void
test_jxl_encoder_handoff_roundtrip_with_icc(void)
{
    static const omc_u8 k_magic[8] = {
        (omc_u8)'O', (omc_u8)'M', (omc_u8)'J', (omc_u8)'X',
        (omc_u8)'I', (omc_u8)'C', (omc_u8)'C', (omc_u8)'1'
    };
    omc_u8 expected[256];
    omc_size expected_size;
    omc_store store;
    omc_arena built_icc;
    omc_arena serialized;
    omc_arena parsed_icc;
    omc_jxl_encoder_handoff_opts opts;
    omc_jxl_encoder_handoff handoff;
    omc_jxl_encoder_handoff parsed;
    omc_jxl_encoder_handoff_io_res io_res;
    omc_jxl_encoder_handoff_io_res parse_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&built_icc);
    omc_arena_init(&serialized);
    omc_arena_init(&parsed_icc);
    build_store_with_test_icc(&store);

    omc_jxl_encoder_handoff_opts_init(&opts);
    opts.icc_block_index = 7U;
    opts.box_count = 2U;
    opts.box_payload_bytes = 1234U;

    status = omc_jxl_encoder_handoff_build(&store, &opts, &built_icc,
                                           &handoff);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(handoff.contract_version,
                          OMC_TRANSFER_CONTRACT_VERSION);
    OMC_TEST_CHECK_U64_EQ(handoff.has_icc_profile, 1U);
    OMC_TEST_CHECK_U64_EQ(handoff.icc_block_index, 7U);
    OMC_TEST_CHECK_U64_EQ(handoff.box_count, 2U);
    OMC_TEST_CHECK_U64_EQ(handoff.box_payload_bytes, 1234U);
    OMC_TEST_CHECK_U64_EQ(handoff.icc_profile.size, built_icc.size);
    OMC_TEST_REQUIRE_MEM_EQ(handoff.icc_profile.data,
                            handoff.icc_profile.size, built_icc.data,
                            built_icc.size);

    status = omc_jxl_encoder_handoff_serialize(&handoff, &serialized, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(io_res.status, OMC_TRANSFER_OK);

    expected_size = 0U;
    append_raw(expected, &expected_size, k_magic, sizeof(k_magic));
    append_u32le(expected, &expected_size, OMC_JXL_ENCODER_HANDOFF_VERSION);
    append_u32le(expected, &expected_size, OMC_TRANSFER_CONTRACT_VERSION);
    append_u8(expected, &expected_size, 1U);
    append_u32le(expected, &expected_size, 7U);
    append_u32le(expected, &expected_size, 2U);
    append_u64le(expected, &expected_size, 1234U);
    append_u64le(expected, &expected_size, built_icc.size);
    append_raw(expected, &expected_size, built_icc.data, built_icc.size);

    OMC_TEST_CHECK_U64_EQ(io_res.bytes, expected_size);
    OMC_TEST_REQUIRE_U64_EQ(serialized.size, expected_size);
    OMC_TEST_REQUIRE_MEM_EQ(serialized.data, serialized.size, expected,
                            expected_size);

    status = omc_jxl_encoder_handoff_deserialize(
        serialized.data, serialized.size, &parsed_icc, &parsed, &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(parse_res.bytes, expected_size);
    OMC_TEST_CHECK_U64_EQ(parsed.contract_version,
                          OMC_TRANSFER_CONTRACT_VERSION);
    OMC_TEST_CHECK_U64_EQ(parsed.has_icc_profile, 1U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_block_index, 7U);
    OMC_TEST_CHECK_U64_EQ(parsed.box_count, 2U);
    OMC_TEST_CHECK_U64_EQ(parsed.box_payload_bytes, 1234U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_profile.size, built_icc.size);
    OMC_TEST_REQUIRE_MEM_EQ(parsed.icc_profile.data, parsed.icc_profile.size,
                            built_icc.data, built_icc.size);

    omc_arena_fini(&parsed_icc);
    omc_arena_fini(&serialized);
    omc_arena_fini(&built_icc);
    omc_store_fini(&store);
}

static void
test_jxl_encoder_handoff_roundtrip_without_icc(void)
{
    static const omc_u8 k_magic[8] = {
        (omc_u8)'O', (omc_u8)'M', (omc_u8)'J', (omc_u8)'X',
        (omc_u8)'I', (omc_u8)'C', (omc_u8)'C', (omc_u8)'1'
    };
    omc_u8 expected[64];
    omc_size expected_size;
    omc_store store;
    omc_arena built_icc;
    omc_arena serialized;
    omc_arena parsed_icc;
    omc_jxl_encoder_handoff_opts opts;
    omc_jxl_encoder_handoff handoff;
    omc_jxl_encoder_handoff parsed;
    omc_jxl_encoder_handoff_io_res io_res;
    omc_jxl_encoder_handoff_io_res parse_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&built_icc);
    omc_arena_init(&serialized);
    omc_arena_init(&parsed_icc);

    omc_jxl_encoder_handoff_opts_init(&opts);
    opts.box_count = 1U;
    opts.box_payload_bytes = 99U;
    status = omc_jxl_encoder_handoff_build(&store, &opts, &built_icc,
                                           &handoff);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(handoff.has_icc_profile, 0U);
    OMC_TEST_CHECK_U64_EQ(handoff.icc_block_index, 0xFFFFFFFFU);
    OMC_TEST_CHECK_U64_EQ(handoff.box_count, 1U);
    OMC_TEST_CHECK_U64_EQ(handoff.box_payload_bytes, 99U);
    OMC_TEST_CHECK_U64_EQ(handoff.icc_profile.size, 0U);

    status = omc_jxl_encoder_handoff_serialize(&handoff, &serialized, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(io_res.status, OMC_TRANSFER_OK);

    expected_size = 0U;
    append_raw(expected, &expected_size, k_magic, sizeof(k_magic));
    append_u32le(expected, &expected_size, OMC_JXL_ENCODER_HANDOFF_VERSION);
    append_u32le(expected, &expected_size, OMC_TRANSFER_CONTRACT_VERSION);
    append_u8(expected, &expected_size, 0U);
    append_u32le(expected, &expected_size, 0xFFFFFFFFU);
    append_u32le(expected, &expected_size, 1U);
    append_u64le(expected, &expected_size, 99U);
    append_u64le(expected, &expected_size, 0U);
    OMC_TEST_REQUIRE_U64_EQ(serialized.size, expected_size);
    OMC_TEST_REQUIRE_MEM_EQ(serialized.data, serialized.size, expected,
                            expected_size);

    status = omc_jxl_encoder_handoff_deserialize(
        serialized.data, serialized.size, &parsed_icc, &parsed, &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(parsed.has_icc_profile, 0U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_block_index, 0xFFFFFFFFU);
    OMC_TEST_CHECK_U64_EQ(parsed.box_count, 1U);
    OMC_TEST_CHECK_U64_EQ(parsed.box_payload_bytes, 99U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_profile.size, 0U);

    omc_arena_fini(&parsed_icc);
    omc_arena_fini(&serialized);
    omc_arena_fini(&built_icc);
    omc_store_fini(&store);
}

static void
test_jxl_encoder_handoff_deserialize_rejects_bad_magic(void)
{
    omc_u8 bytes[16];
    omc_size size;
    omc_arena parsed_icc;
    omc_jxl_encoder_handoff parsed;
    omc_jxl_encoder_handoff_io_res parse_res;
    omc_status status;

    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "badmagic", 8U);
    size = 8U;
    append_u32le(bytes, &size, OMC_JXL_ENCODER_HANDOFF_VERSION);

    omc_arena_init(&parsed_icc);
    status = omc_jxl_encoder_handoff_deserialize(
        bytes, 12U, &parsed_icc, &parsed, &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(parse_res.status, OMC_TRANSFER_UNSUPPORTED);
    omc_arena_fini(&parsed_icc);
}

static void
test_jxl_encoder_handoff_deserialize_rejects_missing_icc_payload(void)
{
    static const omc_u8 k_magic[8] = {
        (omc_u8)'O', (omc_u8)'M', (omc_u8)'J', (omc_u8)'X',
        (omc_u8)'I', (omc_u8)'C', (omc_u8)'C', (omc_u8)'1'
    };
    omc_u8 bytes[64];
    omc_size size;
    omc_arena parsed_icc;
    omc_jxl_encoder_handoff parsed;
    omc_jxl_encoder_handoff_io_res parse_res;
    omc_status status;

    size = 0U;
    append_raw(bytes, &size, k_magic, sizeof(k_magic));
    append_u32le(bytes, &size, OMC_JXL_ENCODER_HANDOFF_VERSION);
    append_u32le(bytes, &size, OMC_TRANSFER_CONTRACT_VERSION);
    append_u8(bytes, &size, 1U);
    append_u32le(bytes, &size, 0U);
    append_u32le(bytes, &size, 0U);
    append_u64le(bytes, &size, 0U);
    append_u64le(bytes, &size, 0U);

    omc_arena_init(&parsed_icc);
    status = omc_jxl_encoder_handoff_deserialize(bytes, size, &parsed_icc,
                                                 &parsed, &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(parse_res.status, OMC_TRANSFER_MALFORMED);
    omc_arena_fini(&parsed_icc);
}

static void
test_jxl_encoder_handoff_parse_view_roundtrip_with_icc(void)
{
    omc_store store;
    omc_arena built_icc;
    omc_arena serialized;
    omc_jxl_encoder_handoff_opts opts;
    omc_jxl_encoder_handoff built;
    omc_jxl_encoder_handoff parsed;
    omc_jxl_encoder_handoff_io_res io_res;
    omc_jxl_encoder_handoff_io_res parse_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&built_icc);
    omc_arena_init(&serialized);
    build_store_with_test_icc(&store);

    omc_jxl_encoder_handoff_opts_init(&opts);
    opts.icc_block_index = 11U;
    opts.box_count = 4U;
    opts.box_payload_bytes = 321U;

    status = omc_jxl_encoder_handoff_build(&store, &opts, &built_icc, &built);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    status = omc_jxl_encoder_handoff_serialize(&built, &serialized, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);

    status = omc_jxl_encoder_handoff_parse_view(serialized.data,
                                                serialized.size, &parsed,
                                                &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(parsed.contract_version,
                          OMC_TRANSFER_CONTRACT_VERSION);
    OMC_TEST_CHECK_U64_EQ(parsed.has_icc_profile, 1U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_block_index, 11U);
    OMC_TEST_CHECK_U64_EQ(parsed.box_count, 4U);
    OMC_TEST_CHECK_U64_EQ(parsed.box_payload_bytes, 321U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_profile.size, built_icc.size);
    OMC_TEST_REQUIRE(parsed.icc_profile.data == serialized.data + 41U);
    OMC_TEST_REQUIRE_MEM_EQ(parsed.icc_profile.data, parsed.icc_profile.size,
                            built_icc.data, built_icc.size);

    omc_arena_fini(&serialized);
    omc_arena_fini(&built_icc);
    omc_store_fini(&store);
}

static void
test_jxl_encoder_handoff_parse_view_without_icc(void)
{
    omc_store store;
    omc_arena built_icc;
    omc_arena serialized;
    omc_jxl_encoder_handoff_opts opts;
    omc_jxl_encoder_handoff built;
    omc_jxl_encoder_handoff parsed;
    omc_jxl_encoder_handoff_io_res io_res;
    omc_jxl_encoder_handoff_io_res parse_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&built_icc);
    omc_arena_init(&serialized);

    omc_jxl_encoder_handoff_opts_init(&opts);
    opts.box_count = 3U;
    opts.box_payload_bytes = 77U;

    status = omc_jxl_encoder_handoff_build(&store, &opts, &built_icc, &built);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    status = omc_jxl_encoder_handoff_serialize(&built, &serialized, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);

    status = omc_jxl_encoder_handoff_parse_view(serialized.data,
                                                serialized.size, &parsed,
                                                &parse_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(parse_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(parsed.has_icc_profile, 0U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_block_index, 0xFFFFFFFFU);
    OMC_TEST_CHECK_U64_EQ(parsed.box_count, 3U);
    OMC_TEST_CHECK_U64_EQ(parsed.box_payload_bytes, 77U);
    OMC_TEST_CHECK_U64_EQ(parsed.icc_profile.size, 0U);
    OMC_TEST_REQUIRE(parsed.icc_profile.data == (const omc_u8*)0);

    omc_arena_fini(&serialized);
    omc_arena_fini(&built_icc);
    omc_store_fini(&store);
}

int
main(void)
{
    test_jxl_encoder_handoff_roundtrip_with_icc();
    test_jxl_encoder_handoff_roundtrip_without_icc();
    test_jxl_encoder_handoff_deserialize_rejects_bad_magic();
    test_jxl_encoder_handoff_deserialize_rejects_missing_icc_payload();
    test_jxl_encoder_handoff_parse_view_roundtrip_with_icc();
    test_jxl_encoder_handoff_parse_view_without_icc();
    return omc_test_finish();
}
