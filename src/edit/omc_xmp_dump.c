#include "omc/omc_xmp_dump.h"

#include <string.h>

typedef struct omc_xmp_dump_writer {
    omc_u8* out;
    omc_size cap;
    omc_u64 needed;
    omc_u64 written;
} omc_xmp_dump_writer;

typedef struct omc_xmp_ns_view {
    const omc_u8* data;
    omc_size size;
} omc_xmp_ns_view;

typedef enum omc_xmp_dump_pass {
    OMC_XMP_DUMP_PASS_EXISTING = 0,
    OMC_XMP_DUMP_PASS_EXIF = 1,
    OMC_XMP_DUMP_PASS_IPTC = 2,
    OMC_XMP_DUMP_PASS_COUNT = 3
} omc_xmp_dump_pass;

typedef enum omc_xmp_dump_container_kind {
    OMC_XMP_DUMP_CONTAINER_NONE = 0,
    OMC_XMP_DUMP_CONTAINER_SEQ = 1,
    OMC_XMP_DUMP_CONTAINER_BAG = 2
} omc_xmp_dump_container_kind;

typedef enum omc_xmp_dump_value_mode {
    OMC_XMP_DUMP_VALUE_NORMAL = 0,
    OMC_XMP_DUMP_VALUE_BYTES_TEXT = 1,
    OMC_XMP_DUMP_VALUE_EXIF_DATE = 2,
    OMC_XMP_DUMP_VALUE_XP_UTF16LE = 3
} omc_xmp_dump_value_mode;

typedef struct omc_xmp_dump_property {
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    const omc_val* value;
    const omc_arena* arena;
    omc_xmp_dump_value_mode value_mode;
    omc_xmp_dump_container_kind container_kind;
    omc_u32 item_index;
} omc_xmp_dump_property;

static const char k_xmp_ns_xap[] = "http://ns.adobe.com/xap/1.0/";
static const char k_xmp_ns_dc[] = "http://purl.org/dc/elements/1.1/";
static const char k_xmp_ns_ps[] = "http://ns.adobe.com/photoshop/1.0/";
static const char k_xmp_ns_exif[] = "http://ns.adobe.com/exif/1.0/";
static const char k_xmp_ns_tiff[] = "http://ns.adobe.com/tiff/1.0/";
static const char k_xmp_ns_mm[] = "http://ns.adobe.com/xap/1.0/mm/";
static const char k_xmp_ns_iptc4xmp[]
    = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";

static const char k_prop_make[] = "Make";
static const char k_prop_model[] = "Model";
static const char k_prop_datetime[] = "DateTime";
static const char k_prop_image_length[] = "ImageLength";
static const char k_prop_image_height[] = "ImageHeight";
static const char k_prop_exposure_time[] = "ExposureTime";
static const char k_prop_exposure_bias_value[] = "ExposureBiasValue";
static const char k_prop_exposure_compensation[] = "ExposureCompensation";
static const char k_prop_iso_speed_ratings[] = "ISOSpeedRatings";
static const char k_prop_iso[] = "ISO";
static const char k_prop_pixel_x_dimension[] = "PixelXDimension";
static const char k_prop_exif_image_width[] = "ExifImageWidth";
static const char k_prop_pixel_y_dimension[] = "PixelYDimension";
static const char k_prop_exif_image_height[] = "ExifImageHeight";
static const char k_prop_focal_length_35mm_film[] = "FocalLengthIn35mmFilm";
static const char k_prop_focal_length_35mm_format[]
    = "FocalLengthIn35mmFormat";
static const char k_prop_datetime_original[] = "DateTimeOriginal";
static const char k_prop_datetime_digitized[] = "DateTimeDigitized";
static const char k_prop_preview_datetime[] = "PreviewDateTime";
static const char k_prop_xp_title[] = "XPTitle";
static const char k_prop_xp_comment[] = "XPComment";
static const char k_prop_xp_author[] = "XPAuthor";
static const char k_prop_xp_keywords[] = "XPKeywords";
static const char k_prop_xp_subject[] = "XPSubject";
static const char k_prop_title[] = "title";
static const char k_prop_description[] = "description";
static const char k_prop_creator[] = "creator";
static const char k_prop_subject[] = "subject";
static const char k_prop_category[] = "Category";
static const char k_prop_supplemental_categories[] = "SupplementalCategories";
static const char k_prop_city[] = "City";
static const char k_prop_location[] = "Location";
static const char k_prop_state[] = "State";
static const char k_prop_country[] = "Country";
static const char k_prop_country_code[] = "CountryCode";
static const char k_prop_headline[] = "Headline";
static const char k_prop_credit[] = "Credit";
static const char k_prop_source[] = "Source";
static const char k_prop_caption_writer[] = "CaptionWriter";
static const char k_prop_maker_note[] = "MakerNote";
static const char k_prop_dng_private_data[] = "DNGPrivateData";

static void
omc_xmp_dump_writer_init(omc_xmp_dump_writer* writer, omc_u8* out,
                         omc_size out_cap)
{
    writer->out = out;
    writer->cap = out_cap;
    writer->needed = 0U;
    writer->written = 0U;
}

static void
omc_xmp_dump_write_bytes(omc_xmp_dump_writer* writer, const char* bytes,
                         omc_size size)
{
    omc_size copy;

    if (writer == (omc_xmp_dump_writer*)0 || bytes == (const char*)0) {
        return;
    }

    if (writer->written < writer->cap) {
        copy = writer->cap - (omc_size)writer->written;
        if (copy > size) {
            copy = size;
        }
        if (copy != 0U) {
            memcpy(writer->out + writer->written, bytes, copy);
            writer->written += (omc_u64)copy;
        }
    }

    writer->needed += (omc_u64)size;
}

static void
omc_xmp_dump_write_byte(omc_xmp_dump_writer* writer, char byte)
{
    omc_xmp_dump_write_bytes(writer, &byte, 1U);
}

static int
omc_xmp_dump_is_name_start(omc_u8 c)
{
    return ((c >= (omc_u8)'A' && c <= (omc_u8)'Z')
            || (c >= (omc_u8)'a' && c <= (omc_u8)'z') || c == (omc_u8)'_');
}

static int
omc_xmp_dump_is_name_char(omc_u8 c)
{
    return omc_xmp_dump_is_name_start(c)
           || (c >= (omc_u8)'0' && c <= (omc_u8)'9') || c == (omc_u8)'-'
           || c == (omc_u8)'.';
}

static int
omc_xmp_dump_is_simple_name(omc_xmp_ns_view name)
{
    omc_size i;

    if (name.data == (const omc_u8*)0 || name.size == 0U) {
        return 0;
    }
    if (!omc_xmp_dump_is_name_start(name.data[0])) {
        return 0;
    }

    for (i = 1U; i < name.size; ++i) {
        if (!omc_xmp_dump_is_name_char(name.data[i])) {
            return 0;
        }
    }

    return 1;
}

static int
omc_xmp_dump_views_equal(omc_xmp_ns_view a, omc_xmp_ns_view b)
{
    if (a.size != b.size) {
        return 0;
    }
    if (a.size == 0U) {
        return 1;
    }
    return memcmp(a.data, b.data, a.size) == 0;
}

static int
omc_xmp_dump_view_equal_lit(omc_xmp_ns_view view, const char* lit)
{
    omc_size lit_size;

    lit_size = strlen(lit);
    if (view.size != lit_size) {
        return 0;
    }
    return memcmp(view.data, lit, lit_size) == 0;
}

static omc_xmp_ns_view
omc_xmp_dump_view_from_ref(const omc_arena* arena, omc_byte_ref ref)
{
    omc_const_bytes bytes;
    omc_xmp_ns_view view;

    view.data = (const omc_u8*)0;
    view.size = 0U;

    bytes = omc_arena_view(arena, ref);
    if (bytes.data == (const omc_u8*)0) {
        return view;
    }

    view.data = bytes.data;
    view.size = bytes.size;
    return view;
}

static omc_xmp_ns_view
omc_xmp_dump_view_from_lit(const char* lit)
{
    omc_xmp_ns_view view;

    view.data = (const omc_u8*)lit;
    view.size = strlen(lit);
    return view;
}

static omc_xmp_dump_container_kind
omc_xmp_dump_existing_container_kind(omc_xmp_ns_view schema_ns,
                                     omc_xmp_ns_view property_name)
{
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)
        && omc_xmp_dump_view_equal_lit(property_name, k_prop_creator)) {
        return OMC_XMP_DUMP_CONTAINER_SEQ;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)
        && omc_xmp_dump_view_equal_lit(property_name, k_prop_subject)) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_ps)
        && omc_xmp_dump_view_equal_lit(property_name,
                                       k_prop_supplemental_categories)) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    return OMC_XMP_DUMP_CONTAINER_SEQ;
}

static int
omc_xmp_dump_parse_indexed_property_name(omc_xmp_ns_view property_path,
                                         omc_xmp_ns_view* out_base_name,
                                         omc_u32* out_index)
{
    omc_size i;
    omc_size base_len;
    omc_u32 index;
    int saw_digit;

    if (out_base_name == (omc_xmp_ns_view*)0 || out_index == (omc_u32*)0) {
        return 0;
    }
    if (property_path.data == (const omc_u8*)0 || property_path.size == 0U) {
        return 0;
    }

    *out_base_name = property_path;
    *out_index = 0U;
    if (omc_xmp_dump_is_simple_name(property_path)) {
        return 1;
    }
    if (property_path.size < 4U || property_path.data[property_path.size - 1U] != (omc_u8)']') {
        return 0;
    }

    i = property_path.size - 2U;
    saw_digit = 0;
    index = 0U;
    while (i < property_path.size && property_path.data[i] != (omc_u8)'[') {
        omc_u8 c;
        saw_digit = 1;
        c = property_path.data[i];
        if (c < (omc_u8)'0' || c > (omc_u8)'9') {
            return 0;
        }
        if (index > (((omc_u32)~0U) / 10U)) {
            return 0;
        }
        index = (index * 10U) + (omc_u32)(c - (omc_u8)'0');
        if (i == 0U) {
            break;
        }
        i -= 1U;
    }
    if (!saw_digit || i >= property_path.size || property_path.data[i] != (omc_u8)'[') {
        return 0;
    }

    base_len = i;
    if (base_len == 0U) {
        return 0;
    }

    out_base_name->data = property_path.data;
    out_base_name->size = base_len;
    if (!omc_xmp_dump_is_simple_name(*out_base_name)) {
        return 0;
    }

    {
        omc_u32 normalized;
        omc_size p;

        normalized = 0U;
        for (p = i + 1U; p + 1U < property_path.size; ++p) {
            normalized = normalized * 10U
                         + (omc_u32)(property_path.data[p] - (omc_u8)'0');
        }
        if (normalized == 0U) {
            return 0;
        }
        *out_index = normalized;
    }
    return 1;
}

static int
omc_xmp_dump_scalar_or_text_supported(const omc_val* value,
                                      const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }

    switch (value->kind) {
    case OMC_VAL_SCALAR:
        switch (value->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_I8:
        case OMC_ELEM_U16:
        case OMC_ELEM_I16:
        case OMC_ELEM_U32:
        case OMC_ELEM_I32:
        case OMC_ELEM_U64:
        case OMC_ELEM_I64:
        case OMC_ELEM_URATIONAL:
        case OMC_ELEM_SRATIONAL:
            return 1;
        default: return 0;
        }
    case OMC_VAL_TEXT:
        if (value->text_encoding != OMC_TEXT_ASCII
            && value->text_encoding != OMC_TEXT_UTF8) {
            return 0;
        }
        bytes = omc_arena_view(arena, value->u.ref);
        return bytes.data != (const omc_u8*)0;
    default: return 0;
    }
}

static int
omc_xmp_dump_textish_supported(const omc_val* value, const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }

    if (value->kind == OMC_VAL_TEXT) {
        if (value->text_encoding != OMC_TEXT_ASCII
            && value->text_encoding != OMC_TEXT_UTF8) {
            return 0;
        }
        bytes = omc_arena_view(arena, value->u.ref);
        return bytes.data != (const omc_u8*)0;
    }
    if (value->kind == OMC_VAL_BYTES) {
        bytes = omc_arena_view(arena, value->u.ref);
        return bytes.data != (const omc_u8*)0;
    }

    return 0;
}

static int
omc_xmp_dump_bytes_view_supported(const omc_val* value,
                                  const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }

    if (value->kind != OMC_VAL_BYTES && value->kind != OMC_VAL_TEXT
        && value->kind != OMC_VAL_ARRAY) {
        return 0;
    }
    if (value->kind == OMC_VAL_ARRAY && value->elem_type != OMC_ELEM_U8) {
        return 0;
    }
    if (value->kind == OMC_VAL_TEXT
        && value->text_encoding != OMC_TEXT_ASCII
        && value->text_encoding != OMC_TEXT_UTF8
        && value->text_encoding != OMC_TEXT_UTF16LE) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    return bytes.data != (const omc_u8*)0;
}

static int
omc_xmp_dump_extract_existing_property(const omc_store* store, omc_size index,
                                       const omc_xmp_sidecar_opts* opts,
                                       omc_xmp_dump_property* out_prop)
{
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    omc_xmp_ns_view base_name;
    omc_u32 item_index;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0
        || out_prop == (omc_xmp_dump_property*)0) {
        return 0;
    }
    if (!opts->include_existing_xmp || index >= store->entry_count) {
        return 0;
    }
    if (store->entries[index].key.kind != OMC_KEY_XMP_PROPERTY) {
        return 0;
    }

    schema_ns = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.xmp_property.schema_ns);
    property_name = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.xmp_property.property_path);
    if (schema_ns.data == (const omc_u8*)0
        || property_name.data == (const omc_u8*)0) {
        return 0;
    }
    if (!omc_xmp_dump_parse_indexed_property_name(property_name, &base_name,
                                                  &item_index)) {
        return 0;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_tiff)) {
        if (omc_xmp_dump_view_equal_lit(base_name, k_prop_image_length)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_image_height);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_dng_private_data)) {
            return 0;
        }
    } else if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_exif)) {
        if (omc_xmp_dump_view_equal_lit(base_name, k_prop_exposure_bias_value)) {
            base_name = omc_xmp_dump_view_from_lit(
                k_prop_exposure_compensation);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_iso_speed_ratings)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_iso);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_pixel_x_dimension)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_exif_image_width);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_pixel_y_dimension)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_exif_image_height);
        } else if (omc_xmp_dump_view_equal_lit(
                       base_name, k_prop_focal_length_35mm_film)) {
            base_name = omc_xmp_dump_view_from_lit(
                k_prop_focal_length_35mm_format);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_maker_note)) {
            return 0;
        }
    }
    if (!omc_xmp_dump_scalar_or_text_supported(&store->entries[index].value,
                                               &store->arena)) {
        return 0;
    }

    out_prop->schema_ns = schema_ns;
    out_prop->property_name = base_name;
    out_prop->value = &store->entries[index].value;
    out_prop->arena = &store->arena;
    out_prop->value_mode = OMC_XMP_DUMP_VALUE_NORMAL;
    out_prop->item_index = item_index;
    if (item_index != 0U) {
        out_prop->container_kind = omc_xmp_dump_existing_container_kind(
            schema_ns, base_name);
    } else {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_NONE;
    }
    return 1;
}

static int
omc_xmp_dump_extract_exif_property(const omc_store* store, omc_size index,
                                   const omc_xmp_sidecar_opts* opts,
                                   omc_xmp_dump_property* out_prop)
{
    omc_xmp_ns_view ifd_name;
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    omc_xmp_dump_value_mode value_mode;
    omc_u16 tag;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0
        || out_prop == (omc_xmp_dump_property*)0) {
        return 0;
    }
    if (!opts->include_exif || index >= store->entry_count) {
        return 0;
    }
    if (store->entries[index].key.kind != OMC_KEY_EXIF_TAG) {
        return 0;
    }

    ifd_name = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.exif_tag.ifd);
    if (ifd_name.data == (const omc_u8*)0) {
        return 0;
    }

    tag = store->entries[index].key.u.exif_tag.tag;
    value_mode = OMC_XMP_DUMP_VALUE_NORMAL;
    if (omc_xmp_dump_view_equal_lit(ifd_name, "ifd0")) {
        if (tag == 0x010FU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_make);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0110U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_model);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0132U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_datetime);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0xC71BU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_preview_datetime);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9BU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_title);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9CU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_comment);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9DU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_author);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9EU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_keywords);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9FU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_subject);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else {
            return 0;
        }
    } else if (omc_xmp_dump_view_equal_lit(ifd_name, "exififd")) {
        if (tag == 0x829AU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_exposure_time);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9003U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_datetime_original);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9004U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_datetime_digitized);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else {
            return 0;
        }
    } else {
        return 0;
    }

    out_prop->schema_ns = schema_ns;
    out_prop->property_name = property_name;
    out_prop->value = &store->entries[index].value;
    out_prop->arena = &store->arena;
    out_prop->value_mode = value_mode;
    out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_NONE;
    out_prop->item_index = 0U;
    return 1;
}

static int
omc_xmp_dump_extract_iptc_property(const omc_store* store, omc_size index,
                                   const omc_xmp_sidecar_opts* opts,
                                   omc_xmp_dump_property* out_prop)
{
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    omc_u16 record;
    omc_u16 dataset;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0
        || out_prop == (omc_xmp_dump_property*)0) {
        return 0;
    }
    if (!opts->include_iptc || index >= store->entry_count) {
        return 0;
    }
    if (store->entries[index].key.kind != OMC_KEY_IPTC_DATASET) {
        return 0;
    }
    if (!omc_xmp_dump_textish_supported(&store->entries[index].value,
                                        &store->arena)) {
        return 0;
    }

    record = store->entries[index].key.u.iptc_dataset.record;
    dataset = store->entries[index].key.u.iptc_dataset.dataset;
    if (record != 2U) {
        return 0;
    }

    switch (dataset) {
    case 5U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_title);
        break;
    case 15U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_category);
        break;
    case 20U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name
            = omc_xmp_dump_view_from_lit(k_prop_supplemental_categories);
        break;
    case 25U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_subject);
        break;
    case 80U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_creator);
        break;
    case 90U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_city);
        break;
    case 92U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_iptc4xmp);
        property_name = omc_xmp_dump_view_from_lit(k_prop_location);
        break;
    case 95U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_state);
        break;
    case 100U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_country);
        break;
    case 101U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_iptc4xmp);
        property_name = omc_xmp_dump_view_from_lit(k_prop_country_code);
        break;
    case 105U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_headline);
        break;
    case 110U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_credit);
        break;
    case 115U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_source);
        break;
    case 120U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_description);
        break;
    case 122U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_caption_writer);
        break;
    default: return 0;
    }

    out_prop->schema_ns = schema_ns;
    out_prop->property_name = property_name;
    out_prop->value = &store->entries[index].value;
    out_prop->arena = &store->arena;
    out_prop->value_mode = OMC_XMP_DUMP_VALUE_BYTES_TEXT;
    out_prop->item_index = 0U;
    if (dataset == 20U || dataset == 25U) {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_BAG;
    } else if (dataset == 80U) {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_SEQ;
    } else {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_NONE;
    }
    return 1;
}

static int
omc_xmp_dump_extract_pass_property(const omc_store* store, omc_size index,
                                   const omc_xmp_sidecar_opts* opts,
                                   omc_xmp_dump_pass pass,
                                   omc_xmp_dump_property* out_prop)
{
    switch (pass) {
    case OMC_XMP_DUMP_PASS_EXISTING:
        return omc_xmp_dump_extract_existing_property(store, index, opts,
                                                      out_prop);
    case OMC_XMP_DUMP_PASS_EXIF:
        return omc_xmp_dump_extract_exif_property(store, index, opts, out_prop);
    case OMC_XMP_DUMP_PASS_IPTC:
        return omc_xmp_dump_extract_iptc_property(store, index, opts, out_prop);
    default: return 0;
    }
}

static int
omc_xmp_dump_property_keys_equal(const omc_xmp_dump_property* a,
                                 const omc_xmp_dump_property* b)
{
    return omc_xmp_dump_views_equal(a->schema_ns, b->schema_ns)
           && omc_xmp_dump_views_equal(a->property_name, b->property_name);
}

static int
omc_xmp_dump_property_needs_group(const omc_xmp_dump_property* prop)
{
    return prop->container_kind != OMC_XMP_DUMP_CONTAINER_NONE;
}

static int
omc_xmp_dump_claimed_before(const omc_store* store, omc_size index,
                            const omc_xmp_sidecar_opts* opts,
                            omc_xmp_dump_pass pass,
                            const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_pass p;
    omc_size i;
    omc_size end;
    omc_xmp_dump_property prior;

    for (p = OMC_XMP_DUMP_PASS_EXISTING; p <= pass; ++p) {
        end = store->entry_count;
        if (p == pass) {
            end = index;
        }
        for (i = 0U; i < end; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, p, &prior)) {
                continue;
            }
            if (omc_xmp_dump_property_keys_equal(&prior, prop)) {
                return 1;
            }
        }
    }

    return 0;
}

static int
omc_xmp_dump_schema_first_for_emitted(const omc_store* store, omc_size index,
                                      const omc_xmp_sidecar_opts* opts,
                                      omc_xmp_dump_pass pass,
                                      omc_xmp_ns_view schema_ns)
{
    omc_xmp_dump_pass p;
    omc_size i;
    omc_size end;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    for (p = OMC_XMP_DUMP_PASS_EXISTING; p <= pass; ++p) {
        end = store->entry_count;
        if (p == pass) {
            end = index;
        }
        for (i = 0U; i < end; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, p, &prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, p, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                return 1;
            }
            if (omc_xmp_dump_views_equal(prop.schema_ns, schema_ns)) {
                return 0;
            }
            emitted += 1U;
        }
    }

    return 1;
}

static int
omc_xmp_dump_known_prefix(omc_xmp_ns_view schema_ns, const char** out_prefix)
{
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_xap)) {
        *out_prefix = "xmp";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)) {
        *out_prefix = "dc";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_ps)) {
        *out_prefix = "photoshop";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_exif)) {
        *out_prefix = "exif";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_tiff)) {
        *out_prefix = "tiff";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_mm)) {
        *out_prefix = "xmpMM";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp)) {
        *out_prefix = "Iptc4xmpCore";
        return 1;
    }

    return 0;
}

static omc_u32
omc_xmp_dump_unknown_prefix_ordinal(const omc_store* store, omc_size index,
                                    const omc_xmp_sidecar_opts* opts,
                                    omc_xmp_dump_pass pass,
                                    omc_xmp_ns_view schema_ns)
{
    omc_xmp_dump_pass p;
    omc_size i;
    omc_size end;
    omc_u32 emitted;
    omc_u32 ordinal;
    omc_xmp_dump_property prop;
    const char* known_prefix;

    emitted = 0U;
    ordinal = 0U;
    for (p = OMC_XMP_DUMP_PASS_EXISTING; p <= pass; ++p) {
        end = store->entry_count;
        if (p == pass) {
            end = index;
        }
        for (i = 0U; i < end; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, p, &prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, p, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                return ordinal == 0U ? 1U : ordinal;
            }
            emitted += 1U;
            if (omc_xmp_dump_known_prefix(prop.schema_ns, &known_prefix)) {
                continue;
            }
            if (omc_xmp_dump_schema_first_for_emitted(store, i, opts, p,
                                                      prop.schema_ns)) {
                ordinal += 1U;
            }
        }
    }

    if (ordinal == 0U) {
        ordinal = 1U;
    }
    if (!omc_xmp_dump_known_prefix(schema_ns, &known_prefix)) {
        return ordinal;
    }
    return 0U;
}

static void
omc_xmp_dump_write_u32_decimal(omc_xmp_dump_writer* writer, omc_u32 value)
{
    char buf[16];
    omc_u32 pos;

    pos = 0U;
    do {
        buf[pos++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (pos != 0U) {
        pos -= 1U;
        omc_xmp_dump_write_byte(writer, buf[pos]);
    }
}

static void
omc_xmp_dump_write_u64_decimal(omc_xmp_dump_writer* writer, omc_u64 value)
{
    char buf[32];
    omc_u32 pos;

    pos = 0U;
    do {
        buf[pos++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (pos != 0U) {
        pos -= 1U;
        omc_xmp_dump_write_byte(writer, buf[pos]);
    }
}

static void
omc_xmp_dump_write_i64_decimal(omc_xmp_dump_writer* writer, omc_s64 value)
{
    omc_u64 magnitude;

    if (value < 0) {
        omc_xmp_dump_write_byte(writer, '-');
        magnitude = (omc_u64)(-(value + 1)) + 1U;
    } else {
        magnitude = (omc_u64)value;
    }

    omc_xmp_dump_write_u64_decimal(writer, magnitude);
}

static void
omc_xmp_dump_write_hex_byte(omc_xmp_dump_writer* writer, omc_u8 value)
{
    static const char k_hex[] = "0123456789ABCDEF";

    omc_xmp_dump_write_byte(writer, k_hex[(value >> 4) & 0x0FU]);
    omc_xmp_dump_write_byte(writer, k_hex[value & 0x0FU]);
}

static void
omc_xmp_dump_write_xml_escaped(omc_xmp_dump_writer* writer,
                               const omc_u8* bytes, omc_size size,
                               int for_attribute)
{
    omc_size i;
    omc_u8 c;

    for (i = 0U; i < size; ++i) {
        c = bytes[i];
        switch (c) {
        case (omc_u8)'&':
            omc_xmp_dump_write_bytes(writer, "&amp;", 5U);
            break;
        case (omc_u8)'<':
            omc_xmp_dump_write_bytes(writer, "&lt;", 4U);
            break;
        case (omc_u8)'>':
            omc_xmp_dump_write_bytes(writer, "&gt;", 4U);
            break;
        case (omc_u8)'"':
            if (for_attribute != 0) {
                omc_xmp_dump_write_bytes(writer, "&quot;", 6U);
            } else {
                omc_xmp_dump_write_byte(writer, '"');
            }
            break;
        case (omc_u8)'\'':
            if (for_attribute != 0) {
                omc_xmp_dump_write_bytes(writer, "&apos;", 6U);
            } else {
                omc_xmp_dump_write_byte(writer, '\'');
            }
            break;
        default:
            if (c < 0x20U && c != (omc_u8)'\t' && c != (omc_u8)'\r'
                && c != (omc_u8)'\n') {
                omc_xmp_dump_write_bytes(writer, "\\x", 2U);
                omc_xmp_dump_write_hex_byte(writer, c);
            } else {
                omc_xmp_dump_write_byte(writer, (char)c);
            }
            break;
        }
    }
}

static void
omc_xmp_dump_write_prefix(omc_xmp_dump_writer* writer, const omc_store* store,
                          omc_size index, const omc_xmp_sidecar_opts* opts,
                          omc_xmp_dump_pass pass, omc_xmp_ns_view schema_ns)
{
    const char* known_prefix;
    omc_u32 ordinal;

    if (omc_xmp_dump_known_prefix(schema_ns, &known_prefix)) {
        omc_xmp_dump_write_bytes(writer, known_prefix, strlen(known_prefix));
        return;
    }

    omc_xmp_dump_write_bytes(writer, "ns", 2U);
    ordinal = omc_xmp_dump_unknown_prefix_ordinal(store, index, opts, pass,
                                                  schema_ns);
    omc_xmp_dump_write_u32_decimal(writer, ordinal);
}

static void
omc_xmp_dump_write_utf8_codepoint(omc_xmp_dump_writer* writer,
                                  omc_u32 codepoint)
{
    char bytes[4];

    if (codepoint <= 0x7FU) {
        omc_xmp_dump_write_byte(writer, (char)codepoint);
        return;
    }
    if (codepoint <= 0x7FFU) {
        bytes[0] = (char)(0xC0U | ((codepoint >> 6) & 0x1FU));
        bytes[1] = (char)(0x80U | (codepoint & 0x3FU));
        omc_xmp_dump_write_bytes(writer, bytes, 2U);
        return;
    }
    if (codepoint <= 0xFFFFU) {
        bytes[0] = (char)(0xE0U | ((codepoint >> 12) & 0x0FU));
        bytes[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[2] = (char)(0x80U | (codepoint & 0x3FU));
        omc_xmp_dump_write_bytes(writer, bytes, 3U);
        return;
    }

    bytes[0] = (char)(0xF0U | ((codepoint >> 18) & 0x07U));
    bytes[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
    bytes[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
    bytes[3] = (char)(0x80U | (codepoint & 0x3FU));
    omc_xmp_dump_write_bytes(writer, bytes, 4U);
}

static void
omc_xmp_dump_write_xml_codepoint(omc_xmp_dump_writer* writer,
                                 omc_u32 codepoint)
{
    if (codepoint == (omc_u32)'&') {
        omc_xmp_dump_write_bytes(writer, "&amp;", 5U);
        return;
    }
    if (codepoint == (omc_u32)'<') {
        omc_xmp_dump_write_bytes(writer, "&lt;", 4U);
        return;
    }
    if (codepoint == (omc_u32)'>') {
        omc_xmp_dump_write_bytes(writer, "&gt;", 4U);
        return;
    }
    if (codepoint == (omc_u32)'"') {
        omc_xmp_dump_write_byte(writer, '"');
        return;
    }
    if (codepoint == (omc_u32)'\'') {
        omc_xmp_dump_write_byte(writer, '\'');
        return;
    }
    if (codepoint < 0x20U && codepoint != (omc_u32)'\t'
        && codepoint != (omc_u32)'\r' && codepoint != (omc_u32)'\n') {
        omc_xmp_dump_write_bytes(writer, "\\x", 2U);
        omc_xmp_dump_write_hex_byte(writer, (omc_u8)codepoint);
        return;
    }

    omc_xmp_dump_write_utf8_codepoint(writer, codepoint);
}

static omc_size
omc_xmp_dump_trim_xp_utf16le_size(const omc_u8* data, omc_size size)
{
    while (size >= 2U && data[size - 2U] == 0U && data[size - 1U] == 0U) {
        size -= 2U;
    }
    return size;
}

static void
omc_xmp_dump_write_utf16le_xml_escaped(omc_xmp_dump_writer* writer,
                                       const omc_u8* data, omc_size size)
{
    omc_size i;
    omc_u16 lead;

    size = omc_xmp_dump_trim_xp_utf16le_size(data, size);
    i = 0U;
    while (i + 1U < size) {
        omc_u32 codepoint;

        lead = (omc_u16)(((omc_u16)data[i])
                         | (omc_u16)(((omc_u16)data[i + 1U]) << 8));
        i += 2U;

        if (lead >= 0xD800U && lead <= 0xDBFFU && i + 1U < size) {
            omc_u16 trail;

            trail = (omc_u16)(((omc_u16)data[i])
                              | (omc_u16)(((omc_u16)data[i + 1U]) << 8));
            if (trail >= 0xDC00U && trail <= 0xDFFFU) {
                codepoint = 0x10000U
                            + (((omc_u32)(lead - 0xD800U)) << 10)
                            + (omc_u32)(trail - 0xDC00U);
                i += 2U;
                omc_xmp_dump_write_xml_codepoint(writer, codepoint);
                continue;
            }
        }
        if (lead >= 0xDC00U && lead <= 0xDFFFU) {
            continue;
        }

        omc_xmp_dump_write_xml_codepoint(writer, (omc_u32)lead);
    }
}

static int
omc_xmp_dump_parse_two_digits(const omc_u8* data, omc_size pos,
                              omc_size size)
{
    if (pos + 1U >= size) {
        return -1;
    }
    if (data[pos] < (omc_u8)'0' || data[pos] > (omc_u8)'9'
        || data[pos + 1U] < (omc_u8)'0' || data[pos + 1U] > (omc_u8)'9') {
        return -1;
    }
    return (int)((data[pos] - (omc_u8)'0') * 10
                 + (data[pos + 1U] - (omc_u8)'0'));
}

static int
omc_xmp_dump_normalize_exif_datetime(const omc_u8* data, omc_size size,
                                     char* out, omc_size out_cap,
                                     omc_size* out_size)
{
    int year0;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    omc_size pos;
    int have_zone;
    char zone_buf[7];

    if (data == (const omc_u8*)0 || out == (char*)0
        || out_size == (omc_size*)0 || out_cap < 20U) {
        return 0;
    }
    if (size < 19U) {
        return 0;
    }

    if (data[0] < (omc_u8)'0' || data[0] > (omc_u8)'9'
        || data[1] < (omc_u8)'0' || data[1] > (omc_u8)'9'
        || data[2] < (omc_u8)'0' || data[2] > (omc_u8)'9'
        || data[3] < (omc_u8)'0' || data[3] > (omc_u8)'9') {
        return 0;
    }
    if ((data[4] != (omc_u8)':' && data[4] != (omc_u8)'-')
        || (data[7] != (omc_u8)':' && data[7] != (omc_u8)'-')
        || data[10] != (omc_u8)' ' || data[13] != (omc_u8)':'
        || data[16] != (omc_u8)':') {
        return 0;
    }

    year0 = (int)((data[0] - (omc_u8)'0') * 1000
                  + (data[1] - (omc_u8)'0') * 100
                  + (data[2] - (omc_u8)'0') * 10
                  + (data[3] - (omc_u8)'0'));
    month = omc_xmp_dump_parse_two_digits(data, 5U, size);
    day = omc_xmp_dump_parse_two_digits(data, 8U, size);
    hour = omc_xmp_dump_parse_two_digits(data, 11U, size);
    minute = omc_xmp_dump_parse_two_digits(data, 14U, size);
    second = omc_xmp_dump_parse_two_digits(data, 17U, size);
    if (month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0) {
        return 0;
    }

    out[0] = (char)data[0];
    out[1] = (char)data[1];
    out[2] = (char)data[2];
    out[3] = (char)data[3];
    out[4] = '-';
    out[5] = (char)data[5];
    out[6] = (char)data[6];
    out[7] = '-';
    out[8] = (char)data[8];
    out[9] = (char)data[9];
    out[10] = 'T';
    out[11] = (char)data[11];
    out[12] = (char)data[12];
    out[13] = ':';
    out[14] = (char)data[14];
    out[15] = (char)data[15];
    out[16] = ':';
    out[17] = (char)data[17];
    out[18] = (char)data[18];
    pos = 19U;

    have_zone = 0;
    if (size >= 23U && data[19] == (omc_u8)' ') {
        if (data[20] == (omc_u8)'U' && data[21] == (omc_u8)'T'
            && data[22] == (omc_u8)'C') {
            zone_buf[0] = 'Z';
            zone_buf[1] = '\0';
            have_zone = 1;
        }
    } else if (size >= 24U
               && (data[19] == (omc_u8)'+' || data[19] == (omc_u8)'-')) {
        int zh;
        int zm;

        zh = omc_xmp_dump_parse_two_digits(data, 20U, size);
        zm = omc_xmp_dump_parse_two_digits(data, 22U, size);
        if (zh >= 0 && zm >= 0) {
            zone_buf[0] = (char)data[19];
            zone_buf[1] = (char)data[20];
            zone_buf[2] = (char)data[21];
            zone_buf[3] = ':';
            zone_buf[4] = (char)data[22];
            zone_buf[5] = (char)data[23];
            zone_buf[6] = '\0';
            have_zone = 1;
        }
    }

    if (have_zone != 0) {
        omc_size zone_len;
        zone_len = strlen(zone_buf);
        if (pos + zone_len > out_cap) {
            return 0;
        }
        memcpy(out + pos, zone_buf, zone_len);
        pos += zone_len;
    }

    (void)year0;
    *out_size = pos;
    return 1;
}

static void
omc_xmp_dump_write_value(omc_xmp_dump_writer* writer,
                         const omc_xmp_dump_property* prop)
{
    omc_const_bytes bytes;
    char date_buf[32];
    omc_size date_size;

    if (prop->value_mode == OMC_XMP_DUMP_VALUE_BYTES_TEXT) {
        if (prop->value->kind == OMC_VAL_BYTES || prop->value->kind == OMC_VAL_TEXT
            || prop->value->kind == OMC_VAL_ARRAY) {
            bytes = omc_arena_view(prop->arena, prop->value->u.ref);
            if (bytes.data != (const omc_u8*)0) {
                omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
            }
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_DATE) {
        bytes = omc_arena_view(prop->arena, prop->value->u.ref);
        if (bytes.data != (const omc_u8*)0
            && omc_xmp_dump_normalize_exif_datetime(bytes.data, bytes.size,
                                                    date_buf, sizeof(date_buf),
                                                    &date_size)) {
            omc_xmp_dump_write_xml_escaped(writer, (const omc_u8*)date_buf,
                                           date_size, 0);
            return;
        }
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_XP_UTF16LE) {
        bytes = omc_arena_view(prop->arena, prop->value->u.ref);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_utf16le_xml_escaped(writer, bytes.data,
                                                   bytes.size);
        }
        return;
    }

    switch (prop->value->kind) {
    case OMC_VAL_SCALAR:
        switch (prop->value->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_U16:
        case OMC_ELEM_U32:
        case OMC_ELEM_U64:
            omc_xmp_dump_write_u64_decimal(writer, prop->value->u.u64);
            return;
        case OMC_ELEM_I8:
        case OMC_ELEM_I16:
        case OMC_ELEM_I32:
        case OMC_ELEM_I64:
            omc_xmp_dump_write_i64_decimal(writer, prop->value->u.i64);
            return;
        case OMC_ELEM_URATIONAL:
            omc_xmp_dump_write_u32_decimal(writer, prop->value->u.ur.numer);
            omc_xmp_dump_write_byte(writer, '/');
            omc_xmp_dump_write_u32_decimal(writer, prop->value->u.ur.denom);
            return;
        case OMC_ELEM_SRATIONAL:
            omc_xmp_dump_write_i64_decimal(writer, prop->value->u.sr.numer);
            omc_xmp_dump_write_byte(writer, '/');
            omc_xmp_dump_write_i64_decimal(writer, prop->value->u.sr.denom);
            return;
        default: return;
        }
    case OMC_VAL_TEXT:
        bytes = omc_arena_view(prop->arena, prop->value->u.ref);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        return;
    default: return;
    }
}

static void
omc_xmp_dump_write_property_open(omc_xmp_dump_writer* writer,
                                 const omc_store* store, omc_size index,
                                 const omc_xmp_sidecar_opts* opts,
                                 omc_xmp_dump_pass pass,
                                 const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_write_byte(writer, '<');
    omc_xmp_dump_write_prefix(writer, store, index, opts, pass,
                              prop->schema_ns);
    omc_xmp_dump_write_byte(writer, ':');
    omc_xmp_dump_write_xml_escaped(writer, prop->property_name.data,
                                   prop->property_name.size, 0);
    omc_xmp_dump_write_byte(writer, '>');
}

static void
omc_xmp_dump_write_property_close(omc_xmp_dump_writer* writer,
                                  const omc_store* store, omc_size index,
                                  const omc_xmp_sidecar_opts* opts,
                                  omc_xmp_dump_pass pass,
                                  const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_write_bytes(writer, "</", 2U);
    omc_xmp_dump_write_prefix(writer, store, index, opts, pass,
                              prop->schema_ns);
    omc_xmp_dump_write_byte(writer, ':');
    omc_xmp_dump_write_xml_escaped(writer, prop->property_name.data,
                                   prop->property_name.size, 0);
    omc_xmp_dump_write_byte(writer, '>');
}

static void
omc_xmp_dump_emit_group_items(omc_xmp_dump_writer* writer,
                              const omc_store* store, omc_size index,
                              const omc_xmp_sidecar_opts* opts,
                              omc_xmp_dump_pass pass,
                              const omc_xmp_dump_property* prop)
{
    omc_size i;
    omc_xmp_dump_property item;
    omc_u32 want_index;
    omc_u32 next_index;
    int found;

    if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_SEQ
        && prop->item_index != 0U) {
        want_index = 1U;
        for (;;) {
            found = 0;
            next_index = 0U;
            for (i = index; i < store->entry_count; ++i) {
                if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass,
                                                        &item)) {
                    continue;
                }
                if (!omc_xmp_dump_property_keys_equal(&item, prop)) {
                    continue;
                }
                if (item.item_index == want_index) {
                    omc_xmp_dump_write_bytes(writer, "<rdf:li>", 8U);
                    omc_xmp_dump_write_value(writer, &item);
                    omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
                    found = 1;
                    want_index += 1U;
                    break;
                }
                if (item.item_index > want_index
                    && (next_index == 0U || item.item_index < next_index)) {
                    next_index = item.item_index;
                }
            }
            if (found != 0) {
                continue;
            }
            if (next_index == 0U) {
                break;
            }
            want_index = next_index;
        }
        return;
    }

    for (i = index; i < store->entry_count; ++i) {
        if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass, &item)) {
            continue;
        }
        if (!omc_xmp_dump_property_keys_equal(&item, prop)) {
            continue;
        }
        omc_xmp_dump_write_bytes(writer, "<rdf:li>", 8U);
        omc_xmp_dump_write_value(writer, &item);
        omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
    }
}

static void
omc_xmp_dump_emit_one_property(omc_xmp_dump_writer* writer,
                               const omc_store* store, omc_size index,
                               const omc_xmp_sidecar_opts* opts,
                               omc_xmp_dump_pass pass,
                               const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_write_property_open(writer, store, index, opts, pass, prop);
    if (omc_xmp_dump_property_needs_group(prop)) {
        if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_SEQ) {
            omc_xmp_dump_write_bytes(writer, "<rdf:Seq>", 9U);
            omc_xmp_dump_emit_group_items(writer, store, index, opts, pass,
                                          prop);
            omc_xmp_dump_write_bytes(writer, "</rdf:Seq>", 10U);
        } else {
            omc_xmp_dump_write_bytes(writer, "<rdf:Bag>", 9U);
            omc_xmp_dump_emit_group_items(writer, store, index, opts, pass,
                                          prop);
            omc_xmp_dump_write_bytes(writer, "</rdf:Bag>", 10U);
        }
    } else {
        omc_xmp_dump_write_value(writer, prop);
    }
    omc_xmp_dump_write_property_close(writer, store, index, opts, pass, prop);
}

static void
omc_xmp_dump_emit_namespace_decls(omc_xmp_dump_writer* writer,
                                  const omc_store* store,
                                  const omc_xmp_sidecar_opts* opts,
                                  int* out_limit_hit)
{
    omc_xmp_dump_pass pass;
    omc_size i;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    for (pass = OMC_XMP_DUMP_PASS_EXISTING; pass < OMC_XMP_DUMP_PASS_COUNT;
         ++pass) {
        for (i = 0U; i < store->entry_count; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass,
                                                    &prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, pass, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                *out_limit_hit = 1;
                continue;
            }
            if (!omc_xmp_dump_schema_first_for_emitted(store, i, opts, pass,
                                                       prop.schema_ns)) {
                emitted += 1U;
                continue;
            }

            omc_xmp_dump_write_bytes(writer, " xmlns:", 7U);
            omc_xmp_dump_write_prefix(writer, store, i, opts, pass,
                                      prop.schema_ns);
            omc_xmp_dump_write_bytes(writer, "=\"", 2U);
            omc_xmp_dump_write_xml_escaped(writer, prop.schema_ns.data,
                                           prop.schema_ns.size, 1);
            omc_xmp_dump_write_byte(writer, '"');
            emitted += 1U;
        }
    }
}

static omc_u32
omc_xmp_dump_emit_properties(omc_xmp_dump_writer* writer, const omc_store* store,
                             const omc_xmp_sidecar_opts* opts,
                             int* out_limit_hit)
{
    omc_xmp_dump_pass pass;
    omc_size i;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    for (pass = OMC_XMP_DUMP_PASS_EXISTING; pass < OMC_XMP_DUMP_PASS_COUNT;
         ++pass) {
        for (i = 0U; i < store->entry_count; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass,
                                                    &prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, pass, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                *out_limit_hit = 1;
                continue;
            }

            omc_xmp_dump_emit_one_property(writer, store, i, opts, pass,
                                           &prop);
            emitted += 1U;
        }
    }

    return emitted;
}

void
omc_xmp_sidecar_opts_init(omc_xmp_sidecar_opts* opts)
{
    if (opts == (omc_xmp_sidecar_opts*)0) {
        return;
    }

    opts->limits.max_output_bytes = 0U;
    opts->limits.max_entries = 0U;
    opts->include_existing_xmp = 1;
    opts->include_exif = 0;
    opts->include_iptc = 0;
}

omc_status
omc_xmp_dump_sidecar(const omc_store* store, omc_u8* out, omc_size out_cap,
                     const omc_xmp_sidecar_opts* opts,
                     omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_opts local_opts;
    omc_xmp_dump_writer writer;
    int limit_hit;
    static const char k_packet_open[]
        = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
          "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
          "x:xmptk=\"OpenMeta-c\"><rdf:RDF><rdf:Description";
    static const char k_packet_close[]
        = "</rdf:Description></rdf:RDF></x:xmpmeta>";

    if (store == (const omc_store*)0 || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (out == (omc_u8*)0 && out_cap != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_sidecar_opts*)0) {
        omc_xmp_sidecar_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_dump_writer_init(&writer, out, out_cap);
    limit_hit = 0;

    omc_xmp_dump_write_bytes(&writer, k_packet_open,
                             sizeof(k_packet_open) - 1U);
    omc_xmp_dump_emit_namespace_decls(&writer, store, opts, &limit_hit);
    omc_xmp_dump_write_byte(&writer, '>');
    out_res->entries
        = omc_xmp_dump_emit_properties(&writer, store, opts, &limit_hit);
    omc_xmp_dump_write_bytes(&writer, k_packet_close,
                             sizeof(k_packet_close) - 1U);

    out_res->needed = writer.needed;
    out_res->written = writer.written;

    if (opts->limits.max_output_bytes != 0U
        && writer.needed > opts->limits.max_output_bytes) {
        limit_hit = 1;
    }

    if (limit_hit != 0) {
        out_res->status = OMC_XMP_DUMP_LIMIT;
    } else if (writer.needed > out_cap) {
        out_res->status = OMC_XMP_DUMP_TRUNCATED;
    } else {
        out_res->status = OMC_XMP_DUMP_OK;
    }

    return OMC_STATUS_OK;
}

omc_status
omc_xmp_dump_sidecar_arena(const omc_store* store, omc_arena* out,
                           const omc_xmp_sidecar_opts* opts,
                           omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_opts local_opts;
    omc_size target_cap;
    omc_status status;

    if (store == (const omc_store*)0 || out == (omc_arena*)0
        || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_sidecar_opts*)0) {
        omc_xmp_sidecar_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_arena_reset(out);
    target_cap = out->capacity;
    if (target_cap == 0U) {
        target_cap = 256U;
    }
    if (opts->limits.max_output_bytes != 0U
        && (omc_u64)target_cap > opts->limits.max_output_bytes) {
        if (opts->limits.max_output_bytes > (omc_u64)(~(omc_size)0)) {
            return OMC_STATUS_OVERFLOW;
        }
        target_cap = (omc_size)opts->limits.max_output_bytes;
        if (target_cap == 0U) {
            target_cap = 1U;
        }
    }

    for (;;) {
        status = omc_arena_reserve(out, target_cap);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        status = omc_xmp_dump_sidecar(store, out->data, out->capacity, opts,
                                      out_res);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        out->size = (omc_size)out_res->written;
        if (out_res->status != OMC_XMP_DUMP_TRUNCATED) {
            return OMC_STATUS_OK;
        }
        if (out_res->needed > (omc_u64)(~(omc_size)0)) {
            return OMC_STATUS_OVERFLOW;
        }

        target_cap = (omc_size)out_res->needed;
    }
}
