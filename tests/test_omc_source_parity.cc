#include <openmeta/random_access_source.h>
#include <openmeta/container_scan.h>
#include <openmeta/exif_tiff_decode.h>
#include <openmeta/exif_tiff_serialize.h>
#include "omc/omc_read_source.h"
#include "omc/omc_exif_tiff_serialize.h"
#include "omc_test_assert.h"
#include <array>
#include <cstring>
#include <vector>

bool run_omc_source_parity();
namespace
{
struct Host {
    std::array<omc_u8, 512> bytes{};
    unsigned mode = 0;
};
omc_source_io_res
c_read(void *p, omc_u64 offset, omc_u8 *out, omc_size size)
{
    auto &h = *static_cast<Host *>(p);
    omc_source_io_res r{OMC_SOURCE_IO_OK, size};
    if (h.mode == 1U)
        r.bytes_read = size ? size - 1U : 0U;
    if (h.mode >= 2U && h.mode <= 4U)
        r.code = static_cast<omc_source_io_code>(h.mode - 1U);
    if (h.mode == 5U)
        r.code = static_cast<omc_source_io_code>(19);
    if (h.mode == 6U)
        r.bytes_read = size + 1U;
    assert(offset <= h.bytes.size() && size <= h.bytes.size() - offset);
    if (h.mode < 5U)
        std::memcpy(out, h.bytes.data() + offset, static_cast<size_t>(r.bytes_read));
    return r;
}
openmeta::RandomAccessIoResult
cpp_read(void *p, uint64_t offset, std::span<std::byte> out) noexcept
{
    const auto r =
        c_read(p, offset, reinterpret_cast<omc_u8 *>(out.data()), out.size());
    return {static_cast<openmeta::RandomAccessIoCode>(r.code), r.bytes_read};
}
void
compare_state(const omc_source_state &a, const openmeta::RandomAccessReadState &b)
{
    assert(static_cast<int>(a.code) == static_cast<int>(b.code));
    assert(static_cast<int>(a.io_code) == static_cast<int>(b.io_code));
    assert(a.requests_issued == b.requests_issued &&
           a.bytes_requested == b.bytes_requested);
    assert(a.bytes_completed == b.bytes_completed &&
           a.failure_offset == b.failure_offset);
    assert(a.failure_request_bytes == b.failure_request_bytes &&
           a.failure_bytes_read == b.failure_bytes_read);
}
void
primitives()
{
    Host h;
    for (size_t i = 0; i < h.bytes.size(); ++i)
        h.bytes[i] = static_cast<omc_u8>(i);
    omc_source_range range{omc_source_callback(512U, &h, c_read, 1), 10U, 16U};
    auto cpp_range = openmeta::make_random_access_source_range(
        openmeta::make_callback_random_access_source(512U, &h, cpp_read, true), 10U,
        16U);
    for (unsigned mode = 0; mode < 7U; ++mode)
        for (unsigned budget = 0; budget < 4U; ++budget) {
            h.mode = mode;
            omc_source_state a{};
            openmeta::RandomAccessReadState b;
            omc_source_limits al{};
            omc_source_limits_init(&al);
            openmeta::RandomAccessReadLimits bl;
            if (budget == 1U) {
                al.max_single_read_bytes = 3U;
                bl.max_single_read_bytes = 3U;
            }
            if (budget == 2U) {
                al.max_requests = 1U;
                bl.max_requests = 1U;
            }
            if (budget == 3U) {
                al.max_total_bytes = 6U;
                bl.max_total_bytes = 6U;
            }
            const unsigned offsets[] = {0U, 5U, 15U}, sizes[] = {4U, 8U, 2U};
            for (unsigned i = 0; i < 3U; ++i) {
                std::array<omc_u8, 8> ca{};
                std::array<std::byte, 8> cb{};
                const auto ac =
                    omc_source_read(&range, offsets[i], ca.data(), sizes[i], &a, &al);
                const auto bc = openmeta::random_access_read_exact(
                    cpp_range, offsets[i], std::span<std::byte>(cb).first(sizes[i]), &b,
                    bl);
                assert(static_cast<int>(ac) == static_cast<int>(bc));
                compare_state(a, b);
                assert(std::memcmp(ca.data(), cb.data(), sizes[i]) == 0);
            }
        }
    h.mode = 0U;
    for (unsigned memory = 0; memory < 2U; ++memory) {
        if (memory) {
            range.source = omc_source_memory(h.bytes.data(), h.bytes.size());
            cpp_range.source = openmeta::make_memory_random_access_source(
                {reinterpret_cast<const std::byte *>(h.bytes.data()), h.bytes.size()});
        }
        range.size = 64U;
        cpp_range.size = 64U;
        omc_source_state a{};
        openmeta::RandomAccessReadState b;
        omc_source_limits al{};
        omc_source_limits_init(&al);
        al.max_total_bytes = 24U;
        al.max_single_read_bytes = 10U;
        openmeta::RandomAccessReadLimits bl;
        bl.max_total_bytes = 24U;
        bl.max_single_read_bytes = 10U;
        std::array<omc_u8, 16> ca{};
        std::array<std::byte, 16> cb{};
        omc_source_window aw{ca.data(), ca.size(), 0U, 0U, 0};
        openmeta::RandomAccessReadWindow bw;
        bw.storage = cb;
        openmeta::RandomAccessReadWindowOptions options;
        options.minimum_read_bytes = 16U;
        const unsigned offsets[] = {0U, 2U, 9U, 20U, 50U, 64U},
                       sizes[] = {3U, 2U, 5U, 2U, 1U, 0U};
        for (unsigned i = 0; i < 6U; ++i) {
            const auto av =
                omc_source_read_view(&range, offsets[i], sizes[i], &aw, &a, &al, 16U);
            const auto bv = openmeta::random_access_read_view(
                cpp_range, offsets[i], sizes[i], &bw, &b, bl, options);
            compare_state(a, b);
            assert(static_cast<int>(av.code) == static_cast<int>(bv.code));
            assert((av.cache_hit != 0) == bv.cache_hit &&
                   av.bytes.size == bv.bytes.size());
            if (av.bytes.size)
                assert(std::memcmp(av.bytes.data, bv.bytes.data(), av.bytes.size) == 0);
        }
    }
}
void
readers()
{
    Host h;
    const omc_u8 tiff[] = {'I', 'I', 42U, 0U, 8U,  0U,    0U, 0U, 2U, 0U,  0U, 1U, 4U,
                           0U,  1U,  0U,  0U, 0U,  0x80U, 2U, 0U, 0U, 15U, 1U, 2U, 0U,
                           7U,  0U,  0U,  0U, 64U, 0U,    0U, 0U, 0U, 0U,  0U, 0U};
    std::memcpy(h.bytes.data() + 7U, tiff, sizeof(tiff));
    std::memcpy(h.bytes.data() + 71U, "Camera", 7U);
    const uint64_t full_size = 5ULL << 30U;
    omc_source_range range{omc_source_callback(full_size + 7U, &h, c_read, 0), 7U,
                           full_size};
    const auto cpp_range = openmeta::make_random_access_source_range(
        openmeta::make_callback_random_access_source(full_size + 7U, &h, cpp_read,
                                                     false),
        7U, full_size);
    std::array<omc_u8, 512> metadata{}, payload{};
    std::array<omc_blk_ref, 8> blocks{};
    std::array<omc_exif_ifd_ref, 8> ifds{};
    std::array<omc_u32, 8> indices{};
    omc_read_source_workspace w{
        metadata.data(), metadata.size(), blocks.data(),  8U, ifds.data(), 8U,
        payload.data(),  payload.size(),  indices.data(), 8U};
    omc_source_state state{};
    omc_store store;
    omc_store_init(&store);
    const auto result = omc_read_source(&range, &store, &w, &state, nullptr);
    assert(result.status == OMC_READ_SOURCE_OK &&
           result.decoded.exif.status == OMC_EXIF_OK);
    std::array<std::byte, 64> window{};
    std::array<std::byte, 512> value{};
    openmeta::ExifRandomAccessScratch scratch;
    scratch.read_window = window;
    scratch.value = value;
    scratch.window_options.minimum_read_bytes = 0U;
    openmeta::MetaStore cpp_store;
    const auto decoded =
        openmeta::decode_exif_tiff_random_access(cpp_range, cpp_store, {}, scratch, {});
    assert(decoded.complete() &&
           decoded.decode.status == openmeta::ExifDecodeStatus::Ok);
    cpp_store.finalize();
    const auto a = omc_serialize_exif_tiff(&store, {}, nullptr);
    const auto b = openmeta::serialize_exif_tiff(cpp_store, {}, {});
    assert(a.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED &&
           b.status == openmeta::ExifTiffSerializeStatus::OutputTruncated);
    assert(a.needed == b.needed);
    std::vector<omc_u8> ca(static_cast<size_t>(a.needed));
    std::vector<std::byte> cb(static_cast<size_t>(b.needed));
    assert(omc_serialize_exif_tiff(&store, {ca.data(), ca.size()}, nullptr).status ==
           OMC_EXIF_TIFF_OK);
    assert(openmeta::serialize_exif_tiff(cpp_store, cb, {}).status ==
           openmeta::ExifTiffSerializeStatus::Ok);
    assert(std::memcmp(ca.data(), cb.data(), ca.size()) == 0);
    omc_store_fini(&store);
    /* JPEG callback scanners agree on range-relative carrier positions. */
    h.bytes.fill(0U);
    const omc_u8 jpeg[] = {0xFFU, 0xD8U, 0xFFU, 0xDBU, 0U,  4U,  0U,    0U,   0xFFU,
                           0xFEU, 0U,    5U,    'a',   'b', 'c', 0xFFU, 0xDAU};
    std::memcpy(h.bytes.data() + 7U, jpeg, sizeof(jpeg));
    omc_store_init(&store);
    omc_source_state_init(&state);
    const auto jr = omc_read_source(&range, &store, &w, &state, nullptr);
    assert(jr.status == OMC_READ_SOURCE_OK && jr.decoded.scan.written == 1U);
    std::array<std::byte, 512> scan_window{};
    std::array<openmeta::ContainerBlockRef, 8> cpp_blocks{};
    openmeta::ContainerRandomAccessScratch scan_scratch;
    scan_scratch.read_window = scan_window;
    scan_scratch.window_options.minimum_read_bytes = 0U;
    const auto js =
        openmeta::scan_jpeg_random_access(cpp_range, cpp_blocks, scan_scratch);
    assert(js.complete() && js.scan.written == 1U);
    assert(blocks[0].outer_offset == cpp_blocks[0].outer_offset &&
           blocks[0].data_offset == cpp_blocks[0].data_offset);
    assert(blocks[0].outer_size == cpp_blocks[0].outer_size &&
           blocks[0].data_size == cpp_blocks[0].data_size);
    omc_store_fini(&store);
}
} // namespace
bool
run_omc_source_parity()
{
    primitives();
    readers();
    return true;
}
