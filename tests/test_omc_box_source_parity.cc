#include <openmeta/container_scan.h>
#include <openmeta/container_payload.h>
#include "omc/omc_read_source.h"
#include "omc_test_assert.h"
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>

bool run_omc_box_source_parity();
namespace {
using Bytes = std::vector<omc_u8>;
constexpr const char text[] = "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:t='urn:test' t:value='sparse'/></rdf:RDF>";
void number(Bytes& b, uint64_t n, unsigned width) {
    for (unsigned i = width; i; --i) b.push_back(static_cast<omc_u8>(n >> ((i - 1U) * 8U)));
}
void patch(Bytes& b, size_t p, uint64_t n, unsigned width) {
    for (unsigned i = 0; i < width; ++i) b[p + i] = static_cast<omc_u8>(n >> ((width - 1U - i) * 8U));
}
void literal(Bytes& b, const char* p, size_t size) { b.insert(b.end(), p, p + size); }
size_t box(Bytes& b, const char* type) { const auto p = b.size(); number(b, 0, 4); literal(b, type, 4); return p; }
void end(Bytes& b, size_t p) { patch(b, p, b.size() - p, 4); }
struct Host {
    Bytes bytes;
    uint64_t gap = 0;
    size_t body_begin = 0, body_end = 0;
    unsigned failure = 0;
};
omc_source_io_res read(void* context, omc_u64 offset, omc_u8* out, omc_size size) {
    auto& h = *static_cast<Host*>(context);
    assert(offset >= 37U); offset -= 37U;
    assert(!size || offset + size <= h.body_begin || offset >= h.body_end + h.gap);
    if (offset >= h.body_end + h.gap) offset -= h.gap;
    assert(offset <= h.bytes.size() && size <= h.bytes.size() - offset);
    omc_source_io_res r{OMC_SOURCE_IO_OK, size};
    if (h.failure == 1 && size) --r.bytes_read;
    if (h.failure == 2) { r.code = OMC_SOURCE_IO_CANCELLED; r.bytes_read = 0; }
    if (r.bytes_read) std::memcpy(out, h.bytes.data() + offset, r.bytes_read);
    return r;
}
openmeta::RandomAccessIoResult cpp_read(void* context, uint64_t offset, std::span<std::byte> out) noexcept {
    const auto r = read(context, offset, reinterpret_cast<omc_u8*>(out.data()), out.size());
    return {static_cast<openmeta::RandomAccessIoCode>(r.code), r.bytes_read};
}
Host make(unsigned format, uint64_t gap) {
    Host h; auto& b = h.bytes;
    if (format >= 3) {
        number(b, 0xffd8, 2);
        for (unsigned part : {1U, 0U}) {
            const size_t length = part ? sizeof(text) - 1U - 5U : 5U;
            const size_t prefix = format == 3 ? 14U : 75U;
            number(b, format == 3 ? 0xffe2 : 0xffe1, 2); number(b, length + prefix + 2U, 2);
            if (format == 3) { literal(b, "ICC_PROFILE", 12); number(b, part + 1U, 1); number(b, 2U, 1); }
            else {
                literal(b, "http://ns.adobe.com/xmp/extension/", 35);
                literal(b, "0123456789ABCDEF0123456789ABCDEF", 32);
                number(b, sizeof(text) - 1U, 4); number(b, part ? 5U : 0U, 4);
            }
            literal(b, text + (part ? 5U : 0U), length);
            if (part) {
                number(b, 0xffdb, 2); number(b, 66, 2);
                h.body_begin = b.size(); b.resize(b.size() + 64, 0xaa); h.body_end = b.size();
            }
        }
        number(b, 0xffda, 2);
        return h;
    }
    size_t first_offset = 0, second_offset = 0, first_data = 0, second_data = 0;
    if (format < 2) {
        const auto p = box(b, format == 0 ? "jP  " : "JXL "); number(b, 0x0d0a870a, 4); end(b, p);
    } else {
        auto p = box(b, "ftyp"); literal(b, "heic", 4); number(b, 0, 4); literal(b, "mif1", 4); end(b, p);
        const auto meta = box(b, "meta"); number(b, 0, 4);
        const auto iinf = box(b, "iinf"); number(b, 0, 4); number(b, 1, 2);
        p = box(b, "infe"); number(b, 0x02000000, 4); number(b, 7, 2); number(b, 0, 2);
        literal(b, "mime", 4); literal(b, "metadata", 9); literal(b, "application/rdf+xml", 20); number(b, 0, 1);
        end(b, p); end(b, iinf);
        p = box(b, "iloc"); number(b, 0x02000000, 4); number(b, 0x8400, 2); number(b, 1, 4);
        number(b, 7, 4); number(b, 0, 2); number(b, 0, 2); number(b, 2, 2);
        first_offset = b.size(); number(b, 0, 8); number(b, 5, 4);
        second_offset = b.size(); number(b, 0, 8); number(b, sizeof(text) - 1U - 5U, 4); end(b, p); end(b, meta);
        p = box(b, "free"); first_data = b.size(); literal(b, text, 5); end(b, p);
    }
    const auto body = b.size(); number(b, 1, 4); literal(b, format == 0 ? "jp2c" : format == 1 ? "jxlc" : "mdat", 4);
    number(b, 80U + gap, 8); h.body_begin = b.size(); b.resize(b.size() + 64, 0xaa); h.body_end = b.size();
    assert(h.body_end - body == 80U);
    const auto p = box(b, format < 2 ? "xml " : "free"); second_data = b.size();
    literal(b, text + (format < 2 ? 0U : 5U), sizeof(text) - 1U - (format < 2 ? 0U : 5U)); end(b, p);
    if (format == 2) { patch(b, first_offset, first_data, 8); patch(b, second_offset, second_data + gap, 8); }
    h.gap = gap;
    return h;
}
void equal(const omc_blk_ref& a, const openmeta::ContainerBlockRef& b) {
    assert(static_cast<int>(a.kind) == static_cast<int>(b.kind));
    assert(a.compression == OMC_BLK_COMP_NONE && b.compression == openmeta::BlockCompression::None);
    assert(static_cast<int>(a.chunking) - (a.chunking == OMC_BLK_CHUNK_JPEG_XMP_EXT ? 1 : 0) == static_cast<int>(b.chunking));
    assert(a.outer_offset == b.outer_offset && a.outer_size == b.outer_size);
    assert(a.data_offset == b.data_offset && a.data_size == b.data_size);
    assert(a.id == b.id && a.group == b.group && a.part_index == b.part_index && a.part_count == b.part_count);
    assert(a.logical_offset == b.logical_offset && a.logical_size == b.logical_size && a.aux_u32 == b.aux_u32);
}
void check(unsigned format, uint64_t gap) {
    Host h = make(format, gap);
    omc_source_range range{omc_source_callback(h.bytes.size() + gap + 37U, &h, read, 0), 37U, h.bytes.size() + gap};
    auto cpp_range = openmeta::make_random_access_source_range(openmeta::make_callback_random_access_source(
        h.bytes.size() + gap + 37U, &h, cpp_read, false), 37U, h.bytes.size() + gap);
    std::array<omc_blk_ref, 8> blocks{};
    std::array<openmeta::ContainerBlockRef, 8> cpp_blocks{};
    std::array<std::byte, 512> window{}, cpp_payload{}, compressed{};
    std::array<omc_u8, 512> payload{};
    std::array<uint32_t, 8> indices{}, cpp_indices{};
    openmeta::ContainerRandomAccessScratch scratch;
    scratch.read_window = window; scratch.window_options.minimum_read_bytes = 0;
    omc_source_state state{};
    const auto a = omc_scan_source(&range, OMC_SCAN_FMT_UNKNOWN, blocks.data(), blocks.size(), &state, nullptr);
    const auto b = format == 0 ? openmeta::scan_jp2_random_access(cpp_range, cpp_blocks, scratch)
                  : format == 1 ? openmeta::scan_jxl_random_access(cpp_range, cpp_blocks, scratch)
                  : format == 2 ? openmeta::scan_bmff_random_access(cpp_range, cpp_blocks, scratch)
                  : openmeta::scan_jpeg_random_access(cpp_range, cpp_blocks, scratch);
    assert(state.code == OMC_SOURCE_OK && a.status == OMC_SCAN_OK && b.complete() && b.scan.status == openmeta::ScanStatus::Ok);
    assert(a.written == (format >= 2 ? 2U : 1U) && a.written == b.scan.written && a.needed == b.scan.needed);
    for (uint32_t i = 0; i < a.written; ++i) equal(blocks[i], cpp_blocks[i]);
    openmeta::PayloadRandomAccessScratch ps; ps.read_window = window; ps.compressed = compressed; ps.window_options.minimum_read_bytes = 0;
    for (size_t capacity : {0U, 7U, 512U}) {
        omc_source_state_init(&state);
        const auto c = omc_pay_ext_source(&range, blocks.data(), a.written, 0, payload.data(), capacity,
                                         indices.data(), indices.size(), nullptr, &state, nullptr, nullptr);
        const auto d = openmeta::extract_payload_random_access(cpp_range, std::span(cpp_blocks).first(a.written), 0,
                        std::span(cpp_payload).first(capacity), cpp_indices, ps, {});
        assert(state.code == OMC_SOURCE_OK && d.complete());
        assert(static_cast<int>(c.status) == static_cast<int>(d.payload.status));
        assert(c.written == d.payload.written && c.needed == d.payload.needed && c.needed == sizeof(text) - 1U);
        assert(std::memcmp(payload.data(), cpp_payload.data(), c.written) == 0);
        assert(std::memcmp(payload.data(), text, c.written) == 0);
        assert(state.bytes_requested == c.written); // No rejected suffix is fetched.
    }
    if (format == 4) {
        const auto original = blocks[0].logical_offset;
        blocks[0].logical_offset = 4U; // Overlapping extended-XMP fragments.
        omc_source_state_init(&state);
        assert(omc_pay_ext_source(&range, blocks.data(), a.written, 0, payload.data(), payload.size(),
            indices.data(), indices.size(), nullptr, &state, nullptr, nullptr).status == OMC_PAY_MALFORMED);
        assert(state.requests_issued == 0U);
        blocks[0].logical_offset = original;
    }
    const auto measured = omc_scan_meas_source(&range, OMC_SCAN_FMT_UNKNOWN, &state, nullptr);
    assert(measured.status == OMC_SCAN_OK && measured.needed == a.needed);
    for (unsigned mode : {1U, 2U}) {
        h.failure = mode; omc_source_state_init(&state);
        const auto failed = omc_scan_source(&range, OMC_SCAN_FMT_UNKNOWN, blocks.data(), blocks.size(), &state, nullptr);
        assert(failed.status == OMC_SCAN_MALFORMED);
        assert(state.code == (mode == 1 ? OMC_SOURCE_SHORT_READ : OMC_SOURCE_CANCELLED));
    }
    h.failure = 0; omc_source_state_init(&state);
    omc_source_limits limits; omc_source_limits_init(&limits); limits.max_requests = 1;
    assert(omc_scan_source(&range, OMC_SCAN_FMT_UNKNOWN, blocks.data(), blocks.size(), &state, &limits).status == OMC_SCAN_MALFORMED);
    assert(state.code == OMC_SOURCE_REQUEST_LIMIT);
    std::printf("box source: format=%u gap=%llu descriptors=%u\n", format, static_cast<unsigned long long>(gap), a.written);
}
}
bool run_omc_box_source_parity() {
    for (unsigned format = 0; format < 3; ++format)
        for (uint64_t gap : {uint64_t{0}, uint64_t{8} << 30U}) check(format, gap);
    check(3, 0); check(4, 0);
    return true;
}
