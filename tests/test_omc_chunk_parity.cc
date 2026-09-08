#include <openmeta/container_scan.h>
#include <openmeta/container_payload.h>
#include "omc/omc_read_source.h"
#include "omc_test_chunk_fixture.h"
#include <array>
#include <cstring>

bool run_omc_chunk_parity();

namespace {
openmeta::RandomAccessIoResult
cpp_chunk_read(void *context, uint64_t offset, std::span<std::byte> out) noexcept
{
    const auto r = omc_test_chunk_read(context, offset,
                                      reinterpret_cast<omc_u8 *>(out.data()), out.size());
    return {static_cast<openmeta::RandomAccessIoCode>(r.code), r.bytes_read};
}

void equal_block(const omc_blk_ref &a, const openmeta::ContainerBlockRef &b)
{
    assert(static_cast<int>(a.format) == static_cast<int>(b.format)); // PNG/WebP
    assert(static_cast<int>(a.kind) == static_cast<int>(b.kind));
    assert(static_cast<int>(a.compression) == static_cast<int>(b.compression));
    assert(a.chunking == OMC_BLK_CHUNK_NONE && b.chunking == openmeta::BlockChunking::None);
    assert(a.outer_offset == b.outer_offset && a.outer_size == b.outer_size);
    assert(a.data_offset == b.data_offset && a.data_size == b.data_size);
    assert(a.id == b.id && a.part_index == b.part_index && a.part_count == b.part_count);
    assert(a.group == b.group && a.logical_offset == b.logical_offset &&
           a.logical_size == b.logical_size && a.aux_u32 == b.aux_u32);
}

void chunk_parity(bool webp)
{
    omc_test_chunk_fixture f;
    /* The pinned C++ build has zlib. Compare matching enabled features;
     * direct tests separately require unsupported results when C disables it. */
    omc_test_chunk_make(&f, webp, OMC_HAVE_ZLIB);
    std::array<omc_blk_ref, 32> blocks{};
    std::array<openmeta::ContainerBlockRef, 32> cpp_blocks{};
    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte *>(f.bytes), f.size};
    for (const uint32_t cap : {0U, 1U, 2U, 32U}) {
        /* C exposes a separate count-only operation; C++ uses empty output. */
        const auto a = cap == 0U
            ? (webp ? omc_scan_meas_webp(f.bytes, f.size) : omc_scan_meas_png(f.bytes, f.size))
            : omc_scan_auto(f.bytes, f.size, blocks.data(), cap);
        const auto b = webp ? openmeta::scan_webp(bytes, std::span(cpp_blocks).first(cap))
                            : openmeta::scan_png(bytes, std::span(cpp_blocks).first(cap));
        assert(static_cast<int>(a.status) == static_cast<int>(b.status));
        assert(a.written == b.written && a.needed == b.needed);
        for (uint32_t i = 0; i < a.written; ++i)
            equal_block(blocks[i], cpp_blocks[i]);
    }
    /* Independently assembled logical payload must match, including both
     * split fragments and a later independent JUMBF stream. */
    const auto scanned = omc_scan_auto(f.bytes, f.size, blocks.data(), 32U);
    std::array<omc_u8, 2048> payload{}, metadata{};
    std::array<std::byte, 2048> cpp_payload{};
    std::array<uint32_t, 32> indices{}, cpp_indices{};
    for (uint32_t i = 0; i < scanned.written; ++i) {
        if (blocks[i].part_count > 1U && blocks[i].part_index != 0U)
            continue;
        const auto a = omc_pay_ext(f.bytes, f.size, blocks.data(), scanned.written,
                                   i, payload.data(), payload.size(), indices.data(),
                                   32U, nullptr);
        const auto b = openmeta::extract_payload(bytes,
            std::span(cpp_blocks).first(scanned.written), i, cpp_payload, cpp_indices, {});
        assert(a.status == OMC_PAY_OK && b.status == openmeta::PayloadStatus::Ok);
        assert(a.written == b.written && a.needed == b.needed);
        assert(std::memcmp(payload.data(), cpp_payload.data(), static_cast<size_t>(a.written)) == 0);
        if (blocks[i].kind == OMC_BLK_JUMBF) {
            assert(a.written == sizeof(omc_test_chunk_jumbf));
            assert(std::memcmp(payload.data(), omc_test_chunk_jumbf, sizeof(omc_test_chunk_jumbf)) == 0);
        }
    }
    /* Callback scanners see original range-relative positions after a 3 GiB
     * image gap. The callback rejects every attempted pixel-body read. */
    omc_test_chunk_set_gap(&f, 0xc0000000U);
    omc_source_range range{
        omc_source_callback(f.base + f.size + f.gap, &f, omc_test_chunk_read, 0),
        f.base, f.size + f.gap};
    const auto cpp_range = openmeta::make_random_access_source_range(
        openmeta::make_callback_random_access_source(f.base + f.size + f.gap, &f,
                                                     cpp_chunk_read, false),
        f.base, f.size + f.gap);
    std::array<omc_exif_ifd_ref, 32> ifds{};
    omc_read_source_workspace w{
        metadata.data(), metadata.size(), blocks.data(), 32U, ifds.data(), 32U,
        payload.data(), payload.size(), indices.data(), 32U};
    omc_store store;
    omc_store_init(&store);
    omc_source_state state{};
    const auto read = omc_read_source(&range, &store, &w, &state, nullptr);
    assert(read.status == OMC_READ_SOURCE_OK && read.decoded.scan.status == OMC_SCAN_OK);
    std::array<std::byte, 512> window{};
    openmeta::ContainerRandomAccessScratch scratch;
    scratch.read_window = window;
    scratch.window_options.minimum_read_bytes = 0U;
    const auto cpp_scan = webp ? openmeta::scan_webp_random_access(cpp_range, cpp_blocks, scratch)
                               : openmeta::scan_png_random_access(cpp_range, cpp_blocks, scratch);
    assert(cpp_scan.complete() && cpp_scan.scan.status == openmeta::ScanStatus::Ok);
    assert(read.decoded.scan.written == cpp_scan.scan.written);
    for (uint32_t i = 0; i < read.decoded.scan.written; ++i) {
        equal_block(blocks[i], cpp_blocks[i]);
        equal_block(store.blocks[i], cpp_blocks[i]);
    }
    omc_store_fini(&store);
}
} // namespace

bool run_omc_chunk_parity()
{
    chunk_parity(false);
    chunk_parity(true);
    return true;
}
