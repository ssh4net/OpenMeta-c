#include "omc/omc_edit.h"

#include <stdlib.h>
#include <string.h>

static omc_status
omc_edit_reserve_ops_internal(omc_edit* edit, omc_size capacity)
{
    void* new_mem;

    if (capacity <= edit->op_capacity) {
        return OMC_STATUS_OK;
    }

    new_mem = realloc(edit->ops, capacity * sizeof(*edit->ops));
    if (new_mem == NULL) {
        return OMC_STATUS_NO_MEMORY;
    }

    edit->ops = (omc_edit_op*)new_mem;
    edit->op_capacity = capacity;
    return OMC_STATUS_OK;
}

static omc_size
omc_edit_next_capacity(omc_size current, omc_size needed)
{
    omc_size next;

    if (current == 0U) {
        next = 8U;
    } else {
        next = current;
    }

    while (next < needed) {
        if (next > ((omc_size)(~(omc_size)0) / 2U)) {
            next = needed;
            break;
        }
        next *= 2U;
    }

    return next;
}

static omc_status
omc_clone_ref(const omc_arena* src, omc_byte_ref ref, omc_arena* dst,
              omc_byte_ref* out_ref)
{
    omc_const_bytes view;

    if (src == NULL || dst == NULL || out_ref == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    out_ref->offset = 0U;
    out_ref->size = 0U;
    if (ref.size == 0U) {
        return OMC_STATUS_OK;
    }

    view = omc_arena_view(src, ref);
    if (view.data == NULL) {
        return OMC_STATUS_STATE;
    }

    return omc_arena_append(dst, view.data, view.size, out_ref);
}

static omc_status
omc_clone_key(const omc_key* key, const omc_arena* src, omc_arena* dst,
              omc_key* out_key)
{
    omc_status status;

    if (key == NULL || src == NULL || dst == NULL || out_key == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_key = *key;
    switch (key->kind) {
    case OMC_KEY_EXIF_TAG:
        return omc_clone_ref(src, key->u.exif_tag.ifd, dst,
                             &out_key->u.exif_tag.ifd);
    case OMC_KEY_COMMENT:
    case OMC_KEY_IPTC_DATASET:
    case OMC_KEY_ICC_HEADER_FIELD:
    case OMC_KEY_ICC_TAG:
    case OMC_KEY_PHOTOSHOP_IRB:
    case OMC_KEY_GEOTIFF_KEY:
        return OMC_STATUS_OK;
    case OMC_KEY_EXR_ATTR:
        return omc_clone_ref(src, key->u.exr_attr.name, dst,
                             &out_key->u.exr_attr.name);
    case OMC_KEY_XMP_PROPERTY:
        status = omc_clone_ref(src, key->u.xmp_property.schema_ns, dst,
                               &out_key->u.xmp_property.schema_ns);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        return omc_clone_ref(src, key->u.xmp_property.property_path, dst,
                             &out_key->u.xmp_property.property_path);
    case OMC_KEY_PHOTOSHOP_IRB_FIELD:
        return omc_clone_ref(src, key->u.photoshop_irb_field.field, dst,
                             &out_key->u.photoshop_irb_field.field);
    case OMC_KEY_PRINTIM_FIELD:
        return omc_clone_ref(src, key->u.printim_field.field, dst,
                             &out_key->u.printim_field.field);
    case OMC_KEY_BMFF_FIELD:
        return omc_clone_ref(src, key->u.bmff_field.field, dst,
                             &out_key->u.bmff_field.field);
    case OMC_KEY_JUMBF_FIELD:
        return omc_clone_ref(src, key->u.jumbf_field.field, dst,
                             &out_key->u.jumbf_field.field);
    case OMC_KEY_JUMBF_CBOR_KEY:
        return omc_clone_ref(src, key->u.jumbf_cbor_key.key, dst,
                             &out_key->u.jumbf_cbor_key.key);
    case OMC_KEY_PNG_TEXT:
        status = omc_clone_ref(src, key->u.png_text.keyword, dst,
                               &out_key->u.png_text.keyword);
        if (status != OMC_STATUS_OK) {
            return status;
        }
        return omc_clone_ref(src, key->u.png_text.field, dst,
                             &out_key->u.png_text.field);
    }

    return OMC_STATUS_STATE;
}

static omc_status
omc_clone_value(const omc_val* value, const omc_arena* src, omc_arena* dst,
                omc_val* out_value)
{
    if (value == NULL || src == NULL || dst == NULL || out_value == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_value = *value;
    if (value->kind == OMC_VAL_ARRAY || value->kind == OMC_VAL_BYTES
        || value->kind == OMC_VAL_TEXT) {
        return omc_clone_ref(src, value->u.ref, dst, &out_value->u.ref);
    }

    return OMC_STATUS_OK;
}

static omc_status
omc_clone_origin(const omc_origin* origin, const omc_arena* src, omc_arena* dst,
                 omc_origin* out_origin)
{
    if (origin == NULL || src == NULL || dst == NULL || out_origin == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_origin = *origin;
    return omc_clone_ref(src, origin->wire_type_name, dst,
                         &out_origin->wire_type_name);
}

static omc_status
omc_clone_entry(const omc_entry* entry, const omc_arena* src, omc_arena* dst,
                omc_entry* out_entry)
{
    omc_status status;

    if (entry == NULL || src == NULL || dst == NULL || out_entry == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    *out_entry = *entry;

    status = omc_clone_key(&entry->key, src, dst, &out_entry->key);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    status = omc_clone_value(&entry->value, src, dst, &out_entry->value);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    return omc_clone_origin(&entry->origin, src, dst, &out_entry->origin);
}

static omc_status
omc_copy_blocks(const omc_store* base, omc_store* out)
{
    omc_size i;
    omc_status status;
    omc_block_id block_id;

    status = omc_store_reserve_blocks(out, base->block_count);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    for (i = 0U; i < base->block_count; ++i) {
        status = omc_store_add_block(out, &base->blocks[i], &block_id);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    return OMC_STATUS_OK;
}

static omc_status
omc_copy_live_entries(const omc_store* base, omc_store* out, int skip_deleted)
{
    omc_size i;
    omc_status status;
    omc_entry copied;
    omc_entry_id entry_id;

    status = omc_store_reserve_entries(out, base->entry_count);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    for (i = 0U; i < base->entry_count; ++i) {
        if (skip_deleted != 0
            && (base->entries[i].flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }

        status = omc_clone_entry(&base->entries[i], &base->arena, &out->arena,
                                 &copied);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        status = omc_store_add_entry(out, &copied, &entry_id);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    return OMC_STATUS_OK;
}

void
omc_edit_init(omc_edit* edit)
{
    if (edit == NULL) {
        return;
    }

    memset(edit, 0, sizeof(*edit));
    omc_arena_init(&edit->arena);
}

void
omc_edit_reset(omc_edit* edit)
{
    if (edit == NULL) {
        return;
    }

    edit->op_count = 0U;
    omc_arena_reset(&edit->arena);
}

void
omc_edit_fini(omc_edit* edit)
{
    if (edit == NULL) {
        return;
    }

    free(edit->ops);
    edit->ops = NULL;
    edit->op_count = 0U;
    edit->op_capacity = 0U;
    omc_arena_fini(&edit->arena);
}

omc_status
omc_edit_reserve_ops(omc_edit* edit, omc_size capacity)
{
    if (edit == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (capacity > ((omc_size)(~(omc_size)0) / sizeof(*edit->ops))) {
        return OMC_STATUS_OVERFLOW;
    }

    return omc_edit_reserve_ops_internal(edit, capacity);
}

omc_status
omc_edit_add_entry(omc_edit* edit, const omc_entry* entry)
{
    omc_size needed;
    omc_size capacity;
    omc_edit_op* op;
    omc_status status;

    if (edit == NULL || entry == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    needed = edit->op_count + 1U;
    if (needed < edit->op_count) {
        return OMC_STATUS_OVERFLOW;
    }

    if (needed > edit->op_capacity) {
        capacity = omc_edit_next_capacity(edit->op_capacity, needed);
        status = omc_edit_reserve_ops(edit, capacity);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    op = &edit->ops[edit->op_count];
    memset(op, 0, sizeof(*op));
    op->kind = OMC_EDIT_OP_ADD_ENTRY;
    op->target = OMC_INVALID_ENTRY_ID;
    op->entry = *entry;
    edit->op_count = needed;
    return OMC_STATUS_OK;
}

omc_status
omc_edit_set_value(omc_edit* edit, omc_entry_id target, const omc_val* value)
{
    omc_size needed;
    omc_size capacity;
    omc_edit_op* op;
    omc_status status;

    if (edit == NULL || value == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    needed = edit->op_count + 1U;
    if (needed < edit->op_count) {
        return OMC_STATUS_OVERFLOW;
    }

    if (needed > edit->op_capacity) {
        capacity = omc_edit_next_capacity(edit->op_capacity, needed);
        status = omc_edit_reserve_ops(edit, capacity);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    op = &edit->ops[edit->op_count];
    memset(op, 0, sizeof(*op));
    op->kind = OMC_EDIT_OP_SET_VALUE;
    op->target = target;
    op->value = *value;
    edit->op_count = needed;
    return OMC_STATUS_OK;
}

omc_status
omc_edit_tombstone(omc_edit* edit, omc_entry_id target)
{
    omc_size needed;
    omc_size capacity;
    omc_edit_op* op;
    omc_status status;

    if (edit == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    needed = edit->op_count + 1U;
    if (needed < edit->op_count) {
        return OMC_STATUS_OVERFLOW;
    }

    if (needed > edit->op_capacity) {
        capacity = omc_edit_next_capacity(edit->op_capacity, needed);
        status = omc_edit_reserve_ops(edit, capacity);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    op = &edit->ops[edit->op_count];
    memset(op, 0, sizeof(*op));
    op->kind = OMC_EDIT_OP_TOMBSTONE;
    op->target = target;
    edit->op_count = needed;
    return OMC_STATUS_OK;
}

omc_status
omc_edit_commit(const omc_store* base, const omc_edit* edits,
                omc_size edit_count, omc_store* out)
{
    omc_status status;
    omc_size i;
    omc_size j;
    omc_size add_count;
    omc_size arena_hint;
    omc_entry entry;
    omc_entry_id entry_id;
    omc_val value;

    if (base == NULL || out == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (edit_count != 0U && edits == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (out == base) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    add_count = 0U;
    arena_hint = base->arena.size;
    for (i = 0U; i < edit_count; ++i) {
        if (arena_hint > ((omc_size)(~(omc_size)0) - edits[i].arena.size)) {
            return OMC_STATUS_OVERFLOW;
        }
        arena_hint += edits[i].arena.size;
        for (j = 0U; j < edits[i].op_count; ++j) {
            if (edits[i].ops[j].kind == OMC_EDIT_OP_ADD_ENTRY) {
                if (add_count == (omc_size)(~(omc_size)0)) {
                    return OMC_STATUS_OVERFLOW;
                }
                add_count += 1U;
            }
        }
    }

    omc_store_reset(out);

    status = omc_arena_reserve(&out->arena, arena_hint);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if (base->entry_count > ((omc_size)(~(omc_size)0) - add_count)) {
        return OMC_STATUS_OVERFLOW;
    }
    status = omc_store_reserve_entries(out, base->entry_count + add_count);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    status = omc_copy_blocks(base, out);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    status = omc_copy_live_entries(base, out, 0);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    for (i = 0U; i < edit_count; ++i) {
        for (j = 0U; j < edits[i].op_count; ++j) {
            switch (edits[i].ops[j].kind) {
            case OMC_EDIT_OP_ADD_ENTRY:
                status = omc_clone_entry(&edits[i].ops[j].entry, &edits[i].arena,
                                         &out->arena, &entry);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                status = omc_store_add_entry(out, &entry, &entry_id);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                break;
            case OMC_EDIT_OP_SET_VALUE:
                if ((omc_size)edits[i].ops[j].target >= out->entry_count) {
                    break;
                }
                status = omc_clone_value(&edits[i].ops[j].value, &edits[i].arena,
                                         &out->arena, &value);
                if (status != OMC_STATUS_OK) {
                    return status;
                }
                out->entries[edits[i].ops[j].target].value = value;
                out->entries[edits[i].ops[j].target].flags |=
                    OMC_ENTRY_FLAG_DIRTY;
                break;
            case OMC_EDIT_OP_TOMBSTONE:
                if ((omc_size)edits[i].ops[j].target >= out->entry_count) {
                    break;
                }
                out->entries[edits[i].ops[j].target].flags |=
                    (OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
                break;
            }
        }
    }

    return OMC_STATUS_OK;
}

omc_status
omc_store_compact(const omc_store* base, omc_store* out)
{
    omc_status status;

    if (base == NULL || out == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (out == base) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_store_reset(out);

    status = omc_arena_reserve(&out->arena, base->arena.size);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    status = omc_copy_blocks(base, out);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    return omc_copy_live_entries(base, out, 1);
}
