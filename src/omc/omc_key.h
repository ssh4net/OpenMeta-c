#ifndef OMC_KEY_H
#define OMC_KEY_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_key_kind {
    OMC_KEY_EXIF_TAG = 0,
    OMC_KEY_COMMENT = 1,
    OMC_KEY_EXR_ATTR = 2,
    OMC_KEY_IPTC_DATASET = 3,
    OMC_KEY_XMP_PROPERTY = 4,
    OMC_KEY_ICC_HEADER_FIELD = 5,
    OMC_KEY_ICC_TAG = 6,
    OMC_KEY_PHOTOSHOP_IRB = 7,
    OMC_KEY_PHOTOSHOP_IRB_FIELD = 8,
    OMC_KEY_GEOTIFF_KEY = 9,
    OMC_KEY_PRINTIM_FIELD = 10,
    OMC_KEY_BMFF_FIELD = 11,
    OMC_KEY_JUMBF_FIELD = 12,
    OMC_KEY_JUMBF_CBOR_KEY = 13,
    OMC_KEY_PNG_TEXT = 14
} omc_key_kind;

typedef struct omc_key {
    omc_key_kind kind;
    union {
        struct {
            omc_byte_ref ifd;
            omc_u16 tag;
        } exif_tag;
        struct {
            omc_u8 reserved;
        } comment;
        struct {
            omc_u32 part_index;
            omc_byte_ref name;
        } exr_attr;
        struct {
            omc_u16 record;
            omc_u16 dataset;
        } iptc_dataset;
        struct {
            omc_byte_ref schema_ns;
            omc_byte_ref property_path;
        } xmp_property;
        struct {
            omc_u32 offset;
        } icc_header_field;
        struct {
            omc_u32 signature;
        } icc_tag;
        struct {
            omc_u16 resource_id;
        } photoshop_irb;
        struct {
            omc_u16 resource_id;
            omc_byte_ref field;
        } photoshop_irb_field;
        struct {
            omc_u16 key_id;
        } geotiff_key;
        struct {
            omc_byte_ref field;
        } printim_field;
        struct {
            omc_byte_ref field;
        } bmff_field;
        struct {
            omc_byte_ref field;
        } jumbf_field;
        struct {
            omc_byte_ref key;
        } jumbf_cbor_key;
        struct {
            omc_byte_ref keyword;
            omc_byte_ref field;
        } png_text;
    } u;
} omc_key;

OMC_API void
omc_key_init(omc_key* key);

OMC_API void
omc_key_make_exif_tag(omc_key* key, omc_byte_ref ifd, omc_u16 tag);

OMC_API void
omc_key_make_comment(omc_key* key);

OMC_API void
omc_key_make_exr_attr(omc_key* key, omc_u32 part_index, omc_byte_ref name);

OMC_API void
omc_key_make_xmp_property(omc_key* key, omc_byte_ref schema_ns,
                          omc_byte_ref property_path);

OMC_API void
omc_key_make_iptc_dataset(omc_key* key, omc_u16 record, omc_u16 dataset);

OMC_API void
omc_key_make_icc_header_field(omc_key* key, omc_u32 offset);

OMC_API void
omc_key_make_icc_tag(omc_key* key, omc_u32 signature);

OMC_API void
omc_key_make_photoshop_irb(omc_key* key, omc_u16 resource_id);

OMC_API void
omc_key_make_photoshop_irb_field(omc_key* key, omc_u16 resource_id,
                                 omc_byte_ref field);

OMC_API void
omc_key_make_geotiff_key(omc_key* key, omc_u16 key_id);

OMC_API void
omc_key_make_printim_field(omc_key* key, omc_byte_ref field);

OMC_API void
omc_key_make_bmff_field(omc_key* key, omc_byte_ref field);

OMC_API void
omc_key_make_jumbf_field(omc_key* key, omc_byte_ref field);

OMC_API void
omc_key_make_jumbf_cbor_key(omc_key* key, omc_byte_ref ref);

OMC_API void
omc_key_make_png_text(omc_key* key, omc_byte_ref keyword, omc_byte_ref field);

OMC_EXTERN_C_END

#endif
