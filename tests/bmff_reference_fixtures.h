// SPDX-License-Identifier: Apache-2.0
// Fixture snapshot from OpenMeta f11de3e0 tests/bmff_fields_decode_test.cc.
// Construction only; expectations come from the linked pinned reference.
namespace omc_bmff_reference_fixtures
{
using openmeta::fourcc;
static void append_u16be(std::vector<std::byte> *out, uint16_t v)
{
    out->push_back(std::byte{static_cast<uint8_t>((v >> 8) & 0xFF)});
    out->push_back(std::byte{static_cast<uint8_t>((v >> 0) & 0xFF)});
}

static void append_u32be(std::vector<std::byte> *out, uint32_t v)
{
    out->push_back(std::byte{static_cast<uint8_t>((v >> 24) & 0xFF)});
    out->push_back(std::byte{static_cast<uint8_t>((v >> 16) & 0xFF)});
    out->push_back(std::byte{static_cast<uint8_t>((v >> 8) & 0xFF)});
    out->push_back(std::byte{static_cast<uint8_t>((v >> 0) & 0xFF)});
}

static void append_u64be(std::vector<std::byte> *out, uint64_t v)
{
    for (uint32_t i = 0U; i < 8U; ++i) {
        const uint32_t shift = (7U - i) * 8U;
        out->push_back(std::byte{static_cast<uint8_t>((v >> shift) & 0xFFU)});
    }
}

static void append_fourcc(std::vector<std::byte> *out, uint32_t fourcc_v)
{
    append_u32be(out, fourcc_v);
}

static void append_bytes(std::vector<std::byte> *out, const char *s)
{
    for (size_t i = 0; s[i] != '\0'; ++i) {
        out->push_back(std::byte{static_cast<uint8_t>(s[i])});
    }
}

static void append_fullbox_header(std::vector<std::byte> *out, uint8_t version)
{
    out->push_back(std::byte{version});
    out->push_back(std::byte{0});
    out->push_back(std::byte{0});
    out->push_back(std::byte{0});
}

static void append_fullbox_header(std::vector<std::byte> *out, uint8_t version,
                                  uint32_t flags)
{
    out->push_back(std::byte{version});
    out->push_back(std::byte{static_cast<uint8_t>((flags >> 16U) & 0xFFU)});
    out->push_back(std::byte{static_cast<uint8_t>((flags >> 8U) & 0xFFU)});
    out->push_back(std::byte{static_cast<uint8_t>(flags & 0xFFU)});
}

static void append_auxc_payload(std::vector<std::byte> *out, const char *aux_type,
                                std::span<const std::byte> subtype)
{
    append_fullbox_header(out, 0);
    append_bytes(out, aux_type);
    out->push_back(std::byte{0x00});
    out->insert(out->end(), subtype.begin(), subtype.end());
}

static void append_bmff_box(std::vector<std::byte> *out, uint32_t type,
                            std::span<const std::byte> payload)
{
    append_u32be(out, static_cast<uint32_t>(8 + payload.size()));
    append_fourcc(out, type);
    out->insert(out->end(), payload.begin(), payload.end());
}

static void append_iref_v0_edge(std::vector<std::byte> *out, uint32_t type,
                                uint16_t from_item_id, uint16_t to_item_id)
{
    std::vector<std::byte> payload;
    append_u16be(&payload, from_item_id);
    append_u16be(&payload, 1U);
    append_u16be(&payload, to_item_id);
    append_bmff_box(out, type, payload);
}

static void append_iref_v1_edge(std::vector<std::byte> *out, uint32_t type,
                                uint32_t from_item_id, uint32_t to_item_id)
{
    std::vector<std::byte> payload;
    append_u32be(&payload, from_item_id);
    append_u16be(&payload, 1U);
    append_u32be(&payload, to_item_id);
    append_bmff_box(out, type, payload);
}

static void append_iloc_v1_entry(std::vector<std::byte> *out, uint16_t item_id,
                                 uint16_t method, uint32_t offset, uint32_t length)
{
    append_u16be(out, item_id);
    append_u16be(out, method);
    append_u16be(out, 0U);
    append_u16be(out, 1U);
    append_u32be(out, offset);
    append_u32be(out, length);
}

static void append_iloc_v1_idat_entry(std::vector<std::byte> *out, uint16_t item_id,
                                      uint32_t offset, uint32_t length)
{
    append_iloc_v1_entry(out, item_id, 1U, offset, length);
}

static void append_iloc_v1_indexed_entry(std::vector<std::byte> *out, uint16_t item_id,
                                         uint16_t method, uint32_t reference_index,
                                         uint32_t offset, uint32_t length)
{
    append_u16be(out, item_id);
    append_u16be(out, method);
    append_u16be(out, 0U);
    append_u16be(out, 1U);
    append_u32be(out, reference_index);
    append_u32be(out, offset);
    append_u32be(out, length);
}

static void append_iloc_v1_split_file_entry(std::vector<std::byte> *out,
                                            uint16_t item_id, uint32_t offset,
                                            uint32_t length_a, uint32_t length_b)
{
    append_u16be(out, item_id);
    append_u16be(out, 0U);
    append_u16be(out, 0U);
    append_u16be(out, 2U);
    append_u32be(out, offset);
    append_u32be(out, length_a);
    append_u32be(out, offset + length_a);
    append_u32be(out, length_b);
}

static void append_entity_group_box(std::vector<std::byte> *out, uint32_t group_type,
                                    uint32_t group_id,
                                    std::span<const uint32_t> entity_ids)
{
    std::vector<std::byte> payload;
    append_fullbox_header(&payload, 0);
    append_u32be(&payload, group_id);
    append_u32be(&payload, static_cast<uint32_t>(entity_ids.size()));
    for (size_t i = 0; i < entity_ids.size(); ++i) {
        append_u32be(&payload, entity_ids[i]);
    }
    append_bmff_box(out, group_type, payload);
}

static void append_infe_v2(std::vector<std::byte> *out, uint16_t item_id,
                           uint16_t protection_index, uint32_t item_type,
                           const char *name)
{
    std::vector<std::byte> payload;
    append_fullbox_header(&payload, 2);
    append_u16be(&payload, item_id);
    append_u16be(&payload, protection_index);
    append_u32be(&payload, item_type);
    append_bytes(&payload, name);
    payload.push_back(std::byte{0});
    append_bmff_box(out, fourcc('i', 'n', 'f', 'e'), payload);
}

static void append_infe_v2_mime(std::vector<std::byte> *out, uint16_t item_id,
                                uint16_t protection_index, const char *name,
                                const char *content_type, const char *content_encoding)
{
    std::vector<std::byte> payload;
    append_fullbox_header(&payload, 2);
    append_u16be(&payload, item_id);
    append_u16be(&payload, protection_index);
    append_u32be(&payload, fourcc('m', 'i', 'm', 'e'));
    append_bytes(&payload, name);
    payload.push_back(std::byte{0});
    append_bytes(&payload, content_type);
    payload.push_back(std::byte{0});
    append_bytes(&payload, content_encoding);
    payload.push_back(std::byte{0});
    append_bmff_box(out, fourcc('i', 'n', 'f', 'e'), payload);
}

static std::vector<std::byte> make_tiled_image_configuration_file(
    uint8_t version, uint32_t flags, uint32_t output_width, uint32_t output_height,
    uint32_t tile_width, uint32_t tile_height, std::span<const uint32_t> dimensions,
    bool truncate_last_dimension, uint8_t ispe_association_count,
    uint8_t tilc_association_count, uint32_t conditional_payload_bytes)
{
    std::vector<std::byte> file;
    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> infe_box;
    append_infe_v2(&infe_box, 1U, 0U, fourcc('t', 'i', 'l', 'i'), "tiled");
    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 1U);
    iinf_payload.insert(iinf_payload.end(), infe_box.begin(), infe_box.end());
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> ispe_payload;
    append_fullbox_header(&ispe_payload, 0U);
    append_u32be(&ispe_payload, output_width);
    append_u32be(&ispe_payload, output_height);
    std::vector<std::byte> ispe_box;
    append_bmff_box(&ispe_box, fourcc('i', 's', 'p', 'e'), ispe_payload);

    std::vector<std::byte> tilc_payload;
    tilc_payload.push_back(std::byte{version});
    tilc_payload.push_back(std::byte{static_cast<uint8_t>((flags >> 16U) & 0xFFU)});
    tilc_payload.push_back(std::byte{static_cast<uint8_t>((flags >> 8U) & 0xFFU)});
    tilc_payload.push_back(std::byte{static_cast<uint8_t>(flags & 0xFFU)});
    append_u32be(&tilc_payload, tile_width);
    append_u32be(&tilc_payload, tile_height);
    tilc_payload.push_back(std::byte{static_cast<uint8_t>(dimensions.size())});
    size_t dimension_count = dimensions.size();
    if (truncate_last_dimension && dimension_count != 0U) {
        dimension_count -= 1U;
    }
    for (size_t i = 0U; i < dimension_count; ++i) {
        append_u32be(&tilc_payload, dimensions[i]);
    }
    for (uint32_t i = 0U; i < conditional_payload_bytes; ++i) {
        tilc_payload.push_back(std::byte{0x5aU});
    }
    std::vector<std::byte> tilc_box;
    append_bmff_box(&tilc_box, fourcc('t', 'i', 'l', 'C'), tilc_payload);

    std::vector<std::byte> ipco_payload;
    ipco_payload.insert(ipco_payload.end(), ispe_box.begin(), ispe_box.end());
    ipco_payload.insert(ipco_payload.end(), tilc_box.begin(), tilc_box.end());
    std::vector<std::byte> ipco_box;
    append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

    std::vector<std::byte> ipma_payload;
    append_fullbox_header(&ipma_payload, 0U);
    append_u32be(&ipma_payload, 1U);
    append_u16be(&ipma_payload, 1U);
    ipma_payload.push_back(std::byte{
        static_cast<uint8_t>(ispe_association_count + tilc_association_count)});
    for (uint8_t i = 0U; i < ispe_association_count; ++i) {
        ipma_payload.push_back(std::byte{1U});
    }
    for (uint8_t i = 0U; i < tilc_association_count; ++i) {
        ipma_payload.push_back(std::byte{0x82U});
    }
    std::vector<std::byte> ipma_box;
    append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

    std::vector<std::byte> iprp_payload;
    iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
    iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
    std::vector<std::byte> iprp_box;
    append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    return file;
}

struct CompleteTiledImageOptions final {
    bool external_tiles = false;
    bool omit_internal_conditional = false;
    bool add_external_conditional = false;
    uint16_t data_reference_index = 1U;
    uint16_t construction_method = 0U;
    uint64_t input_item_count = 2U;
    uint32_t tipa_property_index = 3U;
    uint8_t tipa_version = 0U;
    uint32_t tipa_flags = 0U;
    uint32_t deti_extra_flags = 0U;
    uint8_t external_directory_flags = 1U;
    bool omit_external_template_nul = false;
    bool include_tile_sizes = true;
    bool sequential_order = false;
    uint32_t declared_offset_table_size = 16U;
    uint32_t first_tile_offset = 16U;
    uint32_t first_tile_size = 4U;
    uint32_t second_tile_offset = 20U;
    uint32_t second_tile_size = 4U;
};

static std::vector<std::byte>
make_complete_tiled_image_file(const CompleteTiledImageOptions &options)
{
    std::vector<std::byte> file;
    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    uint32_t mdat_payload_offset = 0U;
    uint32_t mdat_payload_size = 0U;
    if (!options.external_tiles) {
        std::vector<std::byte> mdat_payload;
        append_u32be(&mdat_payload, options.first_tile_offset);
        if (options.include_tile_sizes) {
            append_u32be(&mdat_payload, options.first_tile_size);
        }
        append_u32be(&mdat_payload, options.second_tile_offset);
        if (options.include_tile_sizes) {
            append_u32be(&mdat_payload, options.second_tile_size);
        }
        append_u32be(&mdat_payload, 0x11111111U);
        append_u32be(&mdat_payload, 0x22222222U);
        mdat_payload_offset = static_cast<uint32_t>(file.size() + 8U);
        mdat_payload_size = static_cast<uint32_t>(mdat_payload.size());
        append_bmff_box(&file, fourcc('m', 'd', 'a', 't'), mdat_payload);
    }

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> infe_box;
    append_infe_v2(&infe_box, 1U, 0U, fourcc('t', 'i', 'l', 'i'), "tiled");
    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 1U);
    iinf_payload.insert(iinf_payload.end(), infe_box.begin(), infe_box.end());
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> ispe_payload;
    append_fullbox_header(&ispe_payload, 0U);
    append_u32be(&ispe_payload, 128U);
    append_u32be(&ispe_payload, 64U);
    std::vector<std::byte> ispe_box;
    append_bmff_box(&ispe_box, fourcc('i', 's', 'p', 'e'), ispe_payload);

    std::vector<std::byte> tilc_payload;
    append_fullbox_header(&tilc_payload, 0U);
    append_u32be(&tilc_payload, 64U);
    append_u32be(&tilc_payload, 64U);
    tilc_payload.push_back(std::byte{0U});
    if ((!options.external_tiles && !options.omit_internal_conditional) ||
        (options.external_tiles && options.add_external_conditional)) {
        append_fourcc(&tilc_payload, fourcc('a', 'v', '0', '1'));
        std::vector<std::byte> tipa_payload;
        append_fullbox_header(&tipa_payload, options.tipa_version, options.tipa_flags);
        tipa_payload.push_back(std::byte{1U});
        if ((options.tipa_flags & 1U) == 0U) {
            tipa_payload.push_back(std::byte{
                static_cast<uint8_t>(0x80U | (options.tipa_property_index & 0x7FU))});
        } else {
            append_u16be(&tipa_payload,
                         static_cast<uint16_t>(
                             0x8000U | (options.tipa_property_index & 0x7FFFU)));
        }
        append_bmff_box(&tilc_payload, fourcc('t', 'i', 'p', 'a'), tipa_payload);
    }
    std::vector<std::byte> tilc_box;
    append_bmff_box(&tilc_box, fourcc('t', 'i', 'l', 'C'), tilc_payload);

    std::vector<std::byte> av1c_box;
    const std::array<std::byte, 0> no_payload{};
    append_bmff_box(&av1c_box, fourcc('a', 'v', '1', 'C'), no_payload);
    std::vector<std::byte> ipco_payload;
    ipco_payload.insert(ipco_payload.end(), ispe_box.begin(), ispe_box.end());
    ipco_payload.insert(ipco_payload.end(), tilc_box.begin(), tilc_box.end());
    ipco_payload.insert(ipco_payload.end(), av1c_box.begin(), av1c_box.end());
    std::vector<std::byte> ipco_box;
    append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

    std::vector<std::byte> ipma_payload;
    append_fullbox_header(&ipma_payload, 0U);
    append_u32be(&ipma_payload, 1U);
    append_u16be(&ipma_payload, 1U);
    ipma_payload.push_back(std::byte{2U});
    ipma_payload.push_back(std::byte{1U});
    ipma_payload.push_back(std::byte{0x82U});
    std::vector<std::byte> ipma_box;
    append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

    std::vector<std::byte> iprp_payload;
    iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
    iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
    std::vector<std::byte> iprp_box;
    append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

    std::vector<std::byte> deti_payload;
    uint32_t deti_flags =
        options.external_tiles ? 0x80U : (options.include_tile_sizes ? 0x08U : 0U);
    if (options.sequential_order) {
        deti_flags |= 0x10U;
    }
    deti_flags |= options.deti_extra_flags;
    append_fullbox_header(&deti_payload, 0U, deti_flags);
    deti_payload.push_back(std::byte{static_cast<uint8_t>(options.input_item_count)});
    if (options.external_tiles) {
        deti_payload.push_back(std::byte{options.external_directory_flags});
        append_u16be(&deti_payload, 10U);
        append_u16be(&deti_payload, 12U);
        append_u64be(&deti_payload, 1000U);
        append_bytes(&deti_payload, "https://tiles.example/image/");
        deti_payload.push_back(std::byte{0U});
        append_bytes(&deti_payload, "representation");
        deti_payload.push_back(std::byte{0U});
        append_bytes(&deti_payload, "$tileID$.heif");
        if (!options.omit_external_template_nul) {
            deti_payload.push_back(std::byte{0U});
        }
    } else {
        append_u32be(&deti_payload, 0U);
        append_u32be(&deti_payload, options.declared_offset_table_size);
    }
    std::vector<std::byte> deti_box;
    append_bmff_box(&deti_box, fourcc('d', 'e', 't', 'i'), deti_payload);
    std::vector<std::byte> dref_payload;
    append_fullbox_header(&dref_payload, 0U);
    append_u32be(&dref_payload, 1U);
    dref_payload.insert(dref_payload.end(), deti_box.begin(), deti_box.end());
    std::vector<std::byte> dref_box;
    append_bmff_box(&dref_box, fourcc('d', 'r', 'e', 'f'), dref_payload);
    std::vector<std::byte> dinf_box;
    append_bmff_box(&dinf_box, fourcc('d', 'i', 'n', 'f'), dref_box);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte{0x44U});
    iloc_payload.push_back(std::byte{0U});
    append_u16be(&iloc_payload, 1U);
    append_u16be(&iloc_payload, 1U);
    append_u16be(&iloc_payload, options.construction_method);
    append_u16be(&iloc_payload, options.data_reference_index);
    append_u16be(&iloc_payload, options.external_tiles ? 0U : 1U);
    if (!options.external_tiles) {
        append_u32be(&iloc_payload, mdat_payload_offset);
        append_u32be(&iloc_payload, mdat_payload_size);
    }
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
    meta_payload.insert(meta_payload.end(), dinf_box.begin(), dinf_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    return file;
}

static std::vector<std::byte> EmitsFtypAndPrimaryProps()
{

    // Minimal ISO-BMFF/HEIF with:
    // - ftyp(major_brand='heic', compat=['mif1'])
    // - meta(pitm primary item id=1, iprp/ipco(ispe+irot+imir), ipma associates props)

    std::vector<std::byte> file;

    // ftyp
    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0); // minor_version
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    // meta
    {
        // pitm (FullBox version 0): primary item id=1 (u16)
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        // ipco: ispe + irot + imir + colr
        std::vector<std::byte> ispe_payload;
        append_fullbox_header(&ispe_payload, 0);
        append_u32be(&ispe_payload, 640);
        append_u32be(&ispe_payload, 480);
        std::vector<std::byte> ispe_box;
        append_bmff_box(&ispe_box, fourcc('i', 's', 'p', 'e'), ispe_payload);

        std::vector<std::byte> irot_payload;
        irot_payload.push_back(std::byte{1}); // 90 degrees
        std::vector<std::byte> irot_box;
        append_bmff_box(&irot_box, fourcc('i', 'r', 'o', 't'), irot_payload);

        std::vector<std::byte> imir_payload;
        imir_payload.push_back(std::byte{1});
        std::vector<std::byte> imir_box;
        append_bmff_box(&imir_box, fourcc('i', 'm', 'i', 'r'), imir_payload);

        std::vector<std::byte> colr_payload;
        append_fourcc(&colr_payload, fourcc('n', 'c', 'l', 'x'));
        append_u16be(&colr_payload, 9);
        append_u16be(&colr_payload, 16);
        append_u16be(&colr_payload, 9);
        colr_payload.push_back(std::byte{0x80});
        std::vector<std::byte> colr_box;
        append_bmff_box(&colr_box, fourcc('c', 'o', 'l', 'r'), colr_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), ispe_box.begin(), ispe_box.end());
        ipco_payload.insert(ipco_payload.end(), irot_box.begin(), irot_box.end());
        ipco_payload.insert(ipco_payload.end(), imir_box.begin(), imir_box.end());
        ipco_payload.insert(ipco_payload.end(), colr_box.begin(), colr_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        // ipma (FullBox version 0): item 1 has properties [1,2,3,4]
        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);          // entry_count
        append_u16be(&ipma_payload, 1);          // item_ID
        ipma_payload.push_back(std::byte{4});    // association_count
        ipma_payload.push_back(std::byte{1});    // property_index=1
        ipma_payload.push_back(std::byte{0x82}); // essential, index=2
        ipma_payload.push_back(std::byte{3});    // property_index=3
        ipma_payload.push_back(std::byte{4});    // property_index=4
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsPrimaryApertureAspectAndPixelDepth()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> pasp_payload;
        append_u32be(&pasp_payload, 4);
        append_u32be(&pasp_payload, 3);
        std::vector<std::byte> pasp_box;
        append_bmff_box(&pasp_box, fourcc('p', 'a', 's', 'p'), pasp_payload);

        std::vector<std::byte> pixi_payload;
        append_fullbox_header(&pixi_payload, 0);
        pixi_payload.push_back(std::byte{3});
        pixi_payload.push_back(std::byte{10});
        pixi_payload.push_back(std::byte{10});
        pixi_payload.push_back(std::byte{10});
        std::vector<std::byte> pixi_box;
        append_bmff_box(&pixi_box, fourcc('p', 'i', 'x', 'i'), pixi_payload);

        std::vector<std::byte> clap_payload;
        append_u32be(&clap_payload, 1920);
        append_u32be(&clap_payload, 1);
        append_u32be(&clap_payload, 1080);
        append_u32be(&clap_payload, 1);
        append_u32be(&clap_payload, 0xFFFFFFFCU);
        append_u32be(&clap_payload, 1);
        append_u32be(&clap_payload, 8);
        append_u32be(&clap_payload, 1);
        std::vector<std::byte> clap_box;
        append_bmff_box(&clap_box, fourcc('c', 'l', 'a', 'p'), clap_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), pasp_box.begin(), pasp_box.end());
        ipco_payload.insert(ipco_payload.end(), pixi_box.begin(), pixi_box.end());
        ipco_payload.insert(ipco_payload.end(), clap_box.begin(), clap_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);
        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte{3});
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{2});
        ipma_payload.push_back(std::byte{3});
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsPrimaryIccColorProfileSummary()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> colr_payload;
        append_fourcc(&colr_payload, fourcc('r', 'I', 'C', 'C'));
        for (uint8_t i = 0; i < 12U; ++i) {
            colr_payload.push_back(std::byte{i});
        }
        std::vector<std::byte> colr_box;
        append_bmff_box(&colr_box, fourcc('c', 'o', 'l', 'r'), colr_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), colr_box.begin(), colr_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);
        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{1});
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> IgnoresShortPrimaryColorProperty()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        const std::array<std::byte, 3> short_colr_payload = {
            std::byte{'n'},
            std::byte{'c'},
            std::byte{'l'},
        };
        std::vector<std::byte> colr_box;
        append_bmff_box(&colr_box, fourcc('c', 'o', 'l', 'r'), short_colr_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), colr_box.begin(), colr_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);
        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{1});
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsIrefEdgesAndPrimaryAuxLinks()
{

    // Minimal HEIF-like BMFF:
    // - ftyp(heic)
    // - meta(pitm primary item id=1)
    // - iref auxl edges: auxiliary items 2,3 -> master item 1

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> SeparatesPrimaryDerivedSourcesFromDerivedItems()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 3U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> EmitsDerivedImageConstructionSemantics()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 10U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'), "grid");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('i', 'o', 'v', 'l'), "overlay");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('i', 'd', 'e', 'n'), "identity");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'), "grid_0");
    append_infe_v2(&iinf_payload, 11U, 0U, fourcc('h', 'v', 'c', '1'), "grid_1");
    append_infe_v2(&iinf_payload, 12U, 0U, fourcc('h', 'v', 'c', '1'), "grid_2");
    append_infe_v2(&iinf_payload, 13U, 0U, fourcc('h', 'v', 'c', '1'), "grid_3");
    append_infe_v2(&iinf_payload, 20U, 0U, fourcc('h', 'v', 'c', '1'), "overlay_0");
    append_infe_v2(&iinf_payload, 21U, 0U, fourcc('h', 'v', 'c', '1'), "overlay_1");
    append_infe_v2(&iinf_payload, 30U, 0U, fourcc('h', 'v', 'c', '1'),
                   "identity_source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 11U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 12U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 13U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 20U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 21U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 3U, 30U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> idat_payload;
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{1U});
    idat_payload.push_back(std::byte{1U});
    append_u16be(&idat_payload, 640U);
    append_u16be(&idat_payload, 480U);
    const uint32_t overlay_offset = static_cast<uint32_t>(idat_payload.size());
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{0U});
    append_u16be(&idat_payload, 1U);
    append_u16be(&idat_payload, 2U);
    append_u16be(&idat_payload, 3U);
    append_u16be(&idat_payload, 0xFFFFU);
    append_u16be(&idat_payload, 800U);
    append_u16be(&idat_payload, 600U);
    append_u16be(&idat_payload, static_cast<uint16_t>(-10));
    append_u16be(&idat_payload, 20U);
    append_u16be(&idat_payload, 30U);
    append_u16be(&idat_payload, static_cast<uint16_t>(-40));
    const uint32_t overlay_length =
        static_cast<uint32_t>(idat_payload.size()) - overlay_offset;
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte{0x44U});
    iloc_payload.push_back(std::byte{0x00U});
    append_u16be(&iloc_payload, 2U);
    append_iloc_v1_idat_entry(&iloc_payload, 1U, 0U, 8U);
    append_iloc_v1_idat_entry(&iloc_payload, 2U, overlay_offset, overlay_length);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> ReadsDerivedDescriptorFromFileOffsetExtent()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 2U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'), "file_grid");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'), "grid_source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> descriptor;
    descriptor.push_back(std::byte{0U});
    descriptor.push_back(std::byte{0U});
    descriptor.push_back(std::byte{0U});
    descriptor.push_back(std::byte{0U});
    append_u16be(&descriptor, 320U);
    append_u16be(&descriptor, 240U);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte{0x44U});
    iloc_payload.push_back(std::byte{0x00U});
    append_u16be(&iloc_payload, 1U);
    append_iloc_v1_split_file_entry(&iloc_payload, 1U, 0U, 4U, 4U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    const uint32_t descriptor_offset =
        static_cast<uint32_t>(file.size() + 8U + meta_payload.size() + 8U);

    iloc_payload.clear();
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte{0x44U});
    iloc_payload.push_back(std::byte{0x00U});
    append_u16be(&iloc_payload, 1U);
    append_iloc_v1_split_file_entry(&iloc_payload, 1U, descriptor_offset, 4U, 4U);
    iloc_box.clear();
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);
    meta_payload.clear();
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    append_bmff_box(&file, fourcc('m', 'd', 'a', 't'), descriptor);

    return file;
}
static std::vector<std::byte> ReadsDerivedDescriptorThroughItemOffsets()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 4U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'), "item_grid");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('h', 'v', 'c', '1'), "slice_1");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('h', 'v', 'c', '1'), "slice_2");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'), "grid_source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 2U, 3U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> idat_payload;
    idat_payload.push_back(std::byte{0xAAU});
    idat_payload.push_back(std::byte{0xBBU});
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{0U});
    append_u16be(&idat_payload, 1024U);
    append_u16be(&idat_payload, 768U);
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte{0x44U});
    iloc_payload.push_back(std::byte{0x04U});
    append_u16be(&iloc_payload, 3U);
    append_iloc_v1_indexed_entry(&iloc_payload, 1U, 2U, 1U, 1U, 8U);
    append_iloc_v1_indexed_entry(&iloc_payload, 2U, 2U, 1U, 1U, 9U);
    append_iloc_v1_indexed_entry(&iloc_payload, 3U, 1U, 0U, 0U, 10U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> RejectsInvalidItemOffsetIndexAndCycle()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 3U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'), "bad_index");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('g', 'r', 'i', 'd'), "cycle");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'), "source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 2U, 2U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte{0x44U});
    iloc_payload.push_back(std::byte{0x04U});
    append_u16be(&iloc_payload, 2U);
    append_iloc_v1_indexed_entry(&iloc_payload, 1U, 2U, 2U, 0U, 8U);
    append_iloc_v1_indexed_entry(&iloc_payload, 2U, 2U, 1U, 0U, 8U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> RejectsDerivedGraphCyclesAndMissingSources()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 3U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('i', 'd', 'e', 'n'), "cycle_a");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('i', 'd', 'e', 'n'), "cycle_b");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('i', 'd', 'e', 'n'), "missing");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 1U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 3U, 99U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> RejectsTruncatedDerivedReferenceGraph()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 2U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('i', 'd', 'e', 'n'), "identity");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'), "source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    for (uint32_t i = 0U; i < 513U; ++i) {
        append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    }
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> RejectsInvalidDerivedImageConstructions()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 3U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'), "bad_grid");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('i', 'o', 'v', 'l'), "bad_overlay");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('i', 'd', 'e', 'n'), "self_identity");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 11U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 12U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 20U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 3U, 3U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> idat_payload;
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{1U});
    idat_payload.push_back(std::byte{1U});
    append_u16be(&idat_payload, 640U);
    append_u16be(&idat_payload, 480U);
    const uint32_t overlay_offset = static_cast<uint32_t>(idat_payload.size());
    idat_payload.push_back(std::byte{0U});
    idat_payload.push_back(std::byte{0U});
    append_u16be(&idat_payload, 1U);
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte{0x44U});
    iloc_payload.push_back(std::byte{0x00U});
    append_u16be(&iloc_payload, 2U);
    append_iloc_v1_idat_entry(&iloc_payload, 1U, 0U, 8U);
    append_iloc_v1_idat_entry(&iloc_payload, 2U, overlay_offset, 4U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> EmitsItemGroupsAndPrimaryMembership()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> grpl_payload;
        const std::array<uint32_t, 3> altr_entities{1U, 2U, 3U};
        const std::array<uint32_t, 2> ster_entities{4U, 5U};
        append_entity_group_box(&grpl_payload, fourcc('a', 'l', 't', 'r'), 10U,
                                altr_entities);
        append_entity_group_box(&grpl_payload, fourcc('s', 't', 'e', 'r'), 20U,
                                ster_entities);
        std::vector<std::byte> grpl_box;
        append_bmff_box(&grpl_box, fourcc('g', 'r', 'p', 'l'), grpl_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), grpl_box.begin(), grpl_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsItemLocationAndIdatSummaries()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte{0x44}); // offset/length sizes
        iloc_payload.push_back(std::byte{0x40}); // base/index sizes
        append_u16be(&iloc_payload, 2);          // item_count

        append_u16be(&iloc_payload, 1); // item id
        append_u16be(&iloc_payload, 1); // construction_method=idat offset
        append_u16be(&iloc_payload, 0); // data_reference_index
        append_u32be(&iloc_payload, 0); // base_offset
        append_u16be(&iloc_payload, 2); // extent_count
        append_u32be(&iloc_payload, 4);
        append_u32be(&iloc_payload, 12);
        append_u32be(&iloc_payload, 20);
        append_u32be(&iloc_payload, 8);

        append_u16be(&iloc_payload, 2); // item id
        append_u16be(&iloc_payload, 0); // construction_method=file offset
        append_u16be(&iloc_payload, 0); // data_reference_index
        append_u32be(&iloc_payload, 1000);
        append_u16be(&iloc_payload, 1);
        append_u32be(&iloc_payload, 32);
        append_u32be(&iloc_payload, 16);
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> idat_payload(64, std::byte{0x33});
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsIrefEdgesForVersion1ItemIds()
{

    // Same auxl edge semantics as the v0 test, but with 32-bit item IDs in
    // pitm/iref (version=1 fullboxes).
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kPrimary = 0x10001U;
        const uint32_t kAuxA = 0x10002U;
        const uint32_t kAuxB = 0x10003U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kPrimary);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        append_iref_v1_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), kAuxA, kPrimary);
        append_iref_v1_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), kAuxB, kPrimary);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsNonPrimaryIrefTypedEdgesForVersion1ItemIds()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kPrimary = 0x10001U;
        const uint32_t kFromDimg = 0x20002U;
        const uint32_t kFromThmb = 0x20003U;
        const uint32_t kFromCdsc = 0x20004U;
        const uint32_t kToDimgA = 0x30005U;
        const uint32_t kToDimgB = 0x30006U;
        const uint32_t kToThmb = 0x30007U;
        const uint32_t kToCdsc = 0x30008U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kPrimary);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> dimg_payload;
        append_u32be(&dimg_payload, kFromDimg);
        append_u16be(&dimg_payload, 2);
        append_u32be(&dimg_payload, kToDimgA);
        append_u32be(&dimg_payload, kToDimgB);
        std::vector<std::byte> dimg_box;
        append_bmff_box(&dimg_box, fourcc('d', 'i', 'm', 'g'), dimg_payload);

        std::vector<std::byte> thmb_payload;
        append_u32be(&thmb_payload, kFromThmb);
        append_u16be(&thmb_payload, 1);
        append_u32be(&thmb_payload, kToThmb);
        std::vector<std::byte> thmb_box;
        append_bmff_box(&thmb_box, fourcc('t', 'h', 'm', 'b'), thmb_payload);

        std::vector<std::byte> cdsc_payload;
        append_u32be(&cdsc_payload, kFromCdsc);
        append_u16be(&cdsc_payload, 1);
        append_u32be(&cdsc_payload, kToCdsc);
        std::vector<std::byte> cdsc_box;
        append_bmff_box(&cdsc_box, fourcc('c', 'd', 's', 'c'), cdsc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), dimg_box.begin(), dimg_box.end());
        iref_payload.insert(iref_payload.end(), thmb_box.begin(), thmb_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_box.begin(), cdsc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsPrimaryAuxSemanticsFromAuxC()
{

    // Minimal HEIF-like BMFF:
    // - primary item id = 1
    // - iref auxl edges: auxiliary items 2,3 -> master item 1
    // - ipco has auxC properties:
    //   - prop #1: urn:mpeg:hevc:2015:auxid:2 (depth)
    //   - prop #2: urn:mpeg:hevc:2015:auxid:1 (alpha)
    // - ipma maps item 2 -> prop #1, item 3 -> prop #2

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        append_auxc_payload(&auxc_depth_payload, kDepth, {});
        auxc_depth_payload.push_back(std::byte{0xAA});
        auxc_depth_payload.push_back(std::byte{0xBB});
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        append_auxc_payload(&auxc_alpha_payload, kAlpha, {});
        auxc_alpha_payload.push_back(std::byte{0x11});
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 3); // entry_count

        append_u16be(&ipma_payload, 1);       // item id (primary)
        ipma_payload.push_back(std::byte{0}); // association_count

        append_u16be(&ipma_payload, 2);       // item id (aux depth)
        ipma_payload.push_back(std::byte{1}); // association_count
        ipma_payload.push_back(std::byte{1}); // property_index=1

        append_u16be(&ipma_payload, 3);       // item id (aux alpha)
        ipma_payload.push_back(std::byte{1}); // association_count
        ipma_payload.push_back(std::byte{2}); // property_index=2

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 6);
        append_infe_v2(&iinf_payload, 2, 0, fourcc('a', 'u', 'x', 'l'), "depth_aux");
        append_infe_v2(&iinf_payload, 3, 0, fourcc('a', 'u', 'x', 'l'), "alpha_aux");
        append_infe_v2(&iinf_payload, 4, 0, fourcc('d', 'e', 'r', 'v'), "derived");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('t', 'h', 'm', 'b'), "thumb");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('c', 'd', 's', 'c'), "caption");
        append_infe_v2(&iinf_payload, 7, 0, fourcc('a', 'u', 'x', 'l'), "other_aux");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsPrimaryLinkedItemRoles()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 7U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 4U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('t', 'h', 'm', 'b'), 5U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 6U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 8U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 5U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        append_auxc_payload(&auxc_depth_payload, kDepth, {});
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        append_auxc_payload(&auxc_alpha_payload, kAlpha, {});
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 4);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte{0});

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{1});

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{2});

        append_u16be(&ipma_payload, 7);
        ipma_payload.push_back(std::byte{0});

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 7);
        append_infe_v2(&iinf_payload, 2, 0, fourcc('a', 'u', 'x', 'l'), "depth_aux");
        append_infe_v2(&iinf_payload, 3, 0, fourcc('a', 'u', 'x', 'l'), "alpha_aux");
        append_infe_v2(&iinf_payload, 4, 0, fourcc('d', 'e', 'r', 'v'), "derived");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('t', 'h', 'm', 'b'), "thumb");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('c', 'd', 's', 'c'), "caption");
        append_infe_v2(&iinf_payload, 7, 0, fourcc('a', 'u', 'x', 'l'), "other_aux");
        append_infe_v2_mime(&iinf_payload, 8, 0, "manifest", "application/c2pa+jumbf",
                            "");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsComponentMembershipAndIndependentRoles()
{

    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('a', 'v', 'i', 'f'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> other_payload;
    append_u16be(&other_payload, 1U);
    append_u16be(&other_payload, 1U);
    append_u16be(&other_payload, 2U);
    std::vector<std::byte> other_box;
    append_bmff_box(&other_box, fourcc('a', 'b', 'c', 'd'), other_payload);
    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 2U, 1U);
    iref_payload.insert(iref_payload.end(), other_box.begin(), other_box.end());
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 4U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('a', 'v', '0', '1'), "primary");
    append_infe_v2_mime(&iinf_payload, 2U, 0U, "xmp", "application/rdf+xml", "");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('a', 'v', '0', '1'), "independent");
    append_infe_v2_mime(&iinf_payload, 4U, 0U, "manifest", "application/json", "");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    return file;
}
static std::vector<std::byte> EmitsDisparityAndMatteAuxCountsFromAuxC()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_disparity_payload;
        static constexpr char kDisparity[] =
            "urn:mpeg:mpegB:cicp:systems:auxiliary:disparity";
        append_auxc_payload(&auxc_disparity_payload, kDisparity, {});
        auxc_disparity_payload.push_back(std::byte{0x44});
        std::vector<std::byte> auxc_disparity_box;
        append_bmff_box(&auxc_disparity_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_disparity_payload);

        std::vector<std::byte> auxc_matte_payload;
        static constexpr char kMatte[] = "urn:mpeg:mpegB:cicp:systems:auxiliary:matte";
        append_auxc_payload(&auxc_matte_payload, kMatte, {});
        auxc_matte_payload.push_back(std::byte{0x55});
        std::vector<std::byte> auxc_matte_box;
        append_bmff_box(&auxc_matte_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_matte_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_disparity_box.begin(),
                            auxc_disparity_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_matte_box.begin(),
                            auxc_matte_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 3);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte{0});

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{1});

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{2});

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsNonPrimaryIrefTypedEdges()
{

    // Minimal HEIF-like BMFF:
    // - primary item id = 1
    // - iref edges on non-primary items:
    //   dimg: 2 -> [5,6]
    //   thmb: 3 -> [7]
    //   cdsc: 4 -> [8]

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> dimg_payload;
        append_u16be(&dimg_payload, 2); // from item id
        append_u16be(&dimg_payload, 2); // ref count
        append_u16be(&dimg_payload, 5); // to item id
        append_u16be(&dimg_payload, 6); // to item id
        std::vector<std::byte> dimg_box;
        append_bmff_box(&dimg_box, fourcc('d', 'i', 'm', 'g'), dimg_payload);

        std::vector<std::byte> thmb_payload;
        append_u16be(&thmb_payload, 3); // from item id
        append_u16be(&thmb_payload, 1); // ref count
        append_u16be(&thmb_payload, 7); // to item id
        std::vector<std::byte> thmb_box;
        append_bmff_box(&thmb_box, fourcc('t', 'h', 'm', 'b'), thmb_payload);

        std::vector<std::byte> cdsc_payload;
        append_u16be(&cdsc_payload, 4); // from item id
        append_u16be(&cdsc_payload, 1); // ref count
        append_u16be(&cdsc_payload, 8); // to item id
        std::vector<std::byte> cdsc_box;
        append_bmff_box(&cdsc_box, fourcc('c', 'd', 's', 'c'), cdsc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), dimg_box.begin(), dimg_box.end());
        iref_payload.insert(iref_payload.end(), thmb_box.begin(), thmb_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_box.begin(), cdsc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsDynamicIrefTypedEdgesForUnknownAsciiFourcc()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> pred_payload;
        append_u16be(&pred_payload, 9);  // from item id
        append_u16be(&pred_payload, 2);  // ref count
        append_u16be(&pred_payload, 10); // to item id
        append_u16be(&pred_payload, 11); // to item id
        std::vector<std::byte> pred_box;
        append_bmff_box(&pred_box, fourcc('p', 'r', 'e', 'd'), pred_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), pred_box.begin(), pred_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsAuxSubtypeU64AndAsciiZFromAuxC()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 4U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        append_fullbox_header(&auxc_depth_payload, 0);
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        for (size_t i = 0; i < sizeof(kDepth) - 1; ++i) {
            auxc_depth_payload.push_back(std::byte{static_cast<uint8_t>(kDepth[i])});
        }
        auxc_depth_payload.push_back(std::byte{0x00});
        static constexpr char kAsciiZ[] = "profile";
        for (size_t i = 0; i < sizeof(kAsciiZ) - 1; ++i) {
            auxc_depth_payload.push_back(std::byte{static_cast<uint8_t>(kAsciiZ[i])});
        }
        auxc_depth_payload.push_back(std::byte{0x00});
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        append_fullbox_header(&auxc_alpha_payload, 0);
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        for (size_t i = 0; i < sizeof(kAlpha) - 1; ++i) {
            auxc_alpha_payload.push_back(std::byte{static_cast<uint8_t>(kAlpha[i])});
        }
        auxc_alpha_payload.push_back(std::byte{0x00});
        auxc_alpha_payload.push_back(std::byte{0x11});
        auxc_alpha_payload.push_back(std::byte{0x22});
        auxc_alpha_payload.push_back(std::byte{0x33});
        auxc_alpha_payload.push_back(std::byte{0x44});
        auxc_alpha_payload.push_back(std::byte{0x55});
        auxc_alpha_payload.push_back(std::byte{0x66});
        auxc_alpha_payload.push_back(std::byte{0x77});
        auxc_alpha_payload.push_back(std::byte{0x88});
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> auxc_uuid_payload;
        append_fullbox_header(&auxc_uuid_payload, 0);
        for (size_t i = 0; i < sizeof(kAlpha) - 1; ++i) {
            auxc_uuid_payload.push_back(std::byte{static_cast<uint8_t>(kAlpha[i])});
        }
        auxc_uuid_payload.push_back(std::byte{0x00});
        for (uint8_t i = 0; i < 16U; ++i) {
            auxc_uuid_payload.push_back(std::byte{i});
        }
        std::vector<std::byte> auxc_uuid_box;
        append_bmff_box(&auxc_uuid_box, fourcc('a', 'u', 'x', 'C'), auxc_uuid_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_uuid_box.begin(),
                            auxc_uuid_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 4);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte{0});

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{1});

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{2});

        append_u16be(&ipma_payload, 4);
        ipma_payload.push_back(std::byte{1});
        ipma_payload.push_back(std::byte{3});
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(), ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(), ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(), iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsPerTypeUniqueCountsWithDuplicateEdges()
{

    // Minimal HEIF-like BMFF with duplicate iref edges so per-type
    // unique counters can be distinguished from edge counters.
    //
    // auxl: 1 -> [2,2,3] and 1 -> [3]        => edge=4, from_unique=1, to=2
    // dimg: 2 -> [5,5] and 4 -> [5]          => edge=3, from_unique=2, to=1
    // thmb: 3 -> [7,7] and 3 -> [8]          => edge=3, from_unique=1, to=2
    // cdsc: 4 -> [8,9,9] and 5 -> [8]        => edge=4, from_unique=2, to=2

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_a_payload;
        append_u16be(&auxl_a_payload, 1);
        append_u16be(&auxl_a_payload, 3);
        append_u16be(&auxl_a_payload, 2);
        append_u16be(&auxl_a_payload, 2);
        append_u16be(&auxl_a_payload, 3);
        std::vector<std::byte> auxl_a_box;
        append_bmff_box(&auxl_a_box, fourcc('a', 'u', 'x', 'l'), auxl_a_payload);

        std::vector<std::byte> auxl_b_payload;
        append_u16be(&auxl_b_payload, 1);
        append_u16be(&auxl_b_payload, 1);
        append_u16be(&auxl_b_payload, 3);
        std::vector<std::byte> auxl_b_box;
        append_bmff_box(&auxl_b_box, fourcc('a', 'u', 'x', 'l'), auxl_b_payload);

        std::vector<std::byte> dimg_a_payload;
        append_u16be(&dimg_a_payload, 2);
        append_u16be(&dimg_a_payload, 2);
        append_u16be(&dimg_a_payload, 5);
        append_u16be(&dimg_a_payload, 5);
        std::vector<std::byte> dimg_a_box;
        append_bmff_box(&dimg_a_box, fourcc('d', 'i', 'm', 'g'), dimg_a_payload);

        std::vector<std::byte> dimg_b_payload;
        append_u16be(&dimg_b_payload, 4);
        append_u16be(&dimg_b_payload, 1);
        append_u16be(&dimg_b_payload, 5);
        std::vector<std::byte> dimg_b_box;
        append_bmff_box(&dimg_b_box, fourcc('d', 'i', 'm', 'g'), dimg_b_payload);

        std::vector<std::byte> thmb_a_payload;
        append_u16be(&thmb_a_payload, 3);
        append_u16be(&thmb_a_payload, 2);
        append_u16be(&thmb_a_payload, 7);
        append_u16be(&thmb_a_payload, 7);
        std::vector<std::byte> thmb_a_box;
        append_bmff_box(&thmb_a_box, fourcc('t', 'h', 'm', 'b'), thmb_a_payload);

        std::vector<std::byte> thmb_b_payload;
        append_u16be(&thmb_b_payload, 3);
        append_u16be(&thmb_b_payload, 1);
        append_u16be(&thmb_b_payload, 8);
        std::vector<std::byte> thmb_b_box;
        append_bmff_box(&thmb_b_box, fourcc('t', 'h', 'm', 'b'), thmb_b_payload);

        std::vector<std::byte> cdsc_a_payload;
        append_u16be(&cdsc_a_payload, 4);
        append_u16be(&cdsc_a_payload, 3);
        append_u16be(&cdsc_a_payload, 8);
        append_u16be(&cdsc_a_payload, 9);
        append_u16be(&cdsc_a_payload, 9);
        std::vector<std::byte> cdsc_a_box;
        append_bmff_box(&cdsc_a_box, fourcc('c', 'd', 's', 'c'), cdsc_a_payload);

        std::vector<std::byte> cdsc_b_payload;
        append_u16be(&cdsc_b_payload, 5);
        append_u16be(&cdsc_b_payload, 1);
        append_u16be(&cdsc_b_payload, 8);
        std::vector<std::byte> cdsc_b_box;
        append_bmff_box(&cdsc_b_box, fourcc('c', 'd', 's', 'c'), cdsc_b_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_a_box.begin(), auxl_a_box.end());
        iref_payload.insert(iref_payload.end(), auxl_b_box.begin(), auxl_b_box.end());
        iref_payload.insert(iref_payload.end(), dimg_a_box.begin(), dimg_a_box.end());
        iref_payload.insert(iref_payload.end(), dimg_b_box.begin(), dimg_b_box.end());
        iref_payload.insert(iref_payload.end(), thmb_a_box.begin(), thmb_a_box.end());
        iref_payload.insert(iref_payload.end(), thmb_b_box.begin(), thmb_b_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_a_box.begin(), cdsc_a_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_b_box.begin(), cdsc_b_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsItemInfoRowsAndPrimaryAliases()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kMimeItem = 0x10001U;
        const uint32_t kExifItem = 0x10002U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kExifItem);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe1_payload;
        append_fullbox_header(&infe1_payload, 3);
        append_u32be(&infe1_payload, kMimeItem);
        append_u16be(&infe1_payload, 0);
        append_fourcc(&infe1_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe1_payload, "preview");
        infe1_payload.push_back(std::byte{0});
        append_bytes(&infe1_payload, "image/png");
        infe1_payload.push_back(std::byte{0});
        append_bytes(&infe1_payload, "gzip");
        infe1_payload.push_back(std::byte{0});
        std::vector<std::byte> infe1_box;
        append_bmff_box(&infe1_box, fourcc('i', 'n', 'f', 'e'), infe1_payload);

        std::vector<std::byte> infe2_payload;
        append_fullbox_header(&infe2_payload, 3);
        append_u32be(&infe2_payload, kExifItem);
        append_u16be(&infe2_payload, 0);
        append_fourcc(&infe2_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe2_payload, "exif");
        infe2_payload.push_back(std::byte{0});
        std::vector<std::byte> infe2_box;
        append_bmff_box(&infe2_box, fourcc('i', 'n', 'f', 'e'), infe2_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);
        iinf_payload.insert(iinf_payload.end(), infe1_box.begin(), infe1_box.end());
        iinf_payload.insert(iinf_payload.end(), infe2_box.begin(), infe2_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsItemSemanticLabelsForMetadataCarrierItems()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 3);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 6);
        append_infe_v2(&iinf_payload, 1, 0, fourcc('E', 'x', 'i', 'f'), "exif");
        append_infe_v2_mime(&iinf_payload, 2, 0, "xmp", "application/rdf+xml", "");
        append_infe_v2_mime(&iinf_payload, 3, 0, "manifest", "application/c2pa+jumbf",
                            "");
        append_infe_v2_mime(&iinf_payload, 4, 0, "icc", "application/vnd.iccprofile",
                            "");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('j', 'u', 'm', 'b'), "jumbf");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('t', 'i', 'l', 'i'), "tiled");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsPrimaryMimeItemInfoFromInfeV2()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 7);
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "payload");
        infe_payload.push_back(std::byte{0});
        append_bytes(&infe_payload, "application/rdf+xml");
        infe_payload.push_back(std::byte{0});
        append_bytes(&infe_payload, "gzip");
        infe_payload.push_back(std::byte{0});
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(), infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsItemInfoRowsWithoutPitm()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 3);
        append_u16be(&infe_payload, 0);
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "sidecar");
        infe_payload.push_back(std::byte{0});
        append_bytes(&infe_payload, "application/json");
        infe_payload.push_back(std::byte{0});
        infe_payload.push_back(std::byte{0});
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(), infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> EmitsPrimaryUriItemInfoFromInfeV2()
{

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 3);
        append_fourcc(&infe_payload, fourcc('u', 'r', 'i', ' '));
        append_bytes(&infe_payload, "link");
        infe_payload.push_back(std::byte{0});
        append_bytes(&infe_payload, "https://ns.example/item");
        infe_payload.push_back(std::byte{0});
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(), infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    return file;
}
static std::vector<std::byte> InterpretsBoundedTiledImageConfiguration()
{

    const std::array<uint32_t, 2> dimensions{3U, 5U};
    const std::vector<std::byte> file = make_tiled_image_configuration_file(
        0U, 0U, 1000U, 700U, 256U, 256U, dimensions, false, 1U, 1U, 7U);

    return file;
}
static std::vector<std::byte> RejectsTiledImageTileCountOverflow()
{

    const std::array<uint32_t, 1> dimensions{2U};
    const std::vector<std::byte> file = make_tiled_image_configuration_file(
        0U, 0U, UINT32_MAX, UINT32_MAX, 1U, 1U, dimensions, false, 1U, 1U, 0U);
    return file;
}
static std::vector<std::byte> ValidatesCompleteInternalTiledImageLayout()
{

    const CompleteTiledImageOptions options{};
    const std::vector<std::byte> file = make_complete_tiled_image_file(options);
    return file;
}
static std::vector<std::byte> ValidatesCompleteExternalTiledImageLayout()
{

    CompleteTiledImageOptions options;
    options.external_tiles = true;
    const std::vector<std::byte> file = make_complete_tiled_image_file(options);
    return file;
}
static std::vector<std::byte> InfersSequentialTiledImageSizes()
{

    CompleteTiledImageOptions options;
    options.include_tile_sizes = false;
    options.sequential_order = true;
    options.declared_offset_table_size = 8U;
    options.first_tile_offset = 8U;
    options.second_tile_offset = 12U;
    options.tipa_flags = 1U;
    const std::vector<std::byte> file = make_complete_tiled_image_file(options);
    return file;
}
template <class Callback> bool run_tiled_variants(Callback callback)
{
    bool ok = true;
    const std::array<uint32_t, 1> dimension{2U}, zero{0U};
    const std::array<uint32_t, 9> many{2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U};
    const std::vector<std::vector<std::byte>> bad = {
        make_tiled_image_configuration_file(1U, 0U, 640U, 480U, 64U, 64U, dimension,
                                            false, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 1U, 640U, 480U, 64U, 64U, dimension,
                                            false, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 0U, 64U, dimension,
                                            false, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U, dimension,
                                            true, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U, zero, false,
                                            1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U, many, false,
                                            1U, 1U, 0U)};
    for (size_t i = 0; i < bad.size(); ++i) {
        const auto name = "bmff_ref_MalformedTiledCore_" + std::to_string(i);
        ok = callback(name.c_str(), bad[i]) && ok;
    }
    const std::array<uint32_t, 0> dimensions{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto name = "bmff_ref_TiledPropertyRelationships_" + std::to_string(i);
        ok = callback(name.c_str(), make_tiled_image_configuration_file(
                                        0U, 0U, 640U, 480U, 64U, 64U, dimensions, false,
                                        i == 1 ? 0U : 1U,
                                        i == 0   ? 2U
                                        : i == 1 ? 1U
                                                 : 0U,
                                        0U)) &&
             ok;
    }
    std::array<CompleteTiledImageOptions, 13> variants{};
    variants[0].data_reference_index = 2U;
    variants[1].input_item_count = 3U;
    variants[2].tipa_property_index = 4U;
    variants[3].declared_offset_table_size = 12U;
    variants[4].second_tile_offset = 23U;
    variants[5].omit_internal_conditional = true;
    variants[6].construction_method = 1U;
    variants[7].tipa_version = 1U;
    variants[8].tipa_flags = 2U;
    variants[9].deti_extra_flags = 0x100U;
    variants[10].external_tiles = true;
    variants[10].add_external_conditional = true;
    variants[11].external_tiles = true;
    variants[11].external_directory_flags = 0x80U;
    variants[12].external_tiles = true;
    variants[12].omit_external_template_nul = true;
    for (size_t i = 0; i < variants.size(); ++i) {
        const auto name = "bmff_ref_IncompleteTiledLayout_" + std::to_string(i);
        ok = callback(name.c_str(), make_complete_tiled_image_file(variants[i])) && ok;
    }
    return ok;
}

template <class Callback> bool run(Callback callback)
{
    bool ok = run_tiled_variants(callback);
    ok =
        callback("bmff_ref_EmitsFtypAndPrimaryProps", EmitsFtypAndPrimaryProps()) && ok;
    ok = callback("bmff_ref_EmitsPrimaryApertureAspectAndPixelDepth",
                  EmitsPrimaryApertureAspectAndPixelDepth()) &&
         ok;
    ok = callback("bmff_ref_EmitsPrimaryIccColorProfileSummary",
                  EmitsPrimaryIccColorProfileSummary()) &&
         ok;
    ok = callback("bmff_ref_IgnoresShortPrimaryColorProperty",
                  IgnoresShortPrimaryColorProperty()) &&
         ok;
    ok = callback("bmff_ref_EmitsIrefEdgesAndPrimaryAuxLinks",
                  EmitsIrefEdgesAndPrimaryAuxLinks()) &&
         ok;
    ok = callback("bmff_ref_SeparatesPrimaryDerivedSourcesFromDerivedItems",
                  SeparatesPrimaryDerivedSourcesFromDerivedItems()) &&
         ok;
    ok = callback("bmff_ref_EmitsDerivedImageConstructionSemantics",
                  EmitsDerivedImageConstructionSemantics()) &&
         ok;
    ok = callback("bmff_ref_ReadsDerivedDescriptorFromFileOffsetExtent",
                  ReadsDerivedDescriptorFromFileOffsetExtent()) &&
         ok;
    ok = callback("bmff_ref_ReadsDerivedDescriptorThroughItemOffsets",
                  ReadsDerivedDescriptorThroughItemOffsets()) &&
         ok;
    ok = callback("bmff_ref_RejectsInvalidItemOffsetIndexAndCycle",
                  RejectsInvalidItemOffsetIndexAndCycle()) &&
         ok;
    ok = callback("bmff_ref_RejectsDerivedGraphCyclesAndMissingSources",
                  RejectsDerivedGraphCyclesAndMissingSources()) &&
         ok;
    ok = callback("bmff_ref_RejectsTruncatedDerivedReferenceGraph",
                  RejectsTruncatedDerivedReferenceGraph()) &&
         ok;
    ok = callback("bmff_ref_RejectsInvalidDerivedImageConstructions",
                  RejectsInvalidDerivedImageConstructions()) &&
         ok;
    ok = callback("bmff_ref_EmitsItemGroupsAndPrimaryMembership",
                  EmitsItemGroupsAndPrimaryMembership()) &&
         ok;
    ok = callback("bmff_ref_EmitsItemLocationAndIdatSummaries",
                  EmitsItemLocationAndIdatSummaries()) &&
         ok;
    ok = callback("bmff_ref_EmitsIrefEdgesForVersion1ItemIds",
                  EmitsIrefEdgesForVersion1ItemIds()) &&
         ok;
    ok = callback("bmff_ref_EmitsNonPrimaryIrefTypedEdgesForVersion1ItemIds",
                  EmitsNonPrimaryIrefTypedEdgesForVersion1ItemIds()) &&
         ok;
    ok = callback("bmff_ref_EmitsPrimaryAuxSemanticsFromAuxC",
                  EmitsPrimaryAuxSemanticsFromAuxC()) &&
         ok;
    ok = callback("bmff_ref_EmitsPrimaryLinkedItemRoles",
                  EmitsPrimaryLinkedItemRoles()) &&
         ok;
    ok = callback("bmff_ref_EmitsComponentMembershipAndIndependentRoles",
                  EmitsComponentMembershipAndIndependentRoles()) &&
         ok;
    ok = callback("bmff_ref_EmitsDisparityAndMatteAuxCountsFromAuxC",
                  EmitsDisparityAndMatteAuxCountsFromAuxC()) &&
         ok;
    ok = callback("bmff_ref_EmitsNonPrimaryIrefTypedEdges",
                  EmitsNonPrimaryIrefTypedEdges()) &&
         ok;
    ok = callback("bmff_ref_EmitsDynamicIrefTypedEdgesForUnknownAsciiFourcc",
                  EmitsDynamicIrefTypedEdgesForUnknownAsciiFourcc()) &&
         ok;
    ok = callback("bmff_ref_EmitsAuxSubtypeU64AndAsciiZFromAuxC",
                  EmitsAuxSubtypeU64AndAsciiZFromAuxC()) &&
         ok;
    ok = callback("bmff_ref_EmitsPerTypeUniqueCountsWithDuplicateEdges",
                  EmitsPerTypeUniqueCountsWithDuplicateEdges()) &&
         ok;
    ok = callback("bmff_ref_EmitsItemInfoRowsAndPrimaryAliases",
                  EmitsItemInfoRowsAndPrimaryAliases()) &&
         ok;
    ok = callback("bmff_ref_EmitsItemSemanticLabelsForMetadataCarrierItems",
                  EmitsItemSemanticLabelsForMetadataCarrierItems()) &&
         ok;
    ok = callback("bmff_ref_EmitsPrimaryMimeItemInfoFromInfeV2",
                  EmitsPrimaryMimeItemInfoFromInfeV2()) &&
         ok;
    ok = callback("bmff_ref_EmitsItemInfoRowsWithoutPitm",
                  EmitsItemInfoRowsWithoutPitm()) &&
         ok;
    ok = callback("bmff_ref_EmitsPrimaryUriItemInfoFromInfeV2",
                  EmitsPrimaryUriItemInfoFromInfeV2()) &&
         ok;
    ok = callback("bmff_ref_InterpretsBoundedTiledImageConfiguration",
                  InterpretsBoundedTiledImageConfiguration()) &&
         ok;
    ok = callback("bmff_ref_RejectsTiledImageTileCountOverflow",
                  RejectsTiledImageTileCountOverflow()) &&
         ok;
    ok = callback("bmff_ref_ValidatesCompleteInternalTiledImageLayout",
                  ValidatesCompleteInternalTiledImageLayout()) &&
         ok;
    ok = callback("bmff_ref_ValidatesCompleteExternalTiledImageLayout",
                  ValidatesCompleteExternalTiledImageLayout()) &&
         ok;
    ok = callback("bmff_ref_InfersSequentialTiledImageSizes",
                  InfersSequentialTiledImageSizes()) &&
         ok;
    return ok;
}
} // namespace omc_bmff_reference_fixtures
