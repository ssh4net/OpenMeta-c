#include "omc/omc_transfer.h"
#include <openmeta/metadata_transfer.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <vector>

namespace {
using Bytes = std::vector<std::byte>;
void append(Bytes& out, const void* p, size_t n) {
    const auto* b = static_cast<const std::byte*>(p); out.insert(out.end(), b, b + n);
}
void u16(Bytes& out, unsigned n) {
    out.push_back(std::byte((n >> 8) & 255)); out.push_back(std::byte(n & 255));
}
Bytes record(unsigned id, size_t n, unsigned value) {
    Bytes b; append(b, "8BIM", 4); u16(b, id); append(b, "\002Nm\243", 4);
    for (int shift = 24; shift >= 0; shift -= 8) b.push_back(std::byte((n >> shift) & 255));
    b.insert(b.end(), n, std::byte(value)); if (n & 1) b.push_back(std::byte(0xb5)); return b;
}
void segment(Bytes& out, const Bytes& irb) {
    append(out, "\377\355", 2); u16(out, unsigned(irb.size() + 16));
    append(out, "Photoshop 3.0\0", 14); out.insert(out.end(), irb.begin(), irb.end());
}
Bytes without_xmp(const omc_arena& out) {
    Bytes b; append(b, out.data, 2); size_t off = 2;
    while (off < out.size) {
        assert(out.data[off] == 255); unsigned m = out.data[off + 1];
        if (m == 0xda || m == 0xd9) { append(b, out.data + off, out.size - off); break; }
        size_t n = (size_t(out.data[off + 2]) << 8) + out.data[off + 3] + 2;
        assert(n <= out.size - off);
        if (!(m == 0xe1 && n >= 33 && std::memcmp(out.data + off + 4,
              "http://ns.adobe.com/xap/1.0/\0", 29) == 0)) append(b, out.data + off, n);
        off += n;
    }
    return b;
}
void run(unsigned mode) {
    Bytes input, pure = record(0x0400, mode == 1 ? 40001 : 3, 0x61);
    Bytes mixed = record(0x040d, mode == 1 ? 30001 : 3, 0x62);
    Bytes old = record(0x0404, 3, 0x71); mixed.insert(mixed.end(), old.begin(), old.end());
    append(input, "\377\330", 2); segment(input, pure); append(input, "\377\376\0\3C", 5);
    if (mode != 3) { segment(input, mixed); segment(input, old); }
    segment(input, pure); append(input, "\377\332\0\2\377\355BAD\377\331", 11);
    if (mode == 2) input[2 + 4 + 14] = std::byte('X');
    Bytes desired; append(desired, "Photoshop 3.0\0", 14);
    append(desired, "8BIM\004\004\0\0\0\0\0\010\034\002\005\0\003NEW", 20);
    openmeta::PreparedTransferBundle cpp;
    cpp.target_format = openmeta::TransferTargetFormat::Jpeg;
    openmeta::PreparedTransferBlock block; block.route = "jpeg:app13-iptc"; block.payload = desired;
    cpp.blocks.push_back(block);
    auto plan = openmeta::plan_prepared_bundle_jpeg_edit(input, cpp);
    omc_store source; omc_store_init(&source); omc_entry e {}; omc_byte_ref ref;
    omc_key_make_iptc_dataset(&e.key, 2, 5);
    assert(omc_arena_append(&source.arena, "NEW", 3, &ref) == OMC_STATUS_OK);
    omc_val_make_bytes(&e.value, ref); e.flags = OMC_ENTRY_FLAG_DIRTY;
    assert(omc_store_add_entry(&source, &e, nullptr) == OMC_STATUS_OK);
    omc_transfer_prepare_opts opts; omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG; opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    omc_transfer_bundle bundle; omc_transfer_exec exec; omc_transfer_res res;
    omc_arena out, side; omc_arena_init(&out); omc_arena_init(&side);
    auto* bytes = reinterpret_cast<const omc_u8*>(input.data());
    assert(omc_transfer_prepare(bytes, input.size(), &source, &opts, &bundle) == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK);
    assert(omc_transfer_execute(bytes, input.size(), &source, &out, &side, &exec, &res) == OMC_STATUS_OK);
    if (mode == 2) {
        assert(plan.status == openmeta::TransferStatus::Malformed);
        assert(res.status == OMC_TRANSFER_MALFORMED && out.size == 0);
    } else {
        assert(plan.status == openmeta::TransferStatus::Ok && res.status == OMC_TRANSFER_OK);
        Bytes expected;
        assert(openmeta::apply_prepared_bundle_jpeg_edit(input, cpp, plan, &expected).status == openmeta::TransferStatus::Ok);
        assert(without_xmp(out) == expected);
    }
    omc_arena_fini(&out); omc_arena_fini(&side); omc_store_fini(&source);
}
}
int main() { for (unsigned i = 0; i != 4; ++i) run(i); }
