#include "omc/omc_icc.h"
#include "omc/omc_jxl_encoder_handoff.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer_artifact.h"

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
test_transfer_artifact_inspect_jxl_handoff_with_icc(void)
{
    omc_store store;
    omc_arena built_icc;
    omc_arena serialized;
    omc_jxl_encoder_handoff_opts opts;
    omc_jxl_encoder_handoff handoff;
    omc_jxl_encoder_handoff_io_res io_res;
    omc_transfer_artifact_info info;
    omc_transfer_artifact_io_res inspect_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&built_icc);
    omc_arena_init(&serialized);
    build_store_with_test_icc(&store);

    omc_jxl_encoder_handoff_opts_init(&opts);
    opts.icc_block_index = 9U;
    opts.box_count = 5U;
    opts.box_payload_bytes = 2000U;
    status = omc_jxl_encoder_handoff_build(&store, &opts, &built_icc,
                                           &handoff);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    status = omc_jxl_encoder_handoff_serialize(&handoff, &serialized, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);

    status = omc_transfer_artifact_inspect(serialized.data, serialized.size,
                                           &info, &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(inspect_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(inspect_res.bytes, serialized.size);
    OMC_TEST_CHECK_U64_EQ(info.kind, OMC_TRANSFER_ARTIFACT_JXL_ENCODER_HANDOFF);
    OMC_TEST_CHECK_U64_EQ(info.has_contract_version, 1U);
    OMC_TEST_CHECK_U64_EQ(info.contract_version,
                          OMC_TRANSFER_CONTRACT_VERSION);
    OMC_TEST_CHECK_U64_EQ(info.has_target_format, 1U);
    OMC_TEST_CHECK_U64_EQ(info.target_format, OMC_SCAN_FMT_JXL);
    OMC_TEST_CHECK_U64_EQ(info.entry_count, 5U);
    OMC_TEST_CHECK_U64_EQ(info.has_icc_profile, 1U);
    OMC_TEST_CHECK_U64_EQ(info.icc_block_index, 9U);
    OMC_TEST_CHECK_U64_EQ(info.icc_profile_bytes, built_icc.size);
    OMC_TEST_CHECK_U64_EQ(info.box_payload_bytes, 2000U);
    OMC_TEST_CHECK_U64_EQ(info.payload_bytes, 0U);
    OMC_TEST_CHECK_U64_EQ(info.binding_bytes, 0U);
    OMC_TEST_CHECK_U64_EQ(info.signed_payload_bytes, 0U);

    omc_arena_fini(&serialized);
    omc_arena_fini(&built_icc);
    omc_store_fini(&store);
}

static void
test_transfer_artifact_inspect_jxl_handoff_without_icc(void)
{
    omc_store store;
    omc_arena built_icc;
    omc_arena serialized;
    omc_jxl_encoder_handoff_opts opts;
    omc_jxl_encoder_handoff handoff;
    omc_jxl_encoder_handoff_io_res io_res;
    omc_transfer_artifact_info info;
    omc_transfer_artifact_io_res inspect_res;
    omc_status status;

    omc_store_init(&store);
    omc_arena_init(&built_icc);
    omc_arena_init(&serialized);

    omc_jxl_encoder_handoff_opts_init(&opts);
    opts.box_count = 2U;
    opts.box_payload_bytes = 88U;
    status = omc_jxl_encoder_handoff_build(&store, &opts, &built_icc,
                                           &handoff);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    status = omc_jxl_encoder_handoff_serialize(&handoff, &serialized, &io_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(io_res.status, OMC_TRANSFER_OK);

    status = omc_transfer_artifact_inspect(serialized.data, serialized.size,
                                           &info, &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_REQUIRE_U64_EQ(inspect_res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(info.kind, OMC_TRANSFER_ARTIFACT_JXL_ENCODER_HANDOFF);
    OMC_TEST_CHECK_U64_EQ(info.target_format, OMC_SCAN_FMT_JXL);
    OMC_TEST_CHECK_U64_EQ(info.entry_count, 2U);
    OMC_TEST_CHECK_U64_EQ(info.has_icc_profile, 0U);
    OMC_TEST_CHECK_U64_EQ(info.icc_block_index, 0xFFFFFFFFU);
    OMC_TEST_CHECK_U64_EQ(info.icc_profile_bytes, 0U);
    OMC_TEST_CHECK_U64_EQ(info.box_payload_bytes, 88U);

    omc_arena_fini(&serialized);
    omc_arena_fini(&built_icc);
    omc_store_fini(&store);
}

static void
test_transfer_artifact_inspect_unknown_magic(void)
{
    omc_u8 bytes[8];
    omc_transfer_artifact_info info;
    omc_transfer_artifact_io_res inspect_res;
    omc_status status;

    memcpy(bytes, "NOTJXL!!", sizeof(bytes));
    status = omc_transfer_artifact_inspect(bytes, sizeof(bytes), &info,
                                           &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(inspect_res.status, OMC_TRANSFER_UNSUPPORTED);
    OMC_TEST_CHECK_U64_EQ(info.kind, OMC_TRANSFER_ARTIFACT_UNKNOWN);
    OMC_TEST_CHECK_U64_EQ(info.has_target_format, 0U);
}

static void
test_transfer_artifact_inspect_truncated(void)
{
    static const omc_u8 bytes[7] = {
        (omc_u8)'O', (omc_u8)'M', (omc_u8)'J', (omc_u8)'X',
        (omc_u8)'I', (omc_u8)'C', (omc_u8)'C'
    };
    omc_transfer_artifact_info info;
    omc_transfer_artifact_io_res inspect_res;
    omc_status status;

    status = omc_transfer_artifact_inspect(bytes, sizeof(bytes), &info,
                                           &inspect_res);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    OMC_TEST_CHECK_U64_EQ(inspect_res.status, OMC_TRANSFER_MALFORMED);
    OMC_TEST_CHECK_U64_EQ(info.kind, OMC_TRANSFER_ARTIFACT_UNKNOWN);
}

int
main(void)
{
    test_transfer_artifact_inspect_jxl_handoff_with_icc();
    test_transfer_artifact_inspect_jxl_handoff_without_icc();
    test_transfer_artifact_inspect_unknown_magic();
    test_transfer_artifact_inspect_truncated();
    return omc_test_finish();
}
