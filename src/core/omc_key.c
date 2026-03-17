#include "omc/omc_key.h"

#include <string.h>

void
omc_key_init(omc_key* key)
{
    if (key == NULL) {
        return;
    }

    memset(key, 0, sizeof(*key));
    key->kind = OMC_KEY_EXIF_TAG;
}

void
omc_key_make_exif_tag(omc_key* key, omc_byte_ref ifd, omc_u16 tag)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_EXIF_TAG;
    key->u.exif_tag.ifd = ifd;
    key->u.exif_tag.tag = tag;
}

void
omc_key_make_comment(omc_key* key)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_COMMENT;
    key->u.comment.reserved = 0U;
}

void
omc_key_make_xmp_property(omc_key* key, omc_byte_ref schema_ns,
                          omc_byte_ref property_path)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_XMP_PROPERTY;
    key->u.xmp_property.schema_ns = schema_ns;
    key->u.xmp_property.property_path = property_path;
}

void
omc_key_make_iptc_dataset(omc_key* key, omc_u16 record, omc_u16 dataset)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_IPTC_DATASET;
    key->u.iptc_dataset.record = record;
    key->u.iptc_dataset.dataset = dataset;
}

void
omc_key_make_icc_header_field(omc_key* key, omc_u32 offset)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_ICC_HEADER_FIELD;
    key->u.icc_header_field.offset = offset;
}

void
omc_key_make_icc_tag(omc_key* key, omc_u32 signature)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_ICC_TAG;
    key->u.icc_tag.signature = signature;
}

void
omc_key_make_photoshop_irb(omc_key* key, omc_u16 resource_id)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_PHOTOSHOP_IRB;
    key->u.photoshop_irb.resource_id = resource_id;
}

void
omc_key_make_geotiff_key(omc_key* key, omc_u16 key_id)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_GEOTIFF_KEY;
    key->u.geotiff_key.key_id = key_id;
}

void
omc_key_make_printim_field(omc_key* key, omc_byte_ref field)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_PRINTIM_FIELD;
    key->u.printim_field.field = field;
}

void
omc_key_make_bmff_field(omc_key* key, omc_byte_ref field)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_BMFF_FIELD;
    key->u.bmff_field.field = field;
}

void
omc_key_make_jumbf_field(omc_key* key, omc_byte_ref field)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_JUMBF_FIELD;
    key->u.jumbf_field.field = field;
}

void
omc_key_make_jumbf_cbor_key(omc_key* key, omc_byte_ref ref)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_JUMBF_CBOR_KEY;
    key->u.jumbf_cbor_key.key = ref;
}

void
omc_key_make_png_text(omc_key* key, omc_byte_ref keyword, omc_byte_ref field)
{
    omc_key_init(key);
    if (key == NULL) {
        return;
    }

    key->kind = OMC_KEY_PNG_TEXT;
    key->u.png_text.keyword = keyword;
    key->u.png_text.field = field;
}
