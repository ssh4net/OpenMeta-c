#include "omc/omc_transfer.h"
#include "edit/omc_exif_write.h"
#include "omc_test_assert.h"
#include "native_edit_fixture.h"

#include <openmeta/meta_key.h>
#include <openmeta/meta_value.h>
#include <openmeta/metadata_transfer.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {
struct Stores {
    omc_store c;
    openmeta::MetaStore cpp;
    Stores() { omc_store_init(&c); }
    ~Stores() { omc_store_fini(&c); }
    void add(const char* ifd, uint16_t tag, unsigned flags, const char* text = nullptr) {
        omc_entry entry {};
        omc_byte_ref ref;
        assert(omc_arena_append(&c.arena, ifd, std::strlen(ifd), &ref) == OMC_STATUS_OK);
        omc_key_make_exif_tag(&entry.key, ref, tag);
        entry.flags = flags;
        entry.origin.block = OMC_INVALID_BLOCK_ID;
        openmeta::Entry other;
        other.key = openmeta::make_exif_tag_key(cpp.arena(), ifd, tag);
        other.flags = openmeta::EntryFlags::None;
        if (flags & OMC_ENTRY_FLAG_DIRTY) other.flags |= openmeta::EntryFlags::Dirty;
        if (flags & OMC_ENTRY_FLAG_DELETED) other.flags |= openmeta::EntryFlags::Deleted;
        if (text) {
            assert(omc_arena_append(&c.arena, text, std::strlen(text), &ref) == OMC_STATUS_OK);
            omc_val_make_text(&entry.value, ref, OMC_TEXT_ASCII);
            other.value = openmeta::make_text(cpp.arena(), text, openmeta::TextEncoding::Ascii);
        }
        assert(omc_store_add_entry(&c, &entry, nullptr) == OMC_STATUS_OK);
        assert(cpp.add_entry(other) != openmeta::kInvalidEntryId);
    }
    void add_u16(const char* ifd, uint16_t tag, uint16_t value) {
        omc_entry entry {};
        omc_byte_ref ref;
        assert(omc_arena_append(&c.arena, ifd, std::strlen(ifd), &ref) == OMC_STATUS_OK);
        omc_key_make_exif_tag(&entry.key, ref, tag);
        omc_val_make_u16(&entry.value, value);
        entry.origin.block = OMC_INVALID_BLOCK_ID;
        assert(omc_store_add_entry(&c, &entry, nullptr) == OMC_STATUS_OK);
        openmeta::Entry other;
        other.key = openmeta::make_exif_tag_key(cpp.arena(), ifd, tag);
        other.value = openmeta::make_u16(value);
        assert(cpp.add_entry(other) != openmeta::kInvalidEntryId);
    }

};

struct Field {
    uint16_t type;
    uint64_t count;
    std::vector<uint8_t> bytes;
    bool operator==(const Field&) const = default;
};
using Fields = std::map<std::pair<unsigned, uint16_t>, Field>;

Fields fields(std::span<const uint8_t> bytes, bool big, bool little)
{
    const unsigned cs = big ? 8U : 2U, es = big ? 20U : 12U, ns = big ? 8U : 4U;
    std::array<uint64_t, 4> offsets {};
    offsets[0] = native_get(bytes.data() + (big ? 8U : 4U), ns, little);
    Fields out;
    for (unsigned d = 0U; d < 4U; ++d) {
        if (!offsets[d]) continue;
        assert(offsets[d] + cs <= bytes.size());
        const auto count = native_get(bytes.data() + offsets[d], cs, little);
        assert(count <= (bytes.size() - offsets[d] - cs) / es);
        for (uint64_t i = 0U; i < count; ++i) {
            const auto* e = bytes.data() + offsets[d] + cs + i * es;
            const auto tag = static_cast<uint16_t>(native_get(e, 2U, little));
            const auto type = static_cast<uint16_t>(native_get(e + 2U, 2U, little));
            const auto n = native_get(e + 4U, big ? 8U : 4U, little);
            if ((d == 0U && (tag == 0x8769U || tag == 0x8825U)) ||
                (d == 1U && tag == 0xA005U)) {
                const unsigned child = tag == 0x8769U ? 1U : tag == 0x8825U ? 2U : 3U;
                offsets[child] = native_get(e + (big ? 12U : 8U), type == 18U ? 8U : 4U, little);
                continue;
            }
            const unsigned width = type == 3U || type == 8U ? 2U :
                type == 4U || type == 9U || type == 11U ? 4U :
                type == 5U || type == 10U || type == 12U ? 8U : 1U;
            const auto size = n * width;
            const uint8_t* value = e + (big ? 12U : 8U);
            if (size > ns) {
                const auto offset = native_get(value, ns, little);
                assert(offset <= bytes.size() && size <= bytes.size() - offset);
                value = bytes.data() + offset;
            }
            assert(out.emplace(std::pair(d, tag), Field {type, n, {value, value + size}}).second);
        }
    }
    return out;
}

void check(bool big, bool little, unsigned variant)
{
    std::array<uint8_t, 1024> input;
    native_fixture(input.data(), big, little);
    Stores stores;
    constexpr unsigned removed = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    if (variant == 0U) {
        stores.add("ifd0", 0x010FU, removed);
        stores.add("exififd", 0x9003U, removed);
        stores.add("gpsifd", 1U, removed);
        stores.add("exififd", 0x927CU, removed);
        stores.add("exififd", 0x829AU, OMC_ENTRY_FLAG_DELETED);
    } else if (variant == 5U) {
        for (uint16_t tag = 1U; tag <= 31U; ++tag) stores.add("gpsifd", tag, removed);
    } else if (variant == 3U) {
        stores.add_u16("exififd", 0xA001U, 2U);
    } else if (variant == 4U) {
        stores.add("exififd", 0x9003U, removed);
        stores.add("ifd0", 0x010FU, 0U, "NEWMAKE");
        stores.add_u16("exififd", 0x8827U, 200U);
    } else {
        stores.add("gpsifd", 1U, OMC_ENTRY_FLAG_DIRTY, "S");
        stores.add("gpsifd", 18U, 0U, "IGN");
        const std::array<uint8_t, 4> version {2U, static_cast<uint8_t>(variant == 2U ? 4U : 3U), 0U, 0U};
        omc_entry entry {};
        omc_byte_ref ref;
        assert(omc_arena_append(&stores.c.arena, "gpsifd", 6U, &ref) == OMC_STATUS_OK);
        omc_key_make_exif_tag(&entry.key, ref, 0U);
        assert(omc_arena_append(&stores.c.arena, version.data(), version.size(), &ref) == OMC_STATUS_OK);
        omc_val_make_array(&entry.value, OMC_ELEM_U8, 4U, ref, OMC_BYTE_ORDER_NATIVE);
        entry.flags = OMC_ENTRY_FLAG_DIRTY;
        entry.origin.block = OMC_INVALID_BLOCK_ID;
        assert(omc_store_add_entry(&stores.c, &entry, nullptr) == OMC_STATUS_OK);
        openmeta::Entry other;
        other.key = openmeta::make_exif_tag_key(stores.cpp.arena(), "gpsifd", 0U);
        other.value = openmeta::make_u8_array(stores.cpp.arena(), version);
        other.flags = openmeta::EntryFlags::Dirty;
        assert(stores.cpp.add_entry(other) != openmeta::kInvalidEntryId);
    }
    stores.cpp.finalize();
    openmeta::PrepareTransferRequest request;
    request.target_format = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1 = request.include_icc_app2 = request.include_iptc_app13 = false;
    if (variant == 3U) {
        request.target_image_spec.has_exif_color_space = true;
        request.target_image_spec.exif_color_space = 2U;
    }
    openmeta::PreparedTransferBundle bundle;
    auto prepared = openmeta::prepare_metadata_for_target(stores.cpp, request, &bundle);
    assert(prepared.status == openmeta::TransferStatus::Ok);
    omc_native_edit_intent intent;
    assert(omc_native_edit_intent_collect(&stores.c, &intent) == OMC_STATUS_OK);
    if (variant == 3U) intent.merge_exif = 1;
    assert(std::vector<uint16_t>(intent.ifd0_removals, intent.ifd0_removals + intent.ifd0_removal_count) == bundle.tiff_ifd0_removals);
    assert(std::vector<uint16_t>(intent.exif_removals, intent.exif_removals + intent.exif_removal_count) == bundle.tiff_exif_removals);
    assert(std::vector<uint16_t>(intent.gps_removals, intent.gps_removals + intent.gps_removal_count) == bundle.tiff_gps_removals);
    assert(bool(intent.merge_exif) == bundle.tiff_merge_existing_exif && bool(intent.merge_gps) == bundle.tiff_merge_existing_gps);
    const auto bytes = std::as_bytes(std::span(input));
    const auto plan = openmeta::plan_prepared_bundle_tiff_edit(bytes, bundle);
    omc_arena actual;
    omc_arena_init(&actual);
    omc_byte_ref ref;
    assert(omc_arena_append(&actual, "keep", 4U, &ref) == OMC_STATUS_OK);
    omc_exif_write_res result;
    assert(omc_exif_write_embedded_edit(input.data(), input.size(), &stores.c, &intent,
        &actual, OMC_SCAN_FMT_TIFF, &result) == OMC_STATUS_OK);
    if (variant == 2U) {
        assert(plan.status != openmeta::TransferStatus::Ok && result.status != OMC_EXIF_WRITE_OK);
        assert(actual.size == 4U && std::memcmp(actual.data, "keep", 4U) == 0);
    } else {
        if (plan.status != openmeta::TransferStatus::Ok) std::fprintf(stderr, "%s\n", plan.message.c_str());
        assert(plan.status == openmeta::TransferStatus::Ok && result.status == OMC_EXIF_WRITE_OK);
        std::vector<std::byte> expected;
        assert(openmeta::apply_prepared_bundle_tiff_edit(bytes, bundle, plan, &expected).status == openmeta::TransferStatus::Ok);
        assert(fields(std::span(actual.data, actual.size), big, little) ==
               fields({reinterpret_cast<const uint8_t*>(expected.data()), expected.size()}, big, little));
        assert(std::memcmp(actual.data + 944U, input.data() + 944U, 40U) == 0);
        assert(std::memcmp(expected.data() + 944U, input.data() + 944U, 40U) == 0);
    }
    omc_arena_fini(&actual);
}
} // namespace

int main()
{
    for (bool big : {false, true}) for (bool little : {false, true})
        for (unsigned variant = 0U; variant < 6U; ++variant) check(big, little, variant);
    return 0;
}
