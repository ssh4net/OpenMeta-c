#include "omc/omc_metadata_patch.h"
#include "../core/omc_value_internal.h"

#include <stdlib.h>
#include <string.h>

#define OMC_PATCH_MAX_REQUESTS 4096U
#define OMC_PATCH_MAX_XMP  (16U * 1024U * 1024U)
#define OMC_PATCH_MAX_EXIF (64U * 1024U * 1024U)

typedef struct omc_patch_slot {
    omc_metadata_patch_payload family;
    omc_size offset;
    omc_size width;
    omc_metadata_patch_value_spec expected;
} omc_patch_slot;

typedef struct omc_patch_state {
    omc_u64 plan_id;
    omc_arena exif;
    omc_arena xmp;
    omc_patch_slot *slots;
    omc_u32 slot_count;
} omc_patch_state;

static void
omc_patch_result_init(omc_metadata_patch_result *result)
{
    memset(result, 0, sizeof(*result));
    result->code = OMC_METADATA_PATCH_NONE;
    result->exif_status = OMC_EXIF_TIFF_OK;
    result->xmp_status = OMC_XMP_DUMP_OK;
    result->failed_index = 0xFFFFFFFFU;
}

static omc_metadata_patch_result
omc_patch_error(omc_metadata_patch_code code, omc_u32 index)
{
    omc_metadata_patch_result result;
    omc_patch_result_init(&result);
    result.code = code;
    result.failed_index = index;
    return result;
}

static int
omc_patch_bytes_equal(const omc_arena *a, omc_byte_ref ar,
                      const omc_arena *b, omc_byte_ref br)
{
    omc_const_bytes x = omc_arena_view(a, ar);
    omc_const_bytes y = omc_arena_view(b, br);
    return x.size == y.size && (x.size == 0U || memcmp(x.data, y.data, x.size) == 0);
}

static int
omc_patch_key_equal(const omc_store *store, const omc_key *a,
                    const omc_key *b)
{
    if (a->kind != b->kind)
        return 0;
    if (a->kind == OMC_KEY_EXIF_TAG)
        return a->u.exif_tag.tag == b->u.exif_tag.tag
            && omc_patch_bytes_equal(&store->arena, a->u.exif_tag.ifd,
                                     &store->arena, b->u.exif_tag.ifd);
    if (a->kind == OMC_KEY_XMP_PROPERTY)
        return omc_patch_bytes_equal(&store->arena, a->u.xmp_property.schema_ns,
                                     &store->arena, b->u.xmp_property.schema_ns)
            && omc_patch_bytes_equal(&store->arena, a->u.xmp_property.property_path,
                                     &store->arena, b->u.xmp_property.property_path);
    return 0;
}

static int
omc_patch_spec_equal(const omc_val *value, const omc_metadata_patch_value_spec *spec)
{
    return value->kind == spec->kind && value->elem_type == spec->elem_type
           && (value->kind != OMC_VAL_TEXT || value->text_encoding == spec->text_encoding)
           && value->count == spec->count;
}

static omc_size
omc_patch_scalar_size(const omc_val *value)
{
    return omc_elem_size(value->elem_type);
}

static omc_size
omc_patch_escaped_width(const omc_u8 *bytes, omc_size size)
{
    omc_size i;
    omc_size width = 0U;
    for (i = 0U; i < size; ++i) {
        if (bytes[i] == '&') width += 5U;
        else if (bytes[i] == '<' || bytes[i] == '>') width += 4U;
        else if (bytes[i] == '"' || bytes[i] == '\'') width += 6U;
        else if (bytes[i] == '\r') width += 5U;
        else width += 1U;
    }
    return width;
}

static omc_size
omc_patch_escape_copy(const omc_u8 *bytes, omc_size size,
                      omc_u8 *out, omc_size cap)
{
    omc_size i;
    omc_size p = 0U;
    for (i = 0U; i < size; ++i) {
        const char *text = (const char *)0;
        omc_size n = 1U;
        if (bytes[i] == '&') { text = "&amp;"; n = 5U; }
        else if (bytes[i] == '<') { text = "&lt;"; n = 4U; }
        else if (bytes[i] == '>') { text = "&gt;"; n = 4U; }
        else if (bytes[i] == '"') { text = "&quot;"; n = 6U; }
        else if (bytes[i] == '\'') { text = "&apos;"; n = 6U; }
        else if (bytes[i] == '\r') { text = "&#xD;"; n = 5U; }
        if (p > cap - n)
            return 0U;
        if (text != (const char *)0)
            memcpy(out + p, text, n);
        else
            out[p] = bytes[i];
        p += n;
    }
    return p;
}

static omc_size
omc_patch_find(const omc_u8 *haystack, omc_size haystack_size,
               const omc_u8 *needle, omc_size needle_size, omc_u32 occurrence)
{
    omc_size i;
    omc_u32 found = 0U;
    if (needle_size == 0U || needle_size > haystack_size)
        return (omc_size)~(omc_size)0;
    for (i = 0U; i + needle_size <= haystack_size; ++i) {
        if (memcmp(haystack + i, needle, needle_size) == 0) {
            if (found == occurrence)
                return i;
            ++found;
        }
    }
    return (omc_size)~(omc_size)0;
}

static omc_size
omc_patch_value_bytes(const omc_store *store, const omc_val *value,
                      omc_u8 *scratch, omc_size scratch_size)
{
    omc_size width;
    omc_u64 number;
    if (value->kind == OMC_VAL_TEXT || value->kind == OMC_VAL_BYTES) {
        omc_const_bytes bytes = omc_arena_view(&store->arena, value->u.ref);
        if (bytes.size > scratch_size)
            return 0U;
        memcpy(scratch, bytes.data, bytes.size);
        return bytes.size;
    }
    if (value->kind != OMC_VAL_SCALAR)
        return 0U;
    width = omc_patch_scalar_size(value);
    if (width == 0U || width > scratch_size)
        return 0U;
    number = value->elem_type == OMC_ELEM_I8 || value->elem_type == OMC_ELEM_I16
             || value->elem_type == OMC_ELEM_I32 || value->elem_type == OMC_ELEM_I64
             ? (omc_u64)value->u.i64 : value->u.u64;
    {
        omc_size i;
        for (i = 0U; i < width; ++i)
            scratch[i] = (omc_u8)(number >> (8U * i));
    }
    return omc_elem_size(value->elem_type);
}

static int
omc_patch_find_xmp_slot(const omc_store *store, const omc_key *key,
                        const omc_val *value, omc_u32 occurrence,
                        const omc_arena *payload, omc_size *offset,
                        omc_size *width)
{
    omc_const_bytes text;
    omc_const_bytes path;
    omc_u8 escaped_small[256];
    omc_u8 *escaped;
    omc_size escaped_size;
    omc_size cursor = 0U;
    omc_u32 found = 0U;
    int allocated = 0;
    if (value->kind != OMC_VAL_TEXT)
        return 0;
    text = omc_arena_view(&store->arena, value->u.ref);
    escaped_size = omc_patch_escaped_width(text.data, text.size);
    escaped = escaped_small;
    if (escaped_size > sizeof(escaped_small)) {
        escaped = (omc_u8 *)malloc(escaped_size);
        if (escaped == (omc_u8 *)0)
            return 0;
        allocated = 1;
    }
    if (omc_patch_escape_copy(text.data, text.size, escaped, escaped_size)
        != escaped_size) {
        if (allocated)
            free(escaped);
        return 0;
    }
    path = omc_arena_view(&store->arena, key->u.xmp_property.property_path);
    while (cursor < payload->size) {
        omc_size p = omc_patch_find(payload->data + cursor, payload->size - cursor,
                                    path.data, path.size, 0U);
        omc_size start;
        omc_size end;
        if (p == (omc_size)~(omc_size)0) {
            if (allocated)
                free(escaped);
            return 0;
        }
        p += cursor;
        if (p == 0U || (payload->data[p - 1U] != ':'
                        && payload->data[p - 1U] != '<')) {
            cursor = p + path.size;
            continue;
        }
        start = p + path.size;
        while (start < payload->size && payload->data[start] != '>')
            ++start;
        if (start == payload->size) {
            if (allocated)
                free(escaped);
            return 0;
        }
        ++start;
        end = omc_patch_find(payload->data + start, payload->size - start,
                             escaped, escaped_size, 0U);
        if (end != (omc_size)~(omc_size)0) {
            end += start;
            if (found == occurrence) {
                *offset = end;
                *width = escaped_size;
                if (allocated)
                    free(escaped);
                return 1;
            }
            ++found;
        }
        cursor = p + path.size;
    }
    if (allocated)
        free(escaped);
    return 0;
}

static int
omc_patch_find_slot(const omc_store *store, const omc_metadata_patch_request *request,
                    const omc_patch_state *state, omc_patch_slot *slot)
{
    omc_size i;
    omc_u32 occurrence = 0U;
    omc_u8 needle[64];
    omc_size needle_size;
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry *entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U)
            continue;
        if (!omc_patch_key_equal(store, &entry->key, &request->key))
            continue;
        if (occurrence != request->occurrence) {
            ++occurrence;
            continue;
        }
        if (entry->key.kind == OMC_KEY_XMP_PROPERTY) {
            if (entry->value.kind != OMC_VAL_TEXT)
                return 0;
            if (!omc_patch_spec_equal(&entry->value, &request->expected))
                return 0;
            slot->expected.kind = OMC_VAL_TEXT;
            slot->expected.elem_type = entry->value.elem_type;
            slot->expected.text_encoding = entry->value.text_encoding;
            slot->expected.count = entry->value.count;
            if (request->escaped_width == 0U
                || !omc_patch_find_xmp_slot(store, &entry->key, &entry->value,
                                             request->occurrence, &state->xmp,
                                             &slot->offset, &slot->width))
                return 0;
            slot->family = OMC_METADATA_PATCH_XMP;
            if (slot->width != request->escaped_width)
                return 0;
            return 1;
        }
        if (!omc_patch_spec_equal(&entry->value, &request->expected))
            return 0;
        slot->expected = request->expected;
        if (request->escaped_width != 0U)
            return 0;
        needle_size = omc_patch_value_bytes(store, &entry->value, needle,
                                            sizeof(needle));
        if (needle_size == 0U)
            return 0;
        slot->offset = omc_patch_find(state->exif.data, state->exif.size,
                                      needle, needle_size, request->occurrence);
        if (slot->offset == (omc_size)~(omc_size)0)
            return 0;
        slot->width = needle_size;
        slot->family = OMC_METADATA_PATCH_EXIF_TIFF;
        return 1;
    }
    return 0;
}

void
omc_metadata_patch_opts_init(omc_metadata_patch_opts *opts)
{
    if (opts == (omc_metadata_patch_opts *)0)
        return;
    memset(opts, 0, sizeof(*opts));
    opts->plan_id = 1U;
    opts->exif.include_subifds = 0;
    opts->exif.inject_minimal_dng_version = 0;
    opts->exif.honor_wire_type_hints = 1;
    opts->exif.preserve_opaque_makernote = 0;
    opts->exif.max_output_bytes = (omc_u64)64U * 1024U * 1024U;
    omc_xmp_portable_opts_init(&opts->xmp);
    opts->xmp.include_existing_xmp = 1;
    opts->max_patch_requests = OMC_PATCH_MAX_REQUESTS;
    opts->validate = 1;
}

void omc_metadata_patch_plan_init(omc_metadata_patch_plan *plan)
{ if (plan != (omc_metadata_patch_plan *)0) plan->state = (void *)0; }

void
omc_metadata_patch_plan_reset(omc_metadata_patch_plan *plan)
{
    omc_patch_state *state;
    if (plan == (omc_metadata_patch_plan *)0 || plan->state == (void *)0)
        return;
    state = (omc_patch_state *)plan->state;
    omc_arena_fini(&state->exif);
    omc_arena_fini(&state->xmp);
    free(state->slots);
    free(state);
    plan->state = (void *)0;
}

void omc_metadata_patch_instance_init(omc_metadata_patch_instance *instance)
{ if (instance != (omc_metadata_patch_instance *)0) instance->state = (void *)0; }

void
omc_metadata_patch_instance_reset(omc_metadata_patch_instance *instance)
{
    omc_patch_state *state;
    if (instance == (omc_metadata_patch_instance *)0 || instance->state == (void *)0)
        return;
    state = (omc_patch_state *)instance->state;
    omc_arena_fini(&state->exif);
    omc_arena_fini(&state->xmp);
    free(state->slots);
    free(state);
    instance->state = (void *)0;
}

const char *
omc_metadata_patch_code_name(omc_metadata_patch_code code)
{
    static const char *const names[] = {
        "none", "null_output", "invalid_options", "invalid_metadata",
        "no_exif_data", "limit", "serialization_failed", "empty_requests",
        "handle_buffer_size_mismatch", "invalid_request", "key_not_found",
        "occurrence_out_of_range", "value_type_mismatch", "duplicate_request",
        "allocation_failed", "invalid_plan", "invalid_instance", "empty_updates",
        "invalid_handle", "foreign_handle", "duplicate_handle", "width_mismatch",
        "invalid_value", "replay_failed"
    };
    return code < (sizeof(names) / sizeof(names[0])) ? names[code] : "unknown";
}

static omc_const_bytes
omc_patch_payload_from_state(const omc_patch_state *state,
                             omc_metadata_patch_payload family)
{
    omc_byte_ref ref;
    omc_const_bytes empty;
    empty.data = (const omc_u8 *)0;
    empty.size = 0U;
    if (state == (const omc_patch_state *)0)
        return empty;
    ref.offset = 0U;
    ref.size = (omc_u32)(family == OMC_METADATA_PATCH_EXIF_TIFF
                             ? state->exif.size : state->xmp.size);
    return family == OMC_METADATA_PATCH_EXIF_TIFF
               ? omc_arena_view(&state->exif, ref)
               : omc_arena_view(&state->xmp, ref);
}

omc_const_bytes
omc_metadata_patch_plan_payload(const omc_metadata_patch_plan *plan,
                                omc_metadata_patch_payload family)
{ return omc_patch_payload_from_state(plan == (const omc_metadata_patch_plan *)0 ? (const omc_patch_state *)0 : (const omc_patch_state *)plan->state, family); }

omc_const_bytes
omc_metadata_patch_instance_payload(const omc_metadata_patch_instance *instance,
                                    omc_metadata_patch_payload family)
{ return omc_patch_payload_from_state(instance == (const omc_metadata_patch_instance *)0 ? (const omc_patch_state *)0 : (const omc_patch_state *)instance->state, family); }

omc_metadata_patch_result
omc_metadata_patch_prepare(const omc_store *store,
                           const omc_metadata_patch_request *requests,
                           omc_u32 request_count,
                           const omc_metadata_patch_opts *opts_in,
                           omc_metadata_patch_handle *handles,
                           omc_u32 handle_capacity,
                           omc_metadata_patch_plan *out_plan)
{
    omc_metadata_patch_opts opts;
    omc_patch_state *state;
    omc_exif_tiff_opts exif_opts;
    omc_exif_tiff_res exif_res;
    omc_xmp_dump_res xmp_res;
    omc_mut_bytes output;
    omc_u32 i;
    omc_status status;
    omc_metadata_patch_result result;
    omc_patch_result_init(&result);
    if (out_plan == (omc_metadata_patch_plan *)0)
        return omc_patch_error(OMC_METADATA_PATCH_NULL_OUTPUT, 0U);
    if (opts_in == (const omc_metadata_patch_opts *)0) {
        omc_metadata_patch_opts_init(&opts);
    } else opts = *opts_in;
    if (store == (const omc_store *)0 || requests == (const omc_metadata_patch_request *)0
        || handles == (omc_metadata_patch_handle *)0 || request_count == 0U
        || request_count > OMC_PATCH_MAX_REQUESTS || handle_capacity != request_count
        || opts.plan_id == 0U || opts.plan_id > OMC_METADATA_PATCH_MAX_PLAN_ID
        || opts.max_patch_requests == 0U || opts.max_patch_requests > OMC_PATCH_MAX_REQUESTS
        || !omc_store_shape_valid(store))
        return omc_patch_error(request_count == 0U ? OMC_METADATA_PATCH_EMPTY_REQUESTS
                                                   : OMC_METADATA_PATCH_INVALID_OPTIONS, 0U);
    state = (omc_patch_state *)calloc(1U, sizeof(*state));
    if (state == (omc_patch_state *)0)
        return omc_patch_error(OMC_METADATA_PATCH_ALLOCATION_FAILED, 0U);
    state->plan_id = opts.plan_id;
    omc_arena_init(&state->exif); omc_arena_init(&state->xmp);
    state->slots = (omc_patch_slot *)calloc(request_count, sizeof(*state->slots));
    if (state->slots == (omc_patch_slot *)0) {
        omc_metadata_patch_plan temp; temp.state = state; omc_metadata_patch_plan_reset(&temp);
        return omc_patch_error(OMC_METADATA_PATCH_ALLOCATION_FAILED, 0U);
    }
    omc_exif_tiff_opts_init(&exif_opts);
    exif_opts.include_subifds = opts.exif.include_subifds;
    exif_opts.inject_minimal_dng_version = opts.exif.inject_minimal_dng_version;
    exif_opts.honor_wire_type_hints = opts.exif.honor_wire_type_hints;
    exif_opts.preserve_opaque_makernote = opts.exif.preserve_opaque_makernote;
    exif_opts.max_output_bytes = opts.exif.max_output_bytes;
    exif_opts.validate = opts.validate;
    output.data = (omc_u8 *)0; output.size = 0U;
    exif_res = omc_serialize_exif_tiff(store, output, &exif_opts);
    result.exif_status = exif_res.status;
    if (exif_res.status == OMC_EXIF_TIFF_OUTPUT_TRUNCATED && exif_res.needed != 0U) {
        if (exif_res.needed > OMC_PATCH_MAX_EXIF) goto serialize_fail;
        status = omc_arena_reserve(&state->exif, (omc_size)exif_res.needed);
        if (status != OMC_STATUS_OK) goto alloc_fail;
        output.data = state->exif.data; output.size = state->exif.capacity;
        exif_res = omc_serialize_exif_tiff(store, output, &exif_opts);
        state->exif.size = (omc_size)exif_res.written;
    } else if (exif_res.status == OMC_EXIF_TIFF_NO_EXIF_DATA) {
        state->exif.size = 0U;
    } else if (exif_res.status != OMC_EXIF_TIFF_OK) goto serialize_fail;
    memset(&xmp_res, 0, sizeof(xmp_res));
    status = omc_xmp_dump_portable_arena(store, &state->xmp, &opts.xmp, &xmp_res);
    result.xmp_status = xmp_res.status;
    if (status != OMC_STATUS_OK || xmp_res.status != OMC_XMP_DUMP_OK
        || state->xmp.size > OMC_PATCH_MAX_XMP) goto serialize_fail;
    for (i = 0U; i < request_count; ++i) {
        omc_u32 j;
        if (requests[i].key.kind != OMC_KEY_EXIF_TAG
            && requests[i].key.kind != OMC_KEY_XMP_PROPERTY) goto invalid_request;
        for (j = 0U; j < i; ++j)
            if (requests[j].key.kind == requests[i].key.kind
                && requests[j].occurrence == requests[i].occurrence
                && omc_patch_key_equal(store, &requests[j].key, &requests[i].key))
                goto duplicate_request;
        if (!omc_patch_find_slot(store, &requests[i], state, &state->slots[i])) {
            result.code = OMC_METADATA_PATCH_KEY_NOT_FOUND;
            result.failed_index = i;
            goto fail;
        }
        handles[i].token = (opts.plan_id << 16U) | (omc_u64)(i + 1U);
    }
    state->slot_count = request_count;
    omc_metadata_patch_plan_reset(out_plan);
    out_plan->state = state;
    result.code = OMC_METADATA_PATCH_NONE;
    result.handle_count = request_count;
    result.payload_size = (omc_u64)state->exif.size + (omc_u64)state->xmp.size;
    return result;
serialize_fail:
    result.code = OMC_METADATA_PATCH_SERIALIZATION_FAILED;
    goto fail;
alloc_fail:
    result.code = OMC_METADATA_PATCH_ALLOCATION_FAILED;
    goto fail;
invalid_request:
    result.code = OMC_METADATA_PATCH_INVALID_REQUEST;
    goto fail;
duplicate_request:
    result.code = OMC_METADATA_PATCH_DUPLICATE_REQUEST;
fail:
    {
        omc_metadata_patch_plan failed_plan;
        failed_plan.state = state;
        omc_metadata_patch_plan_reset(&failed_plan);
    }
    if (result.code == OMC_METADATA_PATCH_NONE) result.code = OMC_METADATA_PATCH_INVALID_OPTIONS;
    return result;
}

omc_metadata_patch_result
omc_metadata_patch_instance_create(const omc_metadata_patch_plan *plan,
                                   omc_metadata_patch_instance *out_instance)
{
    const omc_patch_state *source;
    omc_patch_state *copy;
    omc_metadata_patch_result result;
    omc_status status;
    if (out_instance == (omc_metadata_patch_instance *)0)
        return omc_patch_error(OMC_METADATA_PATCH_NULL_OUTPUT, 0U);
    source = plan == (const omc_metadata_patch_plan *)0 ? (const omc_patch_state *)0 : (const omc_patch_state *)plan->state;
    if (source == (const omc_patch_state *)0)
        return omc_patch_error(OMC_METADATA_PATCH_INVALID_PLAN, 0U);
    copy = (omc_patch_state *)calloc(1U, sizeof(*copy));
    if (copy == (omc_patch_state *)0)
        return omc_patch_error(OMC_METADATA_PATCH_ALLOCATION_FAILED, 0U);
    copy->plan_id = source->plan_id; copy->slot_count = source->slot_count;
    copy->slots = (omc_patch_slot *)malloc((omc_size)copy->slot_count * sizeof(*copy->slots));
    if (copy->slots == (omc_patch_slot *)0) { free(copy); return omc_patch_error(OMC_METADATA_PATCH_ALLOCATION_FAILED, 0U); }
    memcpy(copy->slots, source->slots, (omc_size)copy->slot_count * sizeof(*copy->slots));
    omc_arena_init(&copy->exif); omc_arena_init(&copy->xmp);
    status = omc_arena_reserve(&copy->exif, source->exif.size);
    {
        omc_byte_ref copied_ref;
        if (status == OMC_STATUS_OK) {
            status = omc_arena_append(&copy->exif, source->exif.data,
                                      source->exif.size, &copied_ref);
        }
        if (status == OMC_STATUS_OK) {
            status = omc_arena_reserve(&copy->xmp, source->xmp.size);
        }
        if (status == OMC_STATUS_OK) {
            status = omc_arena_append(&copy->xmp, source->xmp.data,
                                      source->xmp.size, &copied_ref);
        }
    }
    if (status != OMC_STATUS_OK) { omc_metadata_patch_instance temp; temp.state = copy; omc_metadata_patch_instance_reset(&temp); return omc_patch_error(OMC_METADATA_PATCH_ALLOCATION_FAILED, 0U); }
    omc_metadata_patch_instance_reset(out_instance); out_instance->state = copy;
    omc_patch_result_init(&result); result.handle_count = copy->slot_count; result.payload_size = (omc_u64)copy->exif.size + copy->xmp.size; return result;
}

static int
omc_patch_scalar_bytes_update(const omc_metadata_patch_update *update,
                              omc_u8 *scratch, omc_size cap, omc_size *size)
{
    const omc_val *value = update->value;
    if (value == (const omc_val *)0 || update->arena == (const omc_arena *)0
        || !omc_value_shape_valid(value, update->arena)) return 0;
    if (value->kind != OMC_VAL_SCALAR || omc_elem_size(value->elem_type) > cap) return 0;
    *size = omc_patch_value_bytes((const omc_store *)0, value, scratch, cap);
    return *size != 0U;
}

omc_metadata_patch_result
omc_metadata_patch_apply(omc_metadata_patch_instance *instance,
                         const omc_metadata_patch_update *updates,
                         omc_u32 update_count)
{
    omc_patch_state *state;
    omc_u32 i;
    omc_u32 j;
    omc_u8 scratch[256];
    omc_size size;
    omc_const_bytes bytes;
    omc_metadata_patch_result result;
    if (instance == (omc_metadata_patch_instance *)0 || instance->state == (void *)0)
        return omc_patch_error(OMC_METADATA_PATCH_INVALID_INSTANCE, 0U);
    if (updates == (const omc_metadata_patch_update *)0 || update_count == 0U)
        return omc_patch_error(OMC_METADATA_PATCH_EMPTY_UPDATES, 0U);
    state = (omc_patch_state *)instance->state;
    for (i = 0U; i < update_count; ++i) {
        omc_u64 token = updates[i].handle.token;
        omc_u64 index;
        if (token == 0U) return omc_patch_error(OMC_METADATA_PATCH_INVALID_HANDLE, i);
        if ((token >> 16U) != state->plan_id)
            return omc_patch_error(OMC_METADATA_PATCH_FOREIGN_HANDLE, i);
        index = (token & 0xFFFFU);
        if (index == 0U || index > state->slot_count)
            return omc_patch_error(OMC_METADATA_PATCH_INVALID_HANDLE, i);
        for (j = 0U; j < i; ++j)
            if (updates[j].handle.token == token)
                return omc_patch_error(OMC_METADATA_PATCH_DUPLICATE_HANDLE, i);
        if (updates[i].value == (const omc_val *)0
            || !omc_patch_spec_equal(updates[i].value, &state->slots[index - 1U].expected))
            return omc_patch_error(OMC_METADATA_PATCH_VALUE_TYPE_MISMATCH, i);
        if (updates[i].value->kind == OMC_VAL_TEXT
            || updates[i].value->kind == OMC_VAL_BYTES) {
            bytes = omc_arena_view(updates[i].arena,
                                   updates[i].value->u.ref);
            size = bytes.size;
        } else if (!omc_patch_scalar_bytes_update(&updates[i], scratch,
                                                  sizeof(scratch), &size)) {
            return omc_patch_error(OMC_METADATA_PATCH_INVALID_VALUE, i);
        }
        if (state->slots[index - 1U].family == OMC_METADATA_PATCH_XMP
            && updates[i].value->kind == OMC_VAL_TEXT
            && omc_patch_escaped_width(bytes.data, size)
                   != state->slots[index - 1U].width)
            return omc_patch_error(OMC_METADATA_PATCH_WIDTH_MISMATCH, i);
        if (state->slots[index - 1U].family == OMC_METADATA_PATCH_EXIF_TIFF
            && size != state->slots[index - 1U].width)
            return omc_patch_error(OMC_METADATA_PATCH_WIDTH_MISMATCH, i);
    }
    for (i = 0U; i < update_count; ++i) {
        omc_u64 index = updates[i].handle.token & 0xFFFFU;
        omc_patch_slot *slot = &state->slots[index - 1U];
        omc_arena *payload = slot->family == OMC_METADATA_PATCH_EXIF_TIFF ? &state->exif : &state->xmp;
        if (updates[i].value->kind == OMC_VAL_TEXT
            || updates[i].value->kind == OMC_VAL_BYTES) {
            bytes = omc_arena_view(updates[i].arena,
                                   updates[i].value->u.ref);
            size = bytes.size;
        } else if (!omc_patch_scalar_bytes_update(&updates[i], scratch,
                                                  sizeof(scratch), &size)) {
            return omc_patch_error(OMC_METADATA_PATCH_INVALID_VALUE, i);
        }
        if (slot->family == OMC_METADATA_PATCH_XMP && updates[i].value->kind == OMC_VAL_TEXT) {
            omc_size p = slot->offset;
            omc_size k;
            for (k = 0U; k < size; ++k) {
                omc_u8 c = bytes.data[k];
                if (c == '&') { memcpy(payload->data + p, "&amp;", 5U); p += 5U; }
                else if (c == '<') { memcpy(payload->data + p, "&lt;", 4U); p += 4U; }
                else if (c == '>') { memcpy(payload->data + p, "&gt;", 4U); p += 4U; }
                else if (c == '"') { memcpy(payload->data + p, "&quot;", 6U); p += 6U; }
                else if (c == '\'') { memcpy(payload->data + p, "&apos;", 6U); p += 6U; }
                else if (c == '\r') { memcpy(payload->data + p, "&#xD;", 5U); p += 5U; }
                else payload->data[p++] = c;
            }
        } else if (updates[i].value->kind == OMC_VAL_TEXT
                   || updates[i].value->kind == OMC_VAL_BYTES) {
            memcpy(payload->data + slot->offset, bytes.data, size);
        } else {
            memcpy(payload->data + slot->offset, scratch, size);
        }
    }
    omc_patch_result_init(&result); result.handle_count = state->slot_count; result.patched_handles = update_count;
    result.payload_size = (omc_u64)state->exif.size + state->xmp.size; return result;
}

omc_metadata_patch_result
omc_metadata_patch_replay(const omc_metadata_patch_instance *instance,
                          omc_metadata_patch_replay_fn callback, void *user)
{
    omc_metadata_patch_result result;
    omc_const_bytes payload;
    if (instance == (const omc_metadata_patch_instance *)0 || instance->state == (void *)0)
        return omc_patch_error(OMC_METADATA_PATCH_INVALID_INSTANCE, 0U);
    if (callback == (omc_metadata_patch_replay_fn)0)
        return omc_patch_error(OMC_METADATA_PATCH_REPLAY_FAILED, 0U);
    payload = omc_metadata_patch_instance_payload(instance, OMC_METADATA_PATCH_EXIF_TIFF);
    if (payload.size != 0U && !callback(user, OMC_METADATA_PATCH_EXIF_TIFF, payload))
        return omc_patch_error(OMC_METADATA_PATCH_REPLAY_FAILED, 0U);
    payload = omc_metadata_patch_instance_payload(instance, OMC_METADATA_PATCH_XMP);
    if (payload.size != 0U && !callback(user, OMC_METADATA_PATCH_XMP, payload))
        return omc_patch_error(OMC_METADATA_PATCH_REPLAY_FAILED, 0U);
    omc_patch_result_init(&result); result.handle_count = ((const omc_patch_state *)instance->state)->slot_count; return result;
}
