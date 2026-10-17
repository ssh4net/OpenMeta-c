#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_transfer.h"
#include "omc/omc_transfer_package.h"
#include "native_edit_fixture.h"
#include <openmeta/meta_key.h>
#include <openmeta/meta_value.h>
#include <openmeta/metadata_transfer.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <span>
#include <vector>

namespace {
using Bytes = std::vector<std::byte>;
struct Stores {
    omc_store c;
    openmeta::MetaStore cpp;
    Stores() { omc_store_init(&c); }
    ~Stores() { omc_store_fini(&c); }
    void add(unsigned record, unsigned dataset, unsigned flags,
             const void* value, size_t size, unsigned kind = 0U) {
        omc_entry entry {};
        omc_byte_ref ref;
        openmeta::Entry other;
        if (kind == 1U) {
            omc_key_make_photoshop_irb(&entry.key, 0x0404U);
            other.key = openmeta::make_photoshop_irb_key(0x0404U);
        } else if (kind == 2U) {
            const char* ns = "http://ns.adobe.com/xap/1.0/";
            omc_byte_ref name, path;
            assert(omc_arena_append(&c.arena, ns, std::strlen(ns), &name) == OMC_STATUS_OK);
            assert(omc_arena_append(&c.arena, "CreateDate", 10U, &path) == OMC_STATUS_OK);
            omc_key_make_xmp_property(&entry.key, name, path);
            other.key = openmeta::make_xmp_property_key(cpp.arena(), ns, "CreateDate");
        } else {
            omc_key_make_iptc_dataset(&entry.key, static_cast<omc_u16>(record), static_cast<omc_u16>(dataset));
            other.key = openmeta::make_iptc_dataset_key(static_cast<uint16_t>(record), static_cast<uint16_t>(dataset));
        }
        assert(omc_arena_append(&c.arena, value, size, &ref) == OMC_STATUS_OK);
        omc_val_make_bytes(&entry.value, ref);
        entry.flags = flags;
        entry.origin.block = OMC_INVALID_BLOCK_ID;
        other.value = openmeta::make_bytes(cpp.arena(),
            std::span(static_cast<const std::byte*>(value), size));
        other.flags = openmeta::EntryFlags::None;
        if (flags & OMC_ENTRY_FLAG_DIRTY) other.flags |= openmeta::EntryFlags::Dirty;
        if (flags & OMC_ENTRY_FLAG_DELETED) other.flags |= openmeta::EntryFlags::Deleted;
        if (flags & OMC_ENTRY_FLAG_UNREADABLE) other.flags |= openmeta::EntryFlags::Unreadable;
        assert(omc_store_add_entry(&c, &entry, nullptr) == OMC_STATUS_OK);
        assert(cpp.add_entry(other) != openmeta::kInvalidEntryId);
    }
    void state(unsigned mode) {
        const unsigned deleted = OMC_ENTRY_FLAG_DELETED;
        const unsigned dirty = OMC_ENTRY_FLAG_DIRTY;
        if (mode != 0U && mode != 6U)
            add(2U, 55U, deleted | (mode >= 2U ? dirty : 0U), "20240102", 8U);
        if (mode == 2U) add(0U, 0U, 0U, "\034\002\005\0\1X", 6U, 1U);
        if (mode == 3U) add(2U, 120U, 0U, "keep", 4U);
        if (mode == 4U) add(2U, 0U, 0U, "\0\4", 2U);
        if (mode == 5U) add(1U, 90U, 0U, "\033%G", 3U);
        if (mode == 6U) add(0U, 0U, dirty | deleted, "2024-01-02", 10U, 2U);
        if (mode == 7U) add(2U, 5U, OMC_ENTRY_FLAG_UNREADABLE, "skip", 4U);
        if (mode == 8U) add(9U, 9U, 0U, "native", 6U);
        cpp.finalize();
    }
};

void append(Bytes& out, const void* value, size_t size) {
    if (size == 0U) return;
    const std::byte* p = static_cast<const std::byte*>(value);
    out.insert(out.end(), p, p + size);
}
void u16(Bytes& out, unsigned value) {
    out.push_back(static_cast<std::byte>(value >> 8U));
    out.push_back(static_cast<std::byte>(value & 255U));
}
Bytes resource(unsigned id, size_t size, unsigned fill) {
    Bytes out;
    append(out, "8BIM", 4U); u16(out, id); append(out, "\002Nm\243", 4U);
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((size >> shift) & 255U));
    out.insert(out.end(), size, static_cast<std::byte>(fill));
    if (size & 1U) out.push_back(std::byte {0xB5U});
    return out;
}
void segment(Bytes& out, const Bytes& records) {
    append(out, "\377\355", 2U); u16(out, static_cast<unsigned>(16U + records.size()));
    append(out, "Photoshop 3.0\0", 14U); append(out, records.data(), records.size());
}
Bytes without_generated_xmp(const omc_arena& out) {
    Bytes result;
    append(result, out.data, 2U);
    size_t off = 2U;
    unsigned removed = 0U;
    while (off < out.size) {
        assert(out.size - off >= 2U && out.data[off] == 255U);
        const unsigned marker = out.data[off + 1U];
        if (marker == 0xDAU || marker == 0xD9U) {
            append(result, out.data + off, out.size - off); break;
        }
        assert(out.size - off >= 4U);
        const size_t size = (size_t(out.data[off + 2U]) << 8U) + out.data[off + 3U] + 2U;
        assert(size >= 4U && size <= out.size - off);
        if (marker == 0xE1U && size >= 33U &&
            std::memcmp(out.data + off + 4U, "http://ns.adobe.com/xap/1.0/\0", 29U) == 0)
            ++removed;
        else append(result, out.data + off, size);
        off += size;
    }
    assert(removed == 1U);
    return result;
}

void options(omc_transfer_prepare_opts& opts, omc_scan_fmt format, bool enabled) {
    omc_transfer_prepare_opts_init(&opts);
    opts.format = format;
    opts.include_exif = 0;
    opts.include_iptc = enabled;
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    opts.embedded.packet.include_existing_xmp = 0;
    opts.embedded.packet.include_exif = 0;
    opts.embedded.packet.include_iptc = 0;
}
omc_transfer_status execute(std::span<const std::byte> input, const Stores& stores,
                            omc_scan_fmt format, omc_arena& out,
                            omc_transfer_res& result) {
    omc_transfer_prepare_opts opts;
    options(opts, format, true);
    omc_transfer_bundle bundle;
    omc_transfer_exec executor;
    omc_arena sidecar;
    omc_arena_init(&sidecar);
    const omc_u8* p = reinterpret_cast<const omc_u8*>(input.data());
    assert(omc_transfer_prepare(p, input.size(), &stores.c, &opts, &bundle) == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK && bundle.remove_existing_iptc);
    assert(omc_transfer_compile(&bundle, &executor) == OMC_STATUS_OK);
    assert(executor.status == OMC_TRANSFER_OK && executor.remove_existing_iptc);
    assert(omc_transfer_execute(p, input.size(), &stores.c, &out, &sidecar,
        &executor, &result) == OMC_STATUS_OK);
    omc_arena_fini(&sidecar);
    return result.status;
}
void package(const Bytes& input, const omc_arena& out, const omc_transfer_res& result) {
    omc_arena storage, wire, decoded, replay;
    omc_arena_init(&storage); omc_arena_init(&wire);
    omc_arena_init(&decoded); omc_arena_init(&replay);
    omc_transfer_package_batch batch, restored;
    omc_transfer_package_io_res io;
    assert(omc_transfer_package_batch_build_executed_output(
        reinterpret_cast<const omc_u8*>(input.data()), input.size(), out.data,
        out.size, &result, &storage, &batch, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK);
    assert(omc_transfer_package_batch_serialize(&batch, &wire, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK);
    omc_arena_fini(&storage);
    assert(omc_transfer_package_batch_deserialize(wire.data, wire.size,
        &decoded, &restored, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK);
    omc_arena_fini(&wire);
    assert(omc_transfer_package_batch_materialize(&restored, &replay, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK && replay.size == out.size);
    assert(std::memcmp(replay.data, out.data, out.size) == 0);
    omc_arena_fini(&decoded); omc_arena_fini(&replay);
}
void derivation() {
    const omc_scan_fmt formats[] = {OMC_SCAN_FMT_JPEG, OMC_SCAN_FMT_TIFF, OMC_SCAN_FMT_DNG};
    const openmeta::TransferTargetFormat targets[] = {openmeta::TransferTargetFormat::Jpeg,
        openmeta::TransferTargetFormat::Tiff, openmeta::TransferTargetFormat::Dng};
    std::array<omc_u8, 1024> tiff;
    native_fixture(tiff.data(), false, true);
    for (unsigned mode = 0U; mode < 9U; ++mode) {
        Stores stores; stores.state(mode);
        for (unsigned f = 0U; f < 3U; ++f) for (unsigned enabled = 0U; enabled < 2U; ++enabled) {
            omc_transfer_prepare_opts opts; options(opts, formats[f], enabled != 0U);
            omc_transfer_bundle bundle;
            assert(omc_transfer_prepare(tiff.data(), tiff.size(), &stores.c, &opts, &bundle) == OMC_STATUS_OK);
            assert(bundle.status == OMC_TRANSFER_OK);
            openmeta::PrepareTransferRequest request;
            request.target_format = targets[f];
            request.include_exif_app1 = request.include_xmp_app1 = request.include_icc_app2 = false;
            request.include_iptc_app13 = enabled != 0U;
            openmeta::PreparedTransferBundle other;
            const openmeta::TransferStatus cpp_status =
                openmeta::prepare_metadata_for_target(stores.cpp, request, &other).status;
            // C preparation defers serialization; C++ detects this empty damaged payload here.
            assert(cpp_status == (mode == 7U && enabled ?
                openmeta::TransferStatus::Unsupported : openmeta::TransferStatus::Ok));
            const bool expected = enabled && mode == 2U;
            assert(bundle.remove_existing_iptc == expected);
            assert(other.remove_existing_iptc == expected);
            omc_transfer_exec executor;
            assert(omc_transfer_compile(&bundle, &executor) == OMC_STATUS_OK);
            assert(executor.status == OMC_TRANSFER_OK && executor.remove_existing_iptc == expected);
            assert(executor.include_iptc == static_cast<int>(enabled));
        }
    }
}
void jpeg(unsigned mode) {
    Stores stores; stores.state(2U);
    Bytes input, expected;
    append(input, "\377\330\377\340\0\3A", 7U); expected = input;
    Bytes keep = resource(0x040DU, mode == 7U ? 30001U : 3U, 0x62U);
    const Bytes duplicate = resource(0x040DU, 3U, 0x63U);
    append(keep, duplicate.data(), duplicate.size());
    const Bytes old = resource(0x0404U, 3U, 0x71U);
    Bytes mixed = old; append(mixed, keep.data(), keep.size()); append(mixed, old.data(), old.size());
    const bool malformed = mode >= 3U && mode <= 5U;
    if (mode == 3U) mixed[0] = std::byte {'X'};
    if (mode == 4U) mixed.resize(7U);
    if (mode == 5U) mixed.pop_back();
    if (mode != 1U && mode != 2U && mode != 6U) {
        segment(input, mixed); segment(expected, keep);
    }
    append(input, "\377\376\0\3C", 5U); append(expected, "\377\376\0\3C", 5U);
    if (mode != 2U && mode != 6U) segment(input, old);
    if (mode == 6U) { segment(input, {}); segment(expected, {}); }
    segment(input, keep); segment(expected, keep);
    append(input, "\377\342\0\3B\377\332\0\2\377\355BAD\377\331", 16U);
    append(expected, "\377\342\0\3B\377\332\0\2\377\355BAD\377\331", 16U);
    openmeta::PreparedTransferBundle other;
    other.target_format = openmeta::TransferTargetFormat::Jpeg;
    other.remove_existing_iptc = true;
    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(input, other);
    omc_arena out; omc_arena_init(&out);
    omc_byte_ref sentinel;
    assert(omc_arena_append(&out, "sentinel", 8U, &sentinel) == OMC_STATUS_OK);
    omc_u8* original_pointer = out.data;
    omc_transfer_res result;
    const omc_transfer_status status = execute(input, stores, OMC_SCAN_FMT_JPEG, out, result);
    if (malformed) {
        assert(plan.status == openmeta::TransferStatus::Malformed);
        assert(status == OMC_TRANSFER_MALFORMED && !result.edited_present);
        assert(out.data == original_pointer && out.size == 8U && std::memcmp(out.data, "sentinel", 8U) == 0);
    } else {
        assert(plan.status == openmeta::TransferStatus::Ok && status == OMC_TRANSFER_OK);
        Bytes cpp_output;
        assert(openmeta::apply_prepared_bundle_jpeg_edit(input, other, plan, &cpp_output).status == openmeta::TransferStatus::Ok);
        assert(cpp_output == expected && without_generated_xmp(out) == expected);
        package(input, out, result);
        Bytes repeat_input; append(repeat_input, out.data, out.size);
        const std::span<const std::byte> aliased_input(
            reinterpret_cast<const std::byte*>(out.data), out.size);
        assert(execute(aliased_input, stores, OMC_SCAN_FMT_JPEG, out, result) == OMC_TRANSFER_OK);
        assert(out.size == repeat_input.size() && std::memcmp(out.data, repeat_input.data(), out.size) == 0);
    }
    omc_arena_fini(&out);
}

struct Field {
    uint16_t type;
    uint64_t count;
    Bytes bytes;
    bool operator==(const Field&) const = default;
};
using Fields = std::map<std::pair<unsigned, uint16_t>, Field>;
Fields fields(std::span<const omc_u8> bytes, bool big, bool little) {
    const unsigned cs = big ? 8U : 2U, es = big ? 20U : 12U, ns = big ? 8U : 4U;
    std::array<uint64_t, 4> offsets {};
    offsets[0] = native_get(bytes.data() + (big ? 8U : 4U), ns, little);
    Fields out;
    for (unsigned d = 0U; d < 4U; ++d) {
        if (!offsets[d]) continue;
        assert(offsets[d] + cs <= bytes.size());
        const uint64_t count = native_get(bytes.data() + offsets[d], cs, little);
        assert(count <= (bytes.size() - offsets[d] - cs) / es);
        for (uint64_t i = 0U; i < count; ++i) {
            const omc_u8* entry = bytes.data() + offsets[d] + cs + i * es;
            const uint16_t tag = static_cast<uint16_t>(native_get(entry, 2U, little));
            const uint16_t type = static_cast<uint16_t>(native_get(entry + 2U, 2U, little));
            const uint64_t n = native_get(entry + 4U, big ? 8U : 4U, little);
            if ((d == 0U && (tag == 0x8769U || tag == 0x8825U)) || (d == 1U && tag == 0xA005U)) {
                offsets[tag == 0x8769U ? 1U : tag == 0x8825U ? 2U : 3U] = native_get(entry + (big ? 12U : 8U), type == 18U ? 8U : 4U, little);
                continue;
            }
            if (tag == 700U) continue; // C's existing transfer route emits an XMP packet.
            const unsigned width = type == 3U || type == 8U ? 2U :
                type == 4U || type == 9U || type == 11U ? 4U :
                type == 5U || type == 10U || type == 12U ? 8U : 1U;
            const uint64_t size = n * width;
            const omc_u8* p = entry + (big ? 12U : 8U);
            if (size > ns) {
                const uint64_t off = native_get(p, ns, little);
                assert(off <= bytes.size() && size <= bytes.size() - off);
                p = bytes.data() + off;
            }
            Bytes value; append(value, p, static_cast<size_t>(size));
            assert(out.emplace(std::pair(d, tag), Field {type, n, value}).second);
        }
    }
    return out;
}
void tiff(bool big, bool little, bool dng, bool has_carrier) {
    std::array<omc_u8, 1024> original;
    native_fixture(original.data(), big, little);
    if (has_carrier) {
        native_put(original.data() + 16U, 8U, big ? 8U : 2U, little);
        omc_u8* p = native_entry(original.data(), 16U, 7U, big, little, 33723U, 7U, 6U, 1000U);
        if (big) std::memcpy(p, "\034\002\005\0\1X", 6U);
        std::memcpy(original.data() + 1000U, "\034\002\005\0\1X", 6U);
    }
    assert((native_find(original.data(), original.size(), 16U, 33723U, big, little) != nullptr) == has_carrier);
    Bytes input; append(input, original.data(), original.size());
    Fields expected = fields(original, big, little);
    assert(expected.erase(std::pair(0U, uint16_t {33723U})) == static_cast<size_t>(has_carrier));
    Stores stores; stores.state(2U);
    openmeta::PreparedTransferBundle other;
    other.target_format = dng ? openmeta::TransferTargetFormat::Dng : openmeta::TransferTargetFormat::Tiff;
    other.remove_existing_iptc = true;
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(input, other);
    assert(plan.status == openmeta::TransferStatus::Ok);
    Bytes cpp_output;
    assert(openmeta::apply_prepared_bundle_tiff_edit(input, other, plan, &cpp_output).status == openmeta::TransferStatus::Ok);
    omc_arena out; omc_arena_init(&out);
    omc_transfer_res result;
    assert(execute(input, stores, dng ? OMC_SCAN_FMT_DNG : OMC_SCAN_FMT_TIFF, out, result) == OMC_TRANSFER_OK);
    assert(fields(std::span(out.data, out.size), big, little) == expected);
    assert(fields(std::span(reinterpret_cast<const omc_u8*>(cpp_output.data()), cpp_output.size()), big, little) == expected);
    assert(std::memcmp(out.data + 944U, "NOTE!!!!", 8U) == 0);
    assert(std::memcmp(out.data + 960U, "OPAQUE!!", 8U) == 0);
    assert(std::memcmp(out.data + 976U, "PIXELS!!", 8U) == 0);
    package(input, out, result);
    omc_arena_fini(&out);
}
}
int main() {
    derivation();
    for (unsigned mode = 0U; mode < 8U; ++mode) jpeg(mode);
    for (unsigned big = 0U; big < 2U; ++big)
        for (unsigned little = 0U; little < 2U; ++little)
            for (unsigned dng = 0U; dng < 2U; ++dng)
                for (unsigned carrier = 0U; carrier < 2U; ++carrier)
                    tiff(big != 0U, little != 0U, dng != 0U, carrier != 0U);
    std::puts("54 derivation comparisons; 8 JPEG and 16 TIFF/DNG carrier cases passed");
}
