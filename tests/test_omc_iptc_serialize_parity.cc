#ifdef NDEBUG
#undef NDEBUG
#endif

#include "omc/omc_iptc_serialize.h"

#include <openmeta/iptc_iim_serialize.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

static void
add_c_bytes(omc_store *store, omc_u16 record, omc_u16 dataset,
            omc_u32 order, const omc_u8 *bytes, omc_size size,
            omc_entry_flags flags)
{
    omc_byte_ref ref;
    omc_entry entry;

    std::memset(&entry, 0, sizeof(entry));
    assert(omc_arena_append(&store->arena, bytes, size, &ref)
           == OMC_STATUS_OK);
    omc_key_make_iptc_dataset(&entry.key, record, dataset);
    omc_val_make_bytes(&entry.value, ref);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.order_in_block = order;
    entry.flags = flags;
    assert(omc_store_add_entry(store, &entry, nullptr) == OMC_STATUS_OK);
}

static void
add_c_array(omc_store *store, omc_u16 record, omc_u16 dataset,
            omc_u32 order, const omc_u8 *bytes, omc_size size)
{
    omc_byte_ref ref;
    omc_entry entry;

    std::memset(&entry, 0, sizeof(entry));
    assert(omc_arena_append(&store->arena, bytes, size, &ref)
           == OMC_STATUS_OK);
    omc_key_make_iptc_dataset(&entry.key, record, dataset);
    omc_val_make_array(&entry.value, OMC_ELEM_U16, 1U, ref,
                       OMC_BYTE_ORDER_BIG);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.order_in_block = order;
    assert(omc_store_add_entry(store, &entry, nullptr) == OMC_STATUS_OK);
}

static void
add_c_text(omc_store *store, omc_u16 record, omc_u16 dataset,
           omc_u32 order, const omc_u8 *bytes, omc_size size)
{
    omc_byte_ref ref;
    omc_entry entry;

    std::memset(&entry, 0, sizeof(entry));
    assert(omc_arena_append(&store->arena, bytes, size, &ref)
           == OMC_STATUS_OK);
    omc_key_make_iptc_dataset(&entry.key, record, dataset);
    omc_val_make_text(&entry.value, ref, OMC_TEXT_UTF8);
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.order_in_block = order;
    assert(omc_store_add_entry(store, &entry, nullptr) == OMC_STATUS_OK);
}

static std::span<const std::byte>
as_cpp_bytes(const omc_u8 *bytes, std::size_t size)
{
    return {reinterpret_cast<const std::byte *>(bytes), size};
}

static void
add_cpp_bytes(openmeta::MetaStore *store, std::uint16_t record,
              std::uint16_t dataset, std::uint32_t order,
              const omc_u8 *bytes, std::size_t size,
              openmeta::EntryFlags flags = openmeta::EntryFlags::None)
{
    openmeta::Entry entry;
    entry.key = openmeta::make_iptc_dataset_key(record, dataset);
    entry.value = openmeta::make_bytes(store->arena(),
                                       as_cpp_bytes(bytes, size));
    entry.origin.order_in_block = order;
    entry.flags = flags;
    assert(store->add_entry(entry) != openmeta::kInvalidEntryId);
}

static void
add_cpp_array(openmeta::MetaStore *store, std::uint16_t record,
              std::uint16_t dataset, std::uint32_t order,
              const omc_u8 *bytes, std::size_t size)
{
    openmeta::Entry entry;
    entry.key = openmeta::make_iptc_dataset_key(record, dataset);
    entry.value = openmeta::make_array(
        store->arena(), openmeta::MetaElementType::U16,
        as_cpp_bytes(bytes, size), 2U);
    entry.origin.order_in_block = order;
    assert(store->add_entry(entry) != openmeta::kInvalidEntryId);
}

static void
add_cpp_text(openmeta::MetaStore *store, std::uint16_t record,
             std::uint16_t dataset, std::uint32_t order,
             const omc_u8 *bytes, std::size_t size)
{
    openmeta::Entry entry;
    entry.key = openmeta::make_iptc_dataset_key(record, dataset);
    entry.value = openmeta::make_text(
        store->arena(),
        std::string_view(reinterpret_cast<const char *>(bytes), size),
        openmeta::TextEncoding::Utf8);
    entry.origin.order_in_block = order;
    assert(store->add_entry(entry) != openmeta::kInvalidEntryId);
}

int
main()
{
    static const omc_u8 late[] = { 'l', 'a', 't', 'e' };
    static const omc_u8 charset[] = { 'U', 'T', 'F', '8' };
    static const omc_u8 text_bytes[] = { 0xFFU, 0U, 'x' };
    static const omc_u8 duplicate[] = { 'd', 'u', 'p' };
    static const omc_u8 raw_array[] = { 0x12U, 0x34U };
    omc_store c_store;
    omc_iptc_iim_serialize_res c_measure;
    omc_iptc_iim_serialize_res c_result;
    omc_mut_bytes c_output;
    openmeta::MetaStore cpp_store;
    openmeta::IptcIimSerializeResult cpp_measure;
    openmeta::IptcIimSerializeResult cpp_result;
    std::vector<omc_u8> c_bytes;
    std::vector<std::byte> cpp_bytes;
    std::size_t output_size;

    omc_store_init(&c_store);
    add_c_bytes(&c_store, 2U, 5U, 4U, late, sizeof(late),
                OMC_ENTRY_FLAG_NONE);
    add_c_bytes(&c_store, 1U, 90U, 9U, charset, sizeof(charset),
                OMC_ENTRY_FLAG_NONE);
    add_c_bytes(&c_store, 1U, 9U, 2U, duplicate, sizeof(duplicate),
                OMC_ENTRY_FLAG_DIRTY);
    add_c_text(&c_store, 1U, 6U, 1U, text_bytes, sizeof(text_bytes));
    add_c_array(&c_store, 1U, 8U, 2U, raw_array, sizeof(raw_array));

    add_cpp_bytes(&cpp_store, 2U, 5U, 4U, late, sizeof(late));
    add_cpp_bytes(&cpp_store, 1U, 90U, 9U, charset, sizeof(charset));
    add_cpp_bytes(&cpp_store, 1U, 9U, 2U, duplicate, sizeof(duplicate),
                  openmeta::EntryFlags::Dirty);
    add_cpp_text(&cpp_store, 1U, 6U, 1U, text_bytes, sizeof(text_bytes));
    add_cpp_array(&cpp_store, 1U, 8U, 2U, raw_array, sizeof(raw_array));
    cpp_store.finalize();

    c_output.data = nullptr;
    c_output.size = 0U;
    c_measure = omc_serialize_iptc_iim(&c_store, c_output, nullptr);
    cpp_measure = openmeta::serialize_iptc_iim(cpp_store, {});
    assert(c_measure.status == OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED);
    assert(cpp_measure.status
           == openmeta::IptcIimSerializeStatus::OutputTruncated);
    assert(c_measure.needed == cpp_measure.needed);
    output_size = static_cast<std::size_t>(c_measure.needed);
    c_bytes.resize(output_size);
    cpp_bytes.resize(output_size);
    c_output.data = c_bytes.data();
    c_output.size = c_bytes.size();
    c_result = omc_serialize_iptc_iim(&c_store, c_output, nullptr);
    cpp_result = openmeta::serialize_iptc_iim(cpp_store, cpp_bytes);
    assert(c_result.status == OMC_IPTC_IIM_SERIALIZE_OK);
    assert(cpp_result.status == openmeta::IptcIimSerializeStatus::Ok);
    assert(c_result.written == cpp_result.written);
    assert(c_result.entries_serialized == cpp_result.entries_serialized);
    assert(std::memcmp(c_bytes.data(), cpp_bytes.data(), output_size) == 0);

    omc_store_fini(&c_store);
    return 0;
}
