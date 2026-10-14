#include "omc/omc_pay.h"
#include "edit/omc_exif_write.h"
#include "omc_test_assert.h"

#include <openmeta/container_payload.h>
#include <openmeta/metadata_transfer.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

static void
check_payload(std::span<const uint8_t> input, omc_blk_comp compression)
{
    omc_blk_ref block {};
    block.kind = OMC_BLK_XMP;
    block.compression = compression;
    block.data_size = input.size();
    openmeta::ContainerBlockRef reference {};
    reference.compression = compression == OMC_BLK_COMP_DEFLATE
                                ? openmeta::BlockCompression::Deflate
                                : openmeta::BlockCompression::Brotli;
    reference.data_size = input.size();
    const std::array<uint64_t, 3> limits { 8U, 7U, 0U };
    const std::array<size_t, 3> capacities { 16U, 3U, 0U };
    for (uint64_t limit : limits) {
        for (size_t capacity : capacities) {
            std::array<uint8_t, 18> actual, expected;
            actual.fill(0xA5U);
            expected.fill(0xA5U);
            omc_pay_opts options;
            omc_pay_opts_init(&options);
            options.limits.max_output_bytes = limit;
            openmeta::PayloadOptions reference_options;
            reference_options.limits.max_output_bytes = limit;
            const auto got = omc_pay_ext(input.data(), input.size(), &block,
                                        1U, 0U, actual.data() + 1U, capacity,
                                        nullptr, 0U, &options);
            const auto want = openmeta::extract_payload(
                std::as_bytes(input), std::span(&reference, 1U), 0U,
                std::as_writable_bytes(std::span(expected).subspan(1U, capacity)),
                {}, reference_options);
            assert(static_cast<unsigned>(got.status)
                   == static_cast<unsigned>(want.status));
            assert(got.written == want.written);
            assert(got.needed == want.needed);
            assert(actual == expected);
            const auto measured = omc_pay_meas(input.data(), input.size(), &block,
                                               1U, 0U, nullptr, 0U, &options);
            assert(measured.written == 0U);
            assert(measured.needed == want.needed);
            assert(measured.status == (got.status == OMC_PAY_TRUNCATED
                                           ? OMC_PAY_OK : got.status));
        }
    }
}

static void
put_tiff(uint8_t* out, uint64_t value, unsigned width, bool little)
{
    for (unsigned i = 0U; i < width; ++i)
        out[little ? i : width - 1U - i] = static_cast<uint8_t>(value >> (8U * i));
}

static void
check_tiff_noops()
{
    for (bool big : {false, true}) {
        for (bool little : {false, true}) {
            const size_t root = big ? 16U : 8U;
            const unsigned cs = big ? 8U : 2U;
            const size_t ns = big ? 8U : 4U;
            for (unsigned variant = 0U; variant < (big ? 4U : 3U); ++variant) {
                std::array<uint8_t, 64> input {};
                input[0] = input[1] = little ? 'I' : 'M';
                put_tiff(input.data() + 2U, big ? 43U : 42U, 2U, little);
                if (big) put_tiff(input.data() + 4U, 8U, 2U, little);
                put_tiff(input.data() + (big ? 8U : 4U), root, big ? 8U : 4U, little);
                std::memcpy(input.data() + 40U, "PIXELS!!", 8U);
                size_t size = variant == 1U ? root + cs + ns - 1U : input.size();
                if (variant >= 2U)
                    put_tiff(input.data() + root,
                             variant == 3U ? UINT64_MAX / 20U + 1U
                                           : big ? UINT64_MAX : 65535U, cs, little);
                omc_store empty;
                omc_store_init(&empty);
                omc_arena actual;
                omc_arena_init(&actual);
                omc_exif_write_res got;
                assert(omc_exif_write_embedded(input.data(), size, &empty, &actual,
                           OMC_SCAN_FMT_TIFF, &got) == OMC_STATUS_OK);
                openmeta::PreparedTransferBundle bundle;
                bundle.target_format = openmeta::TransferTargetFormat::Tiff;
                openmeta::PlanTiffEditOptions options;
                options.require_updates = false;
                const auto bytes = std::as_bytes(std::span(input).first(size));
                const auto plan = openmeta::plan_prepared_bundle_tiff_edit(bytes, bundle, options);
                if (variant == 0U) {
                    assert(got.status == OMC_EXIF_WRITE_OK);
                    assert(plan.status == openmeta::TransferStatus::Ok);
                    std::vector<std::byte> expected;
                    assert(openmeta::apply_prepared_bundle_tiff_edit(bytes, bundle, plan,
                               &expected).status == openmeta::TransferStatus::Ok);
                    assert(actual.size == size && expected.size() == size);
                    assert(std::memcmp(actual.data, input.data(), size) == 0);
                    assert(std::memcmp(actual.data, expected.data(), size) == 0);
                } else {
                    assert(got.status == OMC_EXIF_WRITE_MALFORMED);
                    assert(plan.status != openmeta::TransferStatus::Ok);
                    assert(actual.size == 0U);
                }
                omc_arena_fini(&actual);
                omc_store_fini(&empty);
            }
        }
    }
}

int main()
{
    /* Stored deflate avoids an encoder dependency in the parity executable. */
    std::array<uint8_t, 19> deflate {
        0x78, 0x01, 0x01, 0x08, 0x00, 0xF7, 0xFF,
        'A', 'B', 'C', 'D', '1', '2', '3', '4', 0, 0, 0, 0
    };
    uint32_t s1 = 1U, s2 = 0U;
    for (size_t i = 7U; i < 15U; ++i) {
        s1 = (s1 + deflate[i]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    const uint32_t adler = (s2 << 16U) | s1;
    for (unsigned i = 0U; i < 4U; ++i)
        deflate[15U + i] = static_cast<uint8_t>(adler >> (24U - i * 8U));
    const std::array<uint8_t, 12> brotli {
        0x8B, 0x03, 0x80, 'A', 'B', 'C', 'D', '1', '2', '3', '4', 0x03
    };
#if OMC_HAVE_ZLIB
    check_payload(deflate, OMC_BLK_COMP_DEFLATE);
    {
        omc_blk_ref block {};
        block.kind = OMC_BLK_XMP;
        block.compression = OMC_BLK_COMP_DEFLATE;
        block.data_size = deflate.size() - 1U;
        omc_pay_opts opts;
        omc_pay_opts_init(&opts);
        opts.limits.max_output_bytes = 8U;
        openmeta::ContainerBlockRef reference {};
        reference.compression = openmeta::BlockCompression::Deflate;
        reference.data_size = block.data_size;
        openmeta::PayloadOptions reference_options;
        reference_options.limits.max_output_bytes = 8U;
        std::array<uint8_t, 16> actual {}, expected {};
        const auto got = omc_pay_ext(deflate.data(), deflate.size() - 1U,
                                    &block, 1U, 0U, actual.data(), actual.size(),
                                    nullptr, 0U, &opts);
        const auto want = openmeta::extract_payload(
            std::as_bytes(std::span(deflate).first(deflate.size() - 1U)),
            std::span(&reference, 1U), 0U, std::as_writable_bytes(std::span(expected)),
            {}, reference_options);
        assert(got.status == OMC_PAY_MALFORMED
               && want.status == openmeta::PayloadStatus::Malformed);
        assert(got.written == want.written && got.needed == want.needed);
        assert(actual == expected);
    }
#endif
#if OMC_HAVE_BROTLI
    check_payload(brotli, OMC_BLK_COMP_BROTLI);
#endif
    check_tiff_noops();
    return 0;
}
