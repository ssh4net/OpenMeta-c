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
