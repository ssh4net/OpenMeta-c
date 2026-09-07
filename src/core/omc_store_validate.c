#include "omc/omc_store_validate.h"
#include "omc_value_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct omc_tag_schema {
    int ifd;
    omc_u16 tag;
    omc_u16 types;
    omc_u32 min_count;
    omc_u32 max_count;
} omc_tag_schema;

static const omc_tag_schema omc_schemas[] = {
    {2, 0x0100U, 8U | 16U, 1U, 1U},
    {2, 0x0101U, 8U | 16U, 1U, 1U},
    {2, 0x0102U, 8U, 1U, 0U},
    {2, 0x0103U, 8U, 1U, 1U},
    {2, 0x0106U, 8U, 1U, 1U},
    {1, 0x010FU, 4U, 1U, 0U},
    {1, 0x0110U, 4U, 1U, 0U},
    {2, 0x0112U, 8U, 1U, 1U},
    {2, 0x0115U, 8U, 1U, 1U},
    {2, 0x011AU, 32U, 1U, 1U},
    {2, 0x011BU, 32U, 1U, 1U},
    {2, 0x011CU, 8U, 1U, 1U},
    {2, 0x0128U, 8U, 1U, 1U},
    {1, 0x0131U, 4U, 1U, 0U},
    {1, 0x0132U, 4U, 20U, 20U},
    {3, 0x829AU, 32U, 1U, 1U},
    {3, 0x829DU, 32U, 1U, 1U},
    {3, 0x8827U, 8U | 16U, 1U, 0U},
    {3, 0x9000U, 128U, 4U, 4U},
    {3, 0x9003U, 4U, 20U, 20U},
    {3, 0x9004U, 4U, 20U, 20U},
    {3, 0x9010U, 4U, 7U, 7U},
    {3, 0x9011U, 4U, 7U, 7U},
    {3, 0x9012U, 4U, 7U, 7U},
    {3, 0x9204U, 1024U, 1U, 1U},
    {3, 0x920AU, 32U, 1U, 1U},
    {3, 0xA001U, 8U, 1U, 1U},
    {3, 0xA002U, 8U | 16U, 1U, 1U},
    {3, 0xA003U, 8U | 16U, 1U, 1U},
    {4, 0x0000U, 2U, 4U, 4U},
    {4, 0x0001U, 4U, 2U, 2U},
    {4, 0x0002U, 32U, 3U, 3U},
    {4, 0x0003U, 4U, 2U, 2U},
    {4, 0x0004U, 32U, 3U, 3U},
    {4, 0x0005U, 2U, 1U, 1U},
    {4, 0x0006U, 32U, 1U, 1U},
    {4, 0x0007U, 32U, 3U, 3U},
    {4, 0x001DU, 4U, 11U, 11U},
    {5, 0x828DU, 8U, 2U, 2U},
    {5, 0x828EU, 2U, 1U, 0U},
    {5, 0xC612U, 2U, 4U, 4U},
    {5, 0xC613U, 2U, 4U, 4U},
    {5, 0xC616U, 2U, 1U, 0U},
    {5, 0xC617U, 8U, 1U, 1U},
    {5, 0xC618U, 8U, 1U, 0U},
    {5, 0xC619U, 8U, 2U, 2U},
    {5, 0xC61AU, 8U | 16U | 32U, 1U, 0U},
    {5, 0xC61DU, 8U | 16U, 1U, 0U},
    {5, 0xC621U, 1024U, 1U, 0U},
    {5, 0xC622U, 1024U, 1U, 0U},
    {5, 0xC623U, 1024U, 1U, 0U},
    {5, 0xC624U, 1024U, 1U, 0U},
    {5, 0xC627U, 32U, 1U, 0U},
    {5, 0xC628U, 32U, 1U, 0U},
    {5, 0xC65AU, 8U, 1U, 1U},
    {5, 0xC65BU, 8U, 1U, 1U},
    {5, 0xC714U, 1024U, 1U, 0U},
    {5, 0xC715U, 1024U, 1U, 0U},
};

typedef struct omc_validation {
    const omc_store *store;
    omc_metadata_validate_opts opts;
    omc_metadata_validate_res res;
    omc_metadata_issue *issues;
    omc_u32 capacity;
    int whole_store;
} omc_validation;

static void
omc_issue(omc_validation *v, omc_metadata_issue_code code, omc_entry_id id,
          omc_entry_id related, int warning)
{
    omc_metadata_issue *issue;
    if (v->res.issues_needed >= v->opts.max_issues) {
        v->res.status = OMC_STATUS_OVERFLOW;
        return;
    }
    if (warning && !v->opts.warnings_as_errors) {
        v->res.warning_count++;
    } else {
        v->res.error_count++;
        if (v->res.status == OMC_STATUS_OK)
            v->res.status = OMC_STATUS_STATE;
    }
    if (v->res.issues_written < v->capacity) {
        issue = &v->issues[v->res.issues_written++];
        issue->code = code;
        issue->entry = id;
        issue->related_entry = related;
        issue->warning = warning && !v->opts.warnings_as_errors;
    }
    v->res.issues_needed++;
}

void
omc_metadata_validate_opts_init(omc_metadata_validate_opts *opts)
{
    if (opts == NULL)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->validate_schema = 1;
    opts->validate_wire_hints = 1;
    opts->max_issues = 4096U;
    opts->max_key_bytes = 4096U;
    opts->max_entries = 200000U;
    opts->max_arena_bytes = 64U * 1024U * 1024U;
    opts->max_value_bytes = opts->max_arena_bytes;
}

static int
omc_key_text(const omc_validation *v, omc_byte_ref ref, int required)
{
    omc_size i;
    const omc_u8 *p;
    if (!omc_ref_valid(&v->store->arena, ref) || ref.size > v->opts.max_key_bytes ||
        (required && ref.size == 0U))
        return 0;
    p = ref.size ? v->store->arena.data + ref.offset : NULL;
    for (i = 0U; i < ref.size; ++i) {
        if (p[i] == 0U)
            return 0;
    }
    return 1;
}

static int
omc_validate_key_shape(const omc_validation *v, const omc_key *k)
{
    switch (k->kind) {
    case OMC_KEY_EXIF_TAG:
        return omc_key_text(v, k->u.exif_tag.ifd, 1);
    case OMC_KEY_EXR_ATTR:
        return omc_key_text(v, k->u.exr_attr.name, 1);
    case OMC_KEY_XMP_PROPERTY:
        return omc_key_text(v, k->u.xmp_property.schema_ns, 1) &&
               omc_key_text(v, k->u.xmp_property.property_path, 1);
    case OMC_KEY_PHOTOSHOP_IRB_FIELD:
        return omc_key_text(v, k->u.photoshop_irb_field.field, 0);
    case OMC_KEY_PRINTIM_FIELD:
        return omc_key_text(v, k->u.printim_field.field, 0);
    case OMC_KEY_BMFF_FIELD:
        return omc_key_text(v, k->u.bmff_field.field, 0);
    case OMC_KEY_JUMBF_FIELD:
        return omc_key_text(v, k->u.jumbf_field.field, 0);
    case OMC_KEY_JUMBF_CBOR_KEY:
        return omc_key_text(v, k->u.jumbf_cbor_key.key, 0);
    case OMC_KEY_PNG_TEXT:
        return omc_key_text(v, k->u.png_text.keyword, 1) &&
               omc_key_text(v, k->u.png_text.field, 0);
    case OMC_KEY_IPTC_DATASET:
        return k->u.iptc_dataset.record <= 255U && k->u.iptc_dataset.dataset <= 255U;
    case OMC_KEY_COMMENT:
    case OMC_KEY_ICC_HEADER_FIELD:
    case OMC_KEY_ICC_TAG:
    case OMC_KEY_PHOTOSHOP_IRB:
    case OMC_KEY_GEOTIFF_KEY:
        return 1;
    }
    return 0;
}

static int
omc_utf8_valid(const omc_u8 *p, omc_size n, int ascii)
{
    omc_size i;
    omc_u32 c;
    omc_u32 min;
    omc_u32 more;
    omc_u32 j;
    i = 0U;
    while (i < n) {
        c = p[i++];
        if (c == 0U)
            return 0;
        if (c < 128U)
            continue;
        if (ascii)
            return 0;
        if (c >= 0xC2U && c <= 0xDFU) {
            more = 1U;
            c &= 31U;
            min = 128U;
        } else if (c >= 0xE0U && c <= 0xEFU) {
            more = 2U;
            c &= 15U;
            min = 2048U;
        } else if (c >= 0xF0U && c <= 0xF4U) {
            more = 3U;
            c &= 7U;
            min = 65536U;
        } else
            return 0;
        if (more > n - i)
            return 0;
        for (j = 0U; j < more; ++j) {
            if ((p[i] & 0xC0U) != 0x80U)
                return 0;
            c = (c << 6U) | (p[i++] & 63U);
        }
        if (c < min || c > 0x10FFFFU || (c >= 0xD800U && c <= 0xDFFFU))
            return 0;
    }
    return 1;
}

static int
omc_xmp_name_start(omc_u8 c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int
omc_xmp_name_char(omc_u8 c)
{
    return omc_xmp_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static int
omc_xmp_path_valid(const omc_u8 *p, omc_size n)
{
    omc_size i;
    int colon;
    i = 0U;
    while (i < n) {
        if (!omc_xmp_name_start(p[i++]))
            return 0;
        colon = 0;
        while (i < n && p[i] != '[' && p[i] != '/') {
            if (p[i] == ':') {
                if (colon || ++i == n || !omc_xmp_name_start(p[i]))
                    return 0;
                colon = 1;
            } else if (!omc_xmp_name_char(p[i]))
                return 0;
            i++;
        }
        if (i < n && p[i] == '[') {
            i++;
            if (n - i >= 10U && memcmp(p + i, "@xml:lang=", 10U) == 0) {
                i += 10U;
                if (i == n || !omc_xmp_name_char(p[i]))
                    return 0;
                while (i < n && omc_xmp_name_char(p[i]))
                    i++;
            } else {
                if (i == n || p[i] < '1' || p[i] > '9')
                    return 0;
                while (i < n && p[i] >= '0' && p[i] <= '9')
                    i++;
            }
            if (i == n || p[i++] != ']')
                return 0;
        }
        if (i == n)
            return 1;
        if (p[i++] != '/' || i == n)
            return 0;
    }
    return 0;
}

static int
omc_ref_equals(const omc_arena *a, omc_byte_ref r, const char *s)
{
    omc_size n;
    n = strlen(s);
    return r.size == n && memcmp(a->data + r.offset, s, n) == 0;
}

static int
omc_ifd_class(const omc_arena *a, omc_byte_ref r)
{
    const omc_u8 *p;
    omc_size i;
    if (omc_ref_equals(a, r, "ifd0"))
        return 1;
    if (omc_ref_equals(a, r, "exififd"))
        return 3;
    if (omc_ref_equals(a, r, "gpsifd"))
        return 4;
    if (omc_ref_equals(a, r, "interopifd"))
        return 7;
    p = a->data + r.offset;
    if (r.size > 3U && memcmp(p, "ifd", 3U) == 0)
        i = 3U;
    else if (r.size > 6U && memcmp(p, "subifd", 6U) == 0)
        i = 6U;
    else
        return 0;
    for (; i < r.size; ++i)
        if (p[i] < '0' || p[i] > '9')
            return 0;
    return r.size > 6U && memcmp(p, "subifd", 6U) == 0 ? 6 : 2;
}

static const omc_tag_schema *
omc_find_schema(const omc_store *s, const omc_entry *e, int *known)
{
    omc_size i;
    int ifd;
    *known = 0;
    ifd = omc_ifd_class(&s->arena, e->key.u.exif_tag.ifd);
    for (i = 0U; i < sizeof(omc_schemas) / sizeof(omc_schemas[0]); ++i) {
        if (omc_schemas[i].tag != e->key.u.exif_tag.tag)
            continue;
        *known = 1;
        if (omc_schemas[i].ifd == ifd ||
            (omc_schemas[i].ifd == 2 && (ifd == 1 || ifd == 6)) ||
            (omc_schemas[i].ifd == 5 && (ifd == 1 || ifd == 6))) {
            return &omc_schemas[i];
        }
    }
    if (ifd == 0)
        *known = 0;
    return NULL;
}

static void
omc_check_scalar(omc_validation *v, const omc_val *x, omc_entry_id id)
{
    int valid;
    valid = 1;
    switch (x->elem_type) {
    case OMC_ELEM_U8:
        valid = x->u.u64 <= 255U;
        break;
    case OMC_ELEM_I8:
        valid = x->u.i64 >= -128 && x->u.i64 <= 127;
        break;
    case OMC_ELEM_U16:
        valid = x->u.u64 <= 65535U;
        break;
    case OMC_ELEM_I16:
        valid = x->u.i64 >= -32768 && x->u.i64 <= 32767;
        break;
    case OMC_ELEM_U32:
        valid = x->u.u64 <= 0xFFFFFFFFU;
        break;
    case OMC_ELEM_I32:
        valid = x->u.i64 >= (-2147483647 - 1) && x->u.i64 <= 2147483647;
        break;
    default:
        break;
    }
    if (!valid)
        omc_issue(v, OMC_METADATA_SCALAR_RANGE, id, OMC_INVALID_ENTRY_ID, 0);
    if ((x->elem_type == OMC_ELEM_URATIONAL && x->u.ur.denom == 0U) ||
        (x->elem_type == OMC_ELEM_SRATIONAL && x->u.sr.denom == 0)) {
        omc_issue(v, OMC_METADATA_ZERO_DENOMINATOR, id, OMC_INVALID_ENTRY_ID, 0);
    }
}

static int
omc_check_entry(omc_validation *v, omc_entry_id id)
{
    const omc_entry *e;
    const omc_val *x;
    const omc_tag_schema *schema;
    omc_val scalar;
    omc_u32 i;
    omc_u32 count;
    omc_u16 type;
    omc_u16 hinted;
    int known;
    const omc_u8 *p;
    omc_byte_ref r;
    omc_u32 expected;
    e = &v->store->entries[id];
    v->res.entries_checked++;
    if (!omc_validate_key_shape(v, &e->key)) {
        omc_issue(v, OMC_METADATA_INVALID_KEY, id, OMC_INVALID_ENTRY_ID, 0);
        return 0;
    }
    if ((e->origin.block != OMC_INVALID_BLOCK_ID &&
         e->origin.block >= v->store->block_count) ||
        e->origin.wire_type.family < OMC_WIRE_NONE ||
        e->origin.wire_type.family > OMC_WIRE_OTHER ||
        !omc_key_text(v, e->origin.wire_type_name, 0)) {
        omc_issue(v, OMC_METADATA_INVALID_ORIGIN, id, OMC_INVALID_ENTRY_ID, 0);
    }
    if ((e->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
        return 0;
    x = &e->value;
    if (!omc_value_shape_valid(x, &v->store->arena)) {
        omc_issue(v, OMC_METADATA_INVALID_VALUE, id, OMC_INVALID_ENTRY_ID, 0);
        return 0;
    }
    if (x->kind >= OMC_VAL_ARRAY && x->u.ref.size > v->opts.max_value_bytes) {
        omc_issue(v, OMC_METADATA_LIMIT, id, OMC_INVALID_ENTRY_ID, 0);
        v->res.status = OMC_STATUS_OVERFLOW;
        return 0;
    }
    if (x->kind == OMC_VAL_SCALAR)
        omc_check_scalar(v, x, id);
    if (x->kind == OMC_VAL_ARRAY &&
        (x->elem_type == OMC_ELEM_URATIONAL || x->elem_type == OMC_ELEM_SRATIONAL)) {
        for (i = 0U; i < x->count && v->res.issues_needed < v->opts.max_issues; ++i) {
            omc_value_array_scalar(x, &v->store->arena, i, &scalar);
            omc_check_scalar(v, &scalar, id);
        }
    }
    if (x->kind == OMC_VAL_TEXT &&
        (x->text_encoding == OMC_TEXT_ASCII || x->text_encoding == OMC_TEXT_UTF8)) {
        p = x->u.ref.size ? v->store->arena.data + x->u.ref.offset : NULL;
        if (!omc_utf8_valid(p, x->u.ref.size, x->text_encoding == OMC_TEXT_ASCII)) {
            omc_issue(v, OMC_METADATA_INVALID_TEXT, id, OMC_INVALID_ENTRY_ID, 0);
        }
    }
    if (e->key.kind == OMC_KEY_XMP_PROPERTY) {
        r = e->key.u.xmp_property.schema_ns;
        p = v->store->arena.data + r.offset;
        known = 1;
        for (i = 0U; i < r.size; ++i) {
            if (p[i] < 32U || p[i] >= 127U || p[i] == '"' || p[i] == '&' ||
                p[i] == '<' || p[i] == '>') {
                known = 0;
                break;
            }
        }
        if (!known)
            omc_issue(v, OMC_METADATA_INVALID_NAMESPACE, id, OMC_INVALID_ENTRY_ID, 0);
        r = e->key.u.xmp_property.property_path;
        p = v->store->arena.data + r.offset;
        known = omc_xmp_path_valid(p, r.size);
        if (!known)
            omc_issue(v, OMC_METADATA_INVALID_PATH, id, OMC_INVALID_ENTRY_ID, 0);
        if (x->kind == OMC_VAL_TEXT && x->text_encoding != OMC_TEXT_UTF8 &&
            x->text_encoding != OMC_TEXT_ASCII) {
            omc_issue(v, OMC_METADATA_INVALID_TEXT, id, OMC_INVALID_ENTRY_ID, 0);
        }
    }
    if (e->key.kind != OMC_KEY_EXIF_TAG)
        return 0;
    type = omc_value_tiff_type(x);
    count = x->count;
    if (x->kind == OMC_VAL_TEXT) {
        if (count == 0xFFFFFFFFU) {
            omc_issue(v, OMC_METADATA_LIMIT, id, OMC_INVALID_ENTRY_ID, 0);
            return 0;
        }
        count++;
    }
    if (v->opts.validate_wire_hints) {
        if (e->origin.wire_type.family == OMC_WIRE_TIFF) {
            hinted = e->origin.wire_type.code;
            if (hinted == 0U || hinted > 12U ||
                (hinted != type && !(x->kind == OMC_VAL_BYTES &&
                                     (hinted == 1U || hinted == 6U || hinted == 7U)))) {
                omc_issue(v, OMC_METADATA_INVALID_WIRE_TYPE, id, OMC_INVALID_ENTRY_ID,
                          0);
            } else
                type = hinted;
            if (e->origin.wire_count != 0U && e->origin.wire_count != count) {
                omc_issue(v, OMC_METADATA_INVALID_WIRE_COUNT, id, OMC_INVALID_ENTRY_ID,
                          0);
            }
        } else if (e->origin.wire_type.family == OMC_WIRE_NONE &&
                   (e->origin.wire_type.code != 0U || e->origin.wire_count != 0U)) {
            omc_issue(v, OMC_METADATA_INVALID_WIRE_TYPE, id, OMC_INVALID_ENTRY_ID, 0);
        }
    }
    if (!v->opts.validate_schema)
        return 0;
    schema = omc_find_schema(v->store, e, &known);
    if (schema == NULL) {
        if (known)
            omc_issue(v, OMC_METADATA_WRONG_IFD, id, OMC_INVALID_ENTRY_ID, 0);
        else if (v->opts.unknown_tags != OMC_UNKNOWN_TAG_ALLOW) {
            omc_issue(v, OMC_METADATA_UNKNOWN_TAG, id, OMC_INVALID_ENTRY_ID,
                      v->opts.unknown_tags == OMC_UNKNOWN_TAG_WARNING);
        }
        return 0;
    }
    if (type == 0U || type > 15U || (schema->types & (1U << type)) == 0U) {
        omc_issue(v, OMC_METADATA_WRONG_TYPE, id, OMC_INVALID_ENTRY_ID, 0);
    }
    if (count < schema->min_count ||
        (schema->max_count != 0U && count > schema->max_count)) {
        omc_issue(v, OMC_METADATA_WRONG_COUNT, id, OMC_INVALID_ENTRY_ID, 0);
    }
    if (type == 2U && x->kind == OMC_VAL_TEXT &&
        !omc_utf8_valid(v->store->arena.data + x->u.ref.offset, x->u.ref.size, 1)) {
        omc_issue(v, OMC_METADATA_INVALID_TEXT, id, OMC_INVALID_ENTRY_ID, 0);
    }
    expected = 0U;
    if (v->whole_store && v->opts.has_dimensions &&
        (omc_ref_equals(&v->store->arena, e->key.u.exif_tag.ifd, "ifd0") ||
         omc_ref_equals(&v->store->arena, e->key.u.exif_tag.ifd, "exififd"))) {
        if (e->key.u.exif_tag.tag == 0x0100U || e->key.u.exif_tag.tag == 0xA002U)
            expected = v->opts.width;
        if (e->key.u.exif_tag.tag == 0x0101U || e->key.u.exif_tag.tag == 0xA003U)
            expected = v->opts.height;
    }
    if (v->whole_store && v->opts.has_samples_per_pixel &&
        e->key.u.exif_tag.tag == 0x0115U &&
        omc_ref_equals(&v->store->arena, e->key.u.exif_tag.ifd, "ifd0"))
        expected = v->opts.samples_per_pixel;
    if (expected != 0U && (x->kind != OMC_VAL_SCALAR || x->u.u64 != expected)) {
        omc_issue(v, OMC_METADATA_IMAGE_MISMATCH, id, OMC_INVALID_ENTRY_ID, 0);
    }
    if (v->whole_store && v->opts.has_color_planes &&
        omc_ref_equals(&v->store->arena, e->key.u.exif_tag.ifd, "ifd0")) {
        expected = 0U;
        switch (e->key.u.exif_tag.tag) {
        case 0xC621U:
        case 0xC622U:
        case 0xC714U:
        case 0xC715U:
            expected = (omc_u32)v->opts.color_planes * 3U;
            break;
        case 0xC623U:
        case 0xC624U:
            expected = (omc_u32)v->opts.color_planes * v->opts.color_planes;
            break;
        case 0xC616U:
        case 0xC627U:
        case 0xC628U:
            expected = v->opts.color_planes;
            break;
        default:
            break;
        }
        if (expected != 0U && count != expected) {
            omc_issue(v, OMC_METADATA_IMAGE_MISMATCH, id, OMC_INVALID_ENTRY_ID, 0);
        }
    }
    return 1;
}

static void
omc_validate_cfa(omc_validation *v)
{
    omc_entry_id dimensions;
    omc_entry_id pattern;
    omc_size i;
    const omc_entry *e;
    const omc_val *value;
    omc_val rows;
    omc_val columns;
    dimensions = OMC_INVALID_ENTRY_ID;
    pattern = OMC_INVALID_ENTRY_ID;
    for (i = 0U; i < v->store->entry_count; ++i) {
        e = &v->store->entries[i];
        if (e->key.kind != OMC_KEY_EXIF_TAG || (e->flags & OMC_ENTRY_FLAG_DELETED) ||
            !omc_ref_valid(&v->store->arena, e->key.u.exif_tag.ifd) ||
            !omc_ref_equals(&v->store->arena, e->key.u.exif_tag.ifd, "ifd0"))
            continue;
        if (e->key.u.exif_tag.tag == 0x828DU && dimensions == OMC_INVALID_ENTRY_ID)
            dimensions = (omc_entry_id)i;
        if (e->key.u.exif_tag.tag == 0x828EU && pattern == OMC_INVALID_ENTRY_ID)
            pattern = (omc_entry_id)i;
    }
    if (dimensions == OMC_INVALID_ENTRY_ID || pattern == OMC_INVALID_ENTRY_ID)
        return;
    value = &v->store->entries[dimensions].value;
    if (value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_U16 ||
        value->count != 2U || !omc_value_shape_valid(value, &v->store->arena))
        return;
    omc_value_array_scalar(value, &v->store->arena, 0U, &rows);
    omc_value_array_scalar(value, &v->store->arena, 1U, &columns);
    if (rows.u.u64 == 0U || columns.u.u64 == 0U ||
        rows.u.u64 * columns.u.u64 != v->store->entries[pattern].value.count)
        omc_issue(v, OMC_METADATA_RELATED_ENTRIES, pattern, dimensions, 0);
}

static omc_metadata_validate_res
omc_validate_metadata(const omc_store *store, omc_entry_id id, int single,
                      omc_metadata_issue *issues, omc_u32 capacity,
                      const omc_metadata_validate_opts *opts)
{
    omc_validation v;
    omc_entry_id *slots;
    omc_size nslots;
    omc_size i;
    omc_size j;
    omc_size h;
    omc_u32 k;
    omc_byte_ref r;
    omc_byte_ref prev;
    const omc_entry *e;
    memset(&v, 0, sizeof(v));
    v.store = store;
    v.issues = issues;
    v.capacity = capacity;
    v.whole_store = !single;
    omc_metadata_validate_opts_init(&v.opts);
    if (opts != NULL)
        v.opts = *opts;
    if (!omc_store_shape_valid(store) || (capacity != 0U && issues == NULL) ||
        v.opts.max_entries == 0U || v.opts.max_issues == 0U ||
        v.opts.max_key_bytes == 0U || v.opts.max_arena_bytes == 0U ||
        v.opts.max_value_bytes == 0U ||
        v.opts.max_value_bytes > v.opts.max_arena_bytes ||
        v.opts.unknown_tags < OMC_UNKNOWN_TAG_ALLOW ||
        v.opts.unknown_tags > OMC_UNKNOWN_TAG_ERROR ||
        (v.opts.has_dimensions && (v.opts.width == 0U || v.opts.height == 0U)) ||
        (v.opts.has_samples_per_pixel && v.opts.samples_per_pixel == 0U) ||
        (v.opts.has_color_planes && v.opts.color_planes == 0U) ||
        (single && id >= store->entry_count)) {
        v.res.status = OMC_STATUS_INVALID_ARGUMENT;
        return v.res;
    }
    if (store->entry_count > v.opts.max_entries ||
        store->arena.size > v.opts.max_arena_bytes) {
        omc_issue(&v, OMC_METADATA_LIMIT, OMC_INVALID_ENTRY_ID, OMC_INVALID_ENTRY_ID,
                  0);
        v.res.status = OMC_STATUS_OVERFLOW;
        return v.res;
    }
    nslots = 1U;
    slots = NULL;
    if (!single && v.opts.validate_schema && store->entry_count != 0U) {
        while (nslots < store->entry_count) {
            if (nslots > ((omc_size)-1) / 2U) {
                v.res.status = OMC_STATUS_OVERFLOW;
                return v.res;
            }
            nslots *= 2U;
        }
        if (nslots > ((omc_size)-1) / (2U * sizeof(*slots))) {
            v.res.status = OMC_STATUS_OVERFLOW;
            return v.res;
        }
        nslots *= 2U;
        slots = (omc_entry_id *)malloc(nslots * sizeof(*slots));
        if (slots == NULL) {
            v.res.status = OMC_STATUS_NO_MEMORY;
            return v.res;
        }
        for (i = 0U; i < nslots; ++i)
            slots[i] = OMC_INVALID_ENTRY_ID;
    }
    for (i = single ? id : 0U; i < (single ? (omc_size)id + 1U : store->entry_count);
         ++i) {
        if (v.res.issues_needed >= v.opts.max_issues) {
            v.res.status = OMC_STATUS_OVERFLOW;
            break;
        }
        if (!omc_check_entry(&v, (omc_entry_id)i) || slots == NULL)
            continue;
        e = &store->entries[i];
        r = e->key.u.exif_tag.ifd;
        h = e->key.u.exif_tag.tag;
        for (k = 0U; k < r.size; ++k)
            h = h * 33U + store->arena.data[r.offset + k];
        h &= nslots - 1U;
        for (j = 0U; j < nslots; ++j) {
            if (slots[h] == OMC_INVALID_ENTRY_ID) {
                slots[h] = (omc_entry_id)i;
                break;
            }
            prev = store->entries[slots[h]].key.u.exif_tag.ifd;
            if (store->entries[slots[h]].key.u.exif_tag.tag == e->key.u.exif_tag.tag &&
                prev.size == r.size &&
                memcmp(store->arena.data + prev.offset, store->arena.data + r.offset,
                       r.size) == 0) {
                omc_issue(&v, OMC_METADATA_DUPLICATE_SINGLETON, (omc_entry_id)i,
                          slots[h], 0);
                break;
            }
            h = (h + 1U) & (nslots - 1U);
        }
    }
    if (!single && v.res.issues_needed < v.opts.max_issues)
        omc_validate_cfa(&v);
    free(slots);
    return v.res;
}

omc_metadata_validate_res
omc_validate_store(const omc_store *store, omc_metadata_issue *issues, omc_u32 capacity,
                   const omc_metadata_validate_opts *opts)
{
    return omc_validate_metadata(store, OMC_INVALID_ENTRY_ID, 0, issues, capacity,
                                 opts);
}

omc_metadata_validate_res
omc_validate_entry(const omc_store *store, omc_entry_id entry,
                   omc_metadata_issue *issues, omc_u32 capacity,
                   const omc_metadata_validate_opts *opts)
{
    return omc_validate_metadata(store, entry, 1, issues, capacity, opts);
}
