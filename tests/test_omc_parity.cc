#include <openmeta/meta_key.h>
#include <openmeta/meta_store.h>
#include <openmeta/meta_value.h>
#include <openmeta/exif_tag_names.h>
#include <openmeta/metadata_transfer.h>
#include <openmeta/simple_meta.h>

extern "C" {
#include "omc/omc_arena.h"
#include "omc/omc_exif_name.h"
#include "omc/omc_key.h"
#include "omc/omc_read.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer.h"
#include "omc/omc_transfer_persist.h"
#include "omc/omc_val.h"
}

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using ByteVec = std::vector<unsigned char>;

static void
append_u8(unsigned char* out, std::size_t* io_size, unsigned char value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_bytes(unsigned char* out, std::size_t* io_size, const void* src,
             std::size_t size)
{
    std::memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
append_text(unsigned char* out, std::size_t* io_size, const char* text)
{
    append_bytes(out, io_size, text, std::strlen(text));
}

static void
append_u16be(unsigned char* out, std::size_t* io_size, std::uint16_t value)
{
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
}

static void
append_u16le(unsigned char* out, std::size_t* io_size, std::uint16_t value)
{
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
}

static void
append_u32le(unsigned char* out, std::size_t* io_size, std::uint32_t value)
{
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 24) & 0xFFU));
}

static void
append_i32le(unsigned char* out, std::size_t* io_size, std::int32_t value)
{
    append_u32le(out, io_size, (std::uint32_t)value);
}

static void
append_u64le(unsigned char* out, std::size_t* io_size, std::uint64_t value)
{
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 32) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 40) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 48) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 56) & 0xFFU));
}

static void
append_u32be(unsigned char* out, std::size_t* io_size, std::uint32_t value)
{
    append_u8(out, io_size, (unsigned char)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)(value & 0xFFU));
}

static std::uint32_t
f32_bits(float value)
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void
write_u16be_at(unsigned char* out, std::uint32_t off, std::uint16_t value)
{
    out[off + 0U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 1U] = (unsigned char)(value & 0xFFU);
}

static void
write_u16le_at(unsigned char* out, std::uint32_t off, std::uint16_t value)
{
    out[off + 0U] = (unsigned char)(value & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 8) & 0xFFU);
}

static void
write_u32le_at(unsigned char* out, std::uint32_t off, std::uint32_t value)
{
    out[off + 0U] = (unsigned char)(value & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 2U] = (unsigned char)((value >> 16) & 0xFFU);
    out[off + 3U] = (unsigned char)((value >> 24) & 0xFFU);
}

static void
write_u32be_at(unsigned char* out, std::uint32_t off, std::uint32_t value)
{
    out[off + 0U] = (unsigned char)((value >> 24) & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 16) & 0xFFU);
    out[off + 2U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 3U] = (unsigned char)(value & 0xFFU);
}

static void
write_u64be_at(unsigned char* out, std::uint32_t off, std::uint64_t value)
{
    out[off + 0U] = (unsigned char)((value >> 56) & 0xFFU);
    out[off + 1U] = (unsigned char)((value >> 48) & 0xFFU);
    out[off + 2U] = (unsigned char)((value >> 40) & 0xFFU);
    out[off + 3U] = (unsigned char)((value >> 32) & 0xFFU);
    out[off + 4U] = (unsigned char)((value >> 24) & 0xFFU);
    out[off + 5U] = (unsigned char)((value >> 16) & 0xFFU);
    out[off + 6U] = (unsigned char)((value >> 8) & 0xFFU);
    out[off + 7U] = (unsigned char)(value & 0xFFU);
}

static void
append_jpeg_segment(unsigned char* out, std::size_t* io_size,
                    std::uint16_t marker, const unsigned char* payload,
                    std::size_t payload_size)
{
    append_u8(out, io_size, 0xFFU);
    append_u8(out, io_size, (unsigned char)(marker & 0xFFU));
    append_u16be(out, io_size, (std::uint16_t)(payload_size + 2U));
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static void
append_png_chunk(unsigned char* out, std::size_t* io_size, const char* type,
                 const unsigned char* payload, std::size_t payload_size)
{
    append_u32be(out, io_size, (std::uint32_t)payload_size);
    append_bytes(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
    append_u32be(out, io_size, 0U);
}

static void
append_webp_chunk(unsigned char* out, std::size_t* io_size, const char* type,
                  const unsigned char* payload, std::size_t payload_size)
{
    append_text(out, io_size, type);
    append_u32le(out, io_size, (std::uint32_t)payload_size);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static void
append_fullbox_header(unsigned char* out, std::size_t* io_size,
                      unsigned char version)
{
    append_u8(out, io_size, version);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
}

static void
append_bmff_box(unsigned char* out, std::size_t* io_size, std::uint32_t type,
                const unsigned char* payload, std::size_t payload_size)
{
    append_u32be(out, io_size, (std::uint32_t)(8U + payload_size));
    append_u32be(out, io_size, type);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static void
append_box(unsigned char* out, std::size_t* io_size, const char* type,
           const unsigned char* payload, std::size_t payload_size)
{
    append_u32be(out, io_size, (std::uint32_t)(8U + payload_size));
    append_bytes(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static void
append_cbor_head(unsigned char* out, std::size_t* io_size,
                 unsigned char major, unsigned char arg)
{
    assert(arg < 24U);
    append_u8(out, io_size, (unsigned char)((major << 5) | arg));
}

static void
append_cbor_map(unsigned char* out, std::size_t* io_size, unsigned char count)
{
    append_cbor_head(out, io_size, 5U, count);
}

static void
append_cbor_array(unsigned char* out, std::size_t* io_size,
                  unsigned char count)
{
    append_cbor_head(out, io_size, 4U, count);
}

static void
append_cbor_text(unsigned char* out, std::size_t* io_size, const char* text)
{
    const std::size_t len = std::strlen(text);

    assert(len < 256U);
    if (len < 24U) {
        append_cbor_head(out, io_size, 3U, (unsigned char)len);
    } else {
        append_u8(out, io_size, (unsigned char)((3U << 5) | 24U));
        append_u8(out, io_size, (unsigned char)len);
    }
    append_bytes(out, io_size, text, len);
}

static void
append_cbor_bytes(unsigned char* out, std::size_t* io_size,
                  const unsigned char* bytes, std::size_t size)
{
    assert(size < 256U);
    if (size < 24U) {
        append_cbor_head(out, io_size, 2U, (unsigned char)size);
    } else {
        append_u8(out, io_size, (unsigned char)((2U << 5) | 24U));
        append_u8(out, io_size, (unsigned char)size);
    }
    append_bytes(out, io_size, bytes, size);
}

static void
append_cbor_null(unsigned char* out, std::size_t* io_size)
{
    append_u8(out, io_size, 0xF6U);
}

static void
append_cbor_i64(unsigned char* out, std::size_t* io_size, std::int64_t value)
{
    std::uint64_t arg;
    unsigned char major;

    if (value >= 0) {
        major = 0U;
        arg = (std::uint64_t)value;
    } else {
        major = 1U;
        arg = (std::uint64_t)(-1 - value);
    }
    if (arg < 24U) {
        append_cbor_head(out, io_size, major, (unsigned char)arg);
    } else if (arg <= 0xFFU) {
        append_u8(out, io_size, (unsigned char)((major << 5) | 24U));
        append_u8(out, io_size, (unsigned char)arg);
    } else if (arg <= 0xFFFFU) {
        append_u8(out, io_size, (unsigned char)((major << 5) | 25U));
        append_u8(out, io_size, (unsigned char)((arg >> 8) & 0xFFU));
        append_u8(out, io_size, (unsigned char)(arg & 0xFFU));
    } else {
        append_u8(out, io_size, (unsigned char)((major << 5) | 26U));
        append_u32be(out, io_size, (std::uint32_t)arg);
    }
}

static std::size_t
make_jumb_box_with_label(unsigned char* out, const char* label,
                         const unsigned char* payload_boxes,
                         std::size_t payload_boxes_size)
{
    const std::size_t label_len = std::strlen(label);
    const std::size_t jumd_payload_size = label_len + 1U;
    const std::size_t jumb_payload_size = 8U + jumd_payload_size
                                          + payload_boxes_size;
    std::size_t out_size = 0U;

    append_u32be(out, &out_size, (std::uint32_t)(8U + jumb_payload_size));
    append_bytes(out, &out_size, "jumb", 4U);
    append_u32be(out, &out_size, (std::uint32_t)(8U + jumd_payload_size));
    append_bytes(out, &out_size, "jumd", 4U);
    append_bytes(out, &out_size, label, label_len);
    append_u8(out, &out_size, 0U);
    if (payload_boxes_size != 0U) {
        append_bytes(out, &out_size, payload_boxes, payload_boxes_size);
    }
    return out_size;
}

static std::size_t
make_claim_jumb_box(unsigned char* out, const char* label,
                    const unsigned char* claim_cbor,
                    std::size_t claim_cbor_size)
{
    std::array<unsigned char, 512> cbor_box {};
    std::size_t cbor_box_size = 0U;

    append_box(cbor_box.data(), &cbor_box_size, "cbor", claim_cbor,
               claim_cbor_size);
    return make_jumb_box_with_label(out, label, cbor_box.data(),
                                    cbor_box_size);
}

static std::size_t
make_jumbf_with_cbor(unsigned char* out, const unsigned char* cbor_payload,
                     std::size_t cbor_payload_size)
{
    std::array<unsigned char, 64> jumd_box {};
    std::array<unsigned char, 512> cbor_box {};
    std::array<unsigned char, 640> jumb_payload {};
    std::size_t jumd_size = 0U;
    std::size_t cbor_size = 0U;
    std::size_t jumb_payload_size = 0U;
    std::size_t out_size = 0U;

    append_box(jumd_box.data(), &jumd_size, "jumd",
               (const unsigned char*)"c2pa\0", 5U);
    append_box(cbor_box.data(), &cbor_size, "cbor", cbor_payload,
               cbor_payload_size);
    append_bytes(jumb_payload.data(), &jumb_payload_size, jumd_box.data(),
                 jumd_size);
    append_bytes(jumb_payload.data(), &jumb_payload_size, cbor_box.data(),
                 cbor_size);
    append_box(out, &out_size, "jumb", jumb_payload.data(), jumb_payload_size);
    return out_size;
}

static std::uint32_t
make_fourcc(char a, char b, char c, char d)
{
    return (((std::uint32_t)(unsigned char)a) << 24)
           | (((std::uint32_t)(unsigned char)b) << 16)
           | (((std::uint32_t)(unsigned char)c) << 8)
           | (((std::uint32_t)(unsigned char)d) << 0);
}

static ByteVec
build_jpeg_comment_fixture()
{
    std::array<unsigned char, 128> file {};
    static const char comment[] = "OpenMeta JPEG comment";
    std::size_t size = 0U;

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD8U);
    append_jpeg_segment(file.data(), &size, 0xFFFEU,
                        (const unsigned char*)comment, sizeof(comment) - 1U);
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD9U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static std::string
build_creator_tool_xmp_xml(const char* creator_tool)
{
    std::string out;

    out += "<x:xmpmeta xmlns:x='adobe:ns:meta/'>";
    out += "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>";
    out += "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' ";
    out += "xmp:CreatorTool='";
    out += creator_tool;
    out += "'/>";
    out += "</rdf:RDF>";
    out += "</x:xmpmeta>";
    return out;
}

static ByteVec
build_transfer_xmp_sidecar_fixture(const char* creator_tool)
{
    const std::string xml = build_creator_tool_xmp_xml(creator_tool);
    return ByteVec(xml.begin(), xml.end());
}

static void
append_creator_tool_xmp_app1(unsigned char* out, std::size_t* io_size,
                             const char* creator_tool)
{
    std::array<unsigned char, 1024> payload {};
    std::size_t payload_size = 0U;
    const std::string xml = build_creator_tool_xmp_xml(creator_tool);

    append_text(payload.data(), &payload_size, "http://ns.adobe.com/xap/1.0/");
    append_u8(payload.data(), &payload_size, 0U);
    append_bytes(payload.data(), &payload_size, xml.data(), xml.size());
    append_jpeg_segment(out, io_size, 0xFFE1U, payload.data(), payload_size);
}

static ByteVec
build_transfer_source_jpeg_fixture_common(bool include_exif)
{
    openmeta::MetaStore store;
    openmeta::PreparedTransferBundle bundle;
    openmeta::PreparedTransferExecutionPlan plan;
    openmeta::PrepareTransferRequest request;
    openmeta::PrepareTransferResult prepared;
    openmeta::EmitTransferResult compiled;
    openmeta::Entry exif;
    openmeta::Entry xmp;
    openmeta::BlockId block;
    std::array<unsigned char, 2048> file {};
    std::size_t size = 0U;
    std::size_t i;

    block = store.add_block(openmeta::BlockInfo {});
    if (block == openmeta::kInvalidBlockId) {
        return ByteVec();
    }

    if (include_exif) {
        exif.key = openmeta::make_exif_tag_key(store.arena(), "exififd",
                                               0x9003U);
        exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                         openmeta::TextEncoding::Ascii);
        exif.origin.block          = block;
        exif.origin.order_in_block = 0U;
        if (store.add_entry(exif) == openmeta::kInvalidEntryId) {
            return ByteVec();
        }
    }

    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), "OpenMeta Transfer Source",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = include_exif ? 1U : 0U;
    if (store.add_entry(xmp) == openmeta::kInvalidEntryId) {
        return ByteVec();
    }

    store.finalize();

    request.target_format      = openmeta::TransferTargetFormat::Jpeg;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    prepared = openmeta::prepare_metadata_for_target(store, request, &bundle);
    if (prepared.status != openmeta::TransferStatus::Ok) {
        return ByteVec();
    }

    compiled = openmeta::compile_prepared_transfer_execution(
        bundle, openmeta::EmitTransferOptions {}, &plan);
    if (compiled.status != openmeta::TransferStatus::Ok
        || plan.target_format != openmeta::TransferTargetFormat::Jpeg) {
        return ByteVec();
    }

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD8U);
    for (i = 0U; i < plan.jpeg_emit.ops.size(); ++i) {
        const openmeta::PreparedJpegEmitOp& op = plan.jpeg_emit.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            return ByteVec();
        }
        append_jpeg_segment(
            file.data(), &size, (std::uint16_t)(0xFF00U | op.marker_code),
            (const unsigned char*)bundle.blocks[op.block_index].payload.data(),
            bundle.blocks[op.block_index].payload.size());
    }
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD9U);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_source_jpeg_fixture()
{
    return build_transfer_source_jpeg_fixture_common(false);
}

static ByteVec
build_transfer_source_jpeg_exif_xmp_fixture()
{
    return build_transfer_source_jpeg_fixture_common(true);
}

static ByteVec
build_transfer_target_jpeg_fixture(const char* existing_creator_tool,
                                   bool include_comment)
{
    std::array<unsigned char, 1024> file {};
    std::size_t size = 0U;

    (void)include_comment;
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD8U);
    if (existing_creator_tool != nullptr) {
        append_creator_tool_xmp_app1(file.data(), &size, existing_creator_tool);
    }
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD9U);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_png_fixture(const char* existing_creator_tool)
{
    static const unsigned char png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 13> ihdr {};
    std::array<unsigned char, 512> xmp_payload {};
    std::size_t xmp_size = 0U;
    std::size_t size = 0U;
    const std::string xml = (existing_creator_tool != nullptr)
                                ? build_creator_tool_xmp_xml(
                                      existing_creator_tool)
                                : std::string();

    ihdr[3] = 1U;
    ihdr[7] = 1U;
    ihdr[8] = 8U;
    ihdr[9] = 2U;

    if (existing_creator_tool != nullptr) {
        append_text(xmp_payload.data(), &xmp_size, "XML:com.adobe.xmp");
        append_u8(xmp_payload.data(), &xmp_size, 0U);
        append_u8(xmp_payload.data(), &xmp_size, 0U);
        append_u8(xmp_payload.data(), &xmp_size, 0U);
        append_u8(xmp_payload.data(), &xmp_size, 0U);
        append_u8(xmp_payload.data(), &xmp_size, 0U);
        append_bytes(xmp_payload.data(), &xmp_size, xml.data(), xml.size());
    }

    append_bytes(file.data(), &size, png_sig, sizeof(png_sig));
    append_png_chunk(file.data(), &size, "IHDR", ihdr.data(), ihdr.size());
    if (existing_creator_tool != nullptr) {
        append_png_chunk(file.data(), &size, "iTXt", xmp_payload.data(),
                         xmp_size);
    }
    append_png_chunk(file.data(), &size, "IEND", (const unsigned char*)0, 0U);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_tiff_make_only_fixture()
{
    static const char make[] = "Canon";
    std::array<unsigned char, 128> file {};
    std::size_t size = 0U;
    std::uint32_t make_off;

    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 1U);

    make_off = 8U + 2U + 12U + 4U;

    append_u16le(file.data(), &size, 0x010FU);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)sizeof(make));
    append_u32le(file.data(), &size, make_off);
    append_u32le(file.data(), &size, 0U);
    append_text(file.data(), &size, make);
    append_u8(file.data(), &size, 0U);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_tiff_fixture(const char* existing_creator_tool)
{
    static const char make[] = "Canon";
    std::array<unsigned char, 1024> file {};
    const std::string xml = (existing_creator_tool != nullptr)
                                ? build_creator_tool_xmp_xml(
                                      existing_creator_tool)
                                : std::string();
    std::size_t size = 0U;
    std::uint32_t make_off;
    std::uint32_t xmp_off;
    std::uint16_t entry_count;

    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    entry_count = (existing_creator_tool != nullptr) ? 2U : 1U;
    append_u16le(file.data(), &size, entry_count);

    make_off = 8U + 2U + (std::uint32_t)(entry_count * 12U) + 4U;
    xmp_off = make_off + (std::uint32_t)sizeof(make);

    append_u16le(file.data(), &size, 0x010FU);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)sizeof(make));
    append_u32le(file.data(), &size, make_off);

    if (existing_creator_tool != nullptr) {
        append_u16le(file.data(), &size, 700U);
        append_u16le(file.data(), &size, 7U);
        append_u32le(file.data(), &size, (std::uint32_t)xml.size());
        append_u32le(file.data(), &size, xmp_off);
    }

    append_u32le(file.data(), &size, 0U);
    append_text(file.data(), &size, make);
    append_u8(file.data(), &size, 0U);
    if (existing_creator_tool != nullptr) {
        append_bytes(file.data(), &size, xml.data(), xml.size());
    }
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_bigtiff_fixture(const char* existing_creator_tool)
{
    static const char make[] = { 'C', 'a', 'n', 'o', 'n', 0U };
    std::array<unsigned char, 1024> file {};
    const std::string xml = (existing_creator_tool != nullptr)
                                ? build_creator_tool_xmp_xml(
                                      existing_creator_tool)
                                : std::string();
    std::size_t size = 0U;
    std::uint64_t xmp_off;
    std::uint64_t entry_count;

    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 43U);
    append_u16le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 0U);
    append_u64le(file.data(), &size, 16U);
    entry_count = (existing_creator_tool != nullptr) ? 2U : 1U;
    append_u64le(file.data(), &size, entry_count);

    xmp_off = 16U + 8U + (entry_count * 20U) + 8U;

    append_u16le(file.data(), &size, 0x010FU);
    append_u16le(file.data(), &size, 2U);
    append_u64le(file.data(), &size, (std::uint64_t)sizeof(make));
    append_bytes(file.data(), &size, make, sizeof(make));
    append_u8(file.data(), &size, 0U);
    append_u8(file.data(), &size, 0U);

    if (existing_creator_tool != nullptr) {
        append_u16le(file.data(), &size, 700U);
        append_u16le(file.data(), &size, 7U);
        append_u64le(file.data(), &size, (std::uint64_t)xml.size());
        append_u64le(file.data(), &size, xmp_off);
    }

    append_u64le(file.data(), &size, 0U);
    if (existing_creator_tool != nullptr) {
        append_bytes(file.data(), &size, xml.data(), xml.size());
    }
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_webp_fixture(const char* existing_creator_tool)
{
    std::array<unsigned char, 2048> file {};
    std::array<unsigned char, 16> vp8x {};
    const std::string xml = (existing_creator_tool != nullptr)
                                ? build_creator_tool_xmp_xml(
                                      existing_creator_tool)
                                : std::string();
    std::size_t size = 0U;

    append_text(file.data(), &size, "RIFF");
    append_u32le(file.data(), &size, 0U);
    append_text(file.data(), &size, "WEBP");
    vp8x[0] = (existing_creator_tool != nullptr) ? 0x04U : 0x00U;
    append_webp_chunk(file.data(), &size, "VP8X", vp8x.data(), 10U);
    if (existing_creator_tool != nullptr) {
        append_webp_chunk(file.data(), &size, "XMP ",
                          (const unsigned char*)xml.data(), xml.size());
    }
    append_webp_chunk(file.data(), &size, "VP8 ", (const unsigned char*)"\0",
                      1U);
    write_u32le_at(file.data(), 4U, (std::uint32_t)(size - 8U));
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_jp2_fixture(const char* existing_creator_tool)
{
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 12> ftyp_payload {};
    const std::string xml = (existing_creator_tool != nullptr)
                                ? build_creator_tool_xmp_xml(
                                      existing_creator_tool)
                                : std::string();
    std::size_t ftyp_size = 0U;
    std::size_t size = 0U;

    append_u32be(file.data(), &size, 12U);
    append_u32be(file.data(), &size, make_fourcc('j', 'P', ' ', ' '));
    append_u32be(file.data(), &size, 0x0D0A870AU);
    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('j', 'p', '2', ' '));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('j', 'p', '2', ' '));
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    if (existing_creator_tool != nullptr) {
        append_bmff_box(file.data(), &size, make_fourcc('x', 'm', 'l', ' '),
                        (const unsigned char*)xml.data(), xml.size());
    }
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_heif_minimal_fixture()
{
    std::array<unsigned char, 64> file {};
    std::array<unsigned char, 16> ftyp_payload {};
    static const unsigned char mdat_payload[] = { 0x11U, 0x22U, 0x33U, 0x44U };
    std::size_t ftyp_size = 0U;
    std::size_t size = 0U;

    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('m', 'i', 'f', '1'));
    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('h', 'e', 'i', 'c'));

    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'd', 'a', 't'),
                    mdat_payload, sizeof(mdat_payload));
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_avif_minimal_fixture()
{
    std::array<unsigned char, 64> file {};
    std::array<unsigned char, 16> ftyp_payload {};
    static const unsigned char mdat_payload[] = { 0x11U, 0x22U, 0x33U, 0x44U };
    std::size_t ftyp_size = 0U;
    std::size_t size = 0U;

    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('a', 'v', 'i', 'f'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('m', 'i', 'f', '1'));
    append_u32be(ftyp_payload.data(), &ftyp_size, make_fourcc('a', 'v', 'i', 'f'));

    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'd', 'a', 't'),
                    mdat_payload, sizeof(mdat_payload));
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_bmff_fixture(const char* existing_creator_tool,
                                   std::uint32_t major_brand)
{
    std::array<unsigned char, 4096> file {};
    std::array<unsigned char, 256> exif_payload {};
    std::array<unsigned char, 1024> idat_payload {};
    std::array<unsigned char, 64> infe_exif {};
    std::array<unsigned char, 96> infe_xmp {};
    std::array<unsigned char, 256> iinf_payload {};
    std::array<unsigned char, 160> iloc_payload {};
    std::array<unsigned char, 1024> meta_payload {};
    std::array<unsigned char, 16> moov_box {};
    std::array<unsigned char, 16> ftyp_payload {};
    const ByteVec tiff = build_transfer_tiff_make_only_fixture();
    const std::string xml = (existing_creator_tool != nullptr)
                                ? build_creator_tool_xmp_xml(
                                      existing_creator_tool)
                                : std::string();
    std::size_t exif_size = 0U;
    std::size_t idat_size = 0U;
    std::size_t exif_off;
    std::size_t xmp_off = 0U;
    std::size_t infe_exif_size = 0U;
    std::size_t infe_xmp_size = 0U;
    std::size_t iinf_size = 0U;
    std::size_t iloc_size = 0U;
    std::size_t meta_size = 0U;
    std::size_t moov_size = 0U;
    std::size_t ftyp_size = 0U;
    std::size_t size = 0U;
    std::uint16_t item_count = (existing_creator_tool != nullptr) ? 2U : 1U;

    append_u32be(exif_payload.data(), &exif_size, 6U);
    append_text(exif_payload.data(), &exif_size, "Exif");
    append_u8(exif_payload.data(), &exif_size, 0U);
    append_u8(exif_payload.data(), &exif_size, 0U);
    append_bytes(exif_payload.data(), &exif_size, tiff.data(), tiff.size());

    exif_off = idat_size;
    append_bytes(idat_payload.data(), &idat_size, exif_payload.data(),
                 exif_size);
    if (existing_creator_tool != nullptr) {
        xmp_off = idat_size;
        append_bytes(idat_payload.data(), &idat_size, xml.data(), xml.size());
    }

    append_fullbox_header(infe_exif.data(), &infe_exif_size, 2U);
    append_u16be(infe_exif.data(), &infe_exif_size, 1U);
    append_u16be(infe_exif.data(), &infe_exif_size, 0U);
    append_u32be(infe_exif.data(), &infe_exif_size,
                 make_fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif.data(), &infe_exif_size, "Exif");
    append_u8(infe_exif.data(), &infe_exif_size, 0U);

    if (existing_creator_tool != nullptr) {
        append_fullbox_header(infe_xmp.data(), &infe_xmp_size, 2U);
        append_u16be(infe_xmp.data(), &infe_xmp_size, 2U);
        append_u16be(infe_xmp.data(), &infe_xmp_size, 0U);
        append_u32be(infe_xmp.data(), &infe_xmp_size,
                     make_fourcc('m', 'i', 'm', 'e'));
        append_text(infe_xmp.data(), &infe_xmp_size, "XMP");
        append_u8(infe_xmp.data(), &infe_xmp_size, 0U);
        append_text(infe_xmp.data(), &infe_xmp_size,
                    "application/rdf+xml");
        append_u8(infe_xmp.data(), &infe_xmp_size, 0U);
        append_u8(infe_xmp.data(), &infe_xmp_size, 0U);
    }

    append_fullbox_header(iinf_payload.data(), &iinf_size, 0U);
    append_u16be(iinf_payload.data(), &iinf_size, item_count);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_exif.data(),
                    infe_exif_size);
    if (existing_creator_tool != nullptr) {
        append_bmff_box(iinf_payload.data(), &iinf_size,
                        make_fourcc('i', 'n', 'f', 'e'),
                        infe_xmp.data(), infe_xmp_size);
    }

    append_fullbox_header(iloc_payload.data(), &iloc_size, 1U);
    append_u8(iloc_payload.data(), &iloc_size, 0x44U);
    append_u8(iloc_payload.data(), &iloc_size, 0x40U);
    append_u16be(iloc_payload.data(), &iloc_size, item_count);

    append_u16be(iloc_payload.data(), &iloc_size, 1U);
    append_u16be(iloc_payload.data(), &iloc_size, 1U);
    append_u16be(iloc_payload.data(), &iloc_size, 0U);
    append_u32be(iloc_payload.data(), &iloc_size, 0U);
    append_u16be(iloc_payload.data(), &iloc_size, 1U);
    append_u32be(iloc_payload.data(), &iloc_size, (std::uint32_t)exif_off);
    append_u32be(iloc_payload.data(), &iloc_size, (std::uint32_t)exif_size);

    if (existing_creator_tool != nullptr) {
        append_u16be(iloc_payload.data(), &iloc_size, 2U);
        append_u16be(iloc_payload.data(), &iloc_size, 1U);
        append_u16be(iloc_payload.data(), &iloc_size, 0U);
        append_u32be(iloc_payload.data(), &iloc_size, 0U);
        append_u16be(iloc_payload.data(), &iloc_size, 1U);
        append_u32be(iloc_payload.data(), &iloc_size, (std::uint32_t)xmp_off);
        append_u32be(iloc_payload.data(), &iloc_size, (std::uint32_t)xml.size());
    }

    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'),
                    iinf_payload.data(), iinf_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'l', 'o', 'c'),
                    iloc_payload.data(), iloc_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'd', 'a', 't'),
                    idat_payload.data(), idat_size);

    append_bmff_box(moov_box.data(), &moov_size, make_fourcc('m', 'o', 'o', 'v'),
                    (const unsigned char*)0, 0U);

    append_u32be(ftyp_payload.data(), &ftyp_size, major_brand);
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    append_bytes(file.data(), &size, moov_box.data(), moov_size);
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_transfer_target_heif_fixture(const char* existing_creator_tool)
{
    return build_transfer_target_bmff_fixture(
        existing_creator_tool, make_fourcc('h', 'e', 'i', 'c'));
}

static ByteVec
build_transfer_target_avif_fixture(const char* existing_creator_tool)
{
    return build_transfer_target_bmff_fixture(
        existing_creator_tool, make_fourcc('a', 'v', 'i', 'f'));
}

static ByteVec
build_transfer_target_jxl_fixture(const char* existing_creator_tool)
{
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 256> exif_payload {};
    const ByteVec tiff = build_transfer_tiff_make_only_fixture();
    const std::string xml = (existing_creator_tool != nullptr)
                                ? build_creator_tool_xmp_xml(
                                      existing_creator_tool)
                                : std::string();
    std::size_t exif_size = 0U;
    std::size_t size = 0U;

    append_u32be(exif_payload.data(), &exif_size, 0U);
    append_bytes(exif_payload.data(), &exif_size, tiff.data(), tiff.size());

    append_u32be(file.data(), &size, 12U);
    append_u32be(file.data(), &size, make_fourcc('J', 'X', 'L', ' '));
    append_u32be(file.data(), &size, 0x0D0A870AU);
    append_bmff_box(file.data(), &size, make_fourcc('E', 'x', 'i', 'f'),
                    exif_payload.data(), exif_size);
    if (existing_creator_tool != nullptr) {
        append_bmff_box(file.data(), &size, make_fourcc('x', 'm', 'l', ' '),
                        (const unsigned char*)xml.data(), xml.size());
    }
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static std::string
make_temp_path(const char* stem, const char* suffix)
{
    static unsigned counter = 0U;
    char path[256];
    const unsigned long long stamp
        = (unsigned long long)std::chrono::steady_clock::now()
              .time_since_epoch()
              .count();

    counter += 1U;
    std::snprintf(path, sizeof(path), "/tmp/%s_%llu_%u%s", stem, stamp,
                  counter, suffix);
    return std::string(path);
}

static bool
write_file_bytes_raw(const std::string& path, const void* data, std::size_t size)
{
    std::FILE* f;

    f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    if (size != 0U && std::fwrite(data, 1U, size, f) != size) {
        std::fclose(f);
        std::remove(path.c_str());
        return false;
    }
    if (std::fclose(f) != 0) {
        std::remove(path.c_str());
        return false;
    }
    return true;
}

static bool
write_file_bytes(const std::string& path, const ByteVec& bytes)
{
    return write_file_bytes_raw(path, bytes.data(), bytes.size());
}

static bool
write_file_bytes(const std::string& path, const std::vector<std::byte>& bytes)
{
    return write_file_bytes_raw(path, bytes.data(), bytes.size());
}

static bool
read_file_bytes(const std::string& path, ByteVec* out)
{
    std::FILE* f;
    long end;

    if (out == nullptr) {
        return false;
    }
    f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        out->clear();
        return false;
    }
    if (std::fseek(f, 0L, SEEK_END) != 0) {
        std::fclose(f);
        out->clear();
        return false;
    }
    end = std::ftell(f);
    if (end < 0L || std::fseek(f, 0L, SEEK_SET) != 0) {
        std::fclose(f);
        out->clear();
        return false;
    }
    out->resize((std::size_t)end);
    if (!out->empty()
        && std::fread(out->data(), 1U, out->size(), f) != out->size()) {
        std::fclose(f);
        out->clear();
        return false;
    }
    if (std::fclose(f) != 0) {
        out->clear();
        return false;
    }
    return true;
}

static void
remove_file_if_present(const std::string& path)
{
    if (!path.empty()) {
        (void)std::remove(path.c_str());
    }
}

static std::string
replace_extension(const std::string& path, const char* replacement)
{
    const std::size_t dot = path.find_last_of('.');

    if (dot == std::string::npos) {
        return path + replacement;
    }
    return path.substr(0U, dot) + replacement;
}

static std::size_t
build_tiff_le_blob(unsigned char* out)
{
    std::size_t size = 0U;

    append_text(out, &size, "II");
    append_u16le(out, &size, 42U);
    append_u32le(out, &size, 8U);
    append_u16le(out, &size, 1U);
    append_u16le(out, &size, 0x010FU);
    append_u16le(out, &size, 2U);
    append_u32le(out, &size, 6U);
    append_u32le(out, &size, 26U);
    append_u32le(out, &size, 0U);
    append_text(out, &size, "Canon");
    append_u8(out, &size, 0U);
    return size;
}

static void
build_test_icc_blob(unsigned char* out, std::size_t size)
{
    std::uint32_t i;

    std::memset(out, 0, size);
    write_u32be_at(out, 0U, (std::uint32_t)size);
    write_u32be_at(out, 4U, make_fourcc('a', 'p', 'p', 'l'));
    write_u32be_at(out, 8U, 0x04300000U);
    write_u32be_at(out, 12U, make_fourcc('m', 'n', 't', 'r'));
    write_u32be_at(out, 16U, make_fourcc('R', 'G', 'B', ' '));
    write_u32be_at(out, 20U, make_fourcc('X', 'Y', 'Z', ' '));
    write_u16be_at(out, 24U, 2026U);
    write_u16be_at(out, 26U, 1U);
    write_u16be_at(out, 28U, 28U);
    out[36] = (unsigned char)'a';
    out[37] = (unsigned char)'c';
    out[38] = (unsigned char)'s';
    out[39] = (unsigned char)'p';
    write_u32be_at(out, 40U, make_fourcc('M', 'S', 'F', 'T'));
    write_u32be_at(out, 44U, 1U);
    write_u32be_at(out, 48U, make_fourcc('A', 'P', 'P', 'L'));
    write_u32be_at(out, 52U, make_fourcc('M', '1', '2', '3'));
    write_u64be_at(out, 56U, 1U);
    write_u32be_at(out, 64U, 1U);
    write_u32be_at(out, 68U, 63189U);
    write_u32be_at(out, 72U, 65536U);
    write_u32be_at(out, 76U, 54061U);
    write_u32be_at(out, 80U, make_fourcc('o', 'p', 'n', 'm'));
    write_u32be_at(out, 128U, 1U);
    write_u32be_at(out, 132U, make_fourcc('d', 'e', 's', 'c'));
    write_u32be_at(out, 136U, 144U);
    write_u32be_at(out, 140U, 16U);
    for (i = 0U; i < 16U; ++i) {
        out[144U + i] = (unsigned char)i;
    }
}

static void
append_irb_resource_blob(unsigned char* out, std::size_t* io_size,
                         std::uint16_t resource_id,
                         const unsigned char* payload,
                         std::size_t payload_size)
{
    append_text(out, io_size, "8BIM");
    append_u16be(out, io_size, resource_id);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, (unsigned char)((payload_size >> 24) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((payload_size >> 16) & 0xFFU));
    append_u8(out, io_size, (unsigned char)((payload_size >> 8) & 0xFFU));
    append_u8(out, io_size, (unsigned char)(payload_size & 0xFFU));
    append_bytes(out, io_size, payload, payload_size);
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static ByteVec
build_jpeg_all_fixture()
{
    static const char xmp[] =
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
        "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
        "xmp:CreatorTool='OpenMeta'/>"
        "</rdf:RDF>"
        "</x:xmpmeta>";
    static const unsigned char iptc[] = {
        0x1CU, 0x02U, 0x19U, 0x00U, 0x04U,
        (unsigned char)'t', (unsigned char)'e', (unsigned char)'s',
        (unsigned char)'t'
    };
    static const unsigned char other_irb[] = { 0x01U, 0x02U, 0x03U };
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 64> tiff {};
    std::array<unsigned char, 160> icc {};
    std::array<unsigned char, 64> irb {};
    std::size_t tiff_size;
    std::size_t irb_size;
    std::size_t size;
    std::uint16_t seg_len;

    tiff_size = build_tiff_le_blob(tiff.data());
    build_test_icc_blob(icc.data(), icc.size());
    irb_size = 0U;
    append_irb_resource_blob(irb.data(), &irb_size, 0x0404U, iptc,
                             sizeof(iptc));
    append_irb_resource_blob(irb.data(), &irb_size, 0x1234U, other_irb,
                             sizeof(other_irb));

    size = 0U;
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD8U);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xE1U);
    seg_len = (std::uint16_t)(2U + 6U + tiff_size);
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "Exif");
    append_u8(file.data(), &size, 0U);
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, tiff.data(), tiff_size);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xE1U);
    seg_len = (std::uint16_t)(2U + 29U + (sizeof(xmp) - 1U));
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "http://ns.adobe.com/xap/1.0/");
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, xmp, sizeof(xmp) - 1U);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xE2U);
    seg_len = (std::uint16_t)(2U + 14U + icc.size());
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "ICC_PROFILE");
    append_u8(file.data(), &size, 0U);
    append_u8(file.data(), &size, 1U);
    append_u8(file.data(), &size, 1U);
    append_bytes(file.data(), &size, icc.data(), icc.size());

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xEDU);
    seg_len = (std::uint16_t)(2U + 14U + irb_size);
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "Photoshop 3.0");
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, irb.data(), irb_size);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD9U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_jpeg_irb_fields_fixture()
{
    std::array<unsigned char, 256> file {};
    std::array<unsigned char, 32> irb {};
    std::array<unsigned char, 8> angle {};
    std::size_t angle_size;
    std::size_t irb_size;
    std::size_t size;
    std::uint16_t seg_len;

    angle_size = 0U;
    append_u32be(angle.data(), &angle_size, 30U);

    irb_size = 0U;
    append_irb_resource_blob(irb.data(), &irb_size, 0x040DU, angle.data(),
                             angle_size);

    size = 0U;
    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD8U);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xEDU);
    seg_len = (std::uint16_t)(2U + 14U + irb_size);
    append_u16be(file.data(), &size, seg_len);
    append_text(file.data(), &size, "Photoshop 3.0");
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, irb.data(), irb_size);

    append_u8(file.data(), &size, 0xFFU);
    append_u8(file.data(), &size, 0xD9U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_png_text_fixture()
{
    static const unsigned char png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 96> text_payload {};
    std::array<unsigned char, 160> itxt_payload {};
    std::size_t text_size;
    std::size_t itxt_size;
    std::size_t size;

    text_size = 0U;
    append_text(text_payload.data(), &text_size, "Author");
    append_u8(text_payload.data(), &text_size, 0U);
    append_text(text_payload.data(), &text_size, "Alice");

    itxt_size = 0U;
    append_text(itxt_payload.data(), &itxt_size, "Description");
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_text(itxt_payload.data(), &itxt_size, "en");
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_text(itxt_payload.data(), &itxt_size, "Beschreibung");
    append_u8(itxt_payload.data(), &itxt_size, 0U);
    append_text(itxt_payload.data(), &itxt_size, "OpenMeta PNG");

    size = 0U;
    append_bytes(file.data(), &size, png_sig, sizeof(png_sig));
    append_png_chunk(file.data(), &size, "tEXt", text_payload.data(),
                     text_size);
    append_png_chunk(file.data(), &size, "iTXt", itxt_payload.data(),
                     itxt_size);
    append_png_chunk(file.data(), &size, "IEND", (const unsigned char*)0, 0U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_jumbf_verify_percent_encoded_jumbf_label_fixture()
{
    static const unsigned char k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    std::array<unsigned char, 1024> cbor_payload {};
    std::array<unsigned char, 1152> cbor_box {};
    std::array<unsigned char, 128> claim_bad_jumb {};
    std::array<unsigned char, 128> claim_good_jumb {};
    std::array<unsigned char, 1536> root_payload {};
    std::array<unsigned char, 1792> jumbf {};
    std::size_t cbor_size;
    std::size_t cbor_box_size;
    std::size_t claim_bad_jumb_size;
    std::size_t claim_good_jumb_size;
    std::size_t root_payload_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "payload");
    append_cbor_text(cbor_payload.data(), &cbor_size, "null");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.bad");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_uri");
    append_cbor_text(
        cbor_payload.data(), &cbor_size,
        "https://example.test/asset?jumbf=%63%32%70%61%2E%63%6C%61%69%6D%2E%67%6F%6F%64");

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    cbor_box_size = 0U;
    append_box(cbor_box.data(), &cbor_box_size, "cbor", cbor_payload.data(),
               cbor_size);
    claim_bad_jumb_size = make_claim_jumb_box(claim_bad_jumb.data(),
                                              "c2pa.claim.bad", k_claim_bad,
                                              sizeof(k_claim_bad));
    claim_good_jumb_size = make_claim_jumb_box(claim_good_jumb.data(),
                                               "c2pa.claim.good", k_claim_good,
                                               sizeof(k_claim_good));

    root_payload_size = 0U;
    append_bytes(root_payload.data(), &root_payload_size, claim_bad_jumb.data(),
                 claim_bad_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_good_jumb.data(), claim_good_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size, cbor_box.data(),
                 cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf.data(), "c2pa",
                                          root_payload.data(),
                                          root_payload_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_detached_payload_resolution_fixture()
{
    static const unsigned char k_claim_bytes[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    std::array<unsigned char, 512> cbor_payload {};
    std::array<unsigned char, 768> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_bytes,
                      sizeof(k_claim_bytes));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "payload");
    append_cbor_text(cbor_payload.data(), &cbor_size, "null");

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_explicit_claim_ref_detached_payload_fixture()
{
    static const unsigned char k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x44U };
    std::array<unsigned char, 768> cbor_payload {};
    std::array<unsigned char, 1024> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 3U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "payload");
    append_cbor_text(cbor_payload.data(), &cbor_size, "null");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 1);

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_explicit_label_detached_payload_fixture()
{
    static const unsigned char k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    std::array<unsigned char, 1024> cbor_payload {};
    std::array<unsigned char, 1152> cbor_box {};
    std::array<unsigned char, 128> claim_bad_jumb {};
    std::array<unsigned char, 128> claim_good_jumb {};
    std::array<unsigned char, 1536> root_payload {};
    std::array<unsigned char, 1792> jumbf {};
    std::size_t cbor_size;
    std::size_t cbor_box_size;
    std::size_t claim_bad_jumb_size;
    std::size_t claim_good_jumb_size;
    std::size_t root_payload_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 3U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "payload");
    append_cbor_text(cbor_payload.data(), &cbor_size, "null");
    append_cbor_text(cbor_payload.data(), &cbor_size, "jumbf_uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.good");

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    cbor_box_size = 0U;
    append_box(cbor_box.data(), &cbor_box_size, "cbor", cbor_payload.data(),
               cbor_size);
    claim_bad_jumb_size = make_claim_jumb_box(claim_bad_jumb.data(),
                                              "c2pa.claim.bad", k_claim_bad,
                                              sizeof(k_claim_bad));
    claim_good_jumb_size = make_claim_jumb_box(claim_good_jumb.data(),
                                               "c2pa.claim.good", k_claim_good,
                                               sizeof(k_claim_good));

    root_payload_size = 0U;
    append_bytes(root_payload.data(), &root_payload_size, claim_bad_jumb.data(),
                 claim_bad_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_good_jumb.data(), claim_good_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size, cbor_box.data(),
                 cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf.data(), "c2pa",
                                          root_payload.data(),
                                          root_payload_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_unresolved_detached_payload_fixture()
{
    static const unsigned char k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim_other[] = { 0xA1U, 0x61U, 'a', 0x77U };
    std::array<unsigned char, 1024> cbor_payload {};
    std::array<unsigned char, 1280> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 3U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "payload");
    append_cbor_text(cbor_payload.data(), &cbor_size, "null");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 999);

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_other,
                      sizeof(k_claim_other));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_percent_encoded_claim_ref_fixture()
{
    static const unsigned char k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    std::array<unsigned char, 2048> cbor_payload {};
    std::array<unsigned char, 2560> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;
    std::uint32_t i;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 12U);

    for (i = 0U; i < 11U; ++i) {
        unsigned char claim_value[4];

        claim_value[0] = 0xA1U;
        claim_value[1] = 0x61U;
        claim_value[2] = (unsigned char)'a';
        claim_value[3] = (unsigned char)(i + 1U);

        append_cbor_map(cbor_payload.data(), &cbor_size,
                        (i == 0U) ? 2U : 1U);
        append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
        append_cbor_bytes(cbor_payload.data(), &cbor_size, claim_value,
                          sizeof(claim_value));
        if (i == 0U) {
            append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
            append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
            append_cbor_map(cbor_payload.data(), &cbor_size, 3U);
            append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
            append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
            append_cbor_text(cbor_payload.data(), &cbor_size, "payload");
            append_cbor_text(cbor_payload.data(), &cbor_size, "null");
            append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref");
            append_cbor_text(
                cbor_payload.data(), &cbor_size,
                "https://example.test/media/%63%6C%61%69%6D%73%5B11%5D");
        }
    }

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_require_resolved_references_unresolved_fixture()
{
    static const unsigned char k_claim0[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim1[] = { 0xA1U, 0x61U, 'a', 0x02U };
    std::array<unsigned char, 512> cbor_payload {};
    std::array<unsigned char, 768> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim0,
                      sizeof(k_claim0));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 99);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim1,
                      sizeof(k_claim1));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 0);

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_require_resolved_references_ambiguous_fixture()
{
    static const unsigned char k_claim0[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim1[] = { 0xA1U, 0x61U, 'a', 0x02U };
    std::array<unsigned char, 512> cbor_payload {};
    std::array<unsigned char, 768> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim0,
                      sizeof(k_claim0));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 3U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "alg");
    append_cbor_text(cbor_payload.data(), &cbor_size, "es256");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 0);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims[1]");

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim1,
                      sizeof(k_claim1));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_scaffold_requested_fixture()
{
    std::array<unsigned char, 256> cbor_payload {};
    std::array<unsigned char, 512> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "signature");
    append_cbor_text(cbor_payload.data(), &cbor_size, "ok");

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_cose_signature_fields_fixture()
{
    static const unsigned char k_cbor_payload[] = {
        0xA1U,
        0x69U, 'm', 'a', 'n', 'i', 'f', 'e', 's', 't', 's',
        0xA1U,
        0x6FU, 'a', 'c', 't', 'i', 'v', 'e', '_', 'm',
        'a', 'n', 'i', 'f', 'e', 's', 't',
        0xA1U,
        0x6AU, 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', 's',
        0x81U,
        0x84U,
        0x43U, 0xA1U, 0x01U, 0x26U,
        0xA3U,
        0x01U, 0x26U,
        0x6EU, 'p', 'u', 'b', 'l', 'i', 'c', '_', 'k', 'e',
        'y', '_', 'd', 'e', 'r',
        0x43U, 0x01U, 0x02U, 0x03U,
        0x6FU, 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a',
        't', 'e', '_', 'd', 'e', 'r',
        0x43U, 0x04U, 0x05U, 0x06U,
        0xF6U,
        0x42U, 0xAAU, 0x55U
    };
    std::array<unsigned char, 384> jumbf {};
    std::size_t jumbf_size;

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), k_cbor_payload,
                                      sizeof(k_cbor_payload));
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_cose_detached_payload_from_claim_bytes_fixture()
{
    static const unsigned char k_payload_bytes[] = { 'a', 'b', 'c' };
    static const unsigned char k_protected_header[] = { 0xA1U, 0x01U, 0x26U };
    static const unsigned char k_empty_bytes[] = { 0x00U };
    static const unsigned char k_raw_sig[64] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    std::array<unsigned char, 512> cbor_payload {};
    std::array<unsigned char, 768> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_payload_bytes,
                      sizeof(k_payload_bytes));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);

    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig,
                      sizeof(k_raw_sig));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_reference_map_entries_fixture()
{
    static const unsigned char k_target_claim[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    static const unsigned char k_bad_claim[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_protected_header[] = { 0xA1U, 0x01U, 0x26U };
    static const unsigned char k_empty_bytes[] = { 0x00U };
    static const unsigned char k_raw_sig[64] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    std::array<unsigned char, 1024> cbor_payload {};
    std::array<unsigned char, 1152> cbor_box {};
    std::array<unsigned char, 128> claim_bad_jumb {};
    std::array<unsigned char, 128> claim_good_jumb {};
    std::array<unsigned char, 1536> root_payload {};
    std::array<unsigned char, 1792> jumbf {};
    std::size_t cbor_size;
    std::size_t cbor_box_size;
    std::size_t claim_bad_jumb_size;
    std::size_t claim_good_jumb_size;
    std::size_t root_payload_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_bad_claim,
                      sizeof(k_bad_claim));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "references");
    append_cbor_array(cbor_payload.data(), &cbor_size, 3U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 1);

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.bad");

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.good");

    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig,
                      sizeof(k_raw_sig));

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_target_claim,
                      sizeof(k_target_claim));

    cbor_box_size = 0U;
    append_box(cbor_box.data(), &cbor_box_size, "cbor", cbor_payload.data(),
               cbor_size);
    claim_bad_jumb_size = make_claim_jumb_box(claim_bad_jumb.data(),
                                              "c2pa.claim.bad", k_bad_claim,
                                              sizeof(k_bad_claim));
    claim_good_jumb_size = make_claim_jumb_box(claim_good_jumb.data(),
                                               "c2pa.claim.good",
                                               k_target_claim,
                                               sizeof(k_target_claim));

    root_payload_size = 0U;
    append_bytes(root_payload.data(), &root_payload_size, claim_bad_jumb.data(),
                 claim_bad_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_good_jumb.data(), claim_good_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size, cbor_box.data(),
                 cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf.data(), "c2pa",
                                          root_payload.data(),
                                          root_payload_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_reference_map_claims_array_fixture()
{
    static const unsigned char k_target_claim[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    static const unsigned char k_bad_claim[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_protected_header[] = { 0xA1U, 0x01U, 0x26U };
    static const unsigned char k_empty_bytes[] = { 0x00U };
    static const unsigned char k_raw_sig[64] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    std::array<unsigned char, 768> cbor_payload {};
    std::array<unsigned char, 1024> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_bad_claim,
                      sizeof(k_bad_claim));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "references");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_i64(cbor_payload.data(), &cbor_size, 1);
    append_cbor_i64(cbor_payload.data(), &cbor_size, 999);

    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig,
                      sizeof(k_raw_sig));

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_target_claim,
                      sizeof(k_target_claim));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_reference_map_index_uri_fixture()
{
    static const unsigned char k_target_claim[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    static const unsigned char k_bad_claim[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_protected_header[] = { 0xA1U, 0x01U, 0x26U };
    static const unsigned char k_empty_bytes[] = { 0x00U };
    static const unsigned char k_raw_sig[64] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    std::array<unsigned char, 768> cbor_payload {};
    std::array<unsigned char, 1024> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_bad_claim,
                      sizeof(k_bad_claim));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "reference");
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "index");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 1);
    append_cbor_text(cbor_payload.data(), &cbor_size, "uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.missing");

    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig,
                      sizeof(k_raw_sig));

    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_target_claim,
                      sizeof(k_target_claim));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_multiclaim_multisig_query_index_fixture()
{
    static const unsigned char k_claim0[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    static const unsigned char k_claim1[] = { 0xA1U, 0x61U, 'b', 0x2BU };
    static const unsigned char k_protected_header[] = { 0xA1U, 0x01U, 0x26U };
    static const unsigned char k_empty_bytes[] = { 0x00U };
    static const unsigned char k_raw_sig0[64] = {
        0x10U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    static const unsigned char k_raw_sig1[64] = {
        0x20U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    std::array<unsigned char, 1536> cbor_payload {};
    std::array<unsigned char, 2048> jumbf {};
    std::size_t cbor_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim0,
                      sizeof(k_claim0));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 3U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim-uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/a?claim-index=1&jumbf="
                     "c2pa.claim.missing0");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim-reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.missing0");
    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig0,
                      sizeof(k_raw_sig0));

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim1,
                      sizeof(k_claim1));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 3U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim-uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/b?claim_ref=0&jumbf="
                     "c2pa.claim.missing1");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim-reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.missing1");
    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig1,
                      sizeof(k_raw_sig1));

    jumbf_size = make_jumbf_with_cbor(jumbf.data(), cbor_payload.data(),
                                      cbor_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_multiclaim_multisig_nested_refs_fixture()
{
    static const unsigned char k_claim0[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    static const unsigned char k_claim1[] = { 0xA1U, 0x61U, 'b', 0x2BU };
    static const unsigned char k_claim0_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim1_bad[] = { 0xA1U, 0x61U, 'b', 0x02U };
    static const unsigned char k_protected_header[] = { 0xA1U, 0x01U, 0x26U };
    static const unsigned char k_empty_bytes[] = { 0x00U };
    static const unsigned char k_raw_sig0[64] = {
        0x30U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    static const unsigned char k_raw_sig1[64] = {
        0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    std::array<unsigned char, 2048> cbor_payload {};
    std::array<unsigned char, 2304> cbor_box {};
    std::array<unsigned char, 128> claim_bad0_jumb {};
    std::array<unsigned char, 128> claim_bad1_jumb {};
    std::array<unsigned char, 128> claim_good0_jumb {};
    std::array<unsigned char, 128> claim_good1_jumb {};
    std::array<unsigned char, 3072> root_payload {};
    std::array<unsigned char, 3328> jumbf {};
    std::size_t cbor_size;
    std::size_t cbor_box_size;
    std::size_t claim_bad0_jumb_size;
    std::size_t claim_bad1_jumb_size;
    std::size_t claim_good0_jumb_size;
    std::size_t claim_good1_jumb_size;
    std::size_t root_payload_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim0_bad,
                      sizeof(k_claim0_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "references");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/a?claim-index=1");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.bad0");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/a?jumbf=c2pa.claim.bad0");
    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig0,
                      sizeof(k_raw_sig0));

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim1_bad,
                      sizeof(k_claim1_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "references");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim-uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/b?claim_ref=0");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim-reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.bad1");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/b?jumbf=c2pa.claim.bad1");
    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig1,
                      sizeof(k_raw_sig1));

    cbor_box_size = 0U;
    append_box(cbor_box.data(), &cbor_box_size, "cbor", cbor_payload.data(),
               cbor_size);
    claim_bad0_jumb_size = make_claim_jumb_box(claim_bad0_jumb.data(),
                                               "c2pa.claim.bad0",
                                               k_claim0_bad,
                                               sizeof(k_claim0_bad));
    claim_bad1_jumb_size = make_claim_jumb_box(claim_bad1_jumb.data(),
                                               "c2pa.claim.bad1",
                                               k_claim1_bad,
                                               sizeof(k_claim1_bad));
    claim_good0_jumb_size = make_claim_jumb_box(claim_good0_jumb.data(),
                                                "c2pa.claim.good0", k_claim0,
                                                sizeof(k_claim0));
    claim_good1_jumb_size = make_claim_jumb_box(claim_good1_jumb.data(),
                                                "c2pa.claim.good1", k_claim1,
                                                sizeof(k_claim1));

    root_payload_size = 0U;
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_bad0_jumb.data(), claim_bad0_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_bad1_jumb.data(), claim_bad1_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_good0_jumb.data(), claim_good0_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_good1_jumb.data(), claim_good1_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size, cbor_box.data(),
                 cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf.data(), "c2pa",
                                          root_payload.data(),
                                          root_payload_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_jumbf_verify_multiclaim_multisig_nested_idrefs_fixture()
{
    static const unsigned char k_claim0[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    static const unsigned char k_claim1[] = { 0xA1U, 0x61U, 'b', 0x2BU };
    static const unsigned char k_claim0_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const unsigned char k_claim1_bad[] = { 0xA1U, 0x61U, 'b', 0x02U };
    static const unsigned char k_protected_header[] = { 0xA1U, 0x01U, 0x26U };
    static const unsigned char k_empty_bytes[] = { 0x00U };
    static const unsigned char k_raw_sig0[64] = {
        0x50U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    static const unsigned char k_raw_sig1[64] = {
        0x60U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    std::array<unsigned char, 2304> cbor_payload {};
    std::array<unsigned char, 2560> cbor_box {};
    std::array<unsigned char, 128> claim_bad0_jumb {};
    std::array<unsigned char, 128> claim_bad1_jumb {};
    std::array<unsigned char, 128> claim_good0_jumb {};
    std::array<unsigned char, 128> claim_good1_jumb {};
    std::array<unsigned char, 3328> root_payload {};
    std::array<unsigned char, 3584> jumbf {};
    std::size_t cbor_size;
    std::size_t cbor_box_size;
    std::size_t claim_bad0_jumb_size;
    std::size_t claim_bad1_jumb_size;
    std::size_t claim_good0_jumb_size;
    std::size_t claim_good1_jumb_size;
    std::size_t root_payload_size;
    std::size_t jumbf_size;

    cbor_size = 0U;
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "manifests");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claims");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim0_bad,
                      sizeof(k_claim0_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "references");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 1);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.bad0");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "reference");
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_id");
    append_cbor_text(cbor_payload.data(), &cbor_size, "1");
    append_cbor_text(cbor_payload.data(), &cbor_size, "uri");
    append_cbor_text(cbor_payload.data(), &cbor_size,
                     "https://example.test/a?jumbf=c2pa.claim.bad0");
    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig0,
                      sizeof(k_raw_sig0));

    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_claim1_bad,
                      sizeof(k_claim1_bad));
    append_cbor_text(cbor_payload.data(), &cbor_size, "signatures");
    append_cbor_array(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_array(cbor_payload.data(), &cbor_size, 4U);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_protected_header,
                      sizeof(k_protected_header));
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "public_key_der");
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_empty_bytes, 0U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "references");
    append_cbor_array(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_map(cbor_payload.data(), &cbor_size, 2U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim-reference-id");
    append_cbor_text(cbor_payload.data(), &cbor_size, "0");
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload.data(), &cbor_size, "c2pa.claim.bad1");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "reference");
    append_cbor_map(cbor_payload.data(), &cbor_size, 1U);
    append_cbor_text(cbor_payload.data(), &cbor_size, "claim_id");
    append_cbor_i64(cbor_payload.data(), &cbor_size, 0);
    append_cbor_null(cbor_payload.data(), &cbor_size);
    append_cbor_bytes(cbor_payload.data(), &cbor_size, k_raw_sig1,
                      sizeof(k_raw_sig1));

    cbor_box_size = 0U;
    append_box(cbor_box.data(), &cbor_box_size, "cbor", cbor_payload.data(),
               cbor_size);
    claim_bad0_jumb_size = make_claim_jumb_box(claim_bad0_jumb.data(),
                                               "c2pa.claim.bad0",
                                               k_claim0_bad,
                                               sizeof(k_claim0_bad));
    claim_bad1_jumb_size = make_claim_jumb_box(claim_bad1_jumb.data(),
                                               "c2pa.claim.bad1",
                                               k_claim1_bad,
                                               sizeof(k_claim1_bad));
    claim_good0_jumb_size = make_claim_jumb_box(claim_good0_jumb.data(),
                                                "c2pa.claim.good0", k_claim0,
                                                sizeof(k_claim0));
    claim_good1_jumb_size = make_claim_jumb_box(claim_good1_jumb.data(),
                                                "c2pa.claim.good1", k_claim1,
                                                sizeof(k_claim1));

    root_payload_size = 0U;
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_bad0_jumb.data(), claim_bad0_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_bad1_jumb.data(), claim_bad1_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_good0_jumb.data(), claim_good0_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size,
                 claim_good1_jumb.data(), claim_good1_jumb_size);
    append_bytes(root_payload.data(), &root_payload_size, cbor_box.data(),
                 cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf.data(), "c2pa",
                                          root_payload.data(),
                                          root_payload_size);
    return ByteVec(jumbf.begin(), jumbf.begin() + (std::ptrdiff_t)jumbf_size);
}

static ByteVec
build_tiff_fuji_makernote_fixture()
{
    std::array<unsigned char, 128> file {};
    std::array<unsigned char, 64> makernote {};
    std::size_t maker_size;
    std::size_t size;

    maker_size = 0U;
    append_bytes(makernote.data(), &maker_size, "FUJIFILM", 8U);
    append_u32le(makernote.data(), &maker_size, 12U);
    append_u16le(makernote.data(), &maker_size, 1U);
    append_u16le(makernote.data(), &maker_size, 0x0001U);
    append_u16le(makernote.data(), &maker_size, 3U);
    append_u32le(makernote.data(), &maker_size, 1U);
    append_u32le(makernote.data(), &maker_size, 0x00000042U);
    append_u32le(makernote.data(), &maker_size, 0U);

    size = 0U;
    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0x927CU);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, (std::uint32_t)maker_size);
    append_u32le(file.data(), &size, 26U);
    append_u32le(file.data(), &size, 0U);
    append_bytes(file.data(), &size, makernote.data(), maker_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_tiff_with_make_makernote_fixture(const char* make,
                                       const unsigned char* makernote,
                                       std::size_t makernote_size);

static ByteVec
build_tiff_with_make_model_makernote_fixture(const char* make,
                                             const char* model,
                                             const unsigned char* makernote,
                                             std::size_t makernote_size);

static ByteVec
build_tiff_casio_makernote_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "QVC\0", 4U);
    append_u32be(makernote.data(), &size, 1U);
    append_u16be(makernote.data(), &size, 0x0002U);
    append_u16be(makernote.data(), &size, 3U);
    append_u32be(makernote.data(), &size, 2U);
    append_u16be(makernote.data(), &size, 320U);
    append_u16be(makernote.data(), &size, 240U);

    return build_tiff_with_make_makernote_fixture("CASIO", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_casio_legacy_makernote_fixture()
{
    std::array<unsigned char, 96> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "QVC\0", 4U);
    append_u32be(makernote.data(), &size, 3U);

    append_u16be(makernote.data(), &size, 0x0002U);
    append_u16be(makernote.data(), &size, 3U);
    append_u32be(makernote.data(), &size, 1U);
    append_u16be(makernote.data(), &size, 3U);
    append_u16be(makernote.data(), &size, 0U);

    append_u16be(makernote.data(), &size, 0x0008U);
    append_u16be(makernote.data(), &size, 3U);
    append_u32be(makernote.data(), &size, 1U);
    append_u16be(makernote.data(), &size, 1U);
    append_u16be(makernote.data(), &size, 0U);

    append_u16be(makernote.data(), &size, 0x0E00U);
    append_u16be(makernote.data(), &size, 7U);
    append_u32be(makernote.data(), &size, 4U);
    append_u32be(makernote.data(), &size, 0x01020304U);

    return build_tiff_with_make_makernote_fixture("CASIO", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_casio_legacy_ifd_makernote_fixture()
{
    std::array<unsigned char, 96> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 3U);

    append_u16le(makernote.data(), &size, 0x0002U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0008U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0E00U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 0x04030201U);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_model_makernote_fixture(
        "CASIO", "QV-2100", makernote.data(), size);
}

static ByteVec
build_tiff_casio_faceinfo2_makernote_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::array<unsigned char, 0x54> payload {};
    ByteVec tiff;
    std::size_t size;
    std::uint32_t payload_off;

    size = 0U;
    append_bytes(makernote.data(), &size, "DCI\0", 4U);
    append_u32be(makernote.data(), &size, 1U);
    append_u16be(makernote.data(), &size, 0x2089U);
    append_u16be(makernote.data(), &size, 7U);
    append_u32be(makernote.data(), &size, 0x54U);
    append_u32be(makernote.data(), &size, 0U);

    payload_off = 38U + (std::uint32_t)std::strlen("CASIO") + 1U
                  + (std::uint32_t)size;
    write_u32be_at(makernote.data(), 16U, payload_off);

    payload.fill(0U);
    payload[0] = 0x02U;
    payload[1] = 0x01U;
    payload[2] = 2U;
    write_u16le_at(payload.data(), 0x0004U, 640U);
    write_u16le_at(payload.data(), 0x0006U, 480U);
    payload[0x0008U] = 7U;
    write_u16le_at(payload.data(), 0x0018U, 10U);
    write_u16le_at(payload.data(), 0x001AU, 20U);
    write_u16le_at(payload.data(), 0x001CU, 30U);
    write_u16le_at(payload.data(), 0x001EU, 40U);

    tiff = build_tiff_with_make_makernote_fixture("CASIO", makernote.data(),
                                                  size);
    tiff.insert(tiff.end(), payload.begin(), payload.end());
    return tiff;
}

static ByteVec
build_tiff_samsung_stmn_makernote_fixture()
{
    std::array<unsigned char, 96> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "STMN100", 7U);
    append_u8(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0x12345678U);
    append_u32le(makernote.data(), &size, 0x00010002U);
    while (size < 44U) {
        append_u8(makernote.data(), &size, 0U);
    }
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0004U);
    append_u16le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 6U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, "HELLO", 5U);
    append_u8(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("SAMSUNG", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_samsung_type2_makernote_fixture()
{
    std::array<unsigned char, 96> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 2U);

    append_u16le(makernote.data(), &size, 0x0004U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_bytes(makernote.data(), &size, "ABCD", 4U);

    append_u16le(makernote.data(), &size, 0x0021U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 10U);
    append_u32le(makernote.data(), &size, 30U);

    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 4U);
    append_u16le(makernote.data(), &size, 5U);

    return build_tiff_with_make_makernote_fixture("SAMSUNG", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_samsung_type2_u16_picturewizard_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0021U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 16U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 11U);
    append_u16le(makernote.data(), &size, 22U);
    append_u16le(makernote.data(), &size, 33U);
    append_u16le(makernote.data(), &size, 44U);
    append_u16le(makernote.data(), &size, 55U);

    return build_tiff_with_make_makernote_fixture("SAMSUNG", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_samsung_compat_digits_fixture()
{
    std::array<unsigned char, 16> makernote {};

    makernote.fill(0U);
    makernote[0] = (unsigned char)'B';
    makernote[1] = (unsigned char)'A';
    makernote[2] = (unsigned char)'D';
    makernote[3] = (unsigned char)'!';
    makernote[10] = (unsigned char)'2';
    makernote[11] = (unsigned char)'0';
    makernote[12] = (unsigned char)'2';
    makernote[13] = (unsigned char)'4';

    return build_tiff_with_make_makernote_fixture("SAMSUNG", makernote.data(),
                                                  makernote.size());
}

static ByteVec
build_tiff_samsung_type2_a002_a003_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 2U);

    append_u16le(makernote.data(), &size, 0xA002U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x1234U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0xA003U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x5678U);
    append_u16le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);
    return build_tiff_with_make_makernote_fixture("SAMSUNG", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_minolta_makernote_fixture()
{
    std::array<unsigned char, 32> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 0U);
    return build_tiff_with_make_makernote_fixture("KONICA MINOLTA",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_minolta_binary_makernote_fixture()
{
    std::array<unsigned char, 96> makernote {};
    std::size_t size;
    std::uint32_t cs_off;
    std::uint32_t cs7d_off;
    std::uint32_t cs5d_off;

    size = 0U;
    append_u16le(makernote.data(), &size, 3U);
    cs_off = 2U + (3U * 12U) + 4U;
    cs7d_off = cs_off + 16U;
    cs5d_off = cs7d_off + 8U;

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, cs_off);

    append_u16le(makernote.data(), &size, 0x0004U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, cs7d_off);

    append_u16le(makernote.data(), &size, 0x0114U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    append_u32le(makernote.data(), &size, cs5d_off);

    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 0x12345678U);
    append_u32le(makernote.data(), &size, 9U);
    append_u16le(makernote.data(), &size, 0x1111U);
    append_u16le(makernote.data(), &size, 0x2222U);
    append_u16le(makernote.data(), &size, 0x3333U);
    append_u16le(makernote.data(), &size, 0x4444U);
    append_u16be(makernote.data(), &size, 0x0102U);
    append_u16be(makernote.data(), &size, 0x0304U);
    append_u16be(makernote.data(), &size, 0x0506U);
    append_u16be(makernote.data(), &size, 0x0708U);

    return build_tiff_with_make_makernote_fixture("KONICA MINOLTA",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_motorola_legacy_makernote_fixture()
{
    std::array<unsigned char, 32> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x6420U);
    append_u16le(makernote.data(), &size, 9U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    return build_tiff_with_make_model_makernote_fixture(
        "Motorola", "XT1060", makernote.data(), size);
}

static ByteVec
build_tiff_motorola_modern_makernote_fixture()
{
    std::array<unsigned char, 32> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x6420U);
    append_u16le(makernote.data(), &size, 9U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    return build_tiff_with_make_model_makernote_fixture(
        "Motorola", "motorola edge 50 neo", makernote.data(), size);
}

static ByteVec
build_tiff_pentax_binary_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;
    std::uint32_t aeinfo_off;
    std::uint32_t shot_off;
    std::uint8_t i;

    size = 0U;
    append_bytes(makernote.data(), &size, "AOC", 3U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 4U);

    aeinfo_off = 8U + (4U * 12U) + 4U;
    shot_off = aeinfo_off + 21U;

    append_u16le(makernote.data(), &size, 0x005CU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 0x44332211U);

    append_u16le(makernote.data(), &size, 0x0060U);
    append_u16le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 3U);

    append_u16le(makernote.data(), &size, 0x0206U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 21U);
    append_u32le(makernote.data(), &size, aeinfo_off);

    append_u16le(makernote.data(), &size, 0x0226U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 6U);
    append_u32le(makernote.data(), &size, shot_off);

    append_u32le(makernote.data(), &size, 0U);
    for (i = 0U; i < 21U; ++i) {
        append_u8(makernote.data(), &size, (std::uint8_t)(i + 1U));
    }
    for (i = 0U; i < 6U; ++i) {
        append_u8(makernote.data(), &size, (std::uint8_t)(0xA0U + i));
    }

    return build_tiff_with_make_makernote_fixture("PENTAX", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_pentax_type2_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "AOC", 3U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 4U);

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0004U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0005U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 7U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x1000U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x1234U);
    append_u16le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);
    return build_tiff_with_make_model_makernote_fixture(
        "PENTAX", "PENTAX Optio 330", makernote.data(), size);
}

static ByteVec
build_tiff_pentax_placeholder_makernote_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "AOC", 3U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 1U);

    append_u16le(makernote.data(), &size, 0x0062U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 7U);
    append_u16le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);
    return build_tiff_with_make_makernote_fixture("EASTMAN KODAK COMPANY",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_pentax_zero_faces_makernote_fixture()
{
    std::array<unsigned char, 96> makernote {};
    std::size_t size;
    std::uint32_t facepos_off;
    std::uint32_t facesize_off;

    size = 0U;
    append_bytes(makernote.data(), &size, "AOC", 3U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 3U);

    facepos_off = 8U + (3U * 12U) + 4U;
    facesize_off = facepos_off + 4U;

    append_u16le(makernote.data(), &size, 0x0060U);
    append_u16le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0227U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, facepos_off);

    append_u16le(makernote.data(), &size, 0x0228U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, facesize_off);

    append_u32le(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0x10U);
    append_u8(makernote.data(), &size, 0x11U);
    append_u8(makernote.data(), &size, 0x12U);
    append_u8(makernote.data(), &size, 0x13U);
    append_u8(makernote.data(), &size, 0x20U);
    append_u8(makernote.data(), &size, 0x21U);
    append_u8(makernote.data(), &size, 0x22U);
    append_u8(makernote.data(), &size, 0x23U);

    return build_tiff_with_make_makernote_fixture("PENTAX", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_ricoh_type2_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;
    std::uint32_t model_off_pos;
    std::uint32_t make_off_pos;
    std::uint32_t model_off;
    std::uint32_t make_off;

    size = 0U;
    append_text(makernote.data(), &size, "RICOH");
    append_u8(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0207U);
    append_u16le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 6U);
    model_off_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0300U);
    append_u16le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 6U);
    make_off_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    model_off = (std::uint32_t)size;
    write_u32le_at(makernote.data(), model_off_pos, model_off);
    append_text(makernote.data(), &size, "GRIII");
    append_u8(makernote.data(), &size, 0U);

    make_off = (std::uint32_t)size;
    write_u32le_at(makernote.data(), make_off_pos, make_off);
    append_text(makernote.data(), &size, "RICOH");
    append_u8(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("RICOH", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_ricoh_padded_type2_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;
    std::uint32_t model_off_pos;
    std::uint32_t make_off_pos;
    std::uint32_t model_off;
    std::uint32_t make_off;

    size = 0U;
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);

    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0104U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 1U);

    model_off_pos = (std::uint32_t)size + 8U;
    append_u16le(makernote.data(), &size, 0x0207U);
    append_u16le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 5U);
    append_u32le(makernote.data(), &size, 0U);

    make_off_pos = (std::uint32_t)size + 8U;
    append_u16le(makernote.data(), &size, 0x0300U);
    append_u16le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 6U);
    append_u32le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    model_off = (std::uint32_t)size;
    write_u32le_at(makernote.data(), model_off_pos, model_off);
    append_text(makernote.data(), &size, "HZ15");
    append_u8(makernote.data(), &size, 0U);

    make_off = (std::uint32_t)size;
    write_u32le_at(makernote.data(), make_off_pos, make_off);
    append_text(makernote.data(), &size, "RICOH");
    append_u8(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("RICOH", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_ricoh_native_makernote_fixture()
{
    static const unsigned char imageinfo_bytes[4] = { 1U, 2U, 3U, 4U };
    static const char subdir_hdr[] = "[Ricoh Camera Info]";
    std::array<unsigned char, 256> makernote {};
    std::array<unsigned char, 32> theta_ifd {};
    ByteVec tiff;
    std::size_t size;
    std::uint32_t imageinfo_off;
    std::uint32_t subdir_off;
    std::uint32_t subdir_size;
    std::uint32_t theta_abs_off;
    std::size_t theta_size;

    size = 0U;
    append_text(makernote.data(), &size, "Ricoh");
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 4U);

    imageinfo_off = 8U + 2U + (4U * 12U) + 4U;
    subdir_size = 20U + 2U + 12U + 4U;
    subdir_off = imageinfo_off + (std::uint32_t)sizeof(imageinfo_bytes);
    theta_abs_off = 44U + (std::uint32_t)(subdir_off + subdir_size);

    append_u16le(makernote.data(), &size, 0x1001U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size,
                 (std::uint32_t)sizeof(imageinfo_bytes));
    append_u32le(makernote.data(), &size, imageinfo_off);

    append_u16le(makernote.data(), &size, 0x1002U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 7U);

    append_u16le(makernote.data(), &size, 0x1003U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x4001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, theta_abs_off);

    append_u32le(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, imageinfo_bytes,
                 sizeof(imageinfo_bytes));

    append_text(makernote.data(), &size, subdir_hdr);
    append_u8(makernote.data(), &size, 0U);
    append_u16be(makernote.data(), &size, 1U);
    append_u16be(makernote.data(), &size, 0x0007U);
    append_u16be(makernote.data(), &size, 4U);
    append_u32be(makernote.data(), &size, 1U);
    append_u32be(makernote.data(), &size, 5U);
    append_u32be(makernote.data(), &size, 0U);

    theta_size = 0U;
    append_u16le(theta_ifd.data(), &theta_size, 1U);
    append_u16le(theta_ifd.data(), &theta_size, 0x0003U);
    append_u16le(theta_ifd.data(), &theta_size, 3U);
    append_u32le(theta_ifd.data(), &theta_size, 1U);
    append_u16le(theta_ifd.data(), &theta_size, 9U);
    append_u16le(theta_ifd.data(), &theta_size, 0U);
    append_u32le(theta_ifd.data(), &theta_size, 0U);

    tiff = build_tiff_with_make_makernote_fixture("RICOH", makernote.data(),
                                                  size);
    tiff.insert(tiff.end(), theta_ifd.begin(),
                theta_ifd.begin() + (std::ptrdiff_t)theta_size);
    return tiff;
}

static ByteVec
build_tiff_panasonic_makernote_fixture(bool truncated_next_ifd)
{
    std::array<unsigned char, 160> makernote {};
    ByteVec tiff;
    std::size_t size;
    std::uint32_t maker_off;
    std::uint32_t facedet_abs_off;
    std::uint32_t facerec_abs_off;
    std::uint32_t time_abs_off;
    static const unsigned char facedet_raw[10] = {
        1U, 0U, 10U, 0U, 20U, 0U, 30U, 0U, 40U, 0U
    };
    static const unsigned char facerec_raw[52] = {
        1U, 0U, 0U, 0U,
        'B', 'o', 'b', 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        1U, 0U, 2U, 0U, 3U, 0U, 4U, 0U,
        '2', '5', 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };

    maker_off = 38U + (std::uint32_t)std::strlen("Panasonic") + 1U;
    facedet_abs_off = maker_off + (truncated_next_ifd ? 38U : 42U);
    facerec_abs_off = facedet_abs_off + 10U;
    time_abs_off = facerec_abs_off + 52U;

    size = 0U;
    append_u16le(makernote.data(), &size, 3U);

    append_u16le(makernote.data(), &size, 0x004EU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 49U);
    append_u32le(makernote.data(), &size, facedet_abs_off);

    append_u16le(makernote.data(), &size, 0x0061U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 52U);
    append_u32le(makernote.data(), &size, facerec_abs_off);

    append_u16le(makernote.data(), &size, 0x2003U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 20U);
    append_u32le(makernote.data(), &size, time_abs_off);

    if (!truncated_next_ifd) {
        append_u32le(makernote.data(), &size, 0U);
    }

    append_bytes(makernote.data(), &size, facedet_raw, sizeof(facedet_raw));
    append_bytes(makernote.data(), &size, facerec_raw, sizeof(facerec_raw));
    for (std::size_t i = 0; i < 16U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    append_u32le(makernote.data(), &size, 123U);

    tiff = build_tiff_with_make_makernote_fixture("Panasonic", makernote.data(),
                                                  size);
    return tiff;
}

static ByteVec
build_tiff_panasonic_type2_fixture()
{
    std::array<unsigned char, 16> makernote {};
    std::size_t size;

    size = 0U;
    append_text(makernote.data(), &size, "ABCD");
    append_u16le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 42U);

    return build_tiff_with_make_makernote_fixture("Panasonic", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_panasonic_extended_makernote_fixture(bool truncated_next_ifd)
{
    static const std::uint16_t k_face_offsets[5] = {
        0x0001U, 0x0005U, 0x0009U, 0x000DU, 0x0011U
    };
    static const std::uint16_t k_face_data[5][4] = {
        { 10U, 20U, 30U, 40U },     { 50U, 60U, 70U, 80U },
        { 90U, 100U, 110U, 120U },  { 130U, 140U, 150U, 160U },
        { 170U, 180U, 190U, 200U }
    };
    static const char* k_names[3] = { "Bob", "Ana", "Eve" };
    static const char* k_ages[3] = { "25", "30", "19" };
    static const std::uint16_t k_pos[3][4] = {
        { 1U, 2U, 3U, 4U }, { 5U, 6U, 7U, 8U }, { 9U, 10U, 11U, 12U }
    };
    std::array<unsigned char, 320> makernote {};
    std::array<unsigned char, 42> facedet {};
    std::array<unsigned char, 148> facerec {};
    std::array<unsigned char, 20> timeinfo {};
    ByteVec tiff;
    std::size_t size;
    std::uint32_t maker_off;
    std::uint32_t facedet_abs_off;
    std::uint32_t facerec_abs_off;
    std::uint32_t time_abs_off;
    std::uint32_t i;
    std::uint32_t j;

    facedet.fill(0U);
    facerec.fill(0U);
    timeinfo.fill(0xFFU);

    write_u16le_at(facedet.data(), 0U, 5U);
    for (i = 0U; i < 5U; ++i) {
        const std::size_t byte_off = (std::size_t)k_face_offsets[i] * 2U;
        for (j = 0U; j < 4U; ++j) {
            write_u16le_at(facedet.data(),
                           (std::uint32_t)(byte_off + (std::size_t)j * 2U),
                           k_face_data[i][j]);
        }
    }

    write_u16le_at(facerec.data(), 0U, 3U);
    for (i = 0U; i < 3U; ++i) {
        const std::size_t name_off = 4U + (std::size_t)i * 48U;
        const std::size_t pos_off = 24U + (std::size_t)i * 48U;
        const std::size_t age_off = 32U + (std::size_t)i * 48U;

        std::memcpy(facerec.data() + name_off, k_names[i], std::strlen(k_names[i]));
        for (j = 0U; j < 4U; ++j) {
            write_u16le_at(facerec.data(),
                           (std::uint32_t)(pos_off + (std::size_t)j * 2U),
                           k_pos[i][j]);
        }
        std::memcpy(facerec.data() + age_off, k_ages[i], std::strlen(k_ages[i]));
    }

    timeinfo[0] = 0x20U;
    timeinfo[1] = 0x24U;
    timeinfo[2] = 0x06U;
    timeinfo[3] = 0x27U;
    timeinfo[4] = 0x12U;
    timeinfo[5] = 0x53U;
    timeinfo[6] = 0x52U;
    timeinfo[7] = 0x54U;
    write_u32le_at(timeinfo.data(), 16U, 321U);

    maker_off = 38U + (std::uint32_t)std::strlen("Panasonic") + 1U;
    facedet_abs_off = maker_off + (truncated_next_ifd ? 38U : 42U);
    facerec_abs_off = facedet_abs_off + 42U;
    time_abs_off = facerec_abs_off + 148U;

    size = 0U;
    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 0x004EU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)facedet.size());
    append_u32le(makernote.data(), &size, facedet_abs_off);

    append_u16le(makernote.data(), &size, 0x0061U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)facerec.size());
    append_u32le(makernote.data(), &size, facerec_abs_off);

    append_u16le(makernote.data(), &size, 0x2003U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)timeinfo.size());
    append_u32le(makernote.data(), &size, time_abs_off);

    if (!truncated_next_ifd) {
        append_u32le(makernote.data(), &size, 0U);
    }

    append_bytes(makernote.data(), &size, facedet.data(), facedet.size());
    append_bytes(makernote.data(), &size, facerec.data(), facerec.size());
    append_bytes(makernote.data(), &size, timeinfo.data(), timeinfo.size());

    tiff = build_tiff_with_make_makernote_fixture("Panasonic", makernote.data(),
                                                  size);
    return tiff;
}

static ByteVec
build_tiff_olympus_signature_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;
    std::uint32_t sub_ifd_off;

    size = 0U;
    append_text(makernote.data(), &size, "OLYMPUS");
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 3U);

    append_u16le(makernote.data(), &size, 1U);
    sub_ifd_off = 12U + 18U;

    append_u16le(makernote.data(), &size, 0x4000U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, sub_ifd_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0201U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("OLYMPUS",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_olympus_omsystem_nested_fixture()
{
    std::array<unsigned char, 256> makernote {};
    std::size_t size;
    std::uint32_t main_ifd_off;
    std::uint32_t main_ifd_size;
    std::uint32_t equipment_ifd_off;
    std::uint32_t equipment_ifd_size;
    std::uint32_t camera_ifd_off;
    std::uint32_t camera_ifd_size;
    std::uint32_t aftarget_ifd_off;
    std::uint32_t subjectdetect_ifd_off;

    main_ifd_off = 16U;
    main_ifd_size = 2U + (2U * 12U) + 4U;
    equipment_ifd_off = main_ifd_off + main_ifd_size;
    equipment_ifd_size = 18U;
    camera_ifd_off = equipment_ifd_off + equipment_ifd_size;
    camera_ifd_size = 2U + (3U * 12U) + 4U;
    aftarget_ifd_off = camera_ifd_off + camera_ifd_size;
    subjectdetect_ifd_off = aftarget_ifd_off + 18U;

    size = 0U;
    append_text(makernote.data(), &size, "OM SYSTEM");
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 3U);

    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0x2010U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, equipment_ifd_off);
    append_u16le(makernote.data(), &size, 0x2020U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, camera_ifd_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 7U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 0x030AU);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, aftarget_ifd_off);
    append_u16le(makernote.data(), &size, 0x030BU);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, subjectdetect_ifd_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0000U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 11U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x000AU);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 33U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("OMDS", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_olympus_oldstyle_nested_fixture()
{
    std::array<unsigned char, 160> makernote {};
    std::size_t size;
    std::uint32_t maker_off;
    std::uint32_t main_ifd_off;
    std::uint32_t main_ifd_size;
    std::uint32_t camera_ifd_off;
    std::uint32_t camera_ifd_size;
    std::uint32_t aftarget_ifd_off;
    std::uint32_t subjectdetect_ifd_off;

    maker_off = 38U + (std::uint32_t)std::strlen("OLYMPUS") + 1U;
    main_ifd_off = 8U;
    main_ifd_size = 2U + 12U + 4U;
    camera_ifd_off = main_ifd_off + main_ifd_size;
    camera_ifd_size = 2U + (3U * 12U) + 4U;
    aftarget_ifd_off = camera_ifd_off + camera_ifd_size;
    subjectdetect_ifd_off = aftarget_ifd_off + 18U;

    size = 0U;
    append_text(makernote.data(), &size, "OLYMP");
    append_u8(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x2020U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, maker_off + camera_ifd_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 0x030AU);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, maker_off + aftarget_ifd_off);
    append_u16le(makernote.data(), &size, 0x030BU);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, maker_off + subjectdetect_ifd_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0000U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 22U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x000AU);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 44U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("OLYMPUS",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_olympus_main_subifd_matrix_fixture()
{
    std::array<unsigned char, 320> makernote {};
    std::size_t size;
    std::uint32_t main_ifd_off;
    std::uint32_t main_ifd_size;
    std::uint32_t sub_ifd_size;
    std::uint32_t equipment_ifd_off;
    std::uint32_t rawdev_ifd_off;
    std::uint32_t rawdev2_ifd_off;
    std::uint32_t imageproc_ifd_off;
    std::uint32_t focusinfo_ifd_off;
    std::uint32_t fetags0_ifd_off;
    std::uint32_t fetags1_ifd_off;
    std::uint32_t rawinfo_ifd_off;

    main_ifd_off = 16U;
    main_ifd_size = 2U + (8U * 12U) + 4U;
    sub_ifd_size = 18U;
    equipment_ifd_off = main_ifd_off + main_ifd_size;
    rawdev_ifd_off = equipment_ifd_off + sub_ifd_size;
    rawdev2_ifd_off = rawdev_ifd_off + sub_ifd_size;
    imageproc_ifd_off = rawdev2_ifd_off + sub_ifd_size;
    focusinfo_ifd_off = imageproc_ifd_off + sub_ifd_size;
    fetags0_ifd_off = focusinfo_ifd_off + sub_ifd_size;
    fetags1_ifd_off = fetags0_ifd_off + sub_ifd_size;
    rawinfo_ifd_off = fetags1_ifd_off + sub_ifd_size;

    size = 0U;
    append_text(makernote.data(), &size, "OM SYSTEM");
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 8U);

    append_u16le(makernote.data(), &size, 0x2010U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, equipment_ifd_off);
    append_u16le(makernote.data(), &size, 0x2030U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, rawdev_ifd_off);
    append_u16le(makernote.data(), &size, 0x2031U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, rawdev2_ifd_off);
    append_u16le(makernote.data(), &size, 0x2040U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, imageproc_ifd_off);
    append_u16le(makernote.data(), &size, 0x2050U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, focusinfo_ifd_off);
    append_u16le(makernote.data(), &size, 0x2100U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, fetags0_ifd_off);
    append_u16le(makernote.data(), &size, 0x2200U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, fetags1_ifd_off);
    append_u16le(makernote.data(), &size, 0x3000U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, rawinfo_ifd_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 7U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0000U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 4U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0000U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 5U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0209U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 6U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0100U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0614U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 321U);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("OMDS", makernote.data(),
                                                  size);
}

static ByteVec
build_tiff_olympus_focusinfo_context_fixture(bool with_stabilization)
{
    std::array<unsigned char, 160> makernote {};
    std::size_t size;
    std::uint32_t main_ifd_off;
    std::uint32_t main_ifd_size;
    std::uint32_t camera_ifd_off;
    std::uint32_t focus_ifd_off;
    std::uint32_t focus_val_off;

    main_ifd_off = 16U;
    main_ifd_size = 2U + (2U * 12U) + 4U;
    camera_ifd_off = main_ifd_off + main_ifd_size;
    focus_ifd_off = camera_ifd_off + 18U;
    focus_val_off = focus_ifd_off + 18U;

    size = 0U;
    append_text(makernote.data(), &size, "OM SYSTEM");
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 3U);
    append_u16le(makernote.data(), &size, 2U);

    append_u16le(makernote.data(), &size, 0x2020U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, camera_ifd_off);
    append_u16le(makernote.data(), &size, 0x2050U);
    append_u16le(makernote.data(), &size, 13U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, focus_ifd_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    if (with_stabilization) {
        append_u16le(makernote.data(), &size, 0x0604U);
        append_u16le(makernote.data(), &size, 4U);
        append_u32le(makernote.data(), &size, 1U);
        append_u32le(makernote.data(), &size, 1U);
    } else {
        append_u16le(makernote.data(), &size, 0x0100U);
        append_u16le(makernote.data(), &size, 3U);
        append_u32le(makernote.data(), &size, 1U);
        append_u16le(makernote.data(), &size, 7U);
        append_u16le(makernote.data(), &size, 0U);
    }
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x1600U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    append_u32le(makernote.data(), &size, focus_val_off);
    append_u32le(makernote.data(), &size, 0U);

    append_u8(makernote.data(), &size, 1U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 3U);
    append_u8(makernote.data(), &size, 4U);
    append_u8(makernote.data(), &size, 5U);
    append_u8(makernote.data(), &size, 6U);
    append_u8(makernote.data(), &size, 7U);
    append_u8(makernote.data(), &size, 8U);

    return build_tiff_with_make_makernote_fixture("OMDS", makernote.data(),
                                                  size);
}

struct CiffValueEntry final {
    std::uint16_t tag = 0U;
    ByteVec value;
};

static void
append_u8_vec(ByteVec* out, std::uint8_t value)
{
    out->push_back(value);
}

static void
append_u16le_vec(ByteVec* out, std::uint16_t value)
{
    append_u8_vec(out, (std::uint8_t)(value & 0xFFU));
    append_u8_vec(out, (std::uint8_t)((value >> 8) & 0xFFU));
}

static void
append_u32le_vec(ByteVec* out, std::uint32_t value)
{
    append_u8_vec(out, (std::uint8_t)(value & 0xFFU));
    append_u8_vec(out, (std::uint8_t)((value >> 8) & 0xFFU));
    append_u8_vec(out, (std::uint8_t)((value >> 16) & 0xFFU));
    append_u8_vec(out, (std::uint8_t)((value >> 24) & 0xFFU));
}

static void
append_bytes_vec(ByteVec* out, const ByteVec& bytes)
{
    out->insert(out->end(), bytes.begin(), bytes.end());
}

static ByteVec
make_ciff_directory(const std::vector<CiffValueEntry>& entries)
{
    ByteVec out;
    std::uint32_t data_off;
    std::size_t i;

    out.reserve(2U + entries.size() * 10U + 256U);
    append_u16le_vec(&out, (std::uint16_t)entries.size());
    data_off = 2U + (std::uint32_t)(entries.size() * 10U);

    for (i = 0U; i < entries.size(); ++i) {
        append_u16le_vec(&out, entries[i].tag);
        append_u32le_vec(&out, (std::uint32_t)entries[i].value.size());
        append_u32le_vec(&out, data_off);
        data_off += (std::uint32_t)entries[i].value.size();
    }

    for (i = 0U; i < entries.size(); ++i) {
        append_bytes_vec(&out, entries[i].value);
    }

    append_u32le_vec(&out, 0U);
    return out;
}

static ByteVec
make_ciff_inline_directory(const std::vector<CiffValueEntry>& entries)
{
    ByteVec out;
    std::size_t i;
    std::size_t j;

    out.reserve(2U + entries.size() * 10U + 4U);
    append_u16le_vec(&out, (std::uint16_t)entries.size());
    for (i = 0U; i < entries.size(); ++i) {
        append_u16le_vec(&out, entries[i].tag);
        for (j = 0U; j < 8U; ++j) {
            const std::uint8_t b = (j < entries[i].value.size())
                                       ? entries[i].value[j]
                                       : 0U;
            append_u8_vec(&out, b);
        }
    }
    append_u32le_vec(&out, 0U);
    return out;
}

static ByteVec
make_padded_ascii(std::string_view text, std::size_t width)
{
    ByteVec out(width, 0U);
    const std::size_t n = (text.size() < width) ? text.size() : width;
    if (n != 0U) {
        std::memcpy(out.data(), text.data(), n);
    }
    return out;
}

static ByteVec
make_padded_u16_scalar(std::uint16_t value)
{
    ByteVec out(8U, 0U);
    out[0] = (std::uint8_t)(value & 0xFFU);
    out[1] = (std::uint8_t)((value >> 8) & 0xFFU);
    return out;
}

static ByteVec
make_padded_u32_scalar(std::uint32_t value)
{
    ByteVec out(8U, 0U);
    out[0] = (std::uint8_t)(value & 0xFFU);
    out[1] = (std::uint8_t)((value >> 8) & 0xFFU);
    out[2] = (std::uint8_t)((value >> 16) & 0xFFU);
    out[3] = (std::uint8_t)((value >> 24) & 0xFFU);
    return out;
}

static ByteVec
make_padded_f32_scalar(float value)
{
    return make_padded_u32_scalar(f32_bits(value));
}

static ByteVec
make_u32_pair(std::uint32_t first, std::uint32_t second)
{
    ByteVec out;
    out.reserve(8U);
    append_u32le_vec(&out, first);
    append_u32le_vec(&out, second);
    return out;
}

static ByteVec
build_crw_textual_ciff_fixture()
{
    const ByteVec dir2804 = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x0805U, make_padded_ascii("High definition camera", 32U) },
        });
    const ByteVec dir2807 = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x0810U, make_padded_ascii("Alice", 32U) },
        });
    const ByteVec dir3004 = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x080CU, make_padded_ascii("Ver 2.10", 32U) },
        });
    const ByteVec dir300a = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x0816U, make_padded_ascii("IMG_0001.CRW", 32U) },
        });
    const ByteVec root = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x2804U, dir2804 },
            { 0x2807U, dir2807 },
            { 0x3004U, dir3004 },
            { 0x300AU, dir300a },
        });
    ByteVec file;

    file.reserve(14U + root.size());
    file.push_back((unsigned char)'I');
    file.push_back((unsigned char)'I');
    append_u32le_vec(&file, 14U);
    file.insert(file.end(), { 'H', 'E', 'A', 'P', 'C', 'C', 'D', 'R' });
    append_bytes_vec(&file, root);
    return file;
}

static ByteVec
build_crw_native_projection_fixture()
{
    ByteVec make_model;
    ByteVec subject_distance;
    ByteVec image_format;
    ByteVec exposure_info;
    ByteVec flash_info;
    ByteVec focal_length;
    ByteVec datetime_original;
    ByteVec dimensions_orientation;

    make_model.insert(make_model.end(), { 'C', 'a', 'n', 'o', 'n', 0U });
    make_model.insert(make_model.end(),
                      { 'P', 'o', 'w', 'e', 'r', 'S', 'h', 'o', 't', ' ',
                        'P', 'r', 'o', '7', '0', 0U });

    append_u32le_vec(&subject_distance, 123U);

    append_u32le_vec(&image_format, 0x00020001U);
    append_u32le_vec(&image_format, f32_bits(10.0f));

    append_u32le_vec(&exposure_info, f32_bits(0.33333334f));
    append_u32le_vec(&exposure_info, f32_bits(6.875f));
    append_u32le_vec(&exposure_info, f32_bits(3.0f));

    flash_info = make_u32_pair(f32_bits(0.0f), f32_bits(0.0f));

    append_u16le_vec(&focal_length, 2U);
    append_u16le_vec(&focal_length, 473U);
    append_u16le_vec(&focal_length, 309U);
    append_u16le_vec(&focal_length, 206U);

    append_u32le_vec(&datetime_original, 1700000000U);
    append_u32le_vec(&datetime_original, 0xFFFFFFFDU);
    append_u32le_vec(&datetime_original, 0x0000007BU);

    append_u32le_vec(&dimensions_orientation, 1536U);
    append_u32le_vec(&dimensions_orientation, 1024U);
    append_u32le_vec(&dimensions_orientation, f32_bits(1.0f));
    append_u32le_vec(&dimensions_orientation, 90U);

    const ByteVec dir2807 = make_ciff_directory(
        std::vector<CiffValueEntry> { { 0x080AU, make_model } });
    const ByteVec dir3002 = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x1813U, flash_info },
            { 0x1807U, subject_distance },
            { 0x1818U, exposure_info },
        });
    const ByteVec dir300a = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x1803U, image_format },
            { 0x180EU, datetime_original },
            { 0x1810U, dimensions_orientation },
        });
    const ByteVec dir300b = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x1028U, flash_info },
            { 0x1029U, focal_length },
        });
    const ByteVec root = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x2807U, dir2807 },
            { 0x3002U, dir3002 },
            { 0x300AU, dir300a },
            { 0x300BU, dir300b },
        });
    ByteVec file;

    file.reserve(14U + root.size());
    file.push_back((unsigned char)'I');
    file.push_back((unsigned char)'I');
    append_u32le_vec(&file, 14U);
    file.insert(file.end(), { 'H', 'E', 'A', 'P', 'C', 'C', 'D', 'R' });
    append_bytes_vec(&file, root);
    return file;
}

static ByteVec
build_crw_semantic_native_scalars_fixture()
{
    const ByteVec dir3002 = make_ciff_inline_directory(
        std::vector<CiffValueEntry> {
            { 0x5010U, make_padded_u16_scalar(2U) },
            { 0x5011U, make_padded_u16_scalar(1U) },
            { 0x5016U, make_padded_u16_scalar(3U) },
            { 0x5807U, make_padded_f32_scalar(12.5f) },
        });
    const ByteVec dir3003 = make_ciff_inline_directory(
        std::vector<CiffValueEntry> {
            { 0x5814U, make_padded_f32_scalar(9.5f) },
        });
    const ByteVec dir3004 = make_ciff_inline_directory(
        std::vector<CiffValueEntry> {
            { 0x501CU, make_padded_u16_scalar(100U) },
            { 0x5834U, make_padded_u32_scalar(0x80000169U) },
            { 0x583BU, make_padded_u32_scalar(2U) },
        });
    const ByteVec dir300a = make_ciff_inline_directory(
        std::vector<CiffValueEntry> {
            { 0x500AU, make_padded_u16_scalar(7U) },
            { 0x5804U, make_padded_u32_scalar(42U) },
            { 0x5806U, make_padded_u32_scalar(1000U) },
            { 0x5817U, make_padded_u32_scalar(162U) },
        });
    const ByteVec root = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x3002U, dir3002 },
            { 0x3003U, dir3003 },
            { 0x3004U, dir3004 },
            { 0x300AU, dir300a },
        });
    ByteVec file;

    file.reserve(14U + root.size());
    file.push_back((unsigned char)'I');
    file.push_back((unsigned char)'I');
    append_u32le_vec(&file, 14U);
    file.insert(file.end(), { 'H', 'E', 'A', 'P', 'C', 'C', 'D', 'R' });
    append_bytes_vec(&file, root);
    return file;
}

static ByteVec
build_crw_decoder_table_fixture()
{
    ByteVec decoder_table;
    const ByteVec dir3004 = make_ciff_directory(
        std::vector<CiffValueEntry> {
            { 0x1835U, [&decoder_table]() -> ByteVec {
                  append_u32le_vec(&decoder_table, 7U);
                  append_u32le_vec(&decoder_table, 0U);
                  append_u32le_vec(&decoder_table, 4096U);
                  append_u32le_vec(&decoder_table, 8192U);
                  return decoder_table;
              }() },
        });
    const ByteVec root = make_ciff_directory(
        std::vector<CiffValueEntry> { { 0x3004U, dir3004 } });
    ByteVec file;

    file.reserve(14U + root.size());
    file.push_back((unsigned char)'I');
    file.push_back((unsigned char)'I');
    append_u32le_vec(&file, 14U);
    file.insert(file.end(), { 'H', 'E', 'A', 'P', 'C', 'C', 'D', 'R' });
    append_bytes_vec(&file, root);
    return file;
}

static ByteVec
build_crw_rawjpginfo_whitesample_fixture()
{
    ByteVec raw_jpg_info;
    ByteVec white_sample;
    ByteVec file;
    const ByteVec root = [&]() -> ByteVec {
        const ByteVec dir300b = [&]() -> ByteVec {
            append_u16le_vec(&raw_jpg_info, 0U);
            append_u16le_vec(&raw_jpg_info, 3U);
            append_u16le_vec(&raw_jpg_info, 2U);
            append_u16le_vec(&raw_jpg_info, 2048U);
            append_u16le_vec(&raw_jpg_info, 1536U);

            append_u16le_vec(&white_sample, 0U);
            append_u16le_vec(&white_sample, 64U);
            append_u16le_vec(&white_sample, 48U);
            append_u16le_vec(&white_sample, 4U);
            append_u16le_vec(&white_sample, 2U);
            append_u16le_vec(&white_sample, 10U);

            return make_ciff_directory(std::vector<CiffValueEntry> {
                { 0x1030U, white_sample },
                { 0x10B5U, raw_jpg_info },
            });
        }();
        return make_ciff_directory(
            std::vector<CiffValueEntry> { { 0x300BU, dir300b } });
    }();

    file.reserve(14U + root.size());
    file.push_back((unsigned char)'I');
    file.push_back((unsigned char)'I');
    append_u32le_vec(&file, 14U);
    file.insert(file.end(), { 'H', 'E', 'A', 'P', 'C', 'C', 'D', 'R' });
    append_bytes_vec(&file, root);
    return file;
}

static ByteVec
build_crw_shotinfo_fixture()
{
    ByteVec shot_info;
    ByteVec file;
    const ByteVec root = [&]() -> ByteVec {
        const ByteVec dir300b = [&]() -> ByteVec {
            append_u16le_vec(&shot_info, 100U);
            append_u16le_vec(&shot_info, 200U);
            append_u16le_vec(&shot_info, 300U);
            append_u16le_vec(&shot_info, 400U);
            append_u16le_vec(&shot_info, (std::uint16_t)-64);
            append_u16le_vec(&shot_info, 3U);
            append_u16le_vec(&shot_info, 1U);
            append_u16le_vec(&shot_info, 2U);
            append_u16le_vec(&shot_info, 9U);
            append_u16le_vec(&shot_info, 6U);

            return make_ciff_directory(std::vector<CiffValueEntry> {
                { 0x102AU, shot_info },
            });
        }();
        return make_ciff_directory(
            std::vector<CiffValueEntry> { { 0x300BU, dir300b } });
    }();

    file.reserve(14U + root.size());
    file.push_back((unsigned char)'I');
    file.push_back((unsigned char)'I');
    append_u32le_vec(&file, 14U);
    file.insert(file.end(), { 'H', 'E', 'A', 'P', 'C', 'C', 'D', 'R' });
    append_bytes_vec(&file, root);
    return file;
}

static ByteVec
build_tiff_with_make_makernote_fixture(const char* make,
                                       const unsigned char* makernote,
                                       std::size_t makernote_size)
{
    ByteVec file(64U + std::strlen(make) + 1U + makernote_size);
    std::size_t size;
    std::size_t make_size;
    std::uint32_t make_off;
    std::uint32_t maker_off;

    make_size = std::strlen(make) + 1U;
    make_off = 38U;
    maker_off = make_off + (std::uint32_t)make_size;

    size = 0U;
    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 2U);

    append_u16le(file.data(), &size, 0x010FU);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)make_size);
    append_u32le(file.data(), &size, make_off);

    append_u16le(file.data(), &size, 0x927CU);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, (std::uint32_t)makernote_size);
    append_u32le(file.data(), &size, maker_off);

    append_u32le(file.data(), &size, 0U);
    append_text(file.data(), &size, make);
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, makernote, makernote_size);

    file.resize(size);
    return file;
}

static ByteVec
build_tiff_with_make_model_makernote_fixture(const char* make,
                                             const char* model,
                                             const unsigned char* makernote,
                                             std::size_t makernote_size)
{
    ByteVec file(80U + std::strlen(make) + std::strlen(model) + 2U
                 + makernote_size);
    std::size_t size;
    std::size_t make_size;
    std::size_t model_size;
    std::uint32_t make_off;
    std::uint32_t model_off;
    std::uint32_t maker_off;

    make_size = std::strlen(make) + 1U;
    model_size = std::strlen(model) + 1U;
    make_off = 50U;
    model_off = make_off + (std::uint32_t)make_size;
    maker_off = model_off + (std::uint32_t)model_size;

    size = 0U;
    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 3U);

    append_u16le(file.data(), &size, 0x010FU);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)make_size);
    append_u32le(file.data(), &size, make_off);

    append_u16le(file.data(), &size, 0x0110U);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, (std::uint32_t)model_size);
    append_u32le(file.data(), &size, model_off);

    append_u16le(file.data(), &size, 0x927CU);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, (std::uint32_t)makernote_size);
    append_u32le(file.data(), &size, maker_off);

    append_u32le(file.data(), &size, 0U);
    append_text(file.data(), &size, make);
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, model);
    append_u8(file.data(), &size, 0U);
    append_bytes(file.data(), &size, makernote, makernote_size);

    file.resize(size);
    return file;
}

static unsigned char
sony_encipher_byte(unsigned char value)
{
    std::uint32_t x;
    std::uint32_t x2;
    std::uint32_t x3;

    if (value >= 249U) {
        return value;
    }
    x = value;
    x2 = (x * x) % 249U;
    x3 = (x2 * x) % 249U;
    return (unsigned char)x3;
}

static bool
read_u16le_at_raw(const unsigned char* raw, std::size_t raw_size,
                  std::uint32_t off, std::uint16_t* out_value)
{
    if (raw == nullptr || out_value == nullptr || off > raw_size
        || (raw_size - off) < 2U) {
        return false;
    }
    *out_value = (std::uint16_t)(((std::uint16_t)raw[off + 0U])
                                 | (((std::uint16_t)raw[off + 1U]) << 8));
    return true;
}

static bool
read_u32le_at_raw(const unsigned char* raw, std::size_t raw_size,
                  std::uint32_t off, std::uint32_t* out_value)
{
    if (raw == nullptr || out_value == nullptr || off > raw_size
        || (raw_size - off) < 4U) {
        return false;
    }
    *out_value = ((std::uint32_t)raw[off + 0U])
                 | (((std::uint32_t)raw[off + 1U]) << 8)
                 | (((std::uint32_t)raw[off + 2U]) << 16)
                 | (((std::uint32_t)raw[off + 3U]) << 24);
    return true;
}

static bool
patch_sony_makernote_value_offset_in_tiff(ByteVec* tiff)
{
    std::uint32_t ifd0_off32;
    std::uint16_t ifd0_count;
    std::uint32_t exif_ifd_off32;
    std::uint16_t exif_count;
    std::uint32_t maker_note_off32;
    std::uint32_t i;

    if (tiff == nullptr || tiff->size() < 8U || (*tiff)[0U] != (unsigned char)'I'
        || (*tiff)[1U] != (unsigned char)'I'
        || !read_u32le_at_raw(tiff->data(), tiff->size(), 4U, &ifd0_off32)
        || !read_u16le_at_raw(tiff->data(), tiff->size(), ifd0_off32,
                              &ifd0_count)) {
        return false;
    }

    exif_ifd_off32 = 0U;
    maker_note_off32 = 0U;
    for (i = 0U; i < (std::uint32_t)ifd0_count; ++i) {
        std::uint32_t eoff;
        std::uint16_t tag16;

        eoff = ifd0_off32 + 2U + (i * 12U);
        if (!read_u16le_at_raw(tiff->data(), tiff->size(), eoff, &tag16)) {
            return false;
        }
        if (tag16 == 0x927CU) {
            if (!read_u32le_at_raw(tiff->data(), tiff->size(), eoff + 8U,
                                   &maker_note_off32)) {
                return false;
            }
            break;
        }
        if (tag16 == 0x8769U) {
            if (!read_u32le_at_raw(tiff->data(), tiff->size(), eoff + 8U,
                                   &exif_ifd_off32)) {
                return false;
            }
        }
    }
    if (maker_note_off32 == 0U && exif_ifd_off32 != 0U) {
        if (!read_u16le_at_raw(tiff->data(), tiff->size(), exif_ifd_off32,
                               &exif_count)) {
            return false;
        }
        for (i = 0U; i < (std::uint32_t)exif_count; ++i) {
            std::uint32_t eoff;
            std::uint16_t tag16;

            eoff = exif_ifd_off32 + 2U + (i * 12U);
            if (!read_u16le_at_raw(tiff->data(), tiff->size(), eoff, &tag16)) {
                return false;
            }
            if (tag16 == 0x927CU) {
                if (!read_u32le_at_raw(tiff->data(), tiff->size(), eoff + 8U,
                                       &maker_note_off32)) {
                    return false;
                }
                break;
            }
        }
    }
    if (maker_note_off32 == 0U || maker_note_off32 > tiff->size()) {
        return false;
    }

    if ((tiff->size() - maker_note_off32) >= 34U
        && std::memcmp(tiff->data() + maker_note_off32, "SONY", 4U) == 0) {
        write_u32le_at(tiff->data(), maker_note_off32 + 26U,
                       maker_note_off32 + 34U);
        return true;
    }
    if ((tiff->size() - maker_note_off32) < 18U) {
        return false;
    }
    write_u32le_at(tiff->data(), maker_note_off32 + 10U,
                   maker_note_off32 + 18U);
    return true;
}

static ByteVec
build_tiff_sony_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    ByteVec tiff;
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "SONY", 4U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0x0102U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 7U);
    append_u16le(makernote.data(), &size, 0xB020U);
    append_u16le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 9U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, "Standard", 8U);
    append_u8(makernote.data(), &size, 0U);

    tiff = build_tiff_with_make_makernote_fixture("Sony", makernote.data(),
                                                  size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_sony_ciphered_fixture(std::uint16_t tag, const unsigned char* plain,
                                 std::size_t plain_size)
{
    std::array<unsigned char, 8192> makernote {};
    ByteVec tiff;
    std::size_t size;
    std::size_t i;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, tag);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)plain_size);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    for (i = 0U; i < plain_size; ++i) {
        append_u8(makernote.data(), &size, sony_encipher_byte(plain[i]));
    }

    tiff = build_tiff_with_make_makernote_fixture("Sony", makernote.data(),
                                                  size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_sony_ciphered_model_fixture(const char* model, std::uint16_t tag,
                                       const unsigned char* plain,
                                       std::size_t plain_size)
{
    std::array<unsigned char, 8192> makernote {};
    ByteVec tiff;
    std::size_t size;
    std::size_t i;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, tag);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)plain_size);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    for (i = 0U; i < plain_size; ++i) {
        append_u8(makernote.data(), &size, sony_encipher_byte(plain[i]));
    }

    tiff = build_tiff_with_make_model_makernote_fixture("Sony", model,
                                                        makernote.data(), size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_sony_tag9050b_fixture()
{
    std::array<unsigned char, 0x90> plain {};

    plain[0x0026U] = 0x01U;
    plain[0x0027U] = 0x00U;
    plain[0x0028U] = 0x02U;
    plain[0x0029U] = 0x00U;
    plain[0x002AU] = 0x03U;
    plain[0x002BU] = 0x00U;
    plain[0x003AU] = 0x11U;
    plain[0x003BU] = 0x22U;
    plain[0x003CU] = 0x33U;
    plain[0x003DU] = 0x44U;
    plain[0x0088U] = 0x01U;
    plain[0x0089U] = 0x02U;
    plain[0x008AU] = 0x03U;
    plain[0x008BU] = 0x04U;
    plain[0x008CU] = 0x05U;
    plain[0x008DU] = 0x06U;

    return build_tiff_sony_ciphered_fixture(0x9050U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag2010i_fixture()
{
    std::array<unsigned char, 0x1800> plain {};
    std::uint32_t i;

    plain[0x0217U] = 0x34U;
    plain[0x0218U] = 0x12U;
    plain[0x0252U] = 0x01U;
    plain[0x0253U] = 0x00U;
    plain[0x0254U] = 0x02U;
    plain[0x0255U] = 0x00U;
    plain[0x0256U] = 0x03U;
    plain[0x0257U] = 0x00U;
    plain[0x0320U] = 0x64U;
    plain[0x0321U] = 0x00U;
    for (i = 0U; i < 32U; ++i) {
        plain[0x17D0U + i] = (unsigned char)i;
    }

    return build_tiff_sony_ciphered_fixture(0x2010U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag202a_fixture()
{
    std::array<unsigned char, 2> plain {};

    plain[0x0001U] = 7U;
    return build_tiff_sony_ciphered_fixture(0x202AU, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag9405b_fixture()
{
    std::array<unsigned char, 0x84> plain {};
    std::uint32_t i;

    plain[0x0004U] = 0x34U;
    plain[0x0005U] = 0x12U;
    plain[0x0010U] = 0x01U;
    plain[0x0014U] = 0xFAU;
    plain[0x0024U] = 0x78U;
    plain[0x0025U] = 0x56U;
    plain[0x0026U] = 0x34U;
    plain[0x0027U] = 0x12U;
    plain[0x005EU] = 9U;
    plain[0x0060U] = 0xBCU;
    plain[0x0061U] = 0x0AU;
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x0064U + (i * 2U),
                       (std::uint16_t)(i - 8U));
    }

    return build_tiff_sony_ciphered_fixture(0x9405U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag9416_fixture()
{
    std::array<unsigned char, 0x96> plain {};
    std::uint32_t i;

    plain[0x0000U] = 0x10U;
    plain[0x0004U] = 0x39U;
    plain[0x0005U] = 0x05U;
    plain[0x000CU] = 0x05U;
    plain[0x0010U] = 0x08U;
    plain[0x001DU] = 0xEFU;
    plain[0x001EU] = 0xCDU;
    plain[0x001FU] = 0xABU;
    plain[0x0020U] = 0x89U;
    plain[0x002BU] = 7U;
    plain[0x0048U] = 3U;
    plain[0x004BU] = 0x80U;
    plain[0x004CU] = 0x07U;
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x004FU + (i * 2U),
                       (std::uint16_t)(20U + i));
    }

    return build_tiff_sony_ciphered_fixture(0x9416U, plain.data(),
                                            plain.size());
}

static ByteVec
build_tiff_sony_tag9050a_fixture()
{
    std::array<unsigned char, 0x01C1> plain {};

    plain[0x0000U] = 1U;
    plain[0x0001U] = 2U;
    write_u16le_at(plain.data(), 0x0020U, 10U);
    write_u16le_at(plain.data(), 0x0022U, 20U);
    write_u16le_at(plain.data(), 0x0024U, 30U);
    plain[0x0031U] = 0x31U;
    write_u32le_at(plain.data(), 0x0032U, 0x44332211U);
    write_u16le_at(plain.data(), 0x003AU, 0x1234U);
    write_u16le_at(plain.data(), 0x003CU, 0x5678U);
    plain[0x003FU] = 0x3FU;
    write_u32le_at(plain.data(), 0x004CU, 0x00ABCDEFU);
    plain[0x0051U] = 24U;
    plain[0x0052U] = 3U;
    plain[0x0053U] = 21U;
    plain[0x0054U] = 10U;
    plain[0x0055U] = 11U;
    plain[0x0056U] = 12U;
    plain[0x0067U] = 0x67U;
    plain[0x0105U] = 0x15U;
    plain[0x0106U] = 0x16U;
    write_u16le_at(plain.data(), 0x0107U, 0x0107U);
    write_u16le_at(plain.data(), 0x0109U, 0x0109U);
    plain[0x010BU] = 0x1BU;
    plain[0x0114U] = 0x24U;
    write_u32le_at(plain.data(), 0x01A0U, 0x00112233U);
    write_u32le_at(plain.data(), 0x01AAU, 0x89ABCDEFU);
    write_u32le_at(plain.data(), 0x01BDU, 0x10203040U);

    return build_tiff_sony_ciphered_model_fixture("Lunar", 0x9050U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9050c_fixture()
{
    std::array<unsigned char, 0x90> plain {};

    plain[0x0026U] = 0x01U;
    plain[0x0027U] = 0x00U;
    plain[0x0028U] = 0x02U;
    plain[0x0029U] = 0x00U;
    plain[0x002AU] = 0x03U;
    plain[0x002BU] = 0x00U;
    plain[0x003AU] = 0x11U;
    plain[0x003BU] = 0x22U;
    plain[0x003CU] = 0x33U;
    plain[0x003DU] = 0x44U;
    plain[0x0088U] = 0x01U;
    plain[0x0089U] = 0x02U;
    plain[0x008AU] = 0x03U;
    plain[0x008BU] = 0x04U;
    plain[0x008CU] = 0x05U;
    plain[0x008DU] = 0x06U;

    return build_tiff_sony_ciphered_model_fixture("ILCE-7M4", 0x9050U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag2010b_fixture()
{
    std::array<unsigned char, 0x1A50> plain {};
    std::uint32_t i;

    write_u32le_at(plain.data(), 0x0000U, 0x11223344U);
    write_u32le_at(plain.data(), 0x0004U, 0x55667788U);
    write_u32le_at(plain.data(), 0x0008U, 0x99AABBCCU);
    plain[0x0324U] = 0x24U;
    plain[0x1128U] = 0x28U;
    plain[0x112CU] = 0x2CU;
    write_u16le_at(plain.data(), 0x113EU, 0x113EU);
    write_u16le_at(plain.data(), 0x1140U, 0x1140U);
    write_u16le_at(plain.data(), 0x1218U, 0x1218U);
    write_u16le_at(plain.data(), 0x114CU, (std::uint16_t)-7);
    write_u16le_at(plain.data(), 0x1180U, 1U);
    write_u16le_at(plain.data(), 0x1182U, 2U);
    write_u16le_at(plain.data(), 0x1184U, 3U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x1A23U + (i * 2U),
                       (std::uint16_t)(200U + i));
    }
    for (i = 0U; i < 0x798U; ++i) {
        plain[0x04B4U + i] = (unsigned char)i;
    }

    return build_tiff_sony_ciphered_model_fixture("Lunar", 0x2010U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag2010e_fixture()
{
    std::array<unsigned char, 0x1A90> plain {};
    std::uint32_t i;

    write_u32le_at(plain.data(), 0x0000U, 0xA1B2C3D4U);
    write_u32le_at(plain.data(), 0x0004U, 0x10213243U);
    write_u32le_at(plain.data(), 0x0008U, 0x55667788U);
    plain[0x021CU] = 0x21U;
    plain[0x0328U] = 0x32U;
    plain[0x115CU] = 0x5CU;
    plain[0x1160U] = 0x60U;
    write_u16le_at(plain.data(), 0x1172U, 0x1172U);
    write_u16le_at(plain.data(), 0x1174U, 0x1174U);
    write_u16le_at(plain.data(), 0x1254U, 0x1254U);
    write_u16le_at(plain.data(), 0x1180U, (std::uint16_t)-9);
    write_u16le_at(plain.data(), 0x11B4U, 4U);
    write_u16le_at(plain.data(), 0x11B6U, 5U);
    write_u16le_at(plain.data(), 0x11B8U, 6U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x1870U + (i * 2U),
                       (std::uint16_t)(300U + i));
    }
    plain[0x1891U] = 0x91U;
    plain[0x1892U] = 0x92U;
    write_u16le_at(plain.data(), 0x1893U, 0x1893U);
    write_u16le_at(plain.data(), 0x1896U, 0x1896U);
    plain[0x192CU] = 0x2CU;
    plain[0x1A88U] = 0x88U;
    for (i = 0U; i < 0x798U; ++i) {
        plain[0x04B8U + i] = (unsigned char)(0x80U + i);
    }

    return build_tiff_sony_ciphered_model_fixture("Stellar", 0x2010U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9400a_fixture()
{
    std::array<unsigned char, 0x53> plain {};

    plain[0x0000U] = 0x07U;
    plain[0x0008U] = 0x04U;
    plain[0x0009U] = 0x03U;
    plain[0x000AU] = 0x02U;
    plain[0x000BU] = 0x01U;
    plain[0x000CU] = 0x08U;
    plain[0x000DU] = 0x07U;
    plain[0x000EU] = 0x06U;
    plain[0x000FU] = 0x05U;
    plain[0x0010U] = 2U;
    plain[0x0012U] = 1U;
    plain[0x001AU] = 0x44U;
    plain[0x001BU] = 0x33U;
    plain[0x001CU] = 0x22U;
    plain[0x001DU] = 0x11U;
    plain[0x0022U] = 10U;
    plain[0x0028U] = 3U;
    plain[0x0029U] = 8U;
    plain[0x0044U] = 0xB8U;
    plain[0x0045U] = 0x0BU;
    plain[0x0052U] = 23U;

    return build_tiff_sony_ciphered_model_fixture("SLT-A99", 0x9400U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9404b_fixture()
{
    std::array<unsigned char, 0x20> plain {};

    plain[0x000CU] = 12U;
    plain[0x000EU] = 14U;
    write_u16le_at(plain.data(), 0x001EU, 0x2345U);

    return build_tiff_sony_ciphered_model_fixture("Lunar", 0x9404U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag9405a_fixture()
{
    std::array<unsigned char, 0x06EA> plain {};
    std::uint32_t i;

    plain[0x0600U] = 1U;
    plain[0x0601U] = 2U;
    plain[0x0603U] = 3U;
    plain[0x0604U] = 4U;
    write_u16le_at(plain.data(), 0x0605U, 0x0605U);
    write_u16le_at(plain.data(), 0x0608U, 0x0608U);
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x064AU + (i * 2U),
                       (std::uint16_t)(30U + i));
    }
    for (i = 0U; i < 32U; ++i) {
        write_u16le_at(plain.data(), 0x066AU + (i * 2U),
                       (std::uint16_t)(i - 20U));
    }
    for (i = 0U; i < 16U; ++i) {
        write_u16le_at(plain.data(), 0x06CAU + (i * 2U),
                       (std::uint16_t)(100U + i));
    }

    return build_tiff_sony_ciphered_model_fixture("SLT-A99", 0x9405U,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag940e_fixture()
{
    std::array<unsigned char, 0x1A14> plain {};
    std::uint32_t i;

    plain[0x1A06U] = 2U;
    plain[0x1A07U] = 3U;
    for (i = 0U; i < 12U; ++i) {
        plain[0x1A08U + i] = (unsigned char)(0xA0U + i);
    }

    return build_tiff_sony_ciphered_model_fixture("ILCE-7M3", 0x940EU,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_tag940e_afinfo_fixture()
{
    std::array<unsigned char, 0x0180> plain {};
    std::uint32_t i;

    plain[0x0002U] = 2U;
    plain[0x0004U] = 5U;
    plain[0x0007U] = 6U;
    plain[0x0008U] = 7U;
    plain[0x0009U] = 8U;
    plain[0x000AU] = 9U;
    plain[0x000BU] = 10U;
    for (i = 0U; i < 30U; ++i) {
        write_u16le_at(plain.data(), 0x0011U + (i * 2U),
                       (std::uint16_t)(100U + i));
    }
    plain[0x016EU] = 0x78U;
    plain[0x016FU] = 0x56U;
    plain[0x0170U] = 0x34U;
    plain[0x0171U] = 0x12U;
    plain[0x017DU] = 0xFEU;
    plain[0x017EU] = 4U;

    return build_tiff_sony_ciphered_model_fixture("SLT-A99", 0x940EU,
                                                  plain.data(), plain.size());
}

static ByteVec
build_tiff_sony_shotinfo_fixture()
{
    std::array<unsigned char, 0x44> blob {};
    std::array<unsigned char, 128> makernote {};
    ByteVec tiff;
    std::size_t size;

    blob[0U] = (unsigned char)'I';
    blob[1U] = (unsigned char)'I';
    write_u16le_at(blob.data(), 0x0002U, 94U);
    std::memcpy(blob.data() + 0x0006U, "2017:02:08 07:07:08", 19U);
    write_u16le_at(blob.data(), 0x001AU, 5304U);
    write_u16le_at(blob.data(), 0x001CU, 7952U);
    write_u16le_at(blob.data(), 0x0030U, 2U);
    write_u16le_at(blob.data(), 0x0032U, 37U);
    std::memcpy(blob.data() + 0x0034U, "DC7303320222000", 15U);

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x3000U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)blob.size());
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, blob.data(), blob.size());

    tiff = build_tiff_with_make_makernote_fixture("Sony", makernote.data(),
                                                  size);
    patch_sony_makernote_value_offset_in_tiff(&tiff);
    return tiff;
}

static ByteVec
build_tiff_apple_front_facing_makernote_fixture()
{
    unsigned char makernote[64];
    std::size_t size = 0U;

    append_text(makernote, &size, "Apple iOS");
    append_u8(makernote, &size, 0U);
    append_u8(makernote, &size, 0U);
    append_u8(makernote, &size, 1U);
    append_text(makernote, &size, "MM");
    append_u16be(makernote, &size, 1U);
    append_u16be(makernote, &size, 0x0045U);
    append_u16be(makernote, &size, 3U);
    append_u32be(makernote, &size, 1U);
    append_u16be(makernote, &size, 1U);
    append_u16be(makernote, &size, 0U);
    append_u32be(makernote, &size, 0U);

    return build_tiff_with_make_makernote_fixture("Apple", makernote, size);
}

static ByteVec
build_tiff_flir_makernote_fixture()
{
    unsigned char makernote[32];
    std::size_t size = 0U;

    append_u16le(makernote, &size, 1U);
    append_u16le(makernote, &size, 0x0001U);
    append_u16le(makernote, &size, 4U);
    append_u32le(makernote, &size, 1U);
    append_u32le(makernote, &size, 99U);
    append_u32le(makernote, &size, 0U);

    return build_tiff_with_make_makernote_fixture("FLIR", makernote, size);
}

static ByteVec
build_tiff_nintendo_makernote_fixture()
{
    unsigned char makernote[96];
    std::size_t size = 0U;

    append_u16le(makernote, &size, 1U);
    append_u16le(makernote, &size, 0x1101U);
    append_u16le(makernote, &size, 7U);
    append_u32le(makernote, &size, 0x34U);
    append_u32le(makernote, &size, 18U);
    append_u32le(makernote, &size, 0U);

    std::memset(makernote + size, 0, 0x34U);
    std::memcpy(makernote + size + 0U, "3DS1", 4U);
    write_u32le_at(makernote, 18U + 0x08U, 0x12345678U);
    makernote[18U + 0x18U] = 0xAAU;
    makernote[18U + 0x19U] = 0xBBU;
    makernote[18U + 0x1AU] = 0xCCU;
    makernote[18U + 0x1BU] = 0xDDU;
    write_u32le_at(makernote, 18U + 0x28U, 0x3FC00000U);
    write_u16le_at(makernote, 18U + 0x30U, 5U);
    size += 0x34U;

    return build_tiff_with_make_makernote_fixture("Nintendo", makernote,
                                                  size);
}

static ByteVec
build_tiff_hp_type6_makernote_fixture()
{
    unsigned char makernote[128];

    std::memset(makernote, 0, sizeof(makernote));
    makernote[0U] = (unsigned char)'I';
    makernote[1U] = (unsigned char)'I';
    makernote[2U] = (unsigned char)'I';
    makernote[3U] = (unsigned char)'I';
    makernote[4U] = 0x06U;
    makernote[5U] = 0U;

    write_u16le_at(makernote, 0x000CU, 28U);
    write_u32le_at(makernote, 0x0010U, 50000U);
    std::memcpy(makernote + 0x0014U, "2025:03:16 12:34:56", 19U);
    write_u16le_at(makernote, 0x0034U, 200U);
    std::memcpy(makernote + 0x0058U, "SERIAL NUMBER:HP-12345", 22U);

    return build_tiff_with_make_makernote_fixture("HP", makernote,
                                                  sizeof(makernote));
}

static ByteVec
build_tiff_kodak_kdk_makernote_fixture()
{
    unsigned char makernote[0x70];

    std::memset(makernote, 0, sizeof(makernote));
    makernote[0U] = (unsigned char)'K';
    makernote[1U] = (unsigned char)'D';
    makernote[2U] = (unsigned char)'K';
    std::memcpy(makernote + 0x08U, "CX6330", 6U);
    makernote[0x11U] = 3U;
    makernote[0x12U] = 4U;
    write_u16le_at(makernote, 0x14U, 800U);
    write_u16le_at(makernote, 0x16U, 600U);
    write_u16le_at(makernote, 0x18U, 2025U);
    makernote[0x1AU] = 1U;
    makernote[0x1BU] = 2U;
    makernote[0x1CU] = 3U;
    makernote[0x1DU] = 4U;
    makernote[0x1EU] = 5U;
    makernote[0x1FU] = 6U;

    return build_tiff_with_make_makernote_fixture("KODAK", makernote,
                                                  sizeof(makernote));
}

static ByteVec
build_tiff_kodak_type2_makernote_fixture()
{
    unsigned char makernote[0x74];

    std::memset(makernote, 0, sizeof(makernote));
    std::memcpy(makernote + 0x08U, "Hewlett-Packard", 15U);
    std::memcpy(makernote + 0x28U, "HP PhotoSmart 618", 17U);
    write_u32be_at(makernote, 0x6CU, 800U);
    write_u32be_at(makernote, 0x70U, 600U);

    return build_tiff_with_make_makernote_fixture("KODAK", makernote,
                                                  sizeof(makernote));
}

static ByteVec
build_tiff_kodak_serial_only_makernote_fixture()
{
    unsigned char makernote[16];

    std::memset(makernote, 0, sizeof(makernote));
    std::memcpy(makernote, "C33000641478", 12U);
    return build_tiff_with_make_makernote_fixture("KODAK", makernote, 12U);
}

static ByteVec
build_tiff_kodak_type5_makernote_fixture()
{
    unsigned char makernote[0x2c];

    std::memset(makernote, 0, sizeof(makernote));
    write_u32be_at(makernote, 0x14U, 33333U);
    makernote[0x1AU] = 4U;
    write_u16be_at(makernote, 0x1CU, 45U);
    write_u16be_at(makernote, 0x1EU, 140U);
    write_u16be_at(makernote, 0x20U, 1U);
    write_u16be_at(makernote, 0x22U, 2U);
    makernote[0x27U] = 1U;
    makernote[0x2AU] = 0U;
    makernote[0x2BU] = 1U;

    return build_tiff_with_make_model_makernote_fixture("KODAK", "CX4200",
                                                        makernote,
                                                        sizeof(makernote));
}

static ByteVec
build_tiff_kodak_absolute_type8_makernote_fixture(const char* model)
{
    unsigned char makernote[64];
    std::size_t size;

    size = 0U;
    append_u16le(makernote, &size, 4U);

    append_u16le(makernote, &size, 0x0104U);
    append_u16le(makernote, &size, 3U);
    append_u32le(makernote, &size, 1U);
    append_u16le(makernote, &size, 9U);
    append_u16le(makernote, &size, 0U);

    append_u16le(makernote, &size, 0x0200U);
    append_u16le(makernote, &size, 3U);
    append_u32le(makernote, &size, 1U);
    append_u16le(makernote, &size, 1U);
    append_u16le(makernote, &size, 0U);

    append_u16le(makernote, &size, 0x0203U);
    append_u16le(makernote, &size, 3U);
    append_u32le(makernote, &size, 1U);
    append_u16le(makernote, &size, 2U);
    append_u16le(makernote, &size, 0U);

    append_u16le(makernote, &size, 0x0300U);
    append_u16le(makernote, &size, 3U);
    append_u32le(makernote, &size, 1U);
    append_u16le(makernote, &size, 3U);
    append_u16le(makernote, &size, 0U);

    append_u32le(makernote, &size, 0U);
    return build_tiff_with_make_model_makernote_fixture("KODAK", model,
                                                        makernote, size);
}

static ByteVec
build_tiff_kodak_absolute_type11_makernote_fixture()
{
    return build_tiff_kodak_absolute_type8_makernote_fixture("PIXPRO 4KVR360");
}

static ByteVec
build_tiff_sigma_main_u16_makernote_fixture(const char* model,
                                            std::uint16_t tag,
                                            std::uint16_t value)
{
    unsigned char makernote[32];
    std::size_t size;

    size = 0U;
    append_u16le(makernote, &size, 1U);
    append_u16le(makernote, &size, tag);
    append_u16le(makernote, &size, 3U);
    append_u32le(makernote, &size, 1U);
    append_u16le(makernote, &size, value);
    append_u16le(makernote, &size, 0U);
    append_u32le(makernote, &size, 0U);

    return build_tiff_with_make_model_makernote_fixture("SIGMA", model,
                                                        makernote, size);
}

static ByteVec
build_tiff_sigma_wb_makernote_fixture()
{
    unsigned char makernote[320];
    std::size_t size;
    std::uint32_t i;

    size = 0U;
    append_u16le(makernote, &size, 2U);

    append_u16le(makernote, &size, 0x0120U);
    append_u16le(makernote, &size, 11U);
    append_u32le(makernote, &size, 30U);
    append_u32le(makernote, &size, 30U);

    append_u16le(makernote, &size, 0x0121U);
    append_u16le(makernote, &size, 11U);
    append_u32le(makernote, &size, 30U);
    append_u32le(makernote, &size, 150U);

    append_u32le(makernote, &size, 0U);

    for (i = 0U; i < 30U; ++i) {
        append_u32le(makernote, &size, f32_bits((float)(i + 1U)));
    }
    for (i = 0U; i < 30U; ++i) {
        append_u32le(makernote, &size, f32_bits((float)(i + 41U)));
    }

    return build_tiff_with_make_model_makernote_fixture("SIGMA", "SIGMA fp L",
                                                        makernote, size);
}

static ByteVec
build_tiff_nikon_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);
    append_u16le(makernote.data(), &size, 0x001FU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    append_u32le(makernote.data(), &size, 38U);
    append_u32le(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "0101");
    append_u8(makernote.data(), &size, 1U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_binary_makernote_fixture()
{
    std::array<unsigned char, 512> makernote {};
    std::size_t size;
    std::uint32_t off_vr_pos;
    std::uint32_t off_dist_pos;
    std::uint32_t off_flash_pos;
    std::uint32_t off_multi_pos;
    std::uint32_t off_afinfo_pos;
    std::uint32_t off_file_pos;
    std::uint32_t off_retouch_pos;
    std::uint32_t off_payload;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 8U);

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);

    append_u16le(makernote.data(), &size, 0x001FU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    off_vr_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x002BU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 5U);
    off_dist_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00A8U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 10U);
    off_flash_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B0U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 16U);
    off_multi_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B7U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 30U);
    off_afinfo_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B8U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 10U);
    off_file_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00BBU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    off_retouch_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_vr_pos, off_payload);
    append_text(makernote.data(), &size, "0101");
    append_u8(makernote.data(), &size, 1U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_dist_pos, off_payload);
    {
        std::array<unsigned char, 16> raw {};

        raw[0] = (unsigned char)'0';
        raw[1] = (unsigned char)'1';
        raw[2] = (unsigned char)'0';
        raw[3] = (unsigned char)'0';
        raw[4] = (unsigned char)1U;
        append_bytes(makernote.data(), &size, raw.data(), raw.size());
    }

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_flash_pos, off_payload);
    {
        std::array<unsigned char, 49> raw {};

        raw[0] = (unsigned char)'0';
        raw[1] = (unsigned char)'1';
        raw[2] = (unsigned char)'0';
        raw[3] = (unsigned char)'6';
        raw[4] = (unsigned char)1U;
        raw[6] = (unsigned char)0x11U;
        raw[7] = (unsigned char)0x22U;
        raw[8] = (unsigned char)0x33U;
        raw[0x27] = (unsigned char)0x80U;
        raw[0x2A] = (unsigned char)0xFFU;
        append_bytes(makernote.data(), &size, raw.data(), raw.size());
    }

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_multi_pos, off_payload);
    append_text(makernote.data(), &size, "0100");
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 3U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_afinfo_pos, off_payload);
    {
        std::array<unsigned char, 30> raw {};

        raw[0] = (unsigned char)'0';
        raw[1] = (unsigned char)'1';
        raw[2] = (unsigned char)'0';
        raw[3] = (unsigned char)'0';
        raw[4] = (unsigned char)1U;
        raw[5] = (unsigned char)8U;
        raw[6] = (unsigned char)3U;
        raw[7] = (unsigned char)2U;
        raw[8] = (unsigned char)0xAAU;
        raw[9] = (unsigned char)0xBBU;
        raw[10] = (unsigned char)0xCCU;
        raw[11] = (unsigned char)0xDDU;
        raw[12] = (unsigned char)0xEEU;
        raw[0x10] = (unsigned char)0x34U;
        raw[0x11] = (unsigned char)0x12U;
        raw[0x12] = (unsigned char)0x78U;
        raw[0x13] = (unsigned char)0x56U;
        raw[0x1C] = (unsigned char)1U;
        append_bytes(makernote.data(), &size, raw.data(), raw.size());
    }

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_file_pos, off_payload);
    append_text(makernote.data(), &size, "0100");
    append_u16le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 99U);
    append_u16le(makernote.data(), &size, 7U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_retouch_pos, off_payload);
    {
        std::array<unsigned char, 8> raw {};

        raw[0] = (unsigned char)'0';
        raw[1] = (unsigned char)'2';
        raw[2] = (unsigned char)'0';
        raw[3] = (unsigned char)'0';
        raw[5] = (unsigned char)0xFFU;
        append_bytes(makernote.data(), &size, raw.data(), raw.size());
    }

    return build_tiff_with_make_makernote_fixture("Nikon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_makernote_fixture()
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x12345678U);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_custom_functions2_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0099U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0x01010000U);
    append_u32le(makernote.data(), &size, 0x01020003U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_afinfo2_makernote_fixture()
{
    static const std::int16_t x_pos[9] = {
        0, -649, 649, -1034, 0, 1034, -649, 649, 0
    };
    static const std::int16_t y_pos[9] = {
        562, 298, 298, 0, 0, 0, -298, -298, -562
    };
    std::array<unsigned char, 256> makernote {};
    std::size_t size;
    std::size_t i;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0026U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 48U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 96U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 9U);
    append_u16le(makernote.data(), &size, 9U);
    append_u16le(makernote.data(), &size, 3888U);
    append_u16le(makernote.data(), &size, 2592U);
    append_u16le(makernote.data(), &size, 3888U);
    append_u16le(makernote.data(), &size, 2592U);

    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, 97U);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, 98U);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, (std::uint16_t)x_pos[i]);
    }
    for (i = 0U; i < 9U; ++i) {
        append_u16le(makernote.data(), &size, (std::uint16_t)y_pos[i]);
    }

    append_u16le(makernote.data(), &size, 4U);
    append_u16le(makernote.data(), &size, 4U);
    append_u16le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_preview_settings_aftune_fixture()
{
    std::array<unsigned char, 512> makernote {};
    std::size_t size;
    std::uint32_t off_preview_pos;
    std::uint32_t off_settings_pos;
    std::uint32_t rel_off;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 4U);

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);

    append_u16le(makernote.data(), &size, 0x0011U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    off_preview_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x004EU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 48U);
    off_settings_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x00B9U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u8(makernote.data(), &size, 1U);
    append_u8(makernote.data(), &size, 4U);
    append_u8(makernote.data(), &size, 0xFDU);
    append_u8(makernote.data(), &size, 5U);

    append_u32le(makernote.data(), &size, 0U);

    rel_off = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_settings_pos, rel_off);
    std::memset(makernote.data() + size, 0, 48U);
    write_u32le_at(makernote.data(), (std::uint32_t)size + 20U, 3U);
    makernote[size + 24U + 0U] = 0x01U;
    makernote[size + 24U + 1U] = 0x00U;
    makernote[size + 24U + 2U] = 0x00U;
    makernote[size + 24U + 3U] = 0x04U;
    write_u32le_at(makernote.data(), (std::uint32_t)size + 24U + 4U, 6400U);
    makernote[size + 32U + 0U] = 0x46U;
    makernote[size + 32U + 1U] = 0x00U;
    makernote[size + 32U + 2U] = 0x00U;
    makernote[size + 32U + 3U] = 0x01U;
    write_u32le_at(makernote.data(), (std::uint32_t)size + 32U + 4U, 1U);
    makernote[size + 40U + 0U] = 0x63U;
    makernote[size + 40U + 1U] = 0x00U;
    makernote[size + 40U + 2U] = 0x00U;
    makernote[size + 40U + 3U] = 0x03U;
    write_u32le_at(makernote.data(), (std::uint32_t)size + 40U + 4U, 9U);
    size += 48U;

    rel_off = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_preview_pos, rel_off);
    append_u16le(makernote.data(), &size, 4U);

    append_u16le(makernote.data(), &size, 0x0103U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 6U);
    append_u16le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0201U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x0200U);

    append_u16le(makernote.data(), &size, 0x0202U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x1234U);

    append_u16le(makernote.data(), &size, 0x0213U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 2U);
    append_u16le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_makernote_fixture("Nikon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_flashinfo0108_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;
    std::uint32_t flashinfo_off;
    std::uint32_t payload_off;
    std::size_t i;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 1U);

    flashinfo_off = 8U + 2U + 12U + 4U;
    append_u16le(makernote.data(), &size, 0x00A8U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 49U);
    append_u32le(makernote.data(), &size, flashinfo_off);
    append_u32le(makernote.data(), &size, 0U);

    append_text(makernote.data(), &size, "0108");
    for (i = 4U; i < 49U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    payload_off = 10U + flashinfo_off;
    makernote[payload_off + 4U] = 1U;
    makernote[payload_off + 6U] = 0x12U;
    makernote[payload_off + 7U] = 0x34U;
    makernote[payload_off + 0x000AU] = 0x82U;
    makernote[payload_off + 0x0014U] = 0x66U;
    makernote[payload_off + 0x0015U] = 0x55U;
    makernote[payload_off + 0x0028U] = 0x7FU;
    makernote[payload_off + 0x0029U] = 0x7EU;
    makernote[payload_off + 0x002AU] = 0x7DU;

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_single_main_bytes_fixture(const char* make,
                                           std::uint16_t tag,
                                           const unsigned char* raw,
                                           std::size_t raw_size)
{
    std::array<unsigned char, 512> makernote {};
    std::size_t size;
    std::uint32_t value_off;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 1U);

    value_off = 8U + 2U + 12U + 4U;
    append_u16le(makernote.data(), &size, tag);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, (std::uint32_t)raw_size);
    append_u32le(makernote.data(), &size, value_off);
    append_u32le(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, raw, raw_size);

    return build_tiff_with_make_makernote_fixture(make,
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_nikon_lensdata0100_fixture()
{
    static const unsigned char raw[] = {
        '0', '1', '0', '0', 0U, 0U, 101U, 68U, 96U, 152U, 52U, 60U, 107U
    };

    return build_tiff_nikon_single_main_bytes_fixture("Canon", 0x0098U,
                                                      raw, sizeof(raw));
}

static ByteVec
build_tiff_nikon_lensdata0101_fixture()
{
    static const unsigned char raw[] = {
        '0', '1', '0', '1', 23U, 52U, 0U, 0U, 44U, 15U,
        120U, 33U, 68U, 24U, 70U, 121U, 52U, 60U, 107U
    };

    return build_tiff_nikon_single_main_bytes_fixture("Canon", 0x0098U,
                                                      raw, sizeof(raw));
}

static ByteVec
build_tiff_nikon_colorbalancec_fixture()
{
    std::array<unsigned char, 512> raw {};
    static const std::uint32_t level_offsets[11] = {
        0x0038U, 0x004CU, 0x0060U, 0x0074U, 0x0088U, 0x009CU,
        0x00B0U, 0x00C4U, 0x00D8U, 0x0100U, 0x0114U
    };
    static const std::uint32_t level_sets[11][4] = {
        { 444U, 512U, 513U, 396U }, { 374U, 512U, 513U, 480U },
        { 410U, 512U, 513U, 437U }, { 467U, 512U, 513U, 359U },
        { 253U, 512U, 513U, 714U }, { 394U, 512U, 513U, 636U },
        { 397U, 512U, 513U, 466U }, { 447U, 512U, 513U, 375U },
        { 440U, 512U, 513U, 411U }, { 0U, 0U, 0U, 0U },
        { 421U, 512U, 513U, 409U }
    };
    std::uint32_t i;
    std::uint32_t k;

    std::memset(raw.data(), 0, 0x0124U);
    std::memcpy(raw.data(), "NRW 0104", 8U);
    write_u16le_at(raw.data(), 0x0020U, 200U);
    for (i = 0U; i < 11U; ++i) {
        for (k = 0U; k < 4U; ++k) {
            write_u32le_at(raw.data(), level_offsets[i] + (k * 4U),
                           level_sets[i][k]);
        }
    }

    return build_tiff_nikon_single_main_bytes_fixture("Nikon", 0x0014U,
                                                      raw.data(), 0x0124U);
}

static ByteVec
build_tiff_nikon_main_single_long_fixture(const char* model,
                                          std::uint16_t tag,
                                          std::uint32_t value)
{
    std::array<unsigned char, 64> makernote {};
    std::size_t size;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 1U);

    append_u16le(makernote.data(), &size, tag);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, value);
    append_u32le(makernote.data(), &size, 0U);

    return build_tiff_with_make_model_makernote_fixture("Nikon", model,
                                                        makernote.data(), size);
}

static ByteVec
build_tiff_nikon_info_blocks_fixture()
{
    std::array<unsigned char, 512> makernote {};
    std::size_t size;
    std::uint32_t off_pc_pos;
    std::uint32_t off_iso_pos;
    std::uint32_t off_hdr_pos;
    std::uint32_t off_loc_pos;
    std::uint32_t off_payload;
    std::size_t i;

    size = 0U;
    append_bytes(makernote.data(), &size, "Nikon\0", 6U);
    append_u8(makernote.data(), &size, 2U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_text(makernote.data(), &size, "II");
    append_u16le(makernote.data(), &size, 42U);
    append_u32le(makernote.data(), &size, 8U);
    append_u16le(makernote.data(), &size, 6U);

    append_u16le(makernote.data(), &size, 0x0001U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 0x01020304U);

    append_u16le(makernote.data(), &size, 0x0023U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 66U);
    off_pc_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0024U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 0x0201FDE4U);

    append_u16le(makernote.data(), &size, 0x0025U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 10U);
    off_iso_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0035U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 8U);
    off_hdr_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u16le(makernote.data(), &size, 0x0039U);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, 17U);
    off_loc_pos = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);

    append_u32le(makernote.data(), &size, 0U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_pc_pos, off_payload);
    append_bytes(makernote.data(), &size, "0200", 4U);
    append_bytes(makernote.data(), &size, "NEUTRAL", 7U);
    for (i = 0U; i < 13U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    append_bytes(makernote.data(), &size, "STANDARD", 8U);
    for (i = 0U; i < 12U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    for (i = 0U; i < 21U; ++i) {
        append_u8(makernote.data(), &size, 0U);
    }
    append_u8(makernote.data(), &size, 15U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_iso_pos, off_payload);
    append_bytes(makernote.data(), &size, "0100", 4U);
    append_u32le(makernote.data(), &size, 0U);
    append_u16le(makernote.data(), &size, 400U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_hdr_pos, off_payload);
    append_bytes(makernote.data(), &size, "0100", 4U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 4U);

    off_payload = (std::uint32_t)(size - 10U);
    write_u32le_at(makernote.data(), off_loc_pos, off_payload);
    append_bytes(makernote.data(), &size, "0100", 4U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_u8(makernote.data(), &size, 0U);
    append_bytes(makernote.data(), &size, "TOKYO-JP", 8U);

    return build_tiff_with_make_makernote_fixture("Nikon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_filterinfo_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;
    std::uint32_t filter_size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x4024U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 12U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    filter_size = (std::uint32_t)size;
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 36U);
    append_u32le(makernote.data(), &size, 2U);

    append_u32le(makernote.data(), &size, 0x0402U);
    append_u32le(makernote.data(), &size, 1U);
    append_u32le(makernote.data(), &size, 2U);

    append_u32le(makernote.data(), &size, 0x0403U);
    append_u32le(makernote.data(), &size, 2U);
    append_u32le(makernote.data(), &size, 300U);
    append_u32le(makernote.data(), &size, 700U);

    write_u32le_at(makernote.data(), filter_size,
                   (std::uint32_t)(size - filter_size));
    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_timeinfo_makernote_fixture()
{
    std::array<unsigned char, 128> makernote {};
    std::size_t size;

    size = 0U;
    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x0035U);
    append_u16le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 4U);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 0U);
    append_u32le(makernote.data(), &size, 540U);
    append_u32le(makernote.data(), &size, 1234U);
    append_u32le(makernote.data(), &size, 1U);

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_camera_info_psinfo_makernote_fixture()
{
    std::array<unsigned char, 2048> makernote {};
    std::size_t size;
    std::uint32_t cam_off;
    std::uint32_t cam_bytes;

    size      = 0U;
    cam_bytes = 0x025BU + 0x00DEU;

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x000DU);
    append_u16le(makernote.data(), &size, 7U);
    append_u32le(makernote.data(), &size, cam_bytes);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    cam_off = (std::uint32_t)size;
    std::memset(makernote.data() + size, 0, cam_bytes);
    write_u16le_at(makernote.data(), cam_off + 0U, 1U);
    write_u16le_at(makernote.data(), cam_off + 2U, 0x0003U);
    write_u16le_at(makernote.data(), cam_off + 4U, 3U);
    write_u32le_at(makernote.data(), cam_off + 6U, 1U);
    write_u16le_at(makernote.data(), cam_off + 10U, 42U);
    write_u32le_at(makernote.data(), cam_off + 14U, 0U);
    write_u32le_at(makernote.data(), cam_off + 0x025BU + 0x0004U, 3U);
    write_u16le_at(makernote.data(), cam_off + 0x025BU + 0x00D8U, 129U);
    size += cam_bytes;

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_canon_colordata8_makernote_fixture()
{
    std::array<unsigned char, 4096> makernote {};
    std::size_t size;
    std::uint32_t color_off;
    std::uint32_t color_count;
    std::uint32_t color_bytes;

    size        = 0U;
    color_count = 1353U;
    color_bytes = color_count * 2U;

    append_u16le(makernote.data(), &size, 1U);
    append_u16le(makernote.data(), &size, 0x4001U);
    append_u16le(makernote.data(), &size, 3U);
    append_u32le(makernote.data(), &size, color_count);
    append_u32le(makernote.data(), &size, 18U);
    append_u32le(makernote.data(), &size, 0U);

    color_off = (std::uint32_t)size;
    std::memset(makernote.data() + size, 0, color_bytes);
    write_u16le_at(makernote.data(), color_off + (0x0000U * 2U), 14U);
    write_u16le_at(makernote.data(), color_off + (0x003FU * 2U), 777U);
    write_u16le_at(makernote.data(), color_off + (0x0043U * 2U), 6100U);
    write_u16le_at(makernote.data(), color_off + (0x0107U * 2U), 100U);
    write_u16le_at(makernote.data(), color_off + (0x0108U * 2U),
                   (std::uint16_t)(0xFFFFU - 24U));
    write_u16le_at(makernote.data(), color_off + (0x0109U * 2U), 300U);
    write_u16le_at(makernote.data(), color_off + (0x010AU * 2U), 5200U);
    write_u16le_at(makernote.data(), color_off + (0x010BU * 2U),
                   (std::uint16_t)(0xFFFFU - 9U));
    write_u16le_at(makernote.data(), color_off + (0x010CU * 2U), 20U);
    write_u16le_at(makernote.data(), color_off + (0x010DU * 2U),
                   (std::uint16_t)(0xFFFFU - 29U));
    write_u16le_at(makernote.data(), color_off + (0x010EU * 2U), 40U);
    size += color_bytes;

    return build_tiff_with_make_makernote_fixture("Canon",
                                                  makernote.data(), size);
}

static ByteVec
build_tiff_geotiff_fixture()
{
    static const unsigned char geodouble_bits[8] = {
        0x00U, 0x00U, 0x00U, 0x40U, 0xA6U, 0x54U, 0x58U, 0x41U
    };
    std::array<unsigned char, 160> file {};
    std::size_t size = 0U;

    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 3U);

    append_u16le(file.data(), &size, 0x87AFU);
    append_u16le(file.data(), &size, 3U);
    append_u32le(file.data(), &size, 16U);
    append_u32le(file.data(), &size, 50U);

    append_u16le(file.data(), &size, 0x87B0U);
    append_u16le(file.data(), &size, 12U);
    append_u32le(file.data(), &size, 1U);
    append_u32le(file.data(), &size, 82U);

    append_u16le(file.data(), &size, 0x87B1U);
    append_u16le(file.data(), &size, 2U);
    append_u32le(file.data(), &size, 13U);
    append_u32le(file.data(), &size, 90U);

    append_u32le(file.data(), &size, 0U);

    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0U);
    append_u16le(file.data(), &size, 3U);

    append_u16le(file.data(), &size, 1024U);
    append_u16le(file.data(), &size, 0U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 2U);

    append_u16le(file.data(), &size, 1026U);
    append_u16le(file.data(), &size, 0x87B1U);
    append_u16le(file.data(), &size, 13U);
    append_u16le(file.data(), &size, 0U);

    append_u16le(file.data(), &size, 2057U);
    append_u16le(file.data(), &size, 0x87B0U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0U);

    append_bytes(file.data(), &size, geodouble_bits, sizeof(geodouble_bits));
    append_text(file.data(), &size, "TestCitation|");

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_tiff_printim_fixture()
{
    static const unsigned char printim_magic[8] = {
        'P', 'r', 'i', 'n', 't', 'I', 'M', 0
    };
    std::array<unsigned char, 96> file {};
    std::size_t size = 0U;

    append_text(file.data(), &size, "II");
    append_u16le(file.data(), &size, 42U);
    append_u32le(file.data(), &size, 8U);
    append_u16le(file.data(), &size, 1U);

    append_u16le(file.data(), &size, 0xC4A5U);
    append_u16le(file.data(), &size, 7U);
    append_u32le(file.data(), &size, 22U);
    append_u32le(file.data(), &size, 26U);

    append_u32le(file.data(), &size, 0U);
    append_bytes(file.data(), &size, printim_magic, sizeof(printim_magic));
    append_text(file.data(), &size, "0300");
    append_u16le(file.data(), &size, 0U);
    append_u16le(file.data(), &size, 1U);
    append_u16le(file.data(), &size, 0x0001U);
    append_u32le(file.data(), &size, 0x00160016U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_fields_fixture()
{
    static constexpr unsigned char kAuxSubtypeAlpha[1] = { 0x11U };
    static constexpr unsigned char kAuxSubtypeDepth[2] = { 0xAAU, 0xBBU };
    std::array<unsigned char, 2048> file {};
    std::array<unsigned char, 64> infe_exif {};
    std::array<unsigned char, 96> infe_xmp {};
    std::array<unsigned char, 96> infe_jumb {};
    std::array<unsigned char, 384> iinf_payload {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 16> ispe_payload {};
    std::array<unsigned char, 8> irot_payload {};
    std::array<unsigned char, 8> imir_payload {};
    std::array<unsigned char, 64> auxc_alpha_payload {};
    std::array<unsigned char, 64> auxc_depth_payload {};
    std::array<unsigned char, 192> ipco_payload {};
    std::array<unsigned char, 48> ipma_payload {};
    std::array<unsigned char, 256> iprp_payload {};
    std::array<unsigned char, 16> auxl_payload {};
    std::array<unsigned char, 16> dimg_payload {};
    std::array<unsigned char, 16> thmb_payload {};
    std::array<unsigned char, 128> iref_payload {};
    std::array<unsigned char, 1024> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t infe_exif_size;
    std::size_t infe_xmp_size;
    std::size_t infe_jumb_size;
    std::size_t iinf_size;
    std::size_t pitm_size;
    std::size_t ispe_size;
    std::size_t irot_size;
    std::size_t imir_size;
    std::size_t auxc_alpha_size;
    std::size_t auxc_depth_size;
    std::size_t ipco_size;
    std::size_t ipma_size;
    std::size_t iprp_size;
    std::size_t auxl_size;
    std::size_t dimg_size;
    std::size_t thmb_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif.data(), &infe_exif_size, 2U);
    append_u16be(infe_exif.data(), &infe_exif_size, 1U);
    append_u16be(infe_exif.data(), &infe_exif_size, 0U);
    append_u32be(infe_exif.data(), &infe_exif_size,
                 make_fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif.data(), &infe_exif_size, "Exif");
    append_u8(infe_exif.data(), &infe_exif_size, 0U);

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp.data(), &infe_xmp_size, 2U);
    append_u16be(infe_xmp.data(), &infe_xmp_size, 2U);
    append_u16be(infe_xmp.data(), &infe_xmp_size, 7U);
    append_u32be(infe_xmp.data(), &infe_xmp_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp.data(), &infe_xmp_size, "XMP");
    append_u8(infe_xmp.data(), &infe_xmp_size, 0U);
    append_text(infe_xmp.data(), &infe_xmp_size, "application/rdf+xml");
    append_u8(infe_xmp.data(), &infe_xmp_size, 0U);
    append_u8(infe_xmp.data(), &infe_xmp_size, 0U);

    infe_jumb_size = 0U;
    append_fullbox_header(infe_jumb.data(), &infe_jumb_size, 2U);
    append_u16be(infe_jumb.data(), &infe_jumb_size, 3U);
    append_u16be(infe_jumb.data(), &infe_jumb_size, 0U);
    append_u32be(infe_jumb.data(), &infe_jumb_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_jumb.data(), &infe_jumb_size, "C2PA");
    append_u8(infe_jumb.data(), &infe_jumb_size, 0U);
    append_text(infe_jumb.data(), &infe_jumb_size, "application/jumbf");
    append_u8(infe_jumb.data(), &infe_jumb_size, 0U);
    append_u8(infe_jumb.data(), &infe_jumb_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload.data(), &iinf_size, 0U);
    append_u16be(iinf_payload.data(), &iinf_size, 3U);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_exif.data(),
                    infe_exif_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_xmp.data(),
                    infe_xmp_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_jumb.data(),
                    infe_jumb_size);

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 2U);

    ispe_size = 0U;
    append_fullbox_header(ispe_payload.data(), &ispe_size, 0U);
    append_u32be(ispe_payload.data(), &ispe_size, 640U);
    append_u32be(ispe_payload.data(), &ispe_size, 480U);

    irot_size = 0U;
    append_u8(irot_payload.data(), &irot_size, 1U);

    imir_size = 0U;
    append_u8(imir_payload.data(), &imir_size, 1U);

    auxc_alpha_size = 0U;
    append_fullbox_header(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);
    append_text(auxc_alpha_payload.data(), &auxc_alpha_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);
    append_bytes(auxc_alpha_payload.data(), &auxc_alpha_size,
                 kAuxSubtypeAlpha, sizeof(kAuxSubtypeAlpha));

    auxc_depth_size = 0U;
    append_fullbox_header(auxc_depth_payload.data(), &auxc_depth_size, 0U);
    append_text(auxc_depth_payload.data(), &auxc_depth_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_depth_payload.data(), &auxc_depth_size, 0U);
    append_bytes(auxc_depth_payload.data(), &auxc_depth_size,
                 kAuxSubtypeDepth, sizeof(kAuxSubtypeDepth));

    ipco_size = 0U;
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('i', 's', 'p', 'e'), ispe_payload.data(),
                    ispe_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('i', 'r', 'o', 't'), irot_payload.data(),
                    irot_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('i', 'm', 'i', 'r'), imir_payload.data(),
                    imir_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_alpha_payload.data(), auxc_alpha_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_depth_payload.data(), auxc_depth_size);

    ipma_size = 0U;
    append_fullbox_header(ipma_payload.data(), &ipma_size, 0U);
    append_u32be(ipma_payload.data(), &ipma_size, 3U);
    append_u16be(ipma_payload.data(), &ipma_size, 2U);
    append_u8(ipma_payload.data(), &ipma_size, 3U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 2U);
    append_u8(ipma_payload.data(), &ipma_size, 3U);
    append_u16be(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 4U);
    append_u16be(ipma_payload.data(), &ipma_size, 3U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 5U);

    iprp_size = 0U;
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'c', 'o'), ipco_payload.data(),
                    ipco_size);
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'm', 'a'), ipma_payload.data(),
                    ipma_size);

    auxl_size = 0U;
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 1U);
    append_u16be(auxl_payload.data(), &auxl_size, 3U);

    dimg_size = 0U;
    append_u16be(dimg_payload.data(), &dimg_size, 2U);
    append_u16be(dimg_payload.data(), &dimg_size, 1U);
    append_u16be(dimg_payload.data(), &dimg_size, 3U);

    thmb_size = 0U;
    append_u16be(thmb_payload.data(), &thmb_size, 2U);
    append_u16be(thmb_payload.data(), &thmb_size, 1U);
    append_u16be(thmb_payload.data(), &thmb_size, 1U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_payload.data(),
                    auxl_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('d', 'i', 'm', 'g'), dimg_payload.data(),
                    dimg_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('t', 'h', 'm', 'b'), thmb_payload.data(),
                    thmb_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'), iinf_payload.data(),
                    iinf_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'p', 'r', 'p'), iprp_payload.data(),
                    iprp_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_aux_subtype_kinds_fixture()
{
    static constexpr unsigned char kAsciiZSubtype[] = {
        'p', 'r', 'o', 'f', 'i', 'l', 'e', 0x00U
    };
    static constexpr unsigned char kU64Subtype[8] = {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U
    };
    static constexpr unsigned char kUuidSubtype[16] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU
    };
    std::array<unsigned char, 2048> file {};
    std::array<unsigned char, 96> infe_primary {};
    std::array<unsigned char, 96> infe_aux1 {};
    std::array<unsigned char, 96> infe_aux2 {};
    std::array<unsigned char, 96> infe_aux3 {};
    std::array<unsigned char, 512> iinf_payload {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 96> auxc_text_payload {};
    std::array<unsigned char, 96> auxc_u64_payload {};
    std::array<unsigned char, 112> auxc_uuid_payload {};
    std::array<unsigned char, 352> ipco_payload {};
    std::array<unsigned char, 64> ipma_payload {};
    std::array<unsigned char, 448> iprp_payload {};
    std::array<unsigned char, 16> auxl_payload {};
    std::array<unsigned char, 64> iref_payload {};
    std::array<unsigned char, 1024> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t infe_primary_size;
    std::size_t infe_aux1_size;
    std::size_t infe_aux2_size;
    std::size_t infe_aux3_size;
    std::size_t iinf_size;
    std::size_t pitm_size;
    std::size_t auxc_text_size;
    std::size_t auxc_u64_size;
    std::size_t auxc_uuid_size;
    std::size_t ipco_size;
    std::size_t ipma_size;
    std::size_t iprp_size;
    std::size_t auxl_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    infe_primary_size = 0U;
    append_fullbox_header(infe_primary.data(), &infe_primary_size, 2U);
    append_u16be(infe_primary.data(), &infe_primary_size, 1U);
    append_u16be(infe_primary.data(), &infe_primary_size, 0U);
    append_u32be(infe_primary.data(), &infe_primary_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_primary.data(), &infe_primary_size, "Primary");
    append_u8(infe_primary.data(), &infe_primary_size, 0U);
    append_text(infe_primary.data(), &infe_primary_size,
                "application/octet-stream");
    append_u8(infe_primary.data(), &infe_primary_size, 0U);
    append_u8(infe_primary.data(), &infe_primary_size, 0U);

    infe_aux1_size = 0U;
    append_fullbox_header(infe_aux1.data(), &infe_aux1_size, 2U);
    append_u16be(infe_aux1.data(), &infe_aux1_size, 2U);
    append_u16be(infe_aux1.data(), &infe_aux1_size, 0U);
    append_u32be(infe_aux1.data(), &infe_aux1_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_aux1.data(), &infe_aux1_size, "AuxText");
    append_u8(infe_aux1.data(), &infe_aux1_size, 0U);
    append_text(infe_aux1.data(), &infe_aux1_size,
                "application/octet-stream");
    append_u8(infe_aux1.data(), &infe_aux1_size, 0U);
    append_u8(infe_aux1.data(), &infe_aux1_size, 0U);

    infe_aux2_size = 0U;
    append_fullbox_header(infe_aux2.data(), &infe_aux2_size, 2U);
    append_u16be(infe_aux2.data(), &infe_aux2_size, 3U);
    append_u16be(infe_aux2.data(), &infe_aux2_size, 0U);
    append_u32be(infe_aux2.data(), &infe_aux2_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_aux2.data(), &infe_aux2_size, "AuxU64");
    append_u8(infe_aux2.data(), &infe_aux2_size, 0U);
    append_text(infe_aux2.data(), &infe_aux2_size,
                "application/octet-stream");
    append_u8(infe_aux2.data(), &infe_aux2_size, 0U);
    append_u8(infe_aux2.data(), &infe_aux2_size, 0U);

    infe_aux3_size = 0U;
    append_fullbox_header(infe_aux3.data(), &infe_aux3_size, 2U);
    append_u16be(infe_aux3.data(), &infe_aux3_size, 4U);
    append_u16be(infe_aux3.data(), &infe_aux3_size, 0U);
    append_u32be(infe_aux3.data(), &infe_aux3_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_aux3.data(), &infe_aux3_size, "AuxUuid");
    append_u8(infe_aux3.data(), &infe_aux3_size, 0U);
    append_text(infe_aux3.data(), &infe_aux3_size,
                "application/octet-stream");
    append_u8(infe_aux3.data(), &infe_aux3_size, 0U);
    append_u8(infe_aux3.data(), &infe_aux3_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload.data(), &iinf_size, 0U);
    append_u16be(iinf_payload.data(), &iinf_size, 4U);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_primary.data(),
                    infe_primary_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_aux1.data(),
                    infe_aux1_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_aux2.data(),
                    infe_aux2_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'), infe_aux3.data(),
                    infe_aux3_size);

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    auxc_text_size = 0U;
    append_fullbox_header(auxc_text_payload.data(), &auxc_text_size, 0U);
    append_text(auxc_text_payload.data(), &auxc_text_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_text_payload.data(), &auxc_text_size, 0U);
    append_bytes(auxc_text_payload.data(), &auxc_text_size, kAsciiZSubtype,
                 sizeof(kAsciiZSubtype));

    auxc_u64_size = 0U;
    append_fullbox_header(auxc_u64_payload.data(), &auxc_u64_size, 0U);
    append_text(auxc_u64_payload.data(), &auxc_u64_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_u64_payload.data(), &auxc_u64_size, 0U);
    append_bytes(auxc_u64_payload.data(), &auxc_u64_size, kU64Subtype,
                 sizeof(kU64Subtype));

    auxc_uuid_size = 0U;
    append_fullbox_header(auxc_uuid_payload.data(), &auxc_uuid_size, 0U);
    append_text(auxc_uuid_payload.data(), &auxc_uuid_size,
                "urn:com:apple:photo:2020:aux:matte");
    append_u8(auxc_uuid_payload.data(), &auxc_uuid_size, 0U);
    append_bytes(auxc_uuid_payload.data(), &auxc_uuid_size, kUuidSubtype,
                 sizeof(kUuidSubtype));

    ipco_size = 0U;
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_text_payload.data(), auxc_text_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_u64_payload.data(), auxc_u64_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_uuid_payload.data(), auxc_uuid_size);

    ipma_size = 0U;
    append_fullbox_header(ipma_payload.data(), &ipma_size, 0U);
    append_u32be(ipma_payload.data(), &ipma_size, 4U);
    append_u16be(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 0U);
    append_u16be(ipma_payload.data(), &ipma_size, 2U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u16be(ipma_payload.data(), &ipma_size, 3U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 2U);
    append_u16be(ipma_payload.data(), &ipma_size, 4U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 3U);

    iprp_size = 0U;
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'c', 'o'), ipco_payload.data(),
                    ipco_size);
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'm', 'a'), ipma_payload.data(),
                    ipma_size);

    auxl_size = 0U;
    append_u16be(auxl_payload.data(), &auxl_size, 1U);
    append_u16be(auxl_payload.data(), &auxl_size, 3U);
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 3U);
    append_u16be(auxl_payload.data(), &auxl_size, 4U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_payload.data(),
                    auxl_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'), iinf_payload.data(),
                    iinf_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'p', 'r', 'p'), iprp_payload.data(),
                    iprp_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_pred_fixture()
{
    std::array<unsigned char, 256> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 16> pred_payload {};
    std::array<unsigned char, 64> iref_payload {};
    std::array<unsigned char, 128> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t pred_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    pred_size = 0U;
    append_u16be(pred_payload.data(), &pred_size, 9U);
    append_u16be(pred_payload.data(), &pred_size, 2U);
    append_u16be(pred_payload.data(), &pred_size, 10U);
    append_u16be(pred_payload.data(), &pred_size, 11U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('p', 'r', 'e', 'd'), pred_payload.data(),
                    pred_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_v1_auxl_fixture()
{
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 24> auxl_payload {};
    std::array<unsigned char, 64> iref_payload {};
    std::array<unsigned char, 128> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t auxl_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 1U);
    append_u32be(pitm_payload.data(), &pitm_size, 0x10001U);

    auxl_size = 0U;
    append_u32be(auxl_payload.data(), &auxl_size, 0x10001U);
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u32be(auxl_payload.data(), &auxl_size, 0x10002U);
    append_u32be(auxl_payload.data(), &auxl_size, 0x10003U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 1U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_payload.data(),
                    auxl_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_nonprimary_typed_iref_fixture()
{
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 16> dimg_payload {};
    std::array<unsigned char, 16> thmb_payload {};
    std::array<unsigned char, 16> cdsc_payload {};
    std::array<unsigned char, 128> iref_payload {};
    std::array<unsigned char, 256> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t dimg_size;
    std::size_t thmb_size;
    std::size_t cdsc_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    dimg_size = 0U;
    append_u16be(dimg_payload.data(), &dimg_size, 2U);
    append_u16be(dimg_payload.data(), &dimg_size, 2U);
    append_u16be(dimg_payload.data(), &dimg_size, 5U);
    append_u16be(dimg_payload.data(), &dimg_size, 6U);

    thmb_size = 0U;
    append_u16be(thmb_payload.data(), &thmb_size, 3U);
    append_u16be(thmb_payload.data(), &thmb_size, 1U);
    append_u16be(thmb_payload.data(), &thmb_size, 7U);

    cdsc_size = 0U;
    append_u16be(cdsc_payload.data(), &cdsc_size, 4U);
    append_u16be(cdsc_payload.data(), &cdsc_size, 1U);
    append_u16be(cdsc_payload.data(), &cdsc_size, 8U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('d', 'i', 'm', 'g'), dimg_payload.data(),
                    dimg_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('t', 'h', 'm', 'b'), thmb_payload.data(),
                    thmb_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('c', 'd', 's', 'c'), cdsc_payload.data(),
                    cdsc_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_v1_nonprimary_typed_iref_fixture()
{
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 24> dimg_payload {};
    std::array<unsigned char, 20> thmb_payload {};
    std::array<unsigned char, 20> cdsc_payload {};
    std::array<unsigned char, 128> iref_payload {};
    std::array<unsigned char, 256> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t dimg_size;
    std::size_t thmb_size;
    std::size_t cdsc_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 1U);
    append_u32be(pitm_payload.data(), &pitm_size, 0x10001U);

    dimg_size = 0U;
    append_u32be(dimg_payload.data(), &dimg_size, 0x20002U);
    append_u16be(dimg_payload.data(), &dimg_size, 2U);
    append_u32be(dimg_payload.data(), &dimg_size, 0x30005U);
    append_u32be(dimg_payload.data(), &dimg_size, 0x30006U);

    thmb_size = 0U;
    append_u32be(thmb_payload.data(), &thmb_size, 0x20003U);
    append_u16be(thmb_payload.data(), &thmb_size, 1U);
    append_u32be(thmb_payload.data(), &thmb_size, 0x30007U);

    cdsc_size = 0U;
    append_u32be(cdsc_payload.data(), &cdsc_size, 0x20004U);
    append_u16be(cdsc_payload.data(), &cdsc_size, 1U);
    append_u32be(cdsc_payload.data(), &cdsc_size, 0x30008U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 1U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('d', 'i', 'm', 'g'), dimg_payload.data(),
                    dimg_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('t', 'h', 'm', 'b'), thmb_payload.data(),
                    thmb_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('c', 'd', 's', 'c'), cdsc_payload.data(),
                    cdsc_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_duplicate_edges_fixture()
{
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 16> auxl_a_payload {};
    std::array<unsigned char, 16> auxl_b_payload {};
    std::array<unsigned char, 16> dimg_a_payload {};
    std::array<unsigned char, 16> dimg_b_payload {};
    std::array<unsigned char, 16> thmb_a_payload {};
    std::array<unsigned char, 16> thmb_b_payload {};
    std::array<unsigned char, 16> cdsc_a_payload {};
    std::array<unsigned char, 16> cdsc_b_payload {};
    std::array<unsigned char, 256> iref_payload {};
    std::array<unsigned char, 384> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t auxl_a_size;
    std::size_t auxl_b_size;
    std::size_t dimg_a_size;
    std::size_t dimg_b_size;
    std::size_t thmb_a_size;
    std::size_t thmb_b_size;
    std::size_t cdsc_a_size;
    std::size_t cdsc_b_size;
    std::size_t iref_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    auxl_a_size = 0U;
    append_u16be(auxl_a_payload.data(), &auxl_a_size, 1U);
    append_u16be(auxl_a_payload.data(), &auxl_a_size, 3U);
    append_u16be(auxl_a_payload.data(), &auxl_a_size, 2U);
    append_u16be(auxl_a_payload.data(), &auxl_a_size, 2U);
    append_u16be(auxl_a_payload.data(), &auxl_a_size, 3U);

    auxl_b_size = 0U;
    append_u16be(auxl_b_payload.data(), &auxl_b_size, 1U);
    append_u16be(auxl_b_payload.data(), &auxl_b_size, 1U);
    append_u16be(auxl_b_payload.data(), &auxl_b_size, 3U);

    dimg_a_size = 0U;
    append_u16be(dimg_a_payload.data(), &dimg_a_size, 2U);
    append_u16be(dimg_a_payload.data(), &dimg_a_size, 2U);
    append_u16be(dimg_a_payload.data(), &dimg_a_size, 5U);
    append_u16be(dimg_a_payload.data(), &dimg_a_size, 5U);

    dimg_b_size = 0U;
    append_u16be(dimg_b_payload.data(), &dimg_b_size, 4U);
    append_u16be(dimg_b_payload.data(), &dimg_b_size, 1U);
    append_u16be(dimg_b_payload.data(), &dimg_b_size, 5U);

    thmb_a_size = 0U;
    append_u16be(thmb_a_payload.data(), &thmb_a_size, 3U);
    append_u16be(thmb_a_payload.data(), &thmb_a_size, 2U);
    append_u16be(thmb_a_payload.data(), &thmb_a_size, 7U);
    append_u16be(thmb_a_payload.data(), &thmb_a_size, 7U);

    thmb_b_size = 0U;
    append_u16be(thmb_b_payload.data(), &thmb_b_size, 3U);
    append_u16be(thmb_b_payload.data(), &thmb_b_size, 1U);
    append_u16be(thmb_b_payload.data(), &thmb_b_size, 8U);

    cdsc_a_size = 0U;
    append_u16be(cdsc_a_payload.data(), &cdsc_a_size, 4U);
    append_u16be(cdsc_a_payload.data(), &cdsc_a_size, 3U);
    append_u16be(cdsc_a_payload.data(), &cdsc_a_size, 8U);
    append_u16be(cdsc_a_payload.data(), &cdsc_a_size, 9U);
    append_u16be(cdsc_a_payload.data(), &cdsc_a_size, 9U);

    cdsc_b_size = 0U;
    append_u16be(cdsc_b_payload.data(), &cdsc_b_size, 5U);
    append_u16be(cdsc_b_payload.data(), &cdsc_b_size, 1U);
    append_u16be(cdsc_b_payload.data(), &cdsc_b_size, 8U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_a_payload.data(),
                    auxl_a_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_b_payload.data(),
                    auxl_b_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('d', 'i', 'm', 'g'), dimg_a_payload.data(),
                    dimg_a_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('d', 'i', 'm', 'g'), dimg_b_payload.data(),
                    dimg_b_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('t', 'h', 'm', 'b'), thmb_a_payload.data(),
                    thmb_a_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('t', 'h', 'm', 'b'), thmb_b_payload.data(),
                    thmb_b_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('c', 'd', 's', 'c'), cdsc_a_payload.data(),
                    cdsc_a_size);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('c', 'd', 's', 'c'), cdsc_b_payload.data(),
                    cdsc_b_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_item_info_rows_fixture()
{
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 96> infe_preview {};
    std::array<unsigned char, 64> infe_exif {};
    std::array<unsigned char, 256> iinf_payload {};
    std::array<unsigned char, 384> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t infe_preview_size;
    std::size_t infe_exif_size;
    std::size_t iinf_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 1U);
    append_u32be(pitm_payload.data(), &pitm_size, 0x10002U);

    infe_preview_size = 0U;
    append_fullbox_header(infe_preview.data(), &infe_preview_size, 3U);
    append_u32be(infe_preview.data(), &infe_preview_size, 0x10001U);
    append_u16be(infe_preview.data(), &infe_preview_size, 0U);
    append_u32be(infe_preview.data(), &infe_preview_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_preview.data(), &infe_preview_size, "preview");
    append_u8(infe_preview.data(), &infe_preview_size, 0U);
    append_text(infe_preview.data(), &infe_preview_size, "image/png");
    append_u8(infe_preview.data(), &infe_preview_size, 0U);
    append_text(infe_preview.data(), &infe_preview_size, "gzip");
    append_u8(infe_preview.data(), &infe_preview_size, 0U);

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif.data(), &infe_exif_size, 3U);
    append_u32be(infe_exif.data(), &infe_exif_size, 0x10002U);
    append_u16be(infe_exif.data(), &infe_exif_size, 0U);
    append_u32be(infe_exif.data(), &infe_exif_size,
                 make_fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif.data(), &infe_exif_size, "exif");
    append_u8(infe_exif.data(), &infe_exif_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload.data(), &iinf_size, 2U);
    append_u32be(iinf_payload.data(), &iinf_size, 2U);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'),
                    infe_preview.data(), infe_preview_size);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'),
                    infe_exif.data(), infe_exif_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'),
                    pitm_payload.data(), pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'),
                    iinf_payload.data(), iinf_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_primary_mime_item_info_fixture()
{
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 128> infe_payload {};
    std::array<unsigned char, 192> iinf_payload {};
    std::array<unsigned char, 320> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t infe_size;
    std::size_t iinf_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    infe_size = 0U;
    append_fullbox_header(infe_payload.data(), &infe_size, 2U);
    append_u16be(infe_payload.data(), &infe_size, 1U);
    append_u16be(infe_payload.data(), &infe_size, 7U);
    append_u32be(infe_payload.data(), &infe_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_payload.data(), &infe_size, "payload");
    append_u8(infe_payload.data(), &infe_size, 0U);
    append_text(infe_payload.data(), &infe_size, "application/rdf+xml");
    append_u8(infe_payload.data(), &infe_size, 0U);
    append_text(infe_payload.data(), &infe_size, "gzip");
    append_u8(infe_payload.data(), &infe_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload.data(), &iinf_size, 2U);
    append_u32be(iinf_payload.data(), &iinf_size, 1U);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'),
                    infe_payload.data(), infe_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'),
                    pitm_payload.data(), pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'),
                    iinf_payload.data(), iinf_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_item_info_without_pitm_fixture()
{
    std::array<unsigned char, 384> file {};
    std::array<unsigned char, 128> infe_payload {};
    std::array<unsigned char, 192> iinf_payload {};
    std::array<unsigned char, 256> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t infe_size;
    std::size_t iinf_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    infe_size = 0U;
    append_fullbox_header(infe_payload.data(), &infe_size, 2U);
    append_u16be(infe_payload.data(), &infe_size, 3U);
    append_u16be(infe_payload.data(), &infe_size, 0U);
    append_u32be(infe_payload.data(), &infe_size,
                 make_fourcc('m', 'i', 'm', 'e'));
    append_text(infe_payload.data(), &infe_size, "sidecar");
    append_u8(infe_payload.data(), &infe_size, 0U);
    append_text(infe_payload.data(), &infe_size, "application/json");
    append_u8(infe_payload.data(), &infe_size, 0U);
    append_u8(infe_payload.data(), &infe_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload.data(), &iinf_size, 2U);
    append_u32be(iinf_payload.data(), &iinf_size, 1U);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'),
                    infe_payload.data(), infe_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'),
                    iinf_payload.data(), iinf_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_primary_uri_item_info_fixture()
{
    std::array<unsigned char, 512> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 128> infe_payload {};
    std::array<unsigned char, 192> iinf_payload {};
    std::array<unsigned char, 320> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t infe_size;
    std::size_t iinf_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    infe_size = 0U;
    append_fullbox_header(infe_payload.data(), &infe_size, 2U);
    append_u16be(infe_payload.data(), &infe_size, 1U);
    append_u16be(infe_payload.data(), &infe_size, 3U);
    append_u32be(infe_payload.data(), &infe_size,
                 make_fourcc('u', 'r', 'i', ' '));
    append_text(infe_payload.data(), &infe_size, "link");
    append_u8(infe_payload.data(), &infe_size, 0U);
    append_text(infe_payload.data(), &infe_size, "https://ns.example/item");
    append_u8(infe_payload.data(), &infe_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload.data(), &iinf_size, 2U);
    append_u32be(iinf_payload.data(), &iinf_size, 1U);
    append_bmff_box(iinf_payload.data(), &iinf_size,
                    make_fourcc('i', 'n', 'f', 'e'),
                    infe_payload.data(), infe_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'),
                    pitm_payload.data(), pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'i', 'n', 'f'),
                    iinf_payload.data(), iinf_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_auxc_semantics_fixture()
{
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 16> auxl_payload {};
    std::array<unsigned char, 64> iref_payload {};
    std::array<unsigned char, 64> auxc_depth_payload {};
    std::array<unsigned char, 64> auxc_alpha_payload {};
    std::array<unsigned char, 160> ipco_payload {};
    std::array<unsigned char, 48> ipma_payload {};
    std::array<unsigned char, 224> iprp_payload {};
    std::array<unsigned char, 512> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t auxl_size;
    std::size_t iref_size;
    std::size_t auxc_depth_size;
    std::size_t auxc_alpha_size;
    std::size_t ipco_size;
    std::size_t ipma_size;
    std::size_t iprp_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    auxl_size = 0U;
    append_u16be(auxl_payload.data(), &auxl_size, 1U);
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 3U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_payload.data(),
                    auxl_size);

    auxc_depth_size = 0U;
    append_fullbox_header(auxc_depth_payload.data(), &auxc_depth_size, 0U);
    append_text(auxc_depth_payload.data(), &auxc_depth_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_depth_payload.data(), &auxc_depth_size, 0U);
    append_u8(auxc_depth_payload.data(), &auxc_depth_size, 0xAAU);
    append_u8(auxc_depth_payload.data(), &auxc_depth_size, 0xBBU);

    auxc_alpha_size = 0U;
    append_fullbox_header(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);
    append_text(auxc_alpha_payload.data(), &auxc_alpha_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x11U);

    ipco_size = 0U;
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_depth_payload.data(), auxc_depth_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_alpha_payload.data(), auxc_alpha_size);

    ipma_size = 0U;
    append_fullbox_header(ipma_payload.data(), &ipma_size, 0U);
    append_u32be(ipma_payload.data(), &ipma_size, 3U);
    append_u16be(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 0U);
    append_u16be(ipma_payload.data(), &ipma_size, 2U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u16be(ipma_payload.data(), &ipma_size, 3U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 2U);

    iprp_size = 0U;
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'c', 'o'), ipco_payload.data(),
                    ipco_size);
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'm', 'a'), ipma_payload.data(),
                    ipma_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'p', 'r', 'p'), iprp_payload.data(),
                    iprp_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_bmff_aux_subtype_upstream_fixture()
{
    std::array<unsigned char, 1024> file {};
    std::array<unsigned char, 16> pitm_payload {};
    std::array<unsigned char, 16> auxl_payload {};
    std::array<unsigned char, 64> iref_payload {};
    std::array<unsigned char, 96> auxc_depth_payload {};
    std::array<unsigned char, 96> auxc_alpha_payload {};
    std::array<unsigned char, 112> auxc_uuid_payload {};
    std::array<unsigned char, 352> ipco_payload {};
    std::array<unsigned char, 64> ipma_payload {};
    std::array<unsigned char, 448> iprp_payload {};
    std::array<unsigned char, 768> meta_payload {};
    std::array<unsigned char, 16> ftyp_payload {};
    std::size_t pitm_size;
    std::size_t auxl_size;
    std::size_t iref_size;
    std::size_t auxc_depth_size;
    std::size_t auxc_alpha_size;
    std::size_t auxc_uuid_size;
    std::size_t ipco_size;
    std::size_t ipma_size;
    std::size_t iprp_size;
    std::size_t meta_size;
    std::size_t ftyp_size;
    std::size_t size;

    pitm_size = 0U;
    append_fullbox_header(pitm_payload.data(), &pitm_size, 0U);
    append_u16be(pitm_payload.data(), &pitm_size, 1U);

    auxl_size = 0U;
    append_u16be(auxl_payload.data(), &auxl_size, 1U);
    append_u16be(auxl_payload.data(), &auxl_size, 3U);
    append_u16be(auxl_payload.data(), &auxl_size, 2U);
    append_u16be(auxl_payload.data(), &auxl_size, 3U);
    append_u16be(auxl_payload.data(), &auxl_size, 4U);

    iref_size = 0U;
    append_fullbox_header(iref_payload.data(), &iref_size, 0U);
    append_bmff_box(iref_payload.data(), &iref_size,
                    make_fourcc('a', 'u', 'x', 'l'), auxl_payload.data(),
                    auxl_size);

    auxc_depth_size = 0U;
    append_fullbox_header(auxc_depth_payload.data(), &auxc_depth_size, 0U);
    append_text(auxc_depth_payload.data(), &auxc_depth_size,
                "urn:mpeg:hevc:2015:auxid:2");
    append_u8(auxc_depth_payload.data(), &auxc_depth_size, 0U);
    append_text(auxc_depth_payload.data(), &auxc_depth_size, "profile");
    append_u8(auxc_depth_payload.data(), &auxc_depth_size, 0U);

    auxc_alpha_size = 0U;
    append_fullbox_header(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);
    append_text(auxc_alpha_payload.data(), &auxc_alpha_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x11U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x22U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x33U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x44U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x55U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x66U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x77U);
    append_u8(auxc_alpha_payload.data(), &auxc_alpha_size, 0x88U);

    auxc_uuid_size = 0U;
    append_fullbox_header(auxc_uuid_payload.data(), &auxc_uuid_size, 0U);
    append_text(auxc_uuid_payload.data(), &auxc_uuid_size,
                "urn:mpeg:hevc:2015:auxid:1");
    append_u8(auxc_uuid_payload.data(), &auxc_uuid_size, 0U);
    for (unsigned char i = 0U; i < 16U; ++i) {
        append_u8(auxc_uuid_payload.data(), &auxc_uuid_size, i);
    }

    ipco_size = 0U;
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_depth_payload.data(), auxc_depth_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_alpha_payload.data(), auxc_alpha_size);
    append_bmff_box(ipco_payload.data(), &ipco_size,
                    make_fourcc('a', 'u', 'x', 'C'),
                    auxc_uuid_payload.data(), auxc_uuid_size);

    ipma_size = 0U;
    append_fullbox_header(ipma_payload.data(), &ipma_size, 0U);
    append_u32be(ipma_payload.data(), &ipma_size, 4U);
    append_u16be(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 0U);
    append_u16be(ipma_payload.data(), &ipma_size, 2U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u16be(ipma_payload.data(), &ipma_size, 3U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 2U);
    append_u16be(ipma_payload.data(), &ipma_size, 4U);
    append_u8(ipma_payload.data(), &ipma_size, 1U);
    append_u8(ipma_payload.data(), &ipma_size, 3U);

    iprp_size = 0U;
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'c', 'o'), ipco_payload.data(),
                    ipco_size);
    append_bmff_box(iprp_payload.data(), &iprp_size,
                    make_fourcc('i', 'p', 'm', 'a'), ipma_payload.data(),
                    ipma_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload.data(), &meta_size, 0U);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('p', 'i', 't', 'm'), pitm_payload.data(),
                    pitm_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'r', 'e', 'f'), iref_payload.data(),
                    iref_size);
    append_bmff_box(meta_payload.data(), &meta_size,
                    make_fourcc('i', 'p', 'r', 'p'), iprp_payload.data(),
                    iprp_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('h', 'e', 'i', 'c'));
    append_u32be(ftyp_payload.data(), &ftyp_size, 0U);
    append_u32be(ftyp_payload.data(), &ftyp_size,
                 make_fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(file.data(), &size, make_fourcc('f', 't', 'y', 'p'),
                    ftyp_payload.data(), ftyp_size);
    append_bmff_box(file.data(), &size, make_fourcc('m', 'e', 't', 'a'),
                    meta_payload.data(), meta_size);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static void
append_exr_attr_raw(unsigned char* out, std::size_t* io_size,
                    const char* name, const char* type,
                    const unsigned char* value, std::size_t value_size)
{
    append_text(out, io_size, name);
    append_u8(out, io_size, 0U);
    append_text(out, io_size, type);
    append_u8(out, io_size, 0U);
    append_u32le(out, io_size, (std::uint32_t)value_size);
    append_bytes(out, io_size, value, value_size);
}

static void
append_exr_attr_text(unsigned char* out, std::size_t* io_size,
                     const char* name, const char* type, const char* value)
{
    append_exr_attr_raw(out, io_size, name, type,
                        reinterpret_cast<const unsigned char*>(value),
                        std::strlen(value));
}

static ByteVec
build_exr_single_part_fixture()
{
    std::array<unsigned char, 256> file {};
    unsigned char float_payload[4];
    std::size_t size;

    float_payload[0] = 0x00U;
    float_payload[1] = 0x00U;
    float_payload[2] = 0x80U;
    float_payload[3] = 0x3FU;

    size = 0U;
    append_u32le(file.data(), &size, 20000630U);
    append_u32le(file.data(), &size, 2U);
    append_exr_attr_text(file.data(), &size, "owner", "string", "Vlad");
    append_exr_attr_raw(file.data(), &size, "pixelAspectRatio", "float",
                        float_payload, sizeof(float_payload));
    append_u8(file.data(), &size, 0U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_exr_known_types_fixture()
{
    std::array<unsigned char, 512> file {};
    std::size_t size;

    size = 0U;
    append_u32le(file.data(), &size, 20000630U);
    append_u32le(file.data(), &size, 2U);

    append_text(file.data(), &size, "displayWindow");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "box2i");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 16U);
    append_i32le(file.data(), &size, 1);
    append_i32le(file.data(), &size, 2);
    append_i32le(file.data(), &size, 3);
    append_i32le(file.data(), &size, 4);

    append_text(file.data(), &size, "whiteLuminance");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "rational");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 8U);
    append_i32le(file.data(), &size, -3);
    append_u32le(file.data(), &size, 2U);

    append_text(file.data(), &size, "timeCode");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "timecode");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 8U);
    append_u32le(file.data(), &size, 0x11223344U);
    append_u32le(file.data(), &size, 0x55667788U);

    append_text(file.data(), &size, "adoptedNeutral");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "v2d");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 16U);
    append_u64le(file.data(), &size, 0x3FF8000000000000ULL);
    append_u64le(file.data(), &size, 0x4004000000000000ULL);

    append_text(file.data(), &size, "cameraForward");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "v3i");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 12U);
    append_i32le(file.data(), &size, -1);
    append_i32le(file.data(), &size, 0);
    append_i32le(file.data(), &size, 9);

    append_text(file.data(), &size, "lookModTransform");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "floatvector");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 12U);
    append_u32le(file.data(), &size, 0x3E800000U);
    append_u32le(file.data(), &size, 0x3F000000U);
    append_u32le(file.data(), &size, 0x3F800000U);

    append_text(file.data(), &size, "filmKeyCode");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "keycode");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 28U);
    append_i32le(file.data(), &size, 1);
    append_i32le(file.data(), &size, 2);
    append_i32le(file.data(), &size, 3);
    append_i32le(file.data(), &size, 4);
    append_i32le(file.data(), &size, 5);
    append_i32le(file.data(), &size, 6);
    append_i32le(file.data(), &size, 7);

    append_u8(file.data(), &size, 0U);

    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_exr_float_matrix_types_fixture()
{
    std::array<unsigned char, 512> file {};
    std::size_t size;

    size = 0U;
    append_u32le(file.data(), &size, 20000630U);
    append_u32le(file.data(), &size, 2U);

    append_text(file.data(), &size, "dataWindowF");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "box2f");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 16U);
    append_u32le(file.data(), &size, 0x3F800000U);
    append_u32le(file.data(), &size, 0x40000000U);
    append_u32le(file.data(), &size, 0x40400000U);
    append_u32le(file.data(), &size, 0x40800000U);

    append_text(file.data(), &size, "primaries");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "chromaticities");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 32U);
    append_u32le(file.data(), &size, 0x3E99999AU);
    append_u32le(file.data(), &size, 0x3F19999AU);
    append_u32le(file.data(), &size, 0x3DCCCCCDU);
    append_u32le(file.data(), &size, 0x3E4CCCCDU);
    append_u32le(file.data(), &size, 0x3D4CCCCDU);
    append_u32le(file.data(), &size, 0x3D99999AU);
    append_u32le(file.data(), &size, 0x3F2AAAABU);
    append_u32le(file.data(), &size, 0x3F2AAAABU);

    append_text(file.data(), &size, "cameraTransform");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "m44d");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 128U);
    append_u64le(file.data(), &size, 0x3FF0000000000000ULL);
    append_u64le(file.data(), &size, 0x4000000000000000ULL);
    append_u64le(file.data(), &size, 0x4008000000000000ULL);
    append_u64le(file.data(), &size, 0x4010000000000000ULL);
    append_u64le(file.data(), &size, 0x4014000000000000ULL);
    append_u64le(file.data(), &size, 0x4018000000000000ULL);
    append_u64le(file.data(), &size, 0x401C000000000000ULL);
    append_u64le(file.data(), &size, 0x4020000000000000ULL);
    append_u64le(file.data(), &size, 0x4022000000000000ULL);
    append_u64le(file.data(), &size, 0x4024000000000000ULL);
    append_u64le(file.data(), &size, 0x4026000000000000ULL);
    append_u64le(file.data(), &size, 0x4028000000000000ULL);
    append_u64le(file.data(), &size, 0x402A000000000000ULL);
    append_u64le(file.data(), &size, 0x402C000000000000ULL);
    append_u64le(file.data(), &size, 0x402E000000000000ULL);
    append_u64le(file.data(), &size, 0x4030000000000000ULL);

    append_u8(file.data(), &size, 0U);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_exr_complex_raw_types_fixture()
{
    static const unsigned char k_chlist[] = {
        'R', 0U,
        1U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        0U
    };
    static const unsigned char k_preview[] = {
        0x02U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x00U,
        0x10U, 0x20U, 0x30U, 0x40U,
        0x50U, 0x60U, 0x70U, 0x80U
    };
    static const unsigned char k_stringvector[] = {
        0x03U, 0x00U, 0x00U, 0x00U, 'o', 'n', 'e',
        0x03U, 0x00U, 0x00U, 0x00U, 't', 'w', 'o'
    };
    std::array<unsigned char, 512> file {};
    std::size_t size;

    size = 0U;
    append_u32le(file.data(), &size, 20000630U);
    append_u32le(file.data(), &size, 2U);
    append_exr_attr_raw(file.data(), &size, "channels", "chlist",
                        k_chlist, sizeof(k_chlist));
    append_exr_attr_raw(file.data(), &size, "thumbnail", "preview",
                        k_preview, sizeof(k_preview));
    append_exr_attr_raw(file.data(), &size, "aliases", "stringvector",
                        k_stringvector, sizeof(k_stringvector));
    append_u8(file.data(), &size, 0U);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static ByteVec
build_exr_enum_vector_matrix_types_fixture()
{
    std::array<unsigned char, 1024> file {};
    std::size_t size;

    size = 0U;
    append_u32le(file.data(), &size, 20000630U);
    append_u32le(file.data(), &size, 2U);

    append_text(file.data(), &size, "compressionMode");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "compression");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 1U);
    append_u8(file.data(), &size, 3U);

    append_text(file.data(), &size, "environment");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "envmap");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 1U);
    append_u8(file.data(), &size, 1U);

    append_text(file.data(), &size, "scanOrder");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "lineOrder");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 1U);
    append_u8(file.data(), &size, 2U);

    append_text(file.data(), &size, "deepState");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "deepImageState");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 1U);
    append_u8(file.data(), &size, 2U);

    append_text(file.data(), &size, "screenWindowCenter");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "v2f");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 8U);
    append_u32le(file.data(), &size, 0x3F000000U);
    append_u32le(file.data(), &size, 0x3FC00000U);

    append_text(file.data(), &size, "worldDir");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "v3f");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 12U);
    append_u32le(file.data(), &size, 0x40200000U);
    append_u32le(file.data(), &size, 0x40600000U);
    append_u32le(file.data(), &size, 0x40900000U);

    append_text(file.data(), &size, "matrix3f");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "m33f");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 36U);
    append_u32le(file.data(), &size, 0x3F800000U);
    append_u32le(file.data(), &size, 0x40000000U);
    append_u32le(file.data(), &size, 0x40400000U);
    append_u32le(file.data(), &size, 0x40800000U);
    append_u32le(file.data(), &size, 0x40A00000U);
    append_u32le(file.data(), &size, 0x40C00000U);
    append_u32le(file.data(), &size, 0x40E00000U);
    append_u32le(file.data(), &size, 0x41000000U);
    append_u32le(file.data(), &size, 0x41100000U);

    append_text(file.data(), &size, "cameraUp");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "v3d");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 24U);
    append_u64le(file.data(), &size, 0x4008000000000000ULL);
    append_u64le(file.data(), &size, 0x4014000000000000ULL);
    append_u64le(file.data(), &size, 0x401E000000000000ULL);

    append_text(file.data(), &size, "matrix3d");
    append_u8(file.data(), &size, 0U);
    append_text(file.data(), &size, "m33d");
    append_u8(file.data(), &size, 0U);
    append_u32le(file.data(), &size, 72U);
    append_u64le(file.data(), &size, 0x3FF0000000000000ULL);
    append_u64le(file.data(), &size, 0x4000000000000000ULL);
    append_u64le(file.data(), &size, 0x4008000000000000ULL);
    append_u64le(file.data(), &size, 0x4010000000000000ULL);
    append_u64le(file.data(), &size, 0x4014000000000000ULL);
    append_u64le(file.data(), &size, 0x4018000000000000ULL);
    append_u64le(file.data(), &size, 0x401C000000000000ULL);
    append_u64le(file.data(), &size, 0x4020000000000000ULL);
    append_u64le(file.data(), &size, 0x4022000000000000ULL);

    append_u8(file.data(), &size, 0U);
    return ByteVec(file.begin(), file.begin() + (std::ptrdiff_t)size);
}

static std::span<const std::byte>
as_byte_span(const ByteVec& bytes)
{
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
}

static std::string
escape_bytes(const unsigned char* data, std::size_t size)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    std::size_t i;

    out.reserve(size * 4U);
    for (i = 0U; i < size; ++i) {
        unsigned char ch = data[i];
        if (ch >= 0x20U && ch <= 0x7EU && ch != '\\' && ch != '|') {
            out.push_back((char)ch);
        } else {
            out.push_back('\\');
            out.push_back('x');
            out.push_back(hex[(ch >> 4) & 0x0FU]);
            out.push_back(hex[ch & 0x0FU]);
        }
    }
    return out;
}

static std::string
escape_bytes(std::span<const std::byte> bytes)
{
    std::string out;
    std::size_t i;

    out.reserve(bytes.size() * 4U);
    for (i = 0U; i < bytes.size(); ++i) {
        unsigned char ch = std::to_integer<unsigned char>(bytes[i]);
        out += escape_bytes(&ch, 1U);
    }
    return out;
}

static std::string
hex_bytes(const unsigned char* data, std::size_t size)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    std::size_t i;

    out.reserve(size * 2U);
    for (i = 0U; i < size; ++i) {
        out.push_back(hex[(data[i] >> 4) & 0x0FU]);
        out.push_back(hex[data[i] & 0x0FU]);
    }
    return out;
}

static std::string
hex_bytes(std::span<const std::byte> bytes)
{
    std::string out;
    std::size_t i;

    out.reserve(bytes.size() * 2U);
    for (i = 0U; i < bytes.size(); ++i) {
        unsigned char ch = std::to_integer<unsigned char>(bytes[i]);
        out += hex_bytes(&ch, 1U);
    }
    return out;
}

static std::string
omc_ref_text(const omc_store* store, omc_byte_ref ref)
{
    omc_const_bytes view = omc_arena_view(&store->arena, ref);
    return escape_bytes((const unsigned char*)view.data, (std::size_t)view.size);
}

static std::string
omc_ref_hex(const omc_store* store, omc_byte_ref ref)
{
    omc_const_bytes view = omc_arena_view(&store->arena, ref);
    return hex_bytes((const unsigned char*)view.data, (std::size_t)view.size);
}

static std::string
cpp_span_text(const openmeta::MetaStore& store, openmeta::ByteSpan ref)
{
    return escape_bytes(store.arena().span(ref));
}

static std::string
cpp_span_hex(const openmeta::MetaStore& store, openmeta::ByteSpan ref)
{
    return hex_bytes(store.arena().span(ref));
}

static const char*
omc_key_kind_name(omc_key_kind kind)
{
    switch (kind) {
        case OMC_KEY_EXIF_TAG: return "ExifTag";
        case OMC_KEY_COMMENT: return "Comment";
        case OMC_KEY_EXR_ATTR: return "ExrAttribute";
        case OMC_KEY_IPTC_DATASET: return "IptcDataset";
        case OMC_KEY_XMP_PROPERTY: return "XmpProperty";
        case OMC_KEY_ICC_HEADER_FIELD: return "IccHeaderField";
        case OMC_KEY_ICC_TAG: return "IccTag";
        case OMC_KEY_PHOTOSHOP_IRB: return "PhotoshopIrb";
        case OMC_KEY_PHOTOSHOP_IRB_FIELD: return "PhotoshopIrbField";
        case OMC_KEY_GEOTIFF_KEY: return "GeotiffKey";
        case OMC_KEY_PRINTIM_FIELD: return "PrintImField";
        case OMC_KEY_BMFF_FIELD: return "BmffField";
        case OMC_KEY_JUMBF_FIELD: return "JumbfField";
        case OMC_KEY_JUMBF_CBOR_KEY: return "JumbfCborKey";
        case OMC_KEY_PNG_TEXT: return "PngText";
        default: return "UnknownKey";
    }
}

static const char*
cpp_key_kind_name(openmeta::MetaKeyKind kind)
{
    switch (kind) {
        case openmeta::MetaKeyKind::ExifTag: return "ExifTag";
        case openmeta::MetaKeyKind::Comment: return "Comment";
        case openmeta::MetaKeyKind::ExrAttribute: return "ExrAttribute";
        case openmeta::MetaKeyKind::IptcDataset: return "IptcDataset";
        case openmeta::MetaKeyKind::XmpProperty: return "XmpProperty";
        case openmeta::MetaKeyKind::IccHeaderField: return "IccHeaderField";
        case openmeta::MetaKeyKind::IccTag: return "IccTag";
        case openmeta::MetaKeyKind::PhotoshopIrb: return "PhotoshopIrb";
        case openmeta::MetaKeyKind::PhotoshopIrbField:
            return "PhotoshopIrbField";
        case openmeta::MetaKeyKind::GeotiffKey: return "GeotiffKey";
        case openmeta::MetaKeyKind::PrintImField: return "PrintImField";
        case openmeta::MetaKeyKind::BmffField: return "BmffField";
        case openmeta::MetaKeyKind::JumbfField: return "JumbfField";
        case openmeta::MetaKeyKind::JumbfCborKey: return "JumbfCborKey";
        case openmeta::MetaKeyKind::PngText: return "PngText";
        default: return "UnknownKey";
    }
}

static const char*
omc_val_kind_name(omc_val_kind kind)
{
    switch (kind) {
        case OMC_VAL_EMPTY: return "Empty";
        case OMC_VAL_SCALAR: return "Scalar";
        case OMC_VAL_ARRAY: return "Array";
        case OMC_VAL_BYTES: return "Bytes";
        case OMC_VAL_TEXT: return "Text";
        default: return "UnknownVal";
    }
}

static const char*
cpp_val_kind_name(openmeta::MetaValueKind kind)
{
    switch (kind) {
        case openmeta::MetaValueKind::Empty: return "Empty";
        case openmeta::MetaValueKind::Scalar: return "Scalar";
        case openmeta::MetaValueKind::Array: return "Array";
        case openmeta::MetaValueKind::Bytes: return "Bytes";
        case openmeta::MetaValueKind::Text: return "Text";
        default: return "UnknownVal";
    }
}

static const char*
omc_elem_name(omc_elem_type elem)
{
    switch (elem) {
        case OMC_ELEM_U8: return "U8";
        case OMC_ELEM_I8: return "I8";
        case OMC_ELEM_U16: return "U16";
        case OMC_ELEM_I16: return "I16";
        case OMC_ELEM_U32: return "U32";
        case OMC_ELEM_I32: return "I32";
        case OMC_ELEM_U64: return "U64";
        case OMC_ELEM_I64: return "I64";
        case OMC_ELEM_F32_BITS: return "F32Bits";
        case OMC_ELEM_F64_BITS: return "F64Bits";
        case OMC_ELEM_URATIONAL: return "URational";
        case OMC_ELEM_SRATIONAL: return "SRational";
        default: return "UnknownElem";
    }
}

static const char*
cpp_elem_name(openmeta::MetaElementType elem)
{
    switch (elem) {
        case openmeta::MetaElementType::U8: return "U8";
        case openmeta::MetaElementType::I8: return "I8";
        case openmeta::MetaElementType::U16: return "U16";
        case openmeta::MetaElementType::I16: return "I16";
        case openmeta::MetaElementType::U32: return "U32";
        case openmeta::MetaElementType::I32: return "I32";
        case openmeta::MetaElementType::U64: return "U64";
        case openmeta::MetaElementType::I64: return "I64";
        case openmeta::MetaElementType::F32: return "F32Bits";
        case openmeta::MetaElementType::F64: return "F64Bits";
        case openmeta::MetaElementType::URational: return "URational";
        case openmeta::MetaElementType::SRational: return "SRational";
        default: return "UnknownElem";
    }
}

static const char*
omc_text_enc_name(omc_text_encoding enc)
{
    switch (enc) {
        case OMC_TEXT_UNKNOWN: return "Unknown";
        case OMC_TEXT_ASCII: return "Ascii";
        case OMC_TEXT_UTF8: return "Utf8";
        case OMC_TEXT_UTF16LE: return "Utf16LE";
        case OMC_TEXT_UTF16BE: return "Utf16BE";
        default: return "UnknownEnc";
    }
}

static const char*
cpp_text_enc_name(openmeta::TextEncoding enc)
{
    switch (enc) {
        case openmeta::TextEncoding::Unknown: return "Unknown";
        case openmeta::TextEncoding::Ascii: return "Ascii";
        case openmeta::TextEncoding::Utf8: return "Utf8";
        case openmeta::TextEncoding::Utf16LE: return "Utf16LE";
        case openmeta::TextEncoding::Utf16BE: return "Utf16BE";
        default: return "UnknownEnc";
    }
}

static std::string
canonical_omc_key(const omc_store* store, const omc_key* key)
{
    std::string out;

    out = omc_key_kind_name(key->kind);
    switch (key->kind) {
        case OMC_KEY_EXIF_TAG:
            out += "|ifd=";
            out += omc_ref_text(store, key->u.exif_tag.ifd);
            out += "|tag=";
            out += std::to_string((unsigned int)key->u.exif_tag.tag);
            break;
        case OMC_KEY_COMMENT:
            break;
        case OMC_KEY_EXR_ATTR:
            out += "|part=";
            out += std::to_string((unsigned int)key->u.exr_attr.part_index);
            out += "|name=";
            out += omc_ref_text(store, key->u.exr_attr.name);
            break;
        case OMC_KEY_IPTC_DATASET:
            out += "|record=";
            out += std::to_string((unsigned int)key->u.iptc_dataset.record);
            out += "|dataset=";
            out += std::to_string((unsigned int)key->u.iptc_dataset.dataset);
            break;
        case OMC_KEY_XMP_PROPERTY:
            out += "|schema=";
            out += omc_ref_text(store, key->u.xmp_property.schema_ns);
            out += "|path=";
            out += omc_ref_text(store, key->u.xmp_property.property_path);
            break;
        case OMC_KEY_ICC_HEADER_FIELD:
            out += "|offset=";
            out += std::to_string((unsigned long long)
                                  key->u.icc_header_field.offset);
            break;
        case OMC_KEY_ICC_TAG:
            out += "|sig=";
            out += std::to_string((unsigned long long)key->u.icc_tag.signature);
            break;
        case OMC_KEY_PHOTOSHOP_IRB:
            out += "|id=";
            out += std::to_string((unsigned int)
                                  key->u.photoshop_irb.resource_id);
            break;
        case OMC_KEY_PHOTOSHOP_IRB_FIELD:
            out += "|id=";
            out += std::to_string((unsigned int)
                                  key->u.photoshop_irb_field.resource_id);
            out += "|field=";
            out += omc_ref_text(store, key->u.photoshop_irb_field.field);
            break;
        case OMC_KEY_GEOTIFF_KEY:
            out += "|id=";
            out += std::to_string((unsigned int)key->u.geotiff_key.key_id);
            break;
        case OMC_KEY_PRINTIM_FIELD:
            out += "|field=";
            out += omc_ref_text(store, key->u.printim_field.field);
            break;
        case OMC_KEY_BMFF_FIELD:
            out += "|field=";
            out += omc_ref_text(store, key->u.bmff_field.field);
            break;
        case OMC_KEY_JUMBF_FIELD:
            out += "|field=";
            out += omc_ref_text(store, key->u.jumbf_field.field);
            break;
        case OMC_KEY_JUMBF_CBOR_KEY:
            out += "|key=";
            out += omc_ref_text(store, key->u.jumbf_cbor_key.key);
            break;
        case OMC_KEY_PNG_TEXT:
            out += "|keyword=";
            out += omc_ref_text(store, key->u.png_text.keyword);
            out += "|field=";
            out += omc_ref_text(store, key->u.png_text.field);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_cpp_key(const openmeta::MetaStore& store,
                  const openmeta::MetaKey& key)
{
    std::string out;

    out = cpp_key_kind_name(key.kind);
    switch (key.kind) {
        case openmeta::MetaKeyKind::ExifTag:
            out += "|ifd=";
            out += cpp_span_text(store, key.data.exif_tag.ifd);
            out += "|tag=";
            out += std::to_string((unsigned int)key.data.exif_tag.tag);
            break;
        case openmeta::MetaKeyKind::Comment:
            break;
        case openmeta::MetaKeyKind::ExrAttribute:
            out += "|part=";
            out += std::to_string((unsigned int)
                                  key.data.exr_attribute.part_index);
            out += "|name=";
            out += cpp_span_text(store, key.data.exr_attribute.name);
            break;
        case openmeta::MetaKeyKind::IptcDataset:
            out += "|record=";
            out += std::to_string((unsigned int)key.data.iptc_dataset.record);
            out += "|dataset=";
            out += std::to_string((unsigned int)key.data.iptc_dataset.dataset);
            break;
        case openmeta::MetaKeyKind::XmpProperty:
            out += "|schema=";
            out += cpp_span_text(store, key.data.xmp_property.schema_ns);
            out += "|path=";
            out += cpp_span_text(store, key.data.xmp_property.property_path);
            break;
        case openmeta::MetaKeyKind::IccHeaderField:
            out += "|offset=";
            out += std::to_string((unsigned long long)
                                  key.data.icc_header_field.offset);
            break;
        case openmeta::MetaKeyKind::IccTag:
            out += "|sig=";
            out += std::to_string((unsigned long long)key.data.icc_tag.signature);
            break;
        case openmeta::MetaKeyKind::PhotoshopIrb:
            out += "|id=";
            out += std::to_string((unsigned int)
                                  key.data.photoshop_irb.resource_id);
            break;
        case openmeta::MetaKeyKind::PhotoshopIrbField:
            out += "|id=";
            out += std::to_string((unsigned int)
                                  key.data.photoshop_irb_field.resource_id);
            out += "|field=";
            out += cpp_span_text(store, key.data.photoshop_irb_field.field);
            break;
        case openmeta::MetaKeyKind::GeotiffKey:
            out += "|id=";
            out += std::to_string((unsigned int)key.data.geotiff_key.key_id);
            break;
        case openmeta::MetaKeyKind::PrintImField:
            out += "|field=";
            out += cpp_span_text(store, key.data.printim_field.field);
            break;
        case openmeta::MetaKeyKind::BmffField:
            out += "|field=";
            out += cpp_span_text(store, key.data.bmff_field.field);
            break;
        case openmeta::MetaKeyKind::JumbfField:
            out += "|field=";
            out += cpp_span_text(store, key.data.jumbf_field.field);
            break;
        case openmeta::MetaKeyKind::JumbfCborKey:
            out += "|key=";
            out += cpp_span_text(store, key.data.jumbf_cbor_key.key);
            break;
        case openmeta::MetaKeyKind::PngText:
            out += "|keyword=";
            out += cpp_span_text(store, key.data.png_text.keyword);
            out += "|field=";
            out += cpp_span_text(store, key.data.png_text.field);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_omc_value(const omc_store* store, const omc_val* value)
{
    std::string out;

    out = omc_val_kind_name(value->kind);
    out += "|elem=";
    out += omc_elem_name(value->elem_type);
    out += "|enc=";
    out += omc_text_enc_name(value->text_encoding);
    out += "|count=";
    out += std::to_string((unsigned long long)value->count);

    switch (value->kind) {
        case OMC_VAL_EMPTY:
            break;
        case OMC_VAL_SCALAR:
            out += "|value=";
            switch (value->elem_type) {
                case OMC_ELEM_I8:
                case OMC_ELEM_I16:
                case OMC_ELEM_I32:
                case OMC_ELEM_I64:
                    out += std::to_string((long long)value->u.i64);
                    break;
                case OMC_ELEM_F32_BITS:
                    out += std::to_string((unsigned long long)value->u.f32_bits);
                    break;
                case OMC_ELEM_F64_BITS:
                    out += std::to_string((unsigned long long)value->u.f64_bits);
                    break;
                case OMC_ELEM_URATIONAL:
                    out += std::to_string((unsigned long long)
                                          value->u.ur.numer);
                    out += "/";
                    out += std::to_string((unsigned long long)
                                          value->u.ur.denom);
                    break;
                case OMC_ELEM_SRATIONAL:
                    out += std::to_string((long long)value->u.sr.numer);
                    out += "/";
                    out += std::to_string((long long)value->u.sr.denom);
                    break;
                default:
                    out += std::to_string((unsigned long long)value->u.u64);
                    break;
            }
            break;
        case OMC_VAL_ARRAY:
        case OMC_VAL_BYTES:
            out += "|hex=";
            out += omc_ref_hex(store, value->u.ref);
            break;
        case OMC_VAL_TEXT:
            out += "|text=";
            out += omc_ref_text(store, value->u.ref);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_cpp_value(const openmeta::MetaStore& store,
                    const openmeta::MetaValue& value)
{
    std::string out;

    out = cpp_val_kind_name(value.kind);
    out += "|elem=";
    out += cpp_elem_name(value.elem_type);
    out += "|enc=";
    out += cpp_text_enc_name(value.text_encoding);
    out += "|count=";
    out += std::to_string((unsigned long long)value.count);

    switch (value.kind) {
        case openmeta::MetaValueKind::Empty:
            break;
        case openmeta::MetaValueKind::Scalar:
            out += "|value=";
            switch (value.elem_type) {
                case openmeta::MetaElementType::I8:
                case openmeta::MetaElementType::I16:
                case openmeta::MetaElementType::I32:
                case openmeta::MetaElementType::I64:
                    out += std::to_string((long long)value.data.i64);
                    break;
                case openmeta::MetaElementType::F32:
                    out += std::to_string((unsigned long long)value.data.f32_bits);
                    break;
                case openmeta::MetaElementType::F64:
                    out += std::to_string((unsigned long long)value.data.f64_bits);
                    break;
                case openmeta::MetaElementType::URational:
                    out += std::to_string((unsigned long long)value.data.ur.numer);
                    out += "/";
                    out += std::to_string((unsigned long long)value.data.ur.denom);
                    break;
                case openmeta::MetaElementType::SRational:
                    out += std::to_string((long long)value.data.sr.numer);
                    out += "/";
                    out += std::to_string((long long)value.data.sr.denom);
                    break;
                default:
                    out += std::to_string((unsigned long long)value.data.u64);
                    break;
            }
            break;
        case openmeta::MetaValueKind::Array:
        case openmeta::MetaValueKind::Bytes:
            out += "|hex=";
            out += cpp_span_hex(store, value.data.span);
            break;
        case openmeta::MetaValueKind::Text:
            out += "|text=";
            out += cpp_span_text(store, value.data.span);
            break;
        default:
            break;
    }
    return out;
}

static std::string
canonical_omc_flags(omc_entry_flags flags)
{
    std::string out;

    if (flags == OMC_ENTRY_FLAG_NONE) {
        return "None";
    }
    if ((flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
        out += "Deleted,";
    }
    if ((flags & OMC_ENTRY_FLAG_DIRTY) != 0U) {
        out += "Dirty,";
    }
    if ((flags & OMC_ENTRY_FLAG_DERIVED) != 0U) {
        out += "Derived,";
    }
    if ((flags & OMC_ENTRY_FLAG_TRUNCATED) != 0U) {
        out += "Truncated,";
    }
    if ((flags & OMC_ENTRY_FLAG_UNREADABLE) != 0U) {
        out += "Unreadable,";
    }
    if ((flags & OMC_ENTRY_FLAG_CONTEXTUAL_NAME) != 0U) {
        out += "ContextualName,";
    }
    if (!out.empty()) {
        out.pop_back();
    }
    return out;
}

static std::string
canonical_cpp_flags(openmeta::EntryFlags flags)
{
    std::string out;

    if (flags == openmeta::EntryFlags::None) {
        return "None";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Deleted)) {
        out += "Deleted,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Dirty)) {
        out += "Dirty,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Derived)) {
        out += "Derived,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Truncated)) {
        out += "Truncated,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::Unreadable)) {
        out += "Unreadable,";
    }
    if (openmeta::any(flags, openmeta::EntryFlags::ContextualName)) {
        out += "ContextualName,";
    }
    if (!out.empty()) {
        out.pop_back();
    }
    return out;
}

static std::string
canonical_omc_entry(const omc_store* store, const omc_entry* entry)
{
    std::string out;

    out = canonical_omc_key(store, &entry->key);
    out += "||";
    out += canonical_omc_value(store, &entry->value);
    out += "||flags=";
    out += canonical_omc_flags(entry->flags);
    return out;
}

static std::string
canonical_cpp_entry(const openmeta::MetaStore& store,
                    const openmeta::Entry& entry)
{
    std::string out;

    out = canonical_cpp_key(store, entry.key);
    out += "||";
    out += canonical_cpp_value(store, entry.value);
    out += "||flags=";
    out += canonical_cpp_flags(entry.flags);
    return out;
}

static const char*
canonical_omc_transfer_status(omc_transfer_status status)
{
    switch (status) {
        case OMC_TRANSFER_OK:
            return "ok";
        case OMC_TRANSFER_UNSUPPORTED:
            return "unsupported";
        case OMC_TRANSFER_MALFORMED:
            return "malformed";
        case OMC_TRANSFER_LIMIT:
            return "limit";
    }
    return "unknown";
}

static const char*
canonical_cpp_transfer_status(openmeta::TransferStatus status)
{
    switch (status) {
        case openmeta::TransferStatus::Ok:
            return "ok";
        case openmeta::TransferStatus::InvalidArgument:
            return "invalid_argument";
        case openmeta::TransferStatus::Unsupported:
            return "unsupported";
        case openmeta::TransferStatus::LimitExceeded:
            return "limit";
        case openmeta::TransferStatus::Malformed:
            return "malformed";
        case openmeta::TransferStatus::UnsafeData:
            return "unsafe_data";
        case openmeta::TransferStatus::InternalError:
            return "internal_error";
    }
    return "unknown";
}

static const char*
canonical_omc_xmp_write_status(omc_xmp_write_status status)
{
    switch (status) {
        case OMC_XMP_WRITE_OK:
            return "ok";
        case OMC_XMP_WRITE_LIMIT:
            return "limit";
        case OMC_XMP_WRITE_UNSUPPORTED:
            return "unsupported";
        case OMC_XMP_WRITE_MALFORMED:
            return "malformed";
    }
    return "unknown";
}

static const char*
canonical_omc_xmp_dump_status(omc_xmp_dump_status status)
{
    switch (status) {
        case OMC_XMP_DUMP_OK:
            return "ok";
        case OMC_XMP_DUMP_TRUNCATED:
            return "truncated";
        case OMC_XMP_DUMP_LIMIT:
            return "limit";
    }
    return "unknown";
}

static std::string
byte_ref_text(const omc_arena& arena, omc_byte_ref ref)
{
    const omc_const_bytes view = omc_arena_view(&arena, ref);

    if (view.data == nullptr || view.size == 0U) {
        return std::string();
    }
    return std::string((const char*)view.data, (std::size_t)view.size);
}

struct TransferExecuteCaseOptions final {
    omc_xmp_writeback_mode writeback_mode
        = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    omc_xmp_destination_embedded_mode destination_embedded_mode
        = OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;
    openmeta::TransferTargetFormat cpp_target_format
        = openmeta::TransferTargetFormat::Jpeg;
    const char* target_suffix = ".jpg";
};

struct TransferExecuteParitySummary final {
    std::string status;
    std::string edited_status;
    std::string sidecar_status;
    bool sidecar_requested = false;
    bool edited_present  = false;
    bool sidecar_present = false;
    std::vector<std::string> edited_records;
    std::vector<std::string> sidecar_records;
};

struct TransferPersistParitySummary final {
    std::string status;
    std::string output_status;
    std::string sidecar_status;
    std::string cleanup_status;
    bool sidecar_requested = false;
    bool cleanup_requested = false;
    bool cleanup_removed   = false;
    std::string output_path;
    std::string sidecar_path;
    std::string cleanup_path;
    std::vector<std::string> output_records;
    std::vector<std::string> sidecar_records;
};

struct ReadCaseOptions final {
    bool decode_makernote = false;
    bool verify_c2pa = false;
    bool verify_require_resolved_references = false;
    bool verify_backend_openssl = false;
};

static std::vector<std::string>
read_omc_records(const ByteVec& file_bytes, const ReadCaseOptions& options)
{
    omc_store store;
    omc_read_res res;
    omc_read_opts opts;
    std::array<omc_blk_ref, 128> blocks {};
    std::array<omc_exif_ifd_ref, 128> ifds {};
    std::array<omc_u8, 65536> payload {};
    std::array<omc_u32, 256> scratch {};
    std::vector<std::string> out;
    omc_size i;

    omc_store_init(&store);
    omc_read_opts_init(&opts);
    if (options.decode_makernote) {
        opts.exif.decode_makernote = 1;
    }
    if (options.verify_c2pa) {
        opts.jumbf.verify_c2pa = 1;
        if (options.verify_require_resolved_references) {
            opts.jumbf.verify_require_resolved_references = 1;
        }
        if (options.verify_backend_openssl) {
            opts.jumbf.verify_backend = OMC_C2PA_VERIFY_BACKEND_OPENSSL;
        }
    }
    res = omc_read_simple(file_bytes.data(), (omc_size)file_bytes.size(),
                          &store, blocks.data(),
                          (omc_u32)blocks.size(), ifds.data(),
                          (omc_u32)ifds.size(), payload.data(),
                          (omc_size)payload.size(), scratch.data(),
                          (omc_u32)scratch.size(), &opts);
    if (res.scan.status == OMC_SCAN_MALFORMED) {
        std::fprintf(stderr, "omc scan failed with malformed status\n");
        std::exit(1);
    }

    out.reserve((std::size_t)store.entry_count);
    for (i = 0U; i < store.entry_count; ++i) {
        out.push_back(canonical_omc_entry(&store, &store.entries[i]));
    }
    omc_store_fini(&store);
    std::sort(out.begin(), out.end());
    return out;
}

static bool
read_omc_store(const ByteVec& file_bytes, omc_store* out_store)
{
    omc_read_res res;
    omc_read_opts opts;
    std::array<omc_blk_ref, 128> blocks {};
    std::array<omc_exif_ifd_ref, 128> ifds {};
    std::array<omc_u8, 65536> payload {};
    std::array<omc_u32, 256> scratch {};

    if (out_store == nullptr) {
        return false;
    }
    omc_store_init(out_store);
    omc_read_opts_init(&opts);
    res = omc_read_simple(file_bytes.data(), (omc_size)file_bytes.size(),
                          out_store, blocks.data(),
                          (omc_u32)blocks.size(), ifds.data(),
                          (omc_u32)ifds.size(), payload.data(),
                          (omc_size)payload.size(), scratch.data(),
                          (omc_u32)scratch.size(), &opts);
    if (res.scan.status == OMC_SCAN_MALFORMED) {
        omc_store_fini(out_store);
        return false;
    }
    return true;
}

static std::vector<std::string>
read_cpp_records(const ByteVec& file_bytes, const ReadCaseOptions& options)
{
    openmeta::MetaStore store;
    openmeta::SimpleMetaDecodeOptions opts {};
    std::array<openmeta::ContainerBlockRef, 128> blocks {};
    std::array<openmeta::ExifIfdRef, 128> ifds {};
    std::array<std::byte, 65536> payload {};
    std::array<std::uint32_t, 256> scratch {};
    openmeta::SimpleMetaResult res;
    std::vector<std::string> out;
    std::size_t i;

    if (options.decode_makernote) {
        opts.exif.decode_makernote = true;
    }
    if (options.verify_c2pa) {
        opts.jumbf.verify_c2pa = true;
        if (options.verify_require_resolved_references) {
            opts.jumbf.verify_require_resolved_references = true;
        }
        if (options.verify_backend_openssl) {
            opts.jumbf.verify_backend = openmeta::C2paVerifyBackend::OpenSsl;
        }
    }

    res = openmeta::simple_meta_read(as_byte_span(file_bytes), store, blocks,
                                     ifds, payload, scratch, opts);
    if (res.scan.status == openmeta::ScanStatus::Malformed) {
        std::fprintf(stderr, "openmeta scan failed with malformed status\n");
        std::exit(1);
    }

    out.reserve(store.entries().size());
    for (i = 0U; i < store.entries().size(); ++i) {
        out.push_back(canonical_cpp_entry(store, store.entries()[i]));
    }
    std::sort(out.begin(), out.end());
    return out;
}

static bool
dedupe_sorted_records(std::vector<std::string>* records)
{
    std::vector<std::string>::iterator new_end;

    if (records == nullptr) {
        return false;
    }
    new_end = std::unique(records->begin(), records->end());
    records->erase(new_end, records->end());
    return true;
}

static void
erase_records_containing(std::vector<std::string>* records, const char* needle)
{
    std::vector<std::string>::iterator dst;
    std::vector<std::string>::iterator src;

    if (records == nullptr || needle == nullptr) {
        return;
    }

    dst = records->begin();
    for (src = records->begin(); src != records->end(); ++src) {
        if (src->find(needle) != std::string::npos) {
            continue;
        }
        if (dst != src) {
            *dst = *src;
        }
        ++dst;
    }
    records->erase(dst, records->end());
}

static void
erase_records_with_prefix(std::vector<std::string>* records, const char* prefix)
{
    std::vector<std::string>::iterator dst;
    std::vector<std::string>::iterator src;
    const std::size_t prefix_len
        = (prefix != nullptr) ? std::strlen(prefix) : 0U;

    if (records == nullptr || prefix == nullptr) {
        return;
    }

    dst = records->begin();
    for (src = records->begin(); src != records->end(); ++src) {
        if (src->compare(0U, prefix_len, prefix) == 0) {
            continue;
        }
        if (dst != src) {
            *dst = *src;
        }
        ++dst;
    }
    records->erase(dst, records->end());
}

static void
erase_sony_afstatus19_unstable_records(std::vector<std::string>* records)
{
    std::vector<std::string>::iterator dst;
    std::vector<std::string>::iterator src;

    if (records == nullptr) {
        return;
    }

    dst = records->begin();
    for (src = records->begin(); src != records->end(); ++src) {
        const bool is_afstatus19
            = src->find("ifd=mk_sony_afstatus19_0|tag=") != std::string::npos;
        const bool keep_stable
            = src->find("tag=0||") != std::string::npos
              || src->find("tag=4||") != std::string::npos;

        if (is_afstatus19 && !keep_stable) {
            continue;
        }
        if (dst != src) {
            *dst = *src;
        }
        ++dst;
    }
    records->erase(dst, records->end());
}

static void
normalize_case_records(const char* case_name, std::vector<std::string>* omc,
                       std::vector<std::string>* cpp)
{
    if (case_name == nullptr) {
        return;
    }

    if (std::strcmp(case_name, "tiff_sony_tag2010e_makernote") == 0) {
        /* Current upstream emits a duplicate derived 0x1254 record here. */
        (void)dedupe_sorted_records(omc);
        (void)dedupe_sorted_records(cpp);
    } else if (std::strcmp(case_name,
                           "tiff_sony_tag940e_afinfo_makernote")
               == 0) {
        /*
         * Keep parity on the stable afstatus19 subset that both local direct
         * tests and public upstream tests lock today.
         */
        erase_sony_afstatus19_unstable_records(omc);
        erase_sony_afstatus19_unstable_records(cpp);
    }
}

static bool
compare_records(const char* case_name, const std::vector<std::string>& omc,
                const std::vector<std::string>& cpp)
{
    std::size_t i;
    std::size_t j;
    unsigned shown;

    if (omc == cpp) {
        return true;
    }

    std::fprintf(stderr, "%s parity mismatch\n", case_name);
    std::fprintf(stderr, "  omc entries: %zu\n", omc.size());
    std::fprintf(stderr, "  cpp entries: %zu\n", cpp.size());

    i = 0U;
    j = 0U;
    shown = 0U;
    while ((i < omc.size() || j < cpp.size()) && shown < 16U) {
        if (i < omc.size() && j < cpp.size() && omc[i] == cpp[j]) {
            i += 1U;
            j += 1U;
            continue;
        }
        if (j >= cpp.size()
            || (i < omc.size() && omc[i] < cpp[j])) {
            std::fprintf(stderr, "  only-omc: %s\n", omc[i].c_str());
            i += 1U;
            shown += 1U;
            continue;
        }
        std::fprintf(stderr, "  only-cpp: %s\n", cpp[j].c_str());
        j += 1U;
        shown += 1U;
    }
    return false;
}

static bool
run_case(const char* case_name, const ByteVec& file_bytes,
         const ReadCaseOptions& options)
{
    std::vector<std::string> omc;
    std::vector<std::string> cpp;

    omc = read_omc_records(file_bytes, options);
    cpp = read_cpp_records(file_bytes, options);
    normalize_case_records(case_name, &omc, &cpp);
    return compare_records(case_name, omc, cpp);
}

static bool
run_case(const char* case_name, const ByteVec& file_bytes,
         bool decode_makernote)
{
    ReadCaseOptions options {};

    options.decode_makernote = decode_makernote;
    return run_case(case_name, file_bytes, options);
}

static bool
run_verify_case(const char* case_name, const ByteVec& file_bytes)
{
    ReadCaseOptions options {};

    options.verify_c2pa = true;
    return run_case(case_name, file_bytes, options);
}

static bool
run_verify_policy_case(const char* case_name, const ByteVec& file_bytes,
                       bool require_resolved_references)
{
    ReadCaseOptions options {};

    options.verify_c2pa = true;
    options.verify_require_resolved_references = require_resolved_references;
    return run_case(case_name, file_bytes, options);
}

static bool
run_verify_backend_case(const char* case_name, const ByteVec& file_bytes,
                        bool request_openssl_backend)
{
    ReadCaseOptions options {};

    options.verify_c2pa = true;
    options.verify_backend_openssl = request_openssl_backend;
    return run_case(case_name, file_bytes, options);
}

static openmeta::XmpWritebackMode
to_cpp_writeback_mode(omc_xmp_writeback_mode mode)
{
    switch (mode) {
        case OMC_XMP_WRITEBACK_EMBEDDED_ONLY:
            return openmeta::XmpWritebackMode::EmbeddedOnly;
        case OMC_XMP_WRITEBACK_SIDECAR_ONLY:
            return openmeta::XmpWritebackMode::SidecarOnly;
        case OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR:
            return openmeta::XmpWritebackMode::EmbeddedAndSidecar;
    }
    return openmeta::XmpWritebackMode::EmbeddedOnly;
}

static openmeta::XmpDestinationEmbeddedMode
to_cpp_destination_embedded_mode(omc_xmp_destination_embedded_mode mode)
{
    switch (mode) {
        case OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING:
            return openmeta::XmpDestinationEmbeddedMode::PreserveExisting;
        case OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING:
            return openmeta::XmpDestinationEmbeddedMode::StripExisting;
    }
    return openmeta::XmpDestinationEmbeddedMode::PreserveExisting;
}

static bool
compare_text_field(const char* case_name, const char* field,
                   const std::string& omc, const std::string& cpp)
{
    if (omc == cpp) {
        return true;
    }
    std::fprintf(stderr, "%s mismatch in %s\n", case_name, field);
    std::fprintf(stderr, "  omc: %s\n", omc.c_str());
    std::fprintf(stderr, "  cpp: %s\n", cpp.c_str());
    return false;
}

static bool
compare_bool_field(const char* case_name, const char* field, bool omc,
                   bool cpp)
{
    if (omc == cpp) {
        return true;
    }
    std::fprintf(stderr, "%s mismatch in %s\n", case_name, field);
    std::fprintf(stderr, "  omc: %s\n", omc ? "true" : "false");
    std::fprintf(stderr, "  cpp: %s\n", cpp ? "true" : "false");
    return false;
}

static bool
run_omc_transfer_execute_case(const ByteVec& source_bytes,
                              const ByteVec& target_bytes,
                              const TransferExecuteCaseOptions& options,
                              TransferExecuteParitySummary* out)
{
    omc_store source_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_status status;

    if (out == nullptr) {
        return false;
    }
    *out = TransferExecuteParitySummary {};

    if (!read_omc_store(source_bytes, &source_store)) {
        std::fprintf(stderr, "omc transfer source decode failed\n");
        return false;
    }

    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = options.writeback_mode;
    prepare_opts.destination_embedded_mode
        = options.destination_embedded_mode;

    status = omc_transfer_prepare(target_bytes.data(),
                                  (omc_size)target_bytes.size(),
                                  &source_store, &prepare_opts, &bundle);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }
    status = omc_transfer_compile(&bundle, &exec);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }
    status = omc_transfer_execute(target_bytes.data(),
                                  (omc_size)target_bytes.size(),
                                  &source_store, &edited_out, &sidecar_out,
                                  &exec, &res);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }

    out->status = canonical_omc_transfer_status(res.status);
    out->edited_status = canonical_omc_xmp_write_status(res.embedded.status);
    out->sidecar_status = canonical_omc_xmp_dump_status(res.sidecar.status);
    out->sidecar_requested =
        options.writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    out->edited_present = (res.edited_present != 0);
    out->sidecar_present = (res.sidecar_present != 0);
    if (out->edited_present) {
        out->edited_records = read_omc_records(
            ByteVec(edited_out.data, edited_out.data + edited_out.size),
            ReadCaseOptions {});
    }
    if (out->sidecar_present) {
        out->sidecar_records = read_omc_records(
            ByteVec(sidecar_out.data, sidecar_out.data + sidecar_out.size),
            ReadCaseOptions {});
    }

    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
    return true;
}

static bool
run_cpp_transfer_execute_case(const ByteVec& source_bytes,
                              const ByteVec& target_bytes,
                              const TransferExecuteCaseOptions& options,
                              TransferExecuteParitySummary* out)
{
    openmeta::ExecutePreparedTransferFileOptions exec_opts;
    openmeta::ExecutePreparedTransferFileResult res;
    const std::string source_path
        = make_temp_path("omc_transfer_src", ".jpg");
    const std::string target_path = make_temp_path("omc_transfer_target",
                                                   options.target_suffix);
    ByteVec edited_bytes;
    ByteVec sidecar_bytes;

    if (out == nullptr) {
        return false;
    }
    *out = TransferExecuteParitySummary {};

    if (!write_file_bytes(source_path, source_bytes)
        || !write_file_bytes(target_path, target_bytes)) {
        remove_file_if_present(source_path);
        remove_file_if_present(target_path);
        return false;
    }

    exec_opts.prepare.prepare.target_format = options.cpp_target_format;
    exec_opts.prepare.prepare.include_icc_app2 = false;
    exec_opts.prepare.prepare.include_iptc_app13 = false;
    exec_opts.edit_target_path = target_path;
    exec_opts.execute.edit_apply = true;
    exec_opts.xmp_writeback_mode
        = to_cpp_writeback_mode(options.writeback_mode);
    exec_opts.xmp_destination_embedded_mode
        = to_cpp_destination_embedded_mode(options.destination_embedded_mode);

    res = openmeta::execute_prepared_transfer_file(source_path.c_str(),
                                                   exec_opts);
    remove_file_if_present(source_path);
    remove_file_if_present(target_path);

    out->status = canonical_cpp_transfer_status(res.execute.edit_apply.status);
    out->edited_status
        = canonical_cpp_transfer_status(res.execute.edit_apply.status);
    out->sidecar_status = canonical_cpp_transfer_status(res.xmp_sidecar_status);
    out->sidecar_requested =
        options.writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    out->edited_present = !res.execute.edited_output.empty();
    out->sidecar_present = !res.xmp_sidecar_output.empty();
    if (out->edited_present) {
        edited_bytes.resize(res.execute.edited_output.size());
        std::memcpy(edited_bytes.data(), res.execute.edited_output.data(),
                    edited_bytes.size());
        out->edited_records = read_cpp_records(edited_bytes,
                                               ReadCaseOptions {});
    }
    if (out->sidecar_present) {
        sidecar_bytes.resize(res.xmp_sidecar_output.size());
        std::memcpy(sidecar_bytes.data(), res.xmp_sidecar_output.data(),
                    sidecar_bytes.size());
        out->sidecar_records = read_cpp_records(sidecar_bytes,
                                                ReadCaseOptions {});
    }
    return true;
}

static bool
run_omc_transfer_persist_case(const ByteVec& source_bytes,
                              const ByteVec& target_bytes,
                              const TransferExecuteCaseOptions& options,
                              const std::string& output_path,
                              bool strip_destination_sidecar,
                              TransferPersistParitySummary* out)
{
    omc_store source_store;
    omc_transfer_prepare_opts prepare_opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res transfer_res;
    omc_transfer_persist_opts persist_opts;
    omc_transfer_persist_res persist_res;
    omc_arena edited_out;
    omc_arena sidecar_out;
    omc_arena meta_out;
    omc_status status;
    ByteVec output_bytes;
    ByteVec sidecar_bytes;
    const std::string sidecar_path = replace_extension(output_path, ".xmp");

    if (out == nullptr) {
        return false;
    }
    *out = TransferPersistParitySummary {};

    if (!read_omc_store(source_bytes, &source_store)) {
        std::fprintf(stderr, "omc transfer persist source decode failed\n");
        return false;
    }

    omc_arena_init(&edited_out);
    omc_arena_init(&sidecar_out);
    omc_arena_init(&meta_out);
    omc_transfer_prepare_opts_init(&prepare_opts);
    prepare_opts.writeback_mode = options.writeback_mode;
    prepare_opts.destination_embedded_mode
        = options.destination_embedded_mode;

    status = omc_transfer_prepare(target_bytes.data(),
                                  (omc_size)target_bytes.size(),
                                  &source_store, &prepare_opts, &bundle);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&meta_out);
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }
    status = omc_transfer_compile(&bundle, &exec);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&meta_out);
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }
    status = omc_transfer_execute(target_bytes.data(),
                                  (omc_size)target_bytes.size(),
                                  &source_store, &edited_out, &sidecar_out,
                                  &exec, &transfer_res);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&meta_out);
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }

    if (strip_destination_sidecar
        && !write_file_bytes(sidecar_path,
                             build_transfer_xmp_sidecar_fixture(
                                 "Stale Destination"))) {
        omc_arena_fini(&meta_out);
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }

    omc_transfer_persist_opts_init(&persist_opts);
    persist_opts.output_path = output_path.c_str();
    if (strip_destination_sidecar) {
        persist_opts.destination_sidecar_mode
            = OMC_TRANSFER_DEST_SIDECAR_STRIP_EXISTING;
    }

    status = omc_transfer_persist(edited_out.data, edited_out.size,
                                  sidecar_out.data, sidecar_out.size,
                                  &transfer_res, &persist_opts, &meta_out,
                                  &persist_res);
    if (status != OMC_STATUS_OK) {
        remove_file_if_present(output_path);
        remove_file_if_present(sidecar_path);
        omc_arena_fini(&meta_out);
        omc_arena_fini(&sidecar_out);
        omc_arena_fini(&edited_out);
        omc_store_fini(&source_store);
        return false;
    }

    out->status = canonical_omc_transfer_status(persist_res.status);
    out->output_status = canonical_omc_transfer_status(
        persist_res.output_status);
    out->sidecar_status = canonical_omc_transfer_status(
        persist_res.xmp_sidecar_status);
    out->cleanup_status = canonical_omc_transfer_status(
        persist_res.xmp_sidecar_cleanup_status);
    out->sidecar_requested = (persist_res.xmp_sidecar_requested != 0);
    out->cleanup_requested = (persist_res.xmp_sidecar_cleanup_requested != 0);
    out->cleanup_removed = (persist_res.xmp_sidecar_cleanup_removed != 0);
    out->output_path = byte_ref_text(meta_out, persist_res.output_path);
    out->sidecar_path = byte_ref_text(meta_out, persist_res.xmp_sidecar_path);
    out->cleanup_path = byte_ref_text(meta_out,
                                      persist_res.xmp_sidecar_cleanup_path);

    if (persist_res.output_status == OMC_TRANSFER_OK
        && read_file_bytes(output_path, &output_bytes)) {
        out->output_records = read_omc_records(output_bytes, ReadCaseOptions {});
    }
    if (out->sidecar_requested
        && persist_res.xmp_sidecar_status == OMC_TRANSFER_OK
        && read_file_bytes(sidecar_path, &sidecar_bytes)) {
        out->sidecar_records = read_omc_records(sidecar_bytes,
                                                ReadCaseOptions {});
    }

    remove_file_if_present(output_path);
    remove_file_if_present(sidecar_path);
    omc_arena_fini(&meta_out);
    omc_arena_fini(&sidecar_out);
    omc_arena_fini(&edited_out);
    omc_store_fini(&source_store);
    return true;
}

static bool
run_cpp_transfer_persist_case(const ByteVec& source_bytes,
                              const ByteVec& target_bytes,
                              const TransferExecuteCaseOptions& options,
                              const std::string& output_path,
                              bool strip_destination_sidecar,
                              TransferPersistParitySummary* out)
{
    openmeta::ExecutePreparedTransferFileOptions exec_opts;
    openmeta::ExecutePreparedTransferFileResult prepared;
    openmeta::PersistPreparedTransferFileOptions persist_opts;
    openmeta::PersistPreparedTransferFileResult persisted;
    const std::string source_path
        = make_temp_path("omc_transfer_src", ".jpg");
    const std::string target_path = make_temp_path("omc_transfer_target",
                                                   options.target_suffix);
    const std::string sidecar_path = replace_extension(output_path, ".xmp");
    ByteVec output_bytes;
    ByteVec sidecar_bytes;

    if (out == nullptr) {
        return false;
    }
    *out = TransferPersistParitySummary {};

    if (!write_file_bytes(source_path, source_bytes)
        || !write_file_bytes(target_path, target_bytes)) {
        remove_file_if_present(source_path);
        remove_file_if_present(target_path);
        return false;
    }
    if (strip_destination_sidecar
        && !write_file_bytes(sidecar_path,
                             build_transfer_xmp_sidecar_fixture(
                                 "Stale Destination"))) {
        remove_file_if_present(source_path);
        remove_file_if_present(target_path);
        remove_file_if_present(sidecar_path);
        return false;
    }

    exec_opts.prepare.prepare.target_format = options.cpp_target_format;
    exec_opts.prepare.prepare.include_icc_app2 = false;
    exec_opts.prepare.prepare.include_iptc_app13 = false;
    exec_opts.edit_target_path = target_path;
    exec_opts.xmp_sidecar_base_path = output_path;
    exec_opts.execute.edit_apply = true;
    exec_opts.xmp_writeback_mode
        = to_cpp_writeback_mode(options.writeback_mode);
    exec_opts.xmp_destination_embedded_mode
        = to_cpp_destination_embedded_mode(options.destination_embedded_mode);
    if (strip_destination_sidecar) {
        exec_opts.xmp_destination_sidecar_mode
            = openmeta::XmpDestinationSidecarMode::StripExisting;
    }

    prepared = openmeta::execute_prepared_transfer_file(source_path.c_str(),
                                                        exec_opts);
    persist_opts.output_path = output_path;
    persisted = openmeta::persist_prepared_transfer_file_result(prepared,
                                                                persist_opts);

    out->status = canonical_cpp_transfer_status(persisted.status);
    out->output_status = canonical_cpp_transfer_status(
        persisted.output_status);
    out->sidecar_status = canonical_cpp_transfer_status(
        persisted.xmp_sidecar_status);
    out->cleanup_status = canonical_cpp_transfer_status(
        persisted.xmp_sidecar_cleanup_status);
    out->sidecar_requested = prepared.xmp_sidecar_requested;
    out->cleanup_requested = prepared.xmp_sidecar_cleanup_requested;
    out->cleanup_removed = persisted.xmp_sidecar_cleanup_removed;
    out->output_path = persisted.output_path;
    out->sidecar_path = persisted.xmp_sidecar_path;
    out->cleanup_path = persisted.xmp_sidecar_cleanup_path;

    if (persisted.output_status == openmeta::TransferStatus::Ok
        && read_file_bytes(output_path, &output_bytes)) {
        out->output_records = read_cpp_records(output_bytes,
                                               ReadCaseOptions {});
    }
    if (out->sidecar_requested
        && persisted.xmp_sidecar_status == openmeta::TransferStatus::Ok
        && read_file_bytes(sidecar_path, &sidecar_bytes)) {
        out->sidecar_records = read_cpp_records(sidecar_bytes,
                                                ReadCaseOptions {});
    }

    remove_file_if_present(source_path);
    remove_file_if_present(target_path);
    remove_file_if_present(output_path);
    remove_file_if_present(sidecar_path);
    return true;
}

static bool
compare_transfer_execute_summary(const char* case_name,
                                 const TransferExecuteParitySummary& omc,
                                 const TransferExecuteParitySummary& cpp)
{
    bool ok = true;
    std::vector<std::string> omc_edited = omc.edited_records;
    std::vector<std::string> cpp_edited = cpp.edited_records;
    std::vector<std::string> omc_sidecar = omc.sidecar_records;
    std::vector<std::string> cpp_sidecar = cpp.sidecar_records;

    erase_records_containing(&omc_edited, "XMPToolkit");
    erase_records_containing(&cpp_edited, "XMPToolkit");
    erase_records_containing(&omc_sidecar, "XMPToolkit");
    erase_records_containing(&cpp_sidecar, "XMPToolkit");
    erase_records_containing(&omc_edited, "ExifTag|ifd=ifd0|tag=700||Bytes|");
    erase_records_containing(&cpp_edited, "ExifTag|ifd=ifd0|tag=700||Bytes|");
    erase_records_containing(&omc_edited, "ExifTag|ifd=ifd0|tag=34665||");
    erase_records_containing(&cpp_edited, "ExifTag|ifd=ifd0|tag=34665||");
    erase_records_with_prefix(&omc_edited, "BmffField|");
    erase_records_with_prefix(&cpp_edited, "BmffField|");
    erase_records_containing(
        &omc_edited,
        "XmpProperty|schema=http://ns.adobe.com/tiff/1.0/|path=ExifTag||");
    erase_records_containing(
        &cpp_edited,
        "XmpProperty|schema=http://ns.adobe.com/tiff/1.0/|path=ExifTag||");
    erase_records_containing(
        &omc_sidecar,
        "XmpProperty|schema=http://ns.adobe.com/tiff/1.0/|path=ExifTag||");
    erase_records_containing(
        &cpp_sidecar,
        "XmpProperty|schema=http://ns.adobe.com/tiff/1.0/|path=ExifTag||");

    ok = compare_text_field(case_name, "status", omc.status, cpp.status) && ok;
    ok = compare_text_field(case_name, "edited_status", omc.edited_status,
                            cpp.edited_status)
         && ok;
    ok = compare_bool_field(case_name, "sidecar_requested", omc.sidecar_requested,
                            cpp.sidecar_requested)
         && ok;
    if (omc.sidecar_requested && cpp.sidecar_requested) {
        ok = compare_text_field(case_name, "sidecar_status",
                                omc.sidecar_status, cpp.sidecar_status)
             && ok;
    }
    ok = compare_bool_field(case_name, "edited_present", omc.edited_present,
                            cpp.edited_present)
         && ok;
    ok = compare_bool_field(case_name, "sidecar_present", omc.sidecar_present,
                            cpp.sidecar_present)
         && ok;
    if (omc.edited_present && cpp.edited_present) {
        const std::string label = std::string(case_name) + ".edited";
        ok = compare_records(label.c_str(), omc_edited, cpp_edited)
             && ok;
    }
    if (omc.sidecar_present && cpp.sidecar_present) {
        const std::string label = std::string(case_name) + ".sidecar";
        ok = compare_records(label.c_str(), omc_sidecar, cpp_sidecar)
             && ok;
    }
    return ok;
}

static bool
compare_transfer_persist_summary(const char* case_name,
                                 const TransferPersistParitySummary& omc,
                                 const TransferPersistParitySummary& cpp)
{
    bool ok = true;
    std::vector<std::string> omc_output = omc.output_records;
    std::vector<std::string> cpp_output = cpp.output_records;
    std::vector<std::string> omc_sidecar = omc.sidecar_records;
    std::vector<std::string> cpp_sidecar = cpp.sidecar_records;

    erase_records_containing(&omc_output, "XMPToolkit");
    erase_records_containing(&cpp_output, "XMPToolkit");
    erase_records_containing(&omc_sidecar, "XMPToolkit");
    erase_records_containing(&cpp_sidecar, "XMPToolkit");
    erase_records_containing(&omc_output, "ExifTag|ifd=ifd0|tag=700||Bytes|");
    erase_records_containing(&cpp_output, "ExifTag|ifd=ifd0|tag=700||Bytes|");
    erase_records_containing(&omc_output, "ExifTag|ifd=ifd0|tag=34665||");
    erase_records_containing(&cpp_output, "ExifTag|ifd=ifd0|tag=34665||");
    erase_records_with_prefix(&omc_output, "BmffField|");
    erase_records_with_prefix(&cpp_output, "BmffField|");
    erase_records_containing(
        &omc_sidecar,
        "XmpProperty|schema=http://ns.adobe.com/tiff/1.0/|path=ExifTag||");
    erase_records_containing(
        &cpp_sidecar,
        "XmpProperty|schema=http://ns.adobe.com/tiff/1.0/|path=ExifTag||");

    ok = compare_text_field(case_name, "status", omc.status, cpp.status) && ok;
    ok = compare_text_field(case_name, "output_status", omc.output_status,
                            cpp.output_status)
         && ok;
    ok = compare_text_field(case_name, "sidecar_status", omc.sidecar_status,
                            cpp.sidecar_status)
         && ok;
    ok = compare_text_field(case_name, "cleanup_status", omc.cleanup_status,
                            cpp.cleanup_status)
         && ok;
    ok = compare_bool_field(case_name, "sidecar_requested",
                            omc.sidecar_requested, cpp.sidecar_requested)
         && ok;
    ok = compare_bool_field(case_name, "cleanup_requested",
                            omc.cleanup_requested, cpp.cleanup_requested)
         && ok;
    ok = compare_bool_field(case_name, "cleanup_removed",
                            omc.cleanup_removed, cpp.cleanup_removed)
         && ok;
    ok = compare_text_field(case_name, "output_path", omc.output_path,
                            cpp.output_path)
         && ok;
    ok = compare_text_field(case_name, "sidecar_path", omc.sidecar_path,
                            cpp.sidecar_path)
         && ok;
    ok = compare_text_field(case_name, "cleanup_path", omc.cleanup_path,
                            cpp.cleanup_path)
         && ok;
    if (!omc.output_records.empty() || !cpp.output_records.empty()) {
        const std::string label = std::string(case_name) + ".output";
        ok = compare_records(label.c_str(), omc_output, cpp_output)
             && ok;
    }
    if (!omc.sidecar_records.empty() || !cpp.sidecar_records.empty()) {
        const std::string label = std::string(case_name) + ".sidecar";
        ok = compare_records(label.c_str(), omc_sidecar, cpp_sidecar)
             && ok;
    }
    return ok;
}

static bool
run_transfer_execute_case(const char* case_name, const ByteVec& source_bytes,
                          const ByteVec& target_bytes,
                          const TransferExecuteCaseOptions& options)
{
    TransferExecuteParitySummary omc;
    TransferExecuteParitySummary cpp;

    if (!run_omc_transfer_execute_case(source_bytes, target_bytes, options,
                                       &omc)) {
        std::fprintf(stderr, "%s omc transfer execute failed\n", case_name);
        return false;
    }
    if (!run_cpp_transfer_execute_case(source_bytes, target_bytes, options,
                                       &cpp)) {
        std::fprintf(stderr, "%s cpp transfer execute failed\n", case_name);
        return false;
    }
    return compare_transfer_execute_summary(case_name, omc, cpp);
}

static bool
run_transfer_persist_case(const char* case_name, const ByteVec& source_bytes,
                          const ByteVec& target_bytes,
                          const TransferExecuteCaseOptions& options,
                          bool strip_destination_sidecar)
{
    TransferPersistParitySummary omc;
    TransferPersistParitySummary cpp;
    const std::string output_path
        = make_temp_path("omc_transfer_out", options.target_suffix);

    if (!run_omc_transfer_persist_case(source_bytes, target_bytes, options,
                                       output_path,
                                       strip_destination_sidecar, &omc)) {
        std::fprintf(stderr, "%s omc transfer persist failed\n", case_name);
        return false;
    }
    if (!run_cpp_transfer_persist_case(source_bytes, target_bytes, options,
                                       output_path,
                                       strip_destination_sidecar, &cpp)) {
        std::fprintf(stderr, "%s cpp transfer persist failed\n", case_name);
        return false;
    }
    return compare_transfer_persist_summary(case_name, omc, cpp);
}

struct BenchCase final {
    const char* name;
    ByteVec (*build)();
    bool decode_makernote;
};

static volatile std::uint64_t g_bench_sink = 0U;

static std::uint64_t
bench_omc_iters(const ByteVec& file_bytes, bool decode_makernote,
                std::size_t iters)
{
    std::uint64_t sum = 0U;
    std::size_t i;

    for (i = 0U; i < iters; ++i) {
        omc_store store;
        omc_read_res res;
        omc_read_opts opts;
        std::array<omc_blk_ref, 128> blocks {};
        std::array<omc_exif_ifd_ref, 128> ifds {};
        std::array<omc_u8, 65536> payload {};
        std::array<omc_u32, 256> scratch {};

        omc_store_init(&store);
        omc_read_opts_init(&opts);
        if (decode_makernote) {
            opts.exif.decode_makernote = 1;
        }
        res = omc_read_simple(file_bytes.data(), (omc_size)file_bytes.size(),
                              &store, blocks.data(),
                              (omc_u32)blocks.size(), ifds.data(),
                              (omc_u32)ifds.size(), payload.data(),
                              (omc_size)payload.size(), scratch.data(),
                              (omc_u32)scratch.size(), &opts);
        if (res.scan.status == OMC_SCAN_MALFORMED) {
            std::fprintf(stderr, "omc benchmark scan failed\n");
            std::exit(1);
        }
        sum += (std::uint64_t)store.entry_count;
        sum += (std::uint64_t)res.exif.entries_decoded;
        omc_store_fini(&store);
    }

    return sum;
}

static std::uint64_t
bench_cpp_iters(const ByteVec& file_bytes, bool decode_makernote,
                std::size_t iters)
{
    std::uint64_t sum = 0U;
    std::size_t i;

    for (i = 0U; i < iters; ++i) {
        openmeta::MetaStore store;
        openmeta::SimpleMetaDecodeOptions opts {};
        std::array<openmeta::ContainerBlockRef, 128> blocks {};
        std::array<openmeta::ExifIfdRef, 128> ifds {};
        std::array<std::byte, 65536> payload {};
        std::array<std::uint32_t, 256> scratch {};
        openmeta::SimpleMetaResult res;

        if (decode_makernote) {
            opts.exif.decode_makernote = true;
        }
        res = openmeta::simple_meta_read(as_byte_span(file_bytes), store, blocks,
                                         ifds, payload, scratch, opts);
        if (res.scan.status == openmeta::ScanStatus::Malformed) {
            std::fprintf(stderr, "cpp benchmark scan failed\n");
            std::exit(1);
        }
        sum += (std::uint64_t)store.entries().size();
        sum += (std::uint64_t)res.exif.entries_decoded;
    }

    return sum;
}

static double
time_omc_ns_per_iter(const ByteVec& file_bytes, bool decode_makernote,
                     std::size_t iters)
{
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t sum = bench_omc_iters(file_bytes, decode_makernote,
                                              iters);
    const auto stop = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        stop - start);

    g_bench_sink += sum;
    return (double)ns.count() / (double)iters;
}

static double
time_cpp_ns_per_iter(const ByteVec& file_bytes, bool decode_makernote,
                     std::size_t iters)
{
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t sum = bench_cpp_iters(file_bytes, decode_makernote,
                                              iters);
    const auto stop = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        stop - start);

    g_bench_sink += sum;
    return (double)ns.count() / (double)iters;
}

static std::size_t
calibrate_iters(const ByteVec& file_bytes, bool decode_makernote)
{
    std::size_t iters = 1U;
    double ns_per_iter;
    double total_ns;

    for (;;) {
        ns_per_iter = time_omc_ns_per_iter(file_bytes, decode_makernote,
                                           iters);
        total_ns = ns_per_iter * (double)iters;
        if (total_ns >= 30000000.0 || iters >= 8192U) {
            break;
        }
        iters *= 2U;
    }
    if (iters < 64U) {
        iters = 64U;
    }
    return iters;
}

static double
sorted_percentile(std::vector<double> values, std::size_t numer,
                  std::size_t denom)
{
    std::size_t idx;

    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    idx = ((values.size() - 1U) * numer) / denom;
    return values[idx];
}

static int
run_benchmarks(void)
{
    static const std::array<BenchCase, 6> cases = { {
        { "jpeg_all", build_jpeg_all_fixture, false },
        { "png_text", build_png_text_fixture, false },
        { "tiff_geotiff", build_tiff_geotiff_fixture, false },
        { "tiff_printim", build_tiff_printim_fixture, false },
        { "crw_native_projection", build_crw_native_projection_fixture, false },
        { "tiff_fuji_makernote", build_tiff_fuji_makernote_fixture, true },
    } };
    std::size_t i;

    std::printf("%-22s %8s %8s %14s %14s %10s\n",
                "case", "bytes", "iters", "c_ns_p50", "cpp_ns_p50",
                "c_speedup");
    for (i = 0U; i < cases.size(); ++i) {
        const ByteVec file_bytes = cases[i].build();
        const std::size_t iters = calibrate_iters(file_bytes,
                                                  cases[i].decode_makernote);
        std::vector<double> omc_rounds;
        std::vector<double> cpp_rounds;
        std::size_t round;
        double omc_p50;
        double cpp_p50;
        double omc_p95;
        double cpp_p95;
        double speedup;

        omc_rounds.reserve(9U);
        cpp_rounds.reserve(9U);

        (void)time_omc_ns_per_iter(file_bytes, cases[i].decode_makernote,
                                   iters);
        (void)time_cpp_ns_per_iter(file_bytes, cases[i].decode_makernote,
                                   iters);

        for (round = 0U; round < 9U; ++round) {
            omc_rounds.push_back(
                time_omc_ns_per_iter(file_bytes, cases[i].decode_makernote,
                                     iters));
            cpp_rounds.push_back(
                time_cpp_ns_per_iter(file_bytes, cases[i].decode_makernote,
                                     iters));
        }

        omc_p50 = sorted_percentile(omc_rounds, 1U, 2U);
        cpp_p50 = sorted_percentile(cpp_rounds, 1U, 2U);
        omc_p95 = sorted_percentile(omc_rounds, 19U, 20U);
        cpp_p95 = sorted_percentile(cpp_rounds, 19U, 20U);
        speedup = (omc_p50 > 0.0) ? (cpp_p50 / omc_p50) : 0.0;

        std::printf("%-22s %8zu %8zu %14.1f %14.1f %10.3fx\n",
                    cases[i].name, file_bytes.size(), iters,
                    omc_p50, cpp_p50, speedup);
        std::printf("%-22s %8s %8s %14.1f %14.1f\n",
                    "  p95", "", "", omc_p95, cpp_p95);
    }

    std::printf("bench_sink=%llu\n",
                (unsigned long long)g_bench_sink);
    return 0;
}

}  // namespace

int
main(int argc, char** argv)
{
    bool ok;

    if (argc == 2 && std::strcmp(argv[1], "--bench") == 0) {
        return run_benchmarks();
    }
    if (argc != 1) {
        std::fprintf(stderr, "usage: %s [--bench]\n", argv[0]);
        return 2;
    }

    ok = true;
    ok = run_case("jpeg_comment", build_jpeg_comment_fixture(), false) && ok;
    ok = run_case("jpeg_all", build_jpeg_all_fixture(), false) && ok;
    ok = run_case("jpeg_irb_fields", build_jpeg_irb_fields_fixture(), false)
         && ok;
    ok = run_case("png_text", build_png_text_fixture(), false) && ok;
    if (false) {
        /*
         * Current upstream transfer/persist parity is not stable enough to
         * lock in the shared harness: some cases now report unsupported while
         * others crash inside the C++ transfer runner. Keep the direct C
         * transfer/persist tests as the lock until the upstream-visible
         * workflow stabilizes again.
         */
    if (false) {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        ok = run_transfer_execute_case(
                 "transfer_jpeg_embedded_and_sidecar",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_jpeg_fixture("OldTool", true),
                 transfer_opts)
             && ok;
    }
    if (false) {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Png;
        transfer_opts.target_suffix = ".png";
        ok = run_transfer_execute_case(
                 "transfer_png_embedded_and_sidecar",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_png_fixture("OldTool"), transfer_opts)
             && ok;
    }
    if (false) {
        /*
         * Current upstream file transfer reports these TIFF XMP-only embedded
         * writeback cases as unsupported. Keep the direct C tests as the lock
         * until the upstream-visible workflow aligns again.
         */
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_tiff_embedded_and_sidecar",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_tiff_fixture("OldTool"), transfer_opts)
             && ok;
    }
    if (false) {
        /*
         * Current upstream file transfer reports these TIFF XMP-only embedded
         * writeback cases as unsupported. Keep the direct C tests as the lock
         * until the upstream-visible workflow aligns again.
         */
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_bigtiff_embedded_and_sidecar",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_bigtiff_fixture("OldTool"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        ok = run_transfer_execute_case(
                 "transfer_jpeg_sidecar_only_strip",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_jpeg_fixture("OldTool", true),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        ok = run_transfer_execute_case(
                 "transfer_jpeg_sidecar_only_preserve",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_jpeg_fixture(
                     "Target Embedded Existing", true),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        ok = run_transfer_execute_case(
                 "transfer_jpeg_exif_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jpeg_fixture("OldTool", true),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        ok = run_transfer_execute_case(
                 "transfer_jpeg_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jpeg_fixture(
                     "Target Embedded Existing", true),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        ok = run_transfer_execute_case(
                 "transfer_jpeg_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jpeg_fixture(
                     "Target Embedded Existing", true),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
        ok = run_transfer_persist_case(
                 "transfer_persist_jpeg_embedded_only_strip_sidecar",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_jpeg_fixture(nullptr, true),
                 transfer_opts, true)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
        ok = run_transfer_persist_case(
                 "transfer_persist_jpeg_exif_embedded_only_strip_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jpeg_fixture(nullptr, true),
                 transfer_opts, true)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Webp;
        transfer_opts.target_suffix = ".webp";
        ok = run_transfer_execute_case(
                 "transfer_webp_exif_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_webp_fixture("OldTool"), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jp2;
        transfer_opts.target_suffix = ".jp2";
        ok = run_transfer_execute_case(
                 "transfer_jp2_exif_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jp2_fixture("OldTool"), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Heif;
        transfer_opts.target_suffix = ".heic";
        ok = run_transfer_execute_case(
                 "transfer_heif_exif_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_heif_fixture("OldTool"), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Avif;
        transfer_opts.target_suffix = ".avif";
        ok = run_transfer_execute_case(
                 "transfer_avif_exif_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_avif_fixture("OldTool"), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Webp;
        transfer_opts.target_suffix = ".webp";
        ok = run_transfer_execute_case(
                 "transfer_webp_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_webp_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jp2;
        transfer_opts.target_suffix = ".jp2";
        ok = run_transfer_execute_case(
                 "transfer_jp2_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jp2_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Heif;
        transfer_opts.target_suffix = ".heic";
        ok = run_transfer_execute_case(
                 "transfer_heif_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_heif_fixture(
                     "Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Avif;
        transfer_opts.target_suffix = ".avif";
        ok = run_transfer_execute_case(
                 "transfer_avif_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_avif_fixture(
                     "Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Webp;
        transfer_opts.target_suffix = ".webp";
        ok = run_transfer_execute_case(
                 "transfer_webp_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_webp_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jp2;
        transfer_opts.target_suffix = ".jp2";
        ok = run_transfer_execute_case(
                 "transfer_jp2_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jp2_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Heif;
        transfer_opts.target_suffix = ".heic";
        ok = run_transfer_execute_case(
                 "transfer_heif_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_heif_fixture(
                     "Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Avif;
        transfer_opts.target_suffix = ".avif";
        ok = run_transfer_execute_case(
                 "transfer_avif_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_avif_fixture(
                     "Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Png;
        transfer_opts.target_suffix = ".png";
        ok = run_transfer_persist_case(
                 "transfer_persist_png_embedded_and_sidecar",
                 build_transfer_source_jpeg_fixture(),
                 build_transfer_target_png_fixture("OldTool"), transfer_opts,
                 false)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_persist_case(
                 "transfer_persist_bigtiff_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_bigtiff_fixture(
                     "Target Embedded Existing"),
                 transfer_opts, false)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Heif;
        transfer_opts.target_suffix = ".heic";
        ok = run_transfer_persist_case(
                 "transfer_persist_heif_embedded_only_roundtrip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_heif_minimal_fixture(), transfer_opts,
                 false)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Avif;
        transfer_opts.target_suffix = ".avif";
        ok = run_transfer_persist_case(
                 "transfer_persist_avif_embedded_only_roundtrip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_avif_minimal_fixture(), transfer_opts,
                 false)
             && ok;
    }
    if (false) {
        /*
         * Current upstream file persistence still reports this HEIF strip case
         * as unsupported. Keep the direct C persistence tests as the lock until
         * the upstream-visible workflow aligns.
         */
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode =
            OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Heif;
        transfer_opts.target_suffix = ".heic";
        ok = run_transfer_persist_case(
                 "transfer_persist_heif_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_heif_fixture("Target Embedded Existing"),
                 transfer_opts, false)
             && ok;
    }
    }
    if (false) {
        /*
         * Current upstream EXIF-bearing file transfer parity is not stable
         * enough to lock here: the visible C++ results now report several
         * TIFF-family cases as unsupported and some other transfer cases still
         * crash in the upstream runner. Keep the direct C transfer/persist
         * tests as the lock until the upstream-visible workflow stabilizes.
         */
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_tiff_exif_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_tiff_fixture("OldTool"), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_bigtiff_exif_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_bigtiff_fixture("OldTool"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_tiff_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_tiff_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_bigtiff_exif_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_bigtiff_fixture(
                     "Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode =
            OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_tiff_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_tiff_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode =
            OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_bigtiff_exif_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_bigtiff_fixture(
                     "Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Webp;
        transfer_opts.target_suffix = ".webp";
        ok = run_transfer_execute_case(
                 "transfer_webp_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_webp_fixture("OldTool"), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jp2;
        transfer_opts.target_suffix = ".jp2";
        ok = run_transfer_execute_case(
                 "transfer_jp2_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jp2_fixture("OldTool"), transfer_opts)
             && ok;
    }
    if (false) {
        /*
         * Current upstream transfer crashes inside execute_prepared_transfer on
         * this JXL EXIF-bearing fixture. Keep the direct C tests as the lock
         * until the upstream-visible workflow is stable again.
         */
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jxl;
        transfer_opts.target_suffix = ".jxl";
        ok = run_transfer_execute_case(
                 "transfer_jxl_embedded_and_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jxl_fixture("OldTool"), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Heif;
        transfer_opts.target_suffix = ".heic";
        ok = run_transfer_execute_case(
                 "transfer_heif_embedded_only_roundtrip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_heif_minimal_fixture(), transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Avif;
        transfer_opts.target_suffix = ".avif";
        ok = run_transfer_execute_case(
                 "transfer_avif_embedded_only_roundtrip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_avif_minimal_fixture(), transfer_opts)
             && ok;
    }
    {
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Webp;
        transfer_opts.target_suffix = ".webp";
        ok = run_transfer_execute_case(
                 "transfer_webp_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_webp_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jp2;
        transfer_opts.target_suffix = ".jp2";
        ok = run_transfer_execute_case(
                 "transfer_jp2_sidecar_only_preserve",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jp2_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Webp;
        transfer_opts.target_suffix = ".webp";
        ok = run_transfer_execute_case(
                 "transfer_webp_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_webp_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jp2;
        transfer_opts.target_suffix = ".jp2";
        ok = run_transfer_execute_case(
                 "transfer_jp2_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jp2_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Png;
        transfer_opts.target_suffix = ".png";
        ok = run_transfer_execute_case(
                 "transfer_png_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_png_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_tiff_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_tiff_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    {
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Tiff;
        transfer_opts.target_suffix = ".tif";
        ok = run_transfer_execute_case(
                 "transfer_bigtiff_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_bigtiff_fixture(
                     "Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    if (false) {
        /*
         * Current upstream transfer crashes inside execute_prepared_transfer on
         * this JXL EXIF-bearing fixture. Keep the direct C tests as the lock
         * until the upstream-visible workflow is stable again.
         */
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
        transfer_opts.destination_embedded_mode
            = OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jxl;
        transfer_opts.target_suffix = ".jxl";
        ok = run_transfer_execute_case(
                 "transfer_jxl_sidecar_only_strip",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jxl_fixture("Target Embedded Existing"),
                 transfer_opts)
             && ok;
    }
    if (false) {
        /*
         * Current upstream transfer crashes inside execute_prepared_transfer on
         * this JXL EXIF-bearing fixture. Keep the direct C tests as the lock
         * until the upstream-visible workflow is stable again.
         */
        TransferExecuteCaseOptions transfer_opts {};

        transfer_opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
        transfer_opts.cpp_target_format = openmeta::TransferTargetFormat::Jxl;
        transfer_opts.target_suffix = ".jxl";
        ok = run_transfer_persist_case(
                 "transfer_persist_jxl_embedded_only_strip_sidecar",
                 build_transfer_source_jpeg_exif_xmp_fixture(),
                 build_transfer_target_jxl_fixture("OldTool"), transfer_opts,
                 true)
             && ok;
    }
    }
    }
    ok = run_case("jumbf_verify_not_requested",
                  build_jumbf_verify_scaffold_requested_fixture(), false)
         && ok;
    ok = run_verify_backend_case("jumbf_verify_requested_openssl",
                                 build_jumbf_verify_scaffold_requested_fixture(),
                                 true)
         && ok;
    ok = run_verify_case("jumbf_verify_detached_payload",
                         build_jumbf_verify_detached_payload_resolution_fixture())
         && ok;
    ok = run_verify_case("jumbf_verify_explicit_claim_ref",
                         build_jumbf_verify_explicit_claim_ref_detached_payload_fixture())
         && ok;
    ok = run_verify_case("jumbf_verify_explicit_label",
                         build_jumbf_verify_explicit_label_detached_payload_fixture())
         && ok;
    ok = run_verify_case("jumbf_verify_unresolved_no_fallback",
                         build_jumbf_verify_unresolved_detached_payload_fixture())
         && ok;
    ok = run_verify_case("jumbf_verify_percent_encoded_claim_ref",
                         build_jumbf_verify_percent_encoded_claim_ref_fixture())
         && ok;
    ok = run_verify_case("jumbf_verify_percent_encoded_jumbf_label",
                         build_jumbf_verify_percent_encoded_jumbf_label_fixture())
         && ok;
    ok = run_verify_policy_case(
             "jumbf_verify_require_resolved_refs_unresolved",
             build_jumbf_verify_require_resolved_references_unresolved_fixture(),
             true)
         && ok;
    ok = run_verify_policy_case(
             "jumbf_verify_require_resolved_refs_ambiguous",
             build_jumbf_verify_require_resolved_references_ambiguous_fixture(),
             true)
         && ok;
    ok = run_verify_policy_case(
             "jumbf_verify_require_resolved_refs_disabled",
             build_jumbf_verify_require_resolved_references_unresolved_fixture(),
             false)
         && ok;
    ok = run_case("jumbf_cose_signature_fields",
                  build_jumbf_cose_signature_fields_fixture(), false)
         && ok;
    ok = run_verify_backend_case(
             "jumbf_verify_cose_detached_payload_claim_bytes",
             build_jumbf_verify_cose_detached_payload_from_claim_bytes_fixture(),
             true)
         && ok;
    ok = run_verify_backend_case(
             "jumbf_verify_reference_map_entries",
             build_jumbf_verify_reference_map_entries_fixture(), true)
         && ok;
    ok = run_verify_backend_case(
             "jumbf_verify_reference_map_claims_array",
             build_jumbf_verify_reference_map_claims_array_fixture(), true)
         && ok;
    ok = run_verify_backend_case(
             "jumbf_verify_reference_map_index_uri",
             build_jumbf_verify_reference_map_index_uri_fixture(), true)
         && ok;
    ok = run_verify_backend_case(
             "jumbf_verify_multiclaim_multisig_query_index",
             build_jumbf_verify_multiclaim_multisig_query_index_fixture(),
             true)
         && ok;
    ok = run_verify_backend_case(
             "jumbf_verify_multiclaim_multisig_nested_refs",
             build_jumbf_verify_multiclaim_multisig_nested_refs_fixture(),
             true)
         && ok;
    ok = run_verify_backend_case(
             "jumbf_verify_multiclaim_multisig_nested_idrefs",
             build_jumbf_verify_multiclaim_multisig_nested_idrefs_fixture(),
             true)
         && ok;
    ok = run_case("bmff_auxc_semantics",
                  build_bmff_auxc_semantics_fixture(), false)
         && ok;
    ok = run_case("bmff_aux_subtype_kinds",
                  build_bmff_aux_subtype_upstream_fixture(), false)
         && ok;
    ok = run_case("bmff_pred_iref", build_bmff_pred_fixture(), false) && ok;
    ok = run_case("bmff_v1_auxl_iref", build_bmff_v1_auxl_fixture(), false)
         && ok;
    ok = run_case("bmff_nonprimary_typed_iref",
                  build_bmff_nonprimary_typed_iref_fixture(), false)
         && ok;
    ok = run_case("bmff_v1_nonprimary_typed_iref",
                  build_bmff_v1_nonprimary_typed_iref_fixture(), false)
         && ok;
    ok = run_case("bmff_duplicate_edges",
                  build_bmff_duplicate_edges_fixture(), false)
         && ok;
    ok = run_case("bmff_item_info_rows",
                  build_bmff_item_info_rows_fixture(), false)
         && ok;
    ok = run_case("bmff_primary_mime_item_info",
                  build_bmff_primary_mime_item_info_fixture(), false)
         && ok;
    ok = run_case("bmff_item_info_without_pitm",
                  build_bmff_item_info_without_pitm_fixture(), false)
         && ok;
    ok = run_case("bmff_primary_uri_item_info",
                  build_bmff_primary_uri_item_info_fixture(), false)
         && ok;
    ok = run_case("exr_single_part", build_exr_single_part_fixture(), false)
         && ok;
    ok = run_case("exr_known_types", build_exr_known_types_fixture(), false)
         && ok;
    ok = run_case("exr_float_matrix_types",
                  build_exr_float_matrix_types_fixture(), false)
         && ok;
    ok = run_case("exr_complex_raw_types",
                  build_exr_complex_raw_types_fixture(), false)
         && ok;
    ok = run_case("exr_enum_vector_matrix_types",
                  build_exr_enum_vector_matrix_types_fixture(), false)
         && ok;
    ok = run_case("tiff_geotiff", build_tiff_geotiff_fixture(), false) && ok;
    ok = run_case("tiff_printim", build_tiff_printim_fixture(), false) && ok;
    ok = run_case("crw_textual_ciff", build_crw_textual_ciff_fixture(), false)
         && ok;
    ok = run_case("crw_native_projection",
                  build_crw_native_projection_fixture(), false)
         && ok;
    ok = run_case("crw_semantic_native_scalars",
                  build_crw_semantic_native_scalars_fixture(), false)
         && ok;
    ok = run_case("crw_decoder_table", build_crw_decoder_table_fixture(),
                  false)
         && ok;
    ok = run_case("crw_rawjpginfo_whitesample",
                  build_crw_rawjpginfo_whitesample_fixture(), false)
         && ok;
    ok = run_case("crw_shotinfo", build_crw_shotinfo_fixture(), false) && ok;
    ok = run_case("tiff_casio_makernote",
                  build_tiff_casio_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_casio_legacy_makernote",
                  build_tiff_casio_legacy_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_casio_legacy_ifd_makernote",
                  build_tiff_casio_legacy_ifd_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_casio_faceinfo2_makernote",
                  build_tiff_casio_faceinfo2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_samsung_stmn_makernote",
                  build_tiff_samsung_stmn_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_samsung_type2_makernote",
                  build_tiff_samsung_type2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_samsung_type2_u16_picturewizard_makernote",
                  build_tiff_samsung_type2_u16_picturewizard_fixture(), true)
         && ok;
    ok = run_case("tiff_samsung_compat_digits_makernote",
                  build_tiff_samsung_compat_digits_fixture(), true)
         && ok;
    ok = run_case("tiff_samsung_type2_a002_a003_makernote",
                  build_tiff_samsung_type2_a002_a003_fixture(), true)
         && ok;
    ok = run_case("tiff_minolta_makernote",
                  build_tiff_minolta_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_minolta_binary_makernote",
                  build_tiff_minolta_binary_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_motorola_legacy_makernote",
                  build_tiff_motorola_legacy_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_motorola_modern_makernote",
                  build_tiff_motorola_modern_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_pentax_binary_makernote",
                  build_tiff_pentax_binary_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_pentax_placeholder_makernote",
                  build_tiff_pentax_placeholder_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_ricoh_type2_makernote",
                  build_tiff_ricoh_type2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_ricoh_padded_type2_makernote",
                  build_tiff_ricoh_padded_type2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_ricoh_native_makernote",
                  build_tiff_ricoh_native_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_panasonic_makernote",
                  build_tiff_panasonic_makernote_fixture(false), true)
         && ok;
    ok = run_case("tiff_panasonic_truncated_next_ifd_makernote",
                  build_tiff_panasonic_makernote_fixture(true), true)
         && ok;
    ok = run_case("tiff_panasonic_type2_makernote",
                  build_tiff_panasonic_type2_fixture(), true)
         && ok;
    ok = run_case("tiff_panasonic_extended_makernote",
                  build_tiff_panasonic_extended_makernote_fixture(false), true)
         && ok;
    ok = run_case("tiff_panasonic_extended_truncated_makernote",
                  build_tiff_panasonic_extended_makernote_fixture(true), true)
         && ok;
    ok = run_case("tiff_kodak_kdk_makernote",
                  build_tiff_kodak_kdk_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_kodak_type2_makernote",
                  build_tiff_kodak_type2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_kodak_serial_only_makernote",
                  build_tiff_kodak_serial_only_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_kodak_type5_makernote",
                  build_tiff_kodak_type5_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_kodak_absolute_type8_makernote",
                  build_tiff_kodak_absolute_type8_makernote_fixture(
                      "KODAK P712 ZOOM DIGITAL CAMERA"),
                  true)
         && ok;
    ok = run_case("tiff_kodak_absolute_pixpro_type8_makernote",
                  build_tiff_kodak_absolute_type8_makernote_fixture(
                      "PIXPRO AZ251"),
                  true)
         && ok;
    ok = run_case("tiff_kodak_absolute_type11_makernote",
                  build_tiff_kodak_absolute_type11_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_sigma_dp2_quattro_tag0033_makernote",
                  build_tiff_sigma_main_u16_makernote_fixture(
                      "SIGMA dp2 Quattro", 0x0033U, 41U),
                  true)
         && ok;
    ok = run_case("tiff_sigma_fp_l_tag0033_makernote",
                  build_tiff_sigma_main_u16_makernote_fixture(
                      "SIGMA fp L", 0x0033U, 41U),
                  true)
         && ok;
    ok = run_case("tiff_sigma_fp_l_tag0026_makernote",
                  build_tiff_sigma_main_u16_makernote_fixture(
                      "SIGMA fp L", 0x0026U, 41U),
                  true)
         && ok;
    ok = run_case("tiff_sigma_sd_quattro_h_tag0034_makernote",
                  build_tiff_sigma_main_u16_makernote_fixture(
                      "sd Quattro H", 0x0034U, 41U),
                  true)
         && ok;
    ok = run_case("tiff_sigma_sd_quattro_h_tag004b_makernote",
                  build_tiff_sigma_main_u16_makernote_fixture(
                      "sd Quattro H", 0x004BU, 41U),
                  true)
         && ok;
    ok = run_case("tiff_sigma_wb_makernote",
                  build_tiff_sigma_wb_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_olympus_signature_makernote",
                  build_tiff_olympus_signature_fixture(), true)
         && ok;
    ok = run_case("tiff_olympus_omsystem_nested_makernote",
                  build_tiff_olympus_omsystem_nested_fixture(), true)
         && ok;
    ok = run_case("tiff_olympus_oldstyle_nested_makernote",
                  build_tiff_olympus_oldstyle_nested_fixture(), true)
         && ok;
    ok = run_case("tiff_olympus_main_subifd_matrix_makernote",
                  build_tiff_olympus_main_subifd_matrix_fixture(), true)
         && ok;
    ok = run_case("tiff_olympus_focusinfo_context_makernote",
                  build_tiff_olympus_focusinfo_context_fixture(true), true)
         && ok;
    ok = run_case("tiff_fuji_makernote",
                  build_tiff_fuji_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_apple_front_facing_makernote",
                  build_tiff_apple_front_facing_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_flir_makernote",
                  build_tiff_flir_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nintendo_makernote",
                  build_tiff_nintendo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_hp_type6_makernote",
                  build_tiff_hp_type6_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_makernote",
                  build_tiff_sony_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9050b_makernote",
                  build_tiff_sony_tag9050b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag2010i_makernote",
                  build_tiff_sony_tag2010i_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_shotinfo_makernote",
                  build_tiff_sony_shotinfo_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag202a_makernote",
                  build_tiff_sony_tag202a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9405b_makernote",
                  build_tiff_sony_tag9405b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9416_makernote",
                  build_tiff_sony_tag9416_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9050a_makernote",
                  build_tiff_sony_tag9050a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9050c_makernote",
                  build_tiff_sony_tag9050c_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag2010b_makernote",
                  build_tiff_sony_tag2010b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag2010e_makernote",
                  build_tiff_sony_tag2010e_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9400a_makernote",
                  build_tiff_sony_tag9400a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9404b_makernote",
                  build_tiff_sony_tag9404b_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag9405a_makernote",
                  build_tiff_sony_tag9405a_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag940e_makernote",
                  build_tiff_sony_tag940e_fixture(), true)
         && ok;
    ok = run_case("tiff_sony_tag940e_afinfo_makernote",
                  build_tiff_sony_tag940e_afinfo_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_makernote",
                  build_tiff_canon_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_custom_functions2_makernote",
                  build_tiff_canon_custom_functions2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_afinfo2_makernote",
                  build_tiff_canon_afinfo2_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_filterinfo_makernote",
                  build_tiff_canon_filterinfo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_timeinfo_makernote",
                  build_tiff_canon_timeinfo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_camera_info_psinfo_makernote",
                  build_tiff_canon_camera_info_psinfo_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_canon_colordata8_makernote",
                  build_tiff_canon_colordata8_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_makernote",
                  build_tiff_nikon_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_binary_makernote",
                  build_tiff_nikon_binary_makernote_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_info_blocks_makernote",
                  build_tiff_nikon_info_blocks_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_preview_settings_aftune_makernote",
                  build_tiff_nikon_preview_settings_aftune_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_lensdata0100_makernote",
                  build_tiff_nikon_lensdata0100_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_lensdata0101_makernote",
                  build_tiff_nikon_lensdata0101_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_colorbalancec_makernote",
                  build_tiff_nikon_colorbalancec_fixture(), true)
         && ok;
    ok = run_case("tiff_nikon_main_z_context",
                  build_tiff_nikon_main_single_long_fixture("NIKON Z 5",
                                                            0x002EU, 3008U),
                  true)
         && ok;
    ok = run_case("tiff_nikon_main_compact_type2_context",
                  build_tiff_nikon_main_single_long_fixture("E700",
                                                            0x000AU, 0U),
                  true)
         && ok;
    return ok ? 0 : 1;
}
