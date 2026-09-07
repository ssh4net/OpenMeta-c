#include <openmeta/exif_tiff_decode.h>
#include <openmeta/exif_tiff_serialize.h>
#include <openmeta/metadata_translation.h>
#include <openmeta/metadata_transfer.h>
#include "omc/omc_exif_tiff_serialize.h"
#include "omc/omc_translation.h"
#include "omc_test_assert.h"
#include <cstring>
#include <vector>

bool run_omc_authoring_parity();

static void
run_translation_parity()
{
    const char *xmp = "http://ns.adobe.com/xap/1.0/";
    const char *exif = "http://ns.adobe.com/exif/1.0/";
    const char *tiff = "http://ns.adobe.com/tiff/1.0/";
    const char *dc = "http://purl.org/dc/elements/1.1/";
    omc_store source;
    omc_store result;
    omc_store_init(&source);
    omc_store_init(&result);
    openmeta::MetaStore cpp_source;
    const auto add = [&](const char *ns, const char *path, const char *text) {
        omc_entry e{};
        omc_byte_ref ns_ref{};
        omc_byte_ref path_ref{};
        omc_byte_ref value_ref{};
        assert(omc_arena_append(&source.arena, ns, std::strlen(ns), &ns_ref) ==
               OMC_STATUS_OK);
        assert(omc_arena_append(&source.arena, path, std::strlen(path), &path_ref) ==
               OMC_STATUS_OK);
        assert(omc_arena_append(&source.arena, text, std::strlen(text), &value_ref) ==
               OMC_STATUS_OK);
        omc_key_make_xmp_property(&e.key, ns_ref, path_ref);
        omc_val_make_text(&e.value, value_ref, OMC_TEXT_UTF8);
        e.flags = OMC_ENTRY_FLAG_DIRTY;
        e.origin.block = OMC_INVALID_BLOCK_ID;
        assert(omc_store_add_entry(&source, &e, nullptr) == OMC_STATUS_OK);
        openmeta::Entry ce;
        ce.key = openmeta::make_xmp_property_key(cpp_source.arena(), ns, path);
        ce.value =
            openmeta::make_text(cpp_source.arena(), text, openmeta::TextEncoding::Utf8);
        ce.flags = openmeta::EntryFlags::Dirty;
        assert(cpp_source.add_entry(ce) != openmeta::kInvalidEntryId);
    };
    add(xmp, "CreateDate", "2024-02-29T23:59:60-00:00");
    add(xmp, "ModifyDate", "2026-09-07T12:34:56.123456789+09:00");
    add(xmp, "CreatorTool", "C writer");
    add(exif, "ExposureTime", "8e-3");
    add(exif, "FNumber", "2.8");
    add(exif, "ExposureBiasValue", "-1/3");
    add(exif, "ISOSpeedRatings[1]", "200");
    add(exif, "FocalLength", "24 mm");
    add(dc, "title[@xml:lang=x-default]", "caf\303\251");
    add(dc, "creator[10]", "Ten");
    add(dc, "creator[2]", "Two");
    add(tiff, "Orientation", "6");
    add(tiff, "ImageWidth", "640");
    add(exif, "ExifImageHeight", "480");
    cpp_source.finalize();
    omc_transfer_target_image_spec target{};
    target.has_dimensions = 1;
    target.width = 640U;
    target.height = 480U;
    target.has_orientation = 1;
    target.orientation = 6U;
    assert(omc_translate_xmp(&source, &result, nullptr, &target).status ==
           OMC_TRANSLATION_OK);
    openmeta::MetaStore a;
    openmeta::MetaStore b;
    assert(openmeta::translate_xmp_creation_dates(cpp_source, {}, &a).status ==
           openmeta::MetadataDateTranslationStatus::Ok);
    assert(openmeta::translate_xmp_technical_metadata(a, {}, &b).status ==
           openmeta::MetadataTechnicalTranslationStatus::Ok);
    a = std::move(b);
    assert(openmeta::translate_xmp_capture_metadata(a, {}, &b).status ==
           openmeta::MetadataCaptureTranslationStatus::Ok);
    a = std::move(b);
    assert(openmeta::translate_xmp_descriptive_metadata(a, {}, &b).status ==
           openmeta::MetadataDescriptiveTranslationStatus::Ok);
    a = std::move(b);
    openmeta::TransferTargetImageSpec cpp_target;
    cpp_target.has_dimensions = true;
    cpp_target.width = 640U;
    cpp_target.height = 480U;
    cpp_target.has_orientation = true;
    cpp_target.orientation = 6U;
    assert(openmeta::translate_xmp_image_geometry(a, cpp_target, {}, &b).status ==
           openmeta::MetadataGeometryTranslationStatus::Ok);
    const auto cm = omc_serialize_exif_tiff(&result, {nullptr, 0U}, nullptr);
    const auto cpm = openmeta::serialize_exif_tiff(b, {});
    assert(cm.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED);
    assert(cpm.status == openmeta::ExifTiffSerializeStatus::OutputTruncated);
    std::vector<omc_u8> cbytes(static_cast<size_t>(cm.needed));
    std::vector<std::byte> cppbytes(static_cast<size_t>(cpm.needed));
    assert(omc_serialize_exif_tiff(&result, {cbytes.data(), cbytes.size()}, nullptr)
               .status == OMC_EXIF_TIFF_OK);
    assert(openmeta::serialize_exif_tiff(b, cppbytes).status ==
           openmeta::ExifTiffSerializeStatus::Ok);
    assert(cbytes.size() == cppbytes.size() &&
           std::memcmp(cbytes.data(), cppbytes.data(), cbytes.size()) == 0);
    size_t iptc_count = 0U;
    for (const auto &ce : b.entries()) {
        if (ce.key.kind != openmeta::MetaKeyKind::IptcDataset)
            continue;
        const auto data = b.arena().span(ce.value.data.span);
        size_t occurrence = 0U;
        for (size_t i = 0U; i < result.entry_count; ++i) {
            const auto &e = result.entries[i];
            if (e.key.kind != OMC_KEY_IPTC_DATASET)
                continue;
            if (occurrence++ != iptc_count)
                continue;
            assert(e.key.u.iptc_dataset.record == ce.key.data.iptc_dataset.record);
            assert(e.key.u.iptc_dataset.dataset == ce.key.data.iptc_dataset.dataset);
            const auto cdata = omc_arena_view(&result.arena, e.value.u.ref);
            assert(cdata.size == data.size() &&
                   std::memcmp(cdata.data, data.data(), data.size()) == 0);
            break;
        }
        assert(occurrence > iptc_count);
        iptc_count++;
    }
    omc_store_fini(&result);
    omc_store_fini(&source);
}

bool
run_omc_authoring_parity()
{
    run_translation_parity();
    omc_store store;
    omc_store_init(&store);
    const auto add = [&](const char *ifd, omc_u16 tag, omc_val value) {
        omc_entry e{};
        omc_byte_ref ref{};
        assert(omc_arena_append(&store.arena, ifd, std::strlen(ifd), &ref) ==
               OMC_STATUS_OK);
        omc_key_make_exif_tag(&e.key, ref, tag);
        e.value = value;
        e.origin.block = OMC_INVALID_BLOCK_ID;
        assert(omc_store_add_entry(&store, &e, nullptr) == OMC_STATUS_OK);
    };
    omc_val value{};
    omc_byte_ref ref{};
    assert(omc_arena_append(&store.arena, "C writer", 8U, &ref) == OMC_STATUS_OK);
    omc_val_make_text(&value, ref, OMC_TEXT_ASCII);
    add("ifd0", 0x0131U, value);
    omc_val_make_srational(&value, {-1, 3});
    add("exififd", 0x9204U, value);
    omc_val_make_urational(&value, {1U, 125U});
    add("exififd", 0x829AU, value);
    omc_val_make_u16(&value, 6U);
    add("ifd0", 0x0112U, value);
    omc_val_make_i32(&value, -32769);
    add("ifd0", 0xF000U, value);
    omc_val_make_u32(&value, 48U);
    add("ifd3", 0x0100U, value);
    add("subifd2", 0x0101U, value);
    const unsigned char rational_be[] = {0,   0,   0,   1,   0, 0, 0, 2,
                                         255, 255, 255, 253, 0, 0, 0, 4};
    assert(omc_arena_append(&store.arena, rational_be, sizeof(rational_be), &ref) ==
           OMC_STATUS_OK);
    omc_val_make_array(&value, OMC_ELEM_SRATIONAL, 2U, ref, OMC_BYTE_ORDER_BIG);
    add("ifd0", 0xF001U, value);
    for (int sub = 0; sub <= 1; ++sub) {
        omc_exif_tiff_opts options;
        omc_exif_tiff_opts_init(&options);
        options.include_subifds = sub;
        const auto measured = omc_serialize_exif_tiff(&store, {nullptr, 0U}, &options);
        assert(measured.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED);
        std::vector<omc_u8> bytes(static_cast<size_t>(measured.needed));
        assert(omc_serialize_exif_tiff(&store, {bytes.data(), bytes.size()}, &options)
                   .status == OMC_EXIF_TIFF_OK);
        openmeta::MetaStore decoded;
        const auto read = openmeta::decode_exif_tiff(
            {reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()}, decoded,
            {}, {});
        assert(read.status == openmeta::ExifDecodeStatus::Ok);
        decoded.finalize();
        openmeta::ExifTiffSerializeOptions cpp_options;
        cpp_options.include_subifds = sub != 0;
        const auto cpp_measured =
            openmeta::serialize_exif_tiff(decoded, {}, cpp_options);
        assert(cpp_measured.status ==
               openmeta::ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> cpp_bytes(static_cast<size_t>(cpp_measured.needed));
        assert(openmeta::serialize_exif_tiff(decoded, cpp_bytes, cpp_options).status ==
               openmeta::ExifTiffSerializeStatus::Ok);
        assert(bytes.size() == cpp_bytes.size());
        assert(std::memcmp(bytes.data(), cpp_bytes.data(), bytes.size()) == 0);
        std::vector<omc_u8> prefix(17U);
        const auto truncated =
            omc_serialize_exif_tiff(&store, {prefix.data(), prefix.size()}, &options);
        assert(truncated.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED);
        assert(std::memcmp(prefix.data(), cpp_bytes.data(), prefix.size()) == 0);
    }
    omc_store_fini(&store);
    return true;
}
